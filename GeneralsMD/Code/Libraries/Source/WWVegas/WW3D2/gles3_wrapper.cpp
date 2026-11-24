/*
** GeneralsX Web Port — OpenGL ES 3.0 / WebGL 2.0 Graphics Backend
** Implementation of the DX8Wrapper-compatible API using OpenGL ES 3.0.
**
** This file replaces dx8wrapper.cpp for Emscripten builds.
** The #ifdef __EMSCRIPTEN__ guard ensures it only compiles for web targets.
**
** KEY MAPPING (DX8 → OpenGL ES 3.0):
**   IDirect3DDevice8::DrawIndexedPrimitive → glDrawElements
**   IDirect3DDevice8::SetRenderState       → glEnable/glDisable/glBlendFunc/etc
**   IDirect3DDevice8::SetTexture           → glActiveTexture + glBindTexture
**   IDirect3DDevice8::SetTransform         → uniform mat4 in shader
**   IDirect3DDevice8::CreateVertexBuffer   → glGenBuffers + glBufferData
**   IDirect3DDevice8::CreateTexture        → glGenTextures + glTexImage2D
**   IDirect3DDevice8::BeginScene/EndScene  → no-op (GL has no scene concept)
**   IDirect3DDevice8::Present              → emscripten_webgl_commit_frame
**
** LICENSE: GPL-3.0
*/

#if defined(__EMSCRIPTEN__)

#include "gles3_wrapper.h"
#include <emscripten/html5.h>
#include <emscripten/em_asm.h>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <vector>

// Forward-declare SDL types so we don't need <SDL.h> at compile time.
// The linker resolves SDL_GL_SwapWindow via -sUSE_SDL=2 at link time.
// EmscriptenMain.cpp (which does include <SDL.h>) provides TheSDL3Window.
struct SDL_Window;
extern "C" void SDL_GL_SwapWindow(SDL_Window *window);

// The SDL window created in EmscriptenMain.cpp
extern SDL_Window *TheSDL3Window;

// ============================================================================
// Static state
// ============================================================================
#include <unordered_map>

static GLES3_PipelineState g_State;
static bool g_Initialized = false;
static bool g_SceneDirty = true; // GXScene inputs changed since the last upload (Upload_Scene_Block)
static double g_RenderScale = 1.0;  // Module.gxRenderScale, read about once a second
static int g_SwapCount = 0;         // frames presented, for Module.gxFps (render scale Auto)
double gx_work_ms_1s = 0;           // game CPU work per frame, last second (gles3_mainloop) -> Module.gxWorkMs
static int g_PageSwitches = 7 | 16 | 32 | 64 | 4096 | 8192 | 16384 | 32768 | 65536 | 262144 | 524288 | 1048576 | 4194304 | 16777216 | 33554432;      // bit 0: shroud fold, bit 1: terrain tile culling (Module.gxShroudFold / gxTerrainCull = 0 turns one off), bit 2: 30 fps pacing (setting Steady 30 fps, ?pace=0), bit 3: ground cache (Module.gxGroundCache = 0 turns it off), bit 4: specialised shaders, bit 5: skip unchanged texture re-uploads, bit 6: sounds load in the background (the page's performance switches)
int GLES3_Page_Switches() { return g_PageSwitches; }
// Second switch word (the first is full): bit 0 contactTable, bit 1 sleepyKeys, bit 2 openHeap, bit 3 contactArray (game logic, same game).
static int g_PageSwitches2 = 15;
int GLES3_Page_Switches2() { return g_PageSwitches2; }
static bool g_Scaled3D = false;     // context without MSAA: the offscreen 3D pass is used
static bool g_In3DPass = false;     // the offscreen framebuffer is bound
static GLuint g_BoundVAO = 0; // shadow of glBindVertexArray (see GLES3_Bind_Layout)
// Ground cache (GLES3_Ground_Cache_Begin): every texture and buffer upload takes the next
// serial, so "same texture id" also means "same contents" in a draw's fingerprint.
unsigned int gx_content_serial = 0;
static std::vector<unsigned int> g_TexSerial; // by GL texture id
int gx_gc_hits = 0, gx_gc_captures = 0, gx_gc_plain = 0, gx_gc_slides = 0;
const float* gx_skin_palette = nullptr; unsigned int gx_skin_bones = 0; GLuint gx_skin_bone_buffer = 0; // a skinned instanced draw in progress (with gx_instance_count)
const float* gx_instance_uv = nullptr; // with them: one stage 0 texture offset (2 floats) per instance, or null
const float* gx_instance_matrices = nullptr; unsigned int gx_instance_count = 0; // set around an instanced DrawIndexedPrimitive (DX8Wrapper::Draw_Triangles_Instanced)
int gx_gc_why = 0; // which check stopped the last slide (Module.gxGCWhy): 1 turned, 2 rolled, 3 height changed, 4 projection, 5 viewport, 20-25 cache state // Module.gxGCHits / gxGCCaptures / gxGCPlain
static std::unordered_map<GLuint, bool> g_TextureHasMipmaps;

void GLES3_Register_Texture(GLuint tex, bool has_mipmaps) {
    g_TextureHasMipmaps[tex] = has_mipmaps;
}

void GLES3_Unregister_Texture(GLuint tex) {
    g_TextureHasMipmaps.erase(tex);
}

bool GLES3_Texture_Has_Mipmaps(GLuint tex) {
    auto it = g_TextureHasMipmaps.find(tex);
    if (it != g_TextureHasMipmaps.end()) {
        return it->second;
    }
    return false;
}


// Shader manager statics
GLuint GLES3_ShaderManager::cached_programs[GLES3_PROGRAM_SLOTS] = {0};
GLES3_ProgramUniformLocations GLES3_ShaderManager::cached_locations[GLES3_PROGRAM_SLOTS];

struct GLES3_CachedUniformState { // per program; scene values live in GXScene
    bool initialized;
    float world_matrix[16];
    float material_diffuse[4];
    float material_ambient[4];
    float material_specular[4];
    float material_emissive[4];
    float material_power;
    float alpha_ref;
    int alpha_func;
    int stage_ops[8];
    unsigned int pixel_shader;
    float ps_const[8][4];
    int color_vertex;
    int stage_args[24];
    int tex_bound;
    int tex_coord_index[4];
    int tex_transform[4];
    float tex_matrix[4][16];
    float shroud_matrix[16];
};

static GLES3_CachedUniformState cached_uniform_states[GLES3_PROGRAM_SLOTS] = {};

// ============================================================================
// Helper: DX8 Blend Mode → GL Blend Factor
// ============================================================================
namespace GLES3_Helpers {

GLenum Blend_To_GL(unsigned int dx8_blend) {
    switch (dx8_blend) {
        case GLES3_BLEND_ZERO:         return GL_ZERO;
        case GLES3_BLEND_ONE:          return GL_ONE;
        case GLES3_BLEND_SRCCOLOR:     return GL_SRC_COLOR;
        case GLES3_BLEND_INVSRCCOLOR:  return GL_ONE_MINUS_SRC_COLOR;
        case GLES3_BLEND_SRCALPHA:     return GL_SRC_ALPHA;
        case GLES3_BLEND_INVSRCALPHA:  return GL_ONE_MINUS_SRC_ALPHA;
        case GLES3_BLEND_DESTALPHA:    return GL_DST_ALPHA;
        case GLES3_BLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
        case GLES3_BLEND_DESTCOLOR:    return GL_DST_COLOR;
        case GLES3_BLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
        case GLES3_BLEND_SRCALPHASAT:  return GL_SRC_ALPHA_SATURATE;
        default: return GL_ONE;
    }
}

GLenum StencilOp_To_GL(unsigned int dx8_op) {
    switch (dx8_op) {
        case 2: return GL_ZERO;
        case 3: return GL_REPLACE;
        case 4: return GL_INCR;       // D3DSTENCILOP_INCRSAT
        case 5: return GL_DECR;       // D3DSTENCILOP_DECRSAT
        case 6: return GL_INVERT;
        case 7: return GL_INCR_WRAP;  // D3DSTENCILOP_INCR
        case 8: return GL_DECR_WRAP;  // D3DSTENCILOP_DECR
        default: return GL_KEEP;      // 1 = KEEP
    }
}

GLenum CmpFunc_To_GL(unsigned int dx8_cmp) {
    switch (dx8_cmp) {
        case GLES3_CMP_NEVER:        return GL_NEVER;
        case GLES3_CMP_LESS:         return GL_LESS;
        case GLES3_CMP_EQUAL:        return GL_EQUAL;
        case GLES3_CMP_LESSEQUAL:    return GL_LEQUAL;
        case GLES3_CMP_GREATER:      return GL_GREATER;
        case GLES3_CMP_NOTEQUAL:     return GL_NOTEQUAL;
        case GLES3_CMP_GREATEREQUAL: return GL_GEQUAL;
        case GLES3_CMP_ALWAYS:       return GL_ALWAYS;
        default: return GL_LEQUAL;
    }
}

GLenum CullMode_To_GL(unsigned int dx8_cull) {
    // DX8: 1=NONE, 2=CW, 3=CCW.  glFrontFace(GL_CW) is set at init, so:
    //   CW-wound  polygons = GL "front faces"
    //   CCW-wound polygons = GL "back faces"
    //
    // D3DCULL_CW(2)  = cull CW-wound polygons  → GL_FRONT
    // D3DCULL_CCW(3) = cull CCW-wound polygons → GL_BACK  ← DX8 default
    // (previous mapping was reversed — caused inside-out rendering whenever
    //  the game explicitly set D3DRS_CULLMODE to D3DCULL_CCW)
    switch (dx8_cull) {
        case 1: return 0;           // NONE — caller disables GL_CULL_FACE
        case 2: return GL_FRONT;    // D3DCULL_CW  → cull CW-wound faces
        case 3: return GL_BACK;     // D3DCULL_CCW → cull CCW-wound faces
        default: return GL_BACK;
    }
}

GLenum PrimType_To_GL(unsigned int dx8_prim) {
    switch (dx8_prim) {
        case GLES3_PT_POINTLIST:     return GL_POINTS;
        case GLES3_PT_LINELIST:      return GL_LINES;
        case GLES3_PT_LINESTRIP:     return GL_LINE_STRIP;
        case GLES3_PT_TRIANGLELIST:  return GL_TRIANGLES;
        case GLES3_PT_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
        case GLES3_PT_TRIANGLEFAN:   return GL_TRIANGLE_FAN;
        default: return GL_TRIANGLES;
    }
}

GLenum AddressMode_To_GL(unsigned int dx8_addr) {
    // DX8: 1=WRAP, 2=MIRROR, 3=CLAMP
    switch (dx8_addr) {
        case 1: return GL_REPEAT;
        case 2: return GL_MIRRORED_REPEAT;
        case 3: return GL_CLAMP_TO_EDGE;
        default: return GL_REPEAT;
    }
}

GLenum FilterMode_To_GL(unsigned int dx8_filter) {
    // DX8: 1=POINT, 2=LINEAR, 3=ANISOTROPIC
    switch (dx8_filter) {
        case 1: return GL_NEAREST;
        case 2: return GL_LINEAR;
        case 3: return GL_LINEAR; // anisotropic handled via extension
        default: return GL_LINEAR;
    }
}

void Matrix4x4_To_GL(const float* src_row_major, float* dst_col_major) {
    // DX8 uses row-major matrices, OpenGL uses column-major.
    // Since we multiply matrices on the left of column vectors in the shader
    // (e.g., u_World * vec4), and DX8 multiplies row vectors on the left of row-major matrices,
    // the row-major memory layout read by GL as column-major is already the correct transpose (M^T).
    // Therefore, we do NOT transpose the memory in code. Doing so would warp translations and projection.
    memcpy(dst_col_major, src_row_major, 16 * sizeof(float));
}

} // namespace GLES3_Helpers


// ============================================================================
// DX8Wrapper API Implementation — Lifecycle
// ============================================================================

/*
** DX8Wrapper::Init — Create WebGL 2.0 context via Emscripten
**
** Original DX8 version: Loads d3d8.dll, creates IDirect3D8, enumerates
** adapters, creates IDirect3DDevice8 with present parameters.
**
** WebGL version: Creates an Emscripten WebGL2 context on the canvas element.
*/
// This would be called as: DX8Wrapper::Init(canvas_handle)
bool GLES3_Init(void* hwnd, bool lite) {
    if (g_Initialized) return true;

    // Create WebGL 2.0 context
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;  // WebGL 2.0 = OpenGL ES 3.0
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = true;
    attrs.stencil = true;
    // Page setting "Antialiasing" (web_shell.html; off by default on software or weak GPUs,
    // where multisampling multiplies the cost of every pixel).
    attrs.antialias = EM_ASM_INT({
        var s = window.GeneralsX && window.GeneralsX.settings;
        return s && s.antialias === false ? 0 : 1;
    });
    attrs.premultipliedAlpha = false;
    attrs.preserveDrawingBuffer = false;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        fprintf(stderr, "ERROR: Failed to create WebGL 2.0 context (code: %d)\n", ctx);
        return false;
    }
    emscripten_webgl_make_context_current(ctx);
    {
        GLint samples = 0;
        glGetIntegerv(GL_SAMPLES, &samples);
        // Not on a software GPU: there a full-size canvas costs more than the smaller 3D
        // pass saves (2-core SwiftShader: 1-2 fps vs 3-7 with the whole canvas scaled).
        g_Scaled3D = samples == 0 && !EM_ASM_INT({ return window.gxSoftwareGL ? 1 : 0; });
        EM_ASM({ Module.gxScaled3D = $0; if (window.gxRepinCanvas) window.gxRepinCanvas(); }, g_Scaled3D ? 1 : 0);
    }

    fprintf(stderr, "INFO: WebGL 2.0 context created successfully\n");
    fprintf(stderr, "INFO: GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    fprintf(stderr, "INFO: GL_VERSION: %s\n", glGetString(GL_VERSION));
    fprintf(stderr, "INFO: GL_SHADING_LANGUAGE_VERSION: %s\n",
            glGetString(GL_SHADING_LANGUAGE_VERSION));

    // Initialize state
    memset(&g_State, 0, sizeof(g_State));
    g_State.texture_factor[0] = 1.0f;
    g_State.texture_factor[1] = 1.0f;
    g_State.texture_factor[2] = 1.0f;
    g_State.texture_factor[3] = 1.0f;
    for (int i = 0; i < 8; i++) {
        g_State.texture_stage_state[i][GLES3_TSS_ADDRESSU] = 1; // WRAP
        g_State.texture_stage_state[i][GLES3_TSS_ADDRESSV] = 1; // WRAP
        g_State.texture_stage_state[i][GLES3_TSS_MAGFILTER] = 2; // LINEAR
        g_State.texture_stage_state[i][GLES3_TSS_MINFILTER] = 2; // LINEAR
        g_State.texture_stage_state[i][GLES3_TSS_MIPFILTER] = 0; // NONE
        g_State.texture_stage_state[i][GLES3_TSS_TEXCOORDINDEX] = i; // D3D default: UV set = stage
        // D3D defaults: ARG1 = TEXTURE, ARG2 = CURRENT, ARG0 = CURRENT
        g_State.texture_stage_state[i][GLES3_TSS_COLORARG1] = 2;
        g_State.texture_stage_state[i][GLES3_TSS_COLORARG2] = 1;
        g_State.texture_stage_state[i][GLES3_TSS_ALPHAARG1] = 2;
        g_State.texture_stage_state[i][GLES3_TSS_ALPHAARG2] = 1;
        g_State.texture_stage_state[i][GLES3_TSS_COLORARG0] = 1;
        g_State.texture_stage_state[i][GLES3_TSS_ALPHAARG0] = 1;
        // Stage 0 modulates texture and diffuse; later stages start disabled.
        g_State.texture_stage_state[i][GLES3_TSS_COLOROP] = i == 0 ? 4 : 1;
        g_State.texture_stage_state[i][GLES3_TSS_ALPHAOP] = i == 0 ? 2 : 1;
    }
    // D3D stencil defaults, which are also GL's: func ALWAYS, ops KEEP, masks all ones.
    // GL is set up below with depth test and depth writes on; record that too.
    g_State.render_states[GLES3_RS_ZENABLE] = 1;
    g_State.render_states[GLES3_RS_ZWRITEENABLE] = 1;
    g_State.render_states[GLES3_RS_STENCILFUNC] = 8;
    g_State.render_states[GLES3_RS_STENCILFAIL] = 1;
    g_State.render_states[GLES3_RS_STENCILZFAIL] = 1;
    g_State.render_states[GLES3_RS_STENCILPASS] = 1;
    g_State.render_states[GLES3_RS_STENCILMASK] = 0xFFFFFFFFu;
    g_State.render_states[GLES3_RS_STENCILWRITEMASK] = 0xFFFFFFFFu;


    // Set identity matrices
    float identity[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    memcpy(g_State.world_matrix, identity, sizeof(identity));
    memcpy(g_State.view_matrix, identity, sizeof(identity));
    memcpy(g_State.projection_matrix, identity, sizeof(identity));
    for (int i = 0; i < 8; i++)
        memcpy(g_State.texture_matrices[i], identity, sizeof(identity));

    // Default material (white)
    g_State.material.diffuse[0] = g_State.material.diffuse[1] =
    g_State.material.diffuse[2] = g_State.material.diffuse[3] = 1.0f;
    g_State.material.ambient[0] = g_State.material.ambient[1] =
    g_State.material.ambient[2] = 0.2f;
    g_State.material.ambient[3] = 1.0f;

    // Default global ambient (gray/dark gray fallback matching shader)
    g_State.global_ambient[0] = g_State.global_ambient[1] =
    g_State.global_ambient[2] = 0.2f;
    g_State.global_ambient[3] = 1.0f;

    // Create VAO (required in OpenGL ES 3.0 / WebGL 2.0)
    glGenVertexArrays(1, &g_State.vao);
    glBindVertexArray(g_State.vao);
    g_BoundVAO = g_State.vao;

    // Default GL state matching DX8 defaults
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);  // DX8 default winding

    // Initialize shader manager
    if (!GLES3_ShaderManager::Init()) {
        fprintf(stderr, "ERROR: Failed to initialize shader manager\n");
        return false;
    }

    g_Initialized = true;
    return true;
}

void GLES3_Shutdown() {
    if (!g_Initialized) return;

    GLES3_ShaderManager::Shutdown();

    if (g_State.vao) {
        glDeleteVertexArrays(1, &g_State.vao);
        g_State.vao = 0;
    }

    g_Initialized = false;
}


// ============================================================================
// DX8Wrapper API — Scene Management
// ============================================================================

/*
** BeginScene/EndScene — In DX8 these bracket a frame.
** In OpenGL there's no equivalent — these are essentially no-ops.
** We use them to reset per-frame statistics.
*/
// Worker build: the game thread's OffscreenCanvas presents when the frame callback
// returns; SDL's swap goes through EGL on the main thread, which has no context.
#ifdef __EMSCRIPTEN_PTHREADS__
#define GLES3_WORKER_BUILD 1
#else
#define GLES3_WORKER_BUILD 0
#endif

static int g_BackBufferW = 0, g_BackBufferH = 0;
static bool g_CanvasSizeStale = true; // canvas size re-read once a frame (Canvas_Size)
static int g_CanvasW = 800, g_CanvasH = 600;
static void Canvas_Size() {
    // Looked up once a frame (GLES3_Begin_Scene marks it stale): it goes through a DOM selector.
    if (g_CanvasSizeStale) {
        emscripten_get_canvas_element_size("#canvas", &g_CanvasW, &g_CanvasH);
        g_CanvasSizeStale = false;
    }
}

// ---------------------------------------------------------------------------
// Optional render scale (page setting). Without multisampling the canvas stays at the
// display size: the 3D views (W3DDisplay::draw's drawViews, between GLES3_Begin_3D and
// GLES3_End_3D) draw into an offscreen framebuffer at the scale, which is then scaled up
// into the canvas, and the UI, text and cursor draw over it at full size, sharp. With a
// multisampled context (a blit can't target it) the whole canvas is scaled instead.
// ---------------------------------------------------------------------------
static GLuint g_Fbo = 0, g_FboColor = 0, g_FboDepth = 0;
static int g_FboW = 0, g_FboH = 0;
static struct { bool color, zs; float r, g, b, a, z; unsigned int stencil; } g_FrameClear = {};

void GLES3_Precompile_Programs();
void GLES3_Begin_Scene() {
    g_CanvasSizeStale = true;
    static bool precompiled = false; // the programs the last sessions needed (item 21)
    if (!precompiled) {
        precompiled = true;
        if (MAIN_THREAD_EM_ASM_INT({ var o = typeof window !== 'undefined' && window.GeneralsX && window.GeneralsX.opt; return o && o.precompile === false ? 0 : 1; }))
            GLES3_Precompile_Programs();
    }
    static double next_check = 0;
    const double now = emscripten_get_now();
    if (now >= next_check && g_BackBufferW > 0) {
        // Frames presented since the last check go out as Module.gxFps, which the page's
        // render scale Auto follows.
        static double last_check = 0;
        const double fps = last_check > 0 ? g_SwapCount * 1000.0 / (now - last_check) : 0;
        last_check = now;
        g_SwapCount = 0;
        next_check = now + 1000.0;
        double sc = EM_ASM_DOUBLE({ Module.gxFps = $0; Module.gxWorkMs = $1; Module.gxGCHits = $2; Module.gxGCCaptures = $3; Module.gxGCPlain = $4; Module.gxGCSlides = $5; Module.gxGCWhy = $6; return Module.gxRenderScale || 1; }, fps, gx_work_ms_1s, gx_gc_hits, gx_gc_captures, gx_gc_plain, gx_gc_slides, gx_gc_why);
        g_PageSwitches2 = EM_ASM_INT({
            var o = (window.GeneralsX && window.GeneralsX.opt) || {};
            return (o.contactTable === false ? 0 : 1)   // bit 0: touching-pair table sized for big battles (PartitionManager.cpp)
                 | (o.sleepyKeys === false ? 0 : 2)     // bit 1: update queue keeps each wake-up time next to its entry (GameLogic.cpp)
                 | (o.openHeap === false ? 0 : 4)       // bit 2: route search candidates in a heap instead of a sorted list (AIPathfind.cpp)
                 | (o.contactArray === false ? 0 : 8);  // bit 3: touching pairs kept in flat arrays, nothing allocated per pair (PartitionManager.cpp; same game)
        });
        g_PageSwitches = EM_ASM_INT({
            var st = window.GeneralsX && window.GeneralsX.settings;
            var pace = location.search.indexOf('pace=0') < 0 && !!(st && st.steady30 === true);
            // Ground cache: only without antialiasing (its paste writes depth per pixel, which
            // would lose the smoothed ground edges); Module.gxGroundCache = 0 turns it off.
            // The page's performance switches (Settings; web_shell.html's gxPerf).
            var o = (window.GeneralsX && window.GeneralsX.opt) || {};
            var gc = Module.gxGroundCache !== 0 && o.groundCache !== false && !!st && st.antialias === false;
            return (Module.gxShroudFold === 0 || o.shroudFold === false ? 0 : 1) | (Module.gxTerrainCull === 0 || o.terrainCull === false ? 0 : 2)
                 | (pace ? 4 : 0) | (gc ? 8 : 0) | (o.fastShaders === false ? 0 : 16) | (o.skipSameUploads === false ? 0 : 32)
                 | (o.asyncSounds === false ? 0 : 64)
                 // Pan cache (experiment, off unless switched on): bit 7, and in bits 8-11 how many
                 // frames one real ground draw serves while the camera moves (Module.gxPanFrames, 3).
                 | (o.panCache === true ? 128 : 0) | ((Math.max(2, Math.min(8, Module.gxPanFrames | 0 || 3))) << 8)
                 | (o.unitBatching === false ? 0 : 4096) // bit 12: identical unit parts drawn together (dx8renderer.cpp)
                 | (o.skipCompare === false ? 0 : 8192) // bit 13: always-changing buffers are sent without comparing (d3d8.h)
                 | (o.gpuSkinning === false ? 0 : 16384)  // bit 14: skinned meshes animated in the vertex shader (dx8renderer.cpp)
                 | (o.uiBatching === false ? 0 : 32768)  // bit 15: unit status bars collected into one draw (W3DDisplay.cpp)
                 | (o.sortBatching === false ? 0 : 65536)  // bit 16: see-through draws with the same state merged (sortingrenderer.cpp)
                 | (o.uiScanCache === false ? 0 : 262144)  // bit 18: the command bar's object searches answered once per logic frame (Player.cpp)
                 | (o.freeTextureCopies === false ? 0 : 524288)  // bit 19: texture copies the graphics chip already has are dropped (d3d8.h)
                 | (o.fastDestroy === false ? 0 : 1048576)  // bit 20: destroyed objects' updates found from the object (GameLogic.cpp)
                 | (Module.gxVerify === 1 ? 2097152 : 0)  // bit 21: test only: run old and new code side by side and count differences
                 | (o.asyncShaders === false ? 0 : 4194304)  // bit 22: new shaders compile in the background, the general one draws meanwhile
                 | (o.fastInvSqrt === true ? 8388608 : 0)  // bit 23: fast inverse square root (single player only; changes the simulation slightly)
                 | (o.reuseLayouts === false ? 0 : 16777216)  // bit 24: one vertex layout per buffer setup, re-pointed when the offset changes
                 | (o.flowFields === false ? 0 : 33554432) // bit 25: long ground paths from shared flow fields (on; single player only, changes the simulation)
                 | (o.gpuFlowFields === true ? 67108864 : 0)  // bit 26: the flow fields are built on the GPU (exact integer relaxation)
                 | (o.ignoreUnitsInPaths === true ? 134217728 : 0)  // bit 27: path planning ignores other units (single player)
                 | (o.enemySearchSkip === false ? 0 : 268435456)  // bit 28: enemy searches skip map squares with no possible enemy (PartitionManager.cpp; same game)
                 | (o.collisionOrder === false ? 0 : 536870912)  // bit 29: collision pairs: the cheap status test before the shape test (same game)
                 | (o.allyTable === false ? 0 : 1073741824); // bit 30: who-is-an-ally answers from a table (Player.cpp; same game)
        });
        g_RenderScale = sc < 0.25 ? 0.25 : sc > 1.0 ? 1.0 : sc;
#ifdef __EMSCRIPTEN_PTHREADS__
        // Worker build: the canvas belongs to this thread, so the page can't resize it.
        static double last_canvas_scale = 0;
        const double canvas_scale = g_Scaled3D ? 1.0 : g_RenderScale;
        if (canvas_scale != last_canvas_scale) {
            last_canvas_scale = canvas_scale;
            emscripten_set_canvas_element_size("#canvas", (int)(g_BackBufferW * canvas_scale + 0.5),
                                               (int)(g_BackBufferH * canvas_scale + 0.5));
        }
#endif
    }
    g_State.draw_calls = 0;
    g_State.triangles_drawn = 0;
}

static void Reapply_Viewport() {
    const GLES3_Viewport v = g_State.viewport;
    GLES3_Set_Viewport(v.x, v.y, v.width, v.height, v.min_z, v.max_z);
}

void GLES3_Begin_3D() {
    if (!g_Scaled3D || g_RenderScale >= 0.999 || g_In3DPass) return;
    Canvas_Size();
    int w = (int)(g_CanvasW * g_RenderScale + 0.5), h = (int)(g_CanvasH * g_RenderScale + 0.5);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (!g_Fbo) {
        glGenFramebuffers(1, &g_Fbo);
        glGenRenderbuffers(1, &g_FboColor);
        glGenRenderbuffers(1, &g_FboDepth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_Fbo);
    if (w != g_FboW || h != g_FboH) {
        glBindRenderbuffer(GL_RENDERBUFFER, g_FboColor);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, g_FboDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_FboColor);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_FboDepth);
        g_FboW = w; g_FboH = h;
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "GLES3: offscreen 3D framebuffer incomplete; drawing at full size\n");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            g_Scaled3D = false;
            EM_ASM({ Module.gxScaled3D = 0; if (window.gxRepinCanvas) window.gxRepinCanvas(); });
            return;
        }
    }
    g_In3DPass = true;
    Reapply_Viewport();
    // The frame's clear (WW3D::Begin_Render) went to the canvas; the views expect it here too.
    GLES3_Clear(g_FrameClear.color, g_FrameClear.zs, g_FrameClear.r, g_FrameClear.g,
                g_FrameClear.b, g_FrameClear.a, g_FrameClear.z, g_FrameClear.stencil);
}

void GLES3_End_3D() {
    if (!g_In3DPass) return;
    Canvas_Size();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_Fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, g_FboW, g_FboH, 0, 0, g_CanvasW, g_CanvasH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_In3DPass = false;
    Reapply_Viewport();
}

void GLES3_End_Scene(bool flip_frame) {
    GLES3_End_3D(); // in case a frame ended inside the 3D pass
    if (flip_frame && TheSDL3Window && !GLES3_WORKER_BUILD) {
        SDL_GL_SwapWindow(TheSDL3Window);
    }
}

// The benchmark's map thumbnails (Main/WebBenchmark.cpp): when set, the finished frame is read
// back once, scaled to 320 pixels wide and handed to the page (window.gxBenchShot).
extern "C" { int gx_bench_shot = 0; double gx_mem_buffer_bytes = 0, gx_mem_texture_bytes = 0; unsigned gx_stat_tex_refills = 0; }
extern "C" { double gx_stat_upload_bytes = 0, gx_stat_triangles = 0; unsigned gx_stat_gl_draws = 0, gx_stat_programs = 0, gx_stat_tex_binds = 0, gx_stat_shader_compiles = 0; }
static void GX_Bench_Send_Shot() {
    gx_bench_shot = 0;
    const int w = g_CanvasW, h = g_CanvasH;
    if (w <= 0 || h <= 0) return;
    std::vector<unsigned char> full((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, full.data());
    const int tw = 320, th = (int)((double)h * tw / w);
    std::vector<unsigned char> small((size_t)tw * th * 4);
    for (int y = 0; y < th; ++y) {
        const int sy = h - 1 - (int)((double)y * h / th); // GL rows run bottom up
        for (int x = 0; x < tw; ++x) {
            const int sx = (int)((double)x * w / tw);
            memcpy(&small[((size_t)y * tw + x) * 4], &full[((size_t)sy * w + sx) * 4], 4);
        }
    }
    EM_ASM({ if (window.gxBenchShot) window.gxBenchShot(HEAPU8.slice($0, $0 + $1 * $2 * 4), $1, $2); }, small.data(), tw, th);
}

void GLES3_Swap_Buffers() {
    if (gx_bench_shot) GX_Bench_Send_Shot();
    // Frames presented per second, logged every 5 s (works on the game thread too, where
    // the page cannot see the frames). GXFRAME lines are read by the benchmark scripts.
    static double t0 = 0; static int n = 0;
    double now = emscripten_get_now();
    if (t0 == 0) t0 = now;
    if (++n, now - t0 >= 5000.0) { printf("GXFRAME fps=%.1f\n", n * 1000.0 / (now - t0)); t0 = now; n = 0; }
    g_SwapCount++;
    if (TheSDL3Window && !GLES3_WORKER_BUILD) {
        SDL_GL_SwapWindow(TheSDL3Window);
    }
}

void GLES3_Clear(bool clear_color, bool clear_z_stencil,
                  float r, float g, float b, float a,
                  float z, unsigned int stencil) {
    GLbitfield mask = 0;
    if (clear_color) {
        glClearColor(r, g, b, a);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_z_stencil) {
        glClearDepthf(z);
        glClearStencil(stencil);
        mask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    }
    if (!mask) return;
    if (!g_In3DPass) {
        g_FrameClear.color = clear_color; g_FrameClear.zs = clear_z_stencil;
        g_FrameClear.r = r; g_FrameClear.g = g; g_FrameClear.b = b; g_FrameClear.a = a;
        g_FrameClear.z = z; g_FrameClear.stencil = stencil;
    }
    // D3D's Clear ignores the render states; GL's obeys its write masks and scissor.
    const unsigned int* rs = g_State.render_states;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFFFFFFFFu);
    glClear(mask);
    glDepthMask(rs[GLES3_RS_ZWRITEENABLE] ? GL_TRUE : GL_FALSE);
    glStencilMask(rs[GLES3_RS_STENCILWRITEMASK]);
}


// ============================================================================
// DX8Wrapper API — Render State
// ============================================================================

/*
** Set_DX8_Render_State — Maps D3DRENDERSTATETYPE values to GL calls.
**
** The original dx8wrapper caches these to avoid redundant D3D calls.
** We do the same, plus translate to GL state.
*/
void GLES3_Set_Render_State(unsigned int state, unsigned int value) {
    if (state >= GLES3_RS_MAX) return;
    // No redundant-state skip: DX8Wrapper already drops repeats against its own cache,
    // which starts at 0x12345678 while this one starts at 0, so a skip here lost the
    // engine's first "set to 0" (TEXTUREFACTOR, AMBIENT) and kept our start values.
    g_State.render_states[state] = value;
    if (state == GLES3_RS_FOGCOLOR || state == GLES3_RS_FOGSTART || state == GLES3_RS_FOGEND ||
        state == GLES3_RS_AMBIENT || state == GLES3_RS_TEXTUREFACTOR)
        g_SceneDirty = true;

    switch (state) {
        case GLES3_RS_ZENABLE:
            if (value) glEnable(GL_DEPTH_TEST);
            else       glDisable(GL_DEPTH_TEST);
            break;

        case GLES3_RS_ZWRITEENABLE:
            glDepthMask(value ? GL_TRUE : GL_FALSE);
            break;

        case GLES3_RS_ZFUNC:
            glDepthFunc(GLES3_Helpers::CmpFunc_To_GL(value));
            break;

        case GLES3_RS_ALPHABLENDENABLE:
            if (value) glEnable(GL_BLEND);
            else       glDisable(GL_BLEND);
            break;

        case GLES3_RS_SRCBLEND:
            glBlendFunc(
                GLES3_Helpers::Blend_To_GL(value),
                GLES3_Helpers::Blend_To_GL(
                    g_State.render_states[GLES3_RS_DESTBLEND]));
            break;

        case GLES3_RS_DESTBLEND:
            glBlendFunc(
                GLES3_Helpers::Blend_To_GL(
                    g_State.render_states[GLES3_RS_SRCBLEND]),
                GLES3_Helpers::Blend_To_GL(value));
            break;

        case GLES3_RS_CULLMODE:
            if (value == 1) { // D3DCULL_NONE
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glCullFace(GLES3_Helpers::CullMode_To_GL(value));
            }
            break;

        case GLES3_RS_ALPHATESTENABLE:
            // OpenGL ES 3.0 removed alpha test — emulated in fragment shader
            g_State.alpha_test_enabled = (value != 0);
            break;

        case GLES3_RS_ALPHAREF:
            g_State.alpha_ref = value;
            break;

        case GLES3_RS_ALPHAFUNC:
            g_State.alpha_func = value;
            break;

        case GLES3_RS_FOGENABLE:
            g_State.fog_enabled = (value != 0);
            break;

        case GLES3_RS_FOGCOLOR: {
            // DX8 packs as 0xAARRGGBB
            g_State.fog_color[0] = ((value >> 16) & 0xFF) / 255.0f;
            g_State.fog_color[1] = ((value >>  8) & 0xFF) / 255.0f;
            g_State.fog_color[2] = ((value      ) & 0xFF) / 255.0f;
            break;
        }

        case GLES3_RS_LIGHTING:
            g_State.lighting_enabled = (value != 0);
            break;

        case GLES3_RS_AMBIENT: {
            g_State.global_ambient[0] = ((value >> 16) & 0xFF) / 255.0f;
            g_State.global_ambient[1] = ((value >>  8) & 0xFF) / 255.0f;
            g_State.global_ambient[2] = ((value      ) & 0xFF) / 255.0f;
            g_State.global_ambient[3] = ((value >> 24) & 0xFF) / 255.0f;
            break;
        }

        case GLES3_RS_STENCILENABLE:
            if (value) glEnable(GL_STENCIL_TEST);
            else       glDisable(GL_STENCIL_TEST);
            break;

        case GLES3_RS_STENCILFUNC:
        case GLES3_RS_STENCILREF:
        case GLES3_RS_STENCILMASK: {
            const unsigned int* rs = g_State.render_states;
            glStencilFunc(GLES3_Helpers::CmpFunc_To_GL(rs[GLES3_RS_STENCILFUNC]),
                          (GLint)rs[GLES3_RS_STENCILREF], rs[GLES3_RS_STENCILMASK]);
            break;
        }

        case GLES3_RS_STENCILFAIL:
        case GLES3_RS_STENCILZFAIL:
        case GLES3_RS_STENCILPASS: {
            const unsigned int* rs = g_State.render_states;
            glStencilOp(GLES3_Helpers::StencilOp_To_GL(rs[GLES3_RS_STENCILFAIL]),
                        GLES3_Helpers::StencilOp_To_GL(rs[GLES3_RS_STENCILZFAIL]),
                        GLES3_Helpers::StencilOp_To_GL(rs[GLES3_RS_STENCILPASS]));
            break;
        }

        case GLES3_RS_STENCILWRITEMASK:
            glStencilMask(value);
            break;

        // D3D passes these floats as their bit patterns (DX8Wrapper::Set_Fog).
        case GLES3_RS_FOGSTART:
            memcpy(&g_State.fog_start, &value, sizeof(float));
            break;
        case GLES3_RS_FOGEND:
            memcpy(&g_State.fog_end, &value, sizeof(float));
            break;

        // States handled in shader uniforms (no direct GL equivalent):
        case GLES3_RS_FOGDENSITY:
        case GLES3_RS_COLORVERTEX:
        case GLES3_RS_NORMALIZENORMALS:
            // Stored in g_State, applied via shader uniforms
            break;

        case GLES3_RS_TEXTUREFACTOR: {
            // Unpack 0xAARRGGBB
            g_State.texture_factor[0] = ((value >> 16) & 0xFF) / 255.0f; // Red
            g_State.texture_factor[1] = ((value >>  8) & 0xFF) / 255.0f; // Green
            g_State.texture_factor[2] = ((value      ) & 0xFF) / 255.0f; // Blue
            g_State.texture_factor[3] = ((value >> 24) & 0xFF) / 255.0f; // Alpha
            break;
        }

        case GLES3_RS_ZBIAS:
            if (value > 0) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -((float)value) * 2.0f);
            } else {
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            break;

        default:
            // Unhandled state — log for debugging
            // fprintf(stderr, "WARN: Unhandled render state %d = %u\n", state, value);
            break;
    }
}


// ============================================================================
// DX8Wrapper API — Transforms
// ============================================================================

void GLES3_Set_Transform(unsigned int type, const float* matrix_row_major) {
    float* target = nullptr;
    switch (type) {
        case GLES3_TS_WORLD:      target = g_State.world_matrix; break;
        case GLES3_TS_VIEW:       target = g_State.view_matrix; break;
        case GLES3_TS_PROJECTION: target = g_State.projection_matrix; break;
        default:
            if (type >= GLES3_TS_TEXTURE0 && type <= GLES3_TS_TEXTURE3)
                target = g_State.texture_matrices[type - GLES3_TS_TEXTURE0];
            break;
    }
    if (target) {
        memcpy(target, matrix_row_major, 16 * sizeof(float));
        if (type == GLES3_TS_VIEW || type == GLES3_TS_PROJECTION) g_SceneDirty = true;
    }
}

void GLES3_Get_Transform(unsigned int type, float* matrix_out) {
    const float* src = nullptr;
    switch (type) {
        case GLES3_TS_WORLD:      src = g_State.world_matrix; break;
        case GLES3_TS_VIEW:       src = g_State.view_matrix; break;
        case GLES3_TS_PROJECTION: src = g_State.projection_matrix; break;
        default:
            if (type >= GLES3_TS_TEXTURE0 && type <= GLES3_TS_TEXTURE3)
                src = g_State.texture_matrices[type - GLES3_TS_TEXTURE0];
            break;
    }
    if (src) memcpy(matrix_out, src, 16 * sizeof(float));
}


// ============================================================================
// DX8Wrapper API — Viewport
// ============================================================================

static float g_LodBias = 0.0f; // log2(drawing buffer / back buffer), set by the viewport
void GLES3_Set_Back_Buffer_Size(int w, int h) { g_BackBufferW = w; g_BackBufferH = h; }

static int g_GLViewport[4] = { -1, -1, -1, -1 }; // the GL viewport in force: x, y (from the bottom), w, h
void GLES3_Set_Viewport(unsigned int x, unsigned int y,
                         unsigned int w, unsigned int h,
                         float min_z, float max_z) {
    g_State.viewport = { x, y, w, h, min_z, max_z };
    g_SceneDirty = true; // viewport rect and lod bias
    
    // Drawing target: the offscreen 3D framebuffer during the 3D pass, else the canvas.
    Canvas_Size();
    const int canvas_w = g_In3DPass ? g_FboW : g_CanvasW;
    const int canvas_h = g_In3DPass ? g_FboH : g_CanvasH;
    
    // Direct3D viewport Y coordinate starts at top-left.
    // OpenGL viewport Y coordinate starts at bottom-left.
    // Optional render scale: the drawing buffer can be smaller than the back buffer the
    // engine renders for; scale its viewport (XYZRHW vertices follow u_ViewportRect).
    g_LodBias = 0.0f;
    if (g_BackBufferW > 0 && g_BackBufferH > 0 && (canvas_w != g_BackBufferW || canvas_h != g_BackBufferH)) {
        double sx = (double)canvas_w / g_BackBufferW, sy = (double)canvas_h / g_BackBufferH;
        g_LodBias = (float)std::log2(sx < 1.0 ? sx : 1.0);
        unsigned int x2 = (unsigned int)(x * sx + 0.5), y2 = (unsigned int)(y * sy + 0.5);
        w = (unsigned int)((x + w) * sx + 0.5) - x2;
        h = (unsigned int)((y + h) * sy + 0.5) - y2;
        x = x2; y = y2;
    }
    int gl_y = canvas_h - (int)y - (int)h;
    if (gl_y < 0) gl_y = 0;
    
    // The engine re-sets the same viewport many times a frame.
    int* last = g_GLViewport;
    static float last_z[2] = { -1.0f, -1.0f };
    if (last[0] != (int)x || last[1] != gl_y || last[2] != (int)w || last[3] != (int)h) {
        glViewport(x, (unsigned int)gl_y, w, h);
        last[0] = (int)x; last[1] = gl_y; last[2] = (int)w; last[3] = (int)h;
    }
    if (last_z[0] != min_z || last_z[1] != max_z) {
        glDepthRangef(min_z, max_z);
        last_z[0] = min_z; last_z[1] = max_z;
    }
}


// ============================================================================
// DX8Wrapper API — Texture Binding
// ============================================================================

static GLuint g_ActiveUnit = ~0u;
static GLuint g_UnitTexture[8] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };
static GLuint g_UnitSampler[8] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };

static void Active_Unit(unsigned int stage) {
    if (g_ActiveUnit != stage) { glActiveTexture(GL_TEXTURE0 + stage); g_ActiveUnit = stage; }
}

void GLES3_Bind_Texture_For_Upload(GLuint tex) {
    if (g_ActiveUnit >= 8) Active_Unit(0);
    if (tex >= g_TexSerial.size()) g_TexSerial.resize(tex + 64, 0);
    g_TexSerial[tex] = ++gx_content_serial;
    if (g_UnitTexture[g_ActiveUnit] != tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        g_UnitTexture[g_ActiveUnit] = tex;
    }
}

// Texture uploads bind on the active unit and put the old binding back, but by GL id
// only: forget what that unit holds so the next SetTexture binds for real.
void GLES3_Texture_Binding_Unknown() {
    if (g_ActiveUnit < 8) g_UnitTexture[g_ActiveUnit] = ~0u;
}

// Wrap and filter modes live in WebGL2 sampler objects, one per combination, instead of
// four glTexParameteri on every bind and every stage-state change.
static GLuint Sampler_For(GLenum wrap_s, GLenum wrap_t, GLenum mag, GLenum min) {
    static std::unordered_map<uint64_t, GLuint> cache;
    uint64_t key = (uint64_t)wrap_s << 48 ^ (uint64_t)wrap_t << 32 ^ (uint64_t)mag << 16 ^ (uint64_t)min;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    GLuint smp = 0;
    glGenSamplers(1, &smp);
    glSamplerParameteri(smp, GL_TEXTURE_WRAP_S, wrap_s);
    glSamplerParameteri(smp, GL_TEXTURE_WRAP_T, wrap_t);
    glSamplerParameteri(smp, GL_TEXTURE_MAG_FILTER, mag);
    glSamplerParameteri(smp, GL_TEXTURE_MIN_FILTER, min);
    cache[key] = smp;
    return smp;
}

void GLES3_Apply_Texture_Stage_Parameters(unsigned int stage) {
    GLuint tex = g_State.bound_textures[stage];
    if (tex == 0) return;

    unsigned int u = g_State.texture_stage_state[stage][GLES3_TSS_ADDRESSU];
    unsigned int v = g_State.texture_stage_state[stage][GLES3_TSS_ADDRESSV];
    GLenum wrap_s = u ? GLES3_Helpers::AddressMode_To_GL(u) : GL_REPEAT;
    GLenum wrap_t = v ? GLES3_Helpers::AddressMode_To_GL(v) : GL_REPEAT;

    unsigned int mag = g_State.texture_stage_state[stage][GLES3_TSS_MAGFILTER];
    GLenum gl_mag = mag ? GLES3_Helpers::FilterMode_To_GL(mag) : GL_LINEAR;

    unsigned int min = g_State.texture_stage_state[stage][GLES3_TSS_MINFILTER];
    unsigned int mip = g_State.texture_stage_state[stage][GLES3_TSS_MIPFILTER];
    bool has_mipmaps = GLES3_Texture_Has_Mipmaps(tex);
    if (min == 0) min = 1; // Default D3D8: POINT (1)

    GLenum gl_min_filter = GL_LINEAR;
    if (!has_mipmaps || mip == 0) {
        gl_min_filter = GLES3_Helpers::FilterMode_To_GL(min);
    } else if (mip == 1) {
        gl_min_filter = (min == 1) ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_NEAREST;
    } else {
        // Low-end machines (window.gxLowEnd: software GPU, <= 2 cores or <= 2 GB) sample one
        // mip level instead of blending two (bilinear, not trilinear): half the texel reads
        // for every textured pixel, which a software renderer pays on the CPU. No anisotropic
        // filtering is used anywhere, so there is none to turn off.
        static int low = -1;
        if (low < 0) low = MAIN_THREAD_EM_ASM_INT({
            if (typeof window === 'undefined') return 0;
            var o = window.GeneralsX && window.GeneralsX.opt; // performance switch: Simpler texture filtering
            return (o ? o.bilinear : window.gxLowEnd) ? 1 : 0; });
        if (low) gl_min_filter = (min == 1) ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_NEAREST;
        else gl_min_filter = (min == 1) ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR;
    }

    GLuint smp = Sampler_For(wrap_s, wrap_t, gl_mag, gl_min_filter);
    if (g_UnitSampler[stage] != smp) { glBindSampler(stage, smp); g_UnitSampler[stage] = smp; }
}

void GLES3_Set_Texture(unsigned int stage, GLuint gl_texture) {
    if (stage >= 8) return;
    g_State.bound_textures[stage] = gl_texture;
    if (g_UnitTexture[stage] != gl_texture) {
        Active_Unit(stage);
        glBindTexture(GL_TEXTURE_2D, gl_texture);
        g_UnitTexture[stage] = gl_texture;
    }
    if (gl_texture) GLES3_Apply_Texture_Stage_Parameters(stage);
}

void GLES3_Bind_Shroud_Unit();
int GLES3_Free_Program_Slots();
bool GLES3_Set_Shroud_Fold(GLuint tex, const float* d3d_matrix) {
    // A folded draw needs its own program; if the slots are nearly used up (they are never
    // freed), refuse, so the engine draws the separate shroud pass instead of no shroud.
    if (tex && GLES3_Free_Program_Slots() < 64) tex = 0;
    if (!(g_PageSwitches & 16)) tex = 0; // specialised shaders switched off: only they can fold
    g_State.shroud_tex = tex;
    if (!tex) return false;
    if (d3d_matrix) memcpy(g_State.shroud_matrix, d3d_matrix, sizeof(g_State.shroud_matrix));
    // Unit 4 (the stages use 0-3): the shroud pass sampled it bilinear, edges clamped to the
    // border texels the shroud fills.
    GLES3_Bind_Shroud_Unit();
    return true;
}

// Texture uploads bind on the active unit and forget what it held, so a folded draw checks
// that unit 4 still has the shroud (Apply_Pipeline_State), and the active unit goes back.
void GLES3_Bind_Shroud_Unit() {
    const GLuint tex = g_State.shroud_tex;
    if (!tex) return;
    if (g_UnitTexture[4] != tex) {
        const GLuint was = g_ActiveUnit;
        Active_Unit(4);
        glBindTexture(GL_TEXTURE_2D, tex);
        g_UnitTexture[4] = tex;
        if (was < 8) Active_Unit(was);
    }
    GLuint smp = Sampler_For(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
    if (g_UnitSampler[4] != smp) { glBindSampler(4, smp); g_UnitSampler[4] = smp; }
}

void GLES3_Set_Texture_Stage_State(unsigned int stage, unsigned int state,
                                    unsigned int value) {
    if (stage >= 8 || state >= GLES3_TSS_MAX) return;
    g_State.texture_stage_state[stage][state] = value;

    // Only the wrap and filter states feed the sampler.
    bool sampler_state = state == GLES3_TSS_ADDRESSU || state == GLES3_TSS_ADDRESSV ||
                         state == GLES3_TSS_MAGFILTER || state == GLES3_TSS_MINFILTER ||
                         state == GLES3_TSS_MIPFILTER;
    if (sampler_state && g_State.bound_textures[stage] != 0) {
        GLES3_Apply_Texture_Stage_Parameters(stage);
    }
}



// ============================================================================
// DX8Wrapper API — Lighting
// ============================================================================

void GLES3_Set_Light(unsigned int index, const GLES3_Light* light) {
    if (index >= 4) return;
    memcpy(&g_State.lights[index], light, sizeof(GLES3_Light));
    g_SceneDirty = true;
}

void GLES3_Set_Vertex_Has_Color(bool has_color) {
    g_State.vertex_has_color = has_color;
}

void GLES3_Set_Pretransformed(bool pretransformed) {
    g_State.pretransformed = pretransformed;
}

// ---------------------------------------------------------------------------
// GL state shadow (see gles3_wrapper.h)
// ---------------------------------------------------------------------------
static GLuint g_ArrayBuffer = ~0u, g_ElementBuffer = ~0u;
struct GLES3_AttribShadow {
    int enabled;            // -1 unknown, 0 constant, 1 array
    GLuint buffer; GLint size; GLenum type; GLboolean norm; GLsizei stride; uintptr_t offset;
    float c[4];
};
static GLES3_AttribShadow g_Attrib[16];

void GLES3_Attrib_Shadow_Reset() {
    for (auto& a : g_Attrib) a.enabled = -1;
    g_ArrayBuffer = g_ElementBuffer = ~0u;
}

// ---------------------------------------------------------------------------
// Vertex array objects cached per vertex layout: (vertex buffer, index buffer, FVF, stride,
// base vertex offset). Each draw used to re-point ~5 attributes, because the base vertex is
// baked into the pointer offsets (WebGL2 has no base-vertex draw); a cached layout is one
// bindVertexArray. Attribute arrays and the element buffer are per-VAO state, so the shadow
// above is reset whenever the bound VAO changes; the constant attribute values are context
// state and always the same per location (gles3_fvf.cpp). Index buffers are uploaded through
// GL_COPY_WRITE_BUFFER so an upload never rebinds the element buffer of the bound VAO.
// ---------------------------------------------------------------------------
struct GLES3_LayoutKey {
    GLuint vbo, ibo; unsigned int fvf, stride, base;
    bool operator==(const GLES3_LayoutKey& o) const {
        return vbo == o.vbo && ibo == o.ibo && fvf == o.fvf && stride == o.stride && base == o.base;
    }
};
struct GLES3_LayoutHash {
    size_t operator()(const GLES3_LayoutKey& k) const {
        size_t h = k.vbo * 0x9E3779B1u;
        h ^= k.ibo * 0x85EBCA77u + (h << 6) + (h >> 2);
        h ^= k.fvf * 0xC2B2AE3Du + (h << 6) + (h >> 2);
        h ^= k.stride + (h << 6) + (h >> 2);
        h ^= k.base * 0x27D4EB2Fu + (h << 6) + (h >> 2);
        return h;
    }
};
static std::unordered_map<GLES3_LayoutKey, GLuint, GLES3_LayoutHash> g_Layouts;
static std::unordered_map<GLuint, unsigned int> g_LayoutBase;	// a reused layout's current offset
static GLuint g_CopyWriteBuffer = ~0u; // index uploads (GLES3_Upload_Index_Data)
int gx_layout_misses = 0, gx_layout_clears = 0; // GXLAYOUT log (gles3_mainloop.cpp)

static void Bind_VAO(GLuint vao, GLuint element_buffer) {
    if (vao == g_BoundVAO) return;
    glBindVertexArray(vao);
    g_BoundVAO = vao;
    for (auto& a : g_Attrib) a.enabled = -1;
    g_ElementBuffer = element_buffer;
}

void GLES3_Bind_Default_VAO() { Bind_VAO(g_State.vao, ~0u); }

static void GLES3_Layouts_Clear() {
    if (g_Layouts.empty()) return;
    GLES3_Bind_Default_VAO();
    for (auto& kv : g_Layouts) glDeleteVertexArrays(1, &kv.second);
    g_Layouts.clear();
    g_LayoutBase.clear();
    gx_layout_clears++;
}

// Reused layouts (Settings switch reuseLayouts, page switch bit 24): effect and dynamic buffers
// draw from a different starting offset almost every time, and the offset was part of the key,
// so a battle made ~228,000 vertex layouts (VAOs) and flushed the cache 55 times. With the
// switch, one layout per buffer setup is kept and only re-pointed when the offset changes.

bool GLES3_Bind_Layout(GLuint vbo, GLuint ibo, unsigned int fvf, unsigned int stride, unsigned int base) {
    const bool reuse = (g_PageSwitches & 16777216) != 0;
    GLES3_LayoutKey k = { vbo, ibo, fvf, stride, reuse ? 0u : base };
    static GLES3_LayoutKey last_key = {};
    static GLuint last_vao = 0;
    if (last_vao && last_vao == g_BoundVAO && k == last_key && (!reuse || g_LayoutBase[last_vao] == base)) return true; // same layout as the last draw
    auto it = g_Layouts.find(k);
    if (it != g_Layouts.end()) {
        Bind_VAO(it->second, ibo);
        last_key = k; last_vao = it->second;
        if (reuse) {
            unsigned int& b = g_LayoutBase[it->second];
            if (b != base) {	// same buffers, new offset: the caller re-points the attributes
                b = base;
                GLES3_Bind_Array_Buffer(vbo);
                return false;
            }
        }
        return true;
    }
    // ponytail: flush when full; an LRU if a scene ever cycles through more layouts than this.
    if (g_Layouts.size() >= 4096) GLES3_Layouts_Clear();
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    g_Layouts.emplace(k, vao);
    if (reuse) g_LayoutBase[vao] = base;
    Bind_VAO(vao, ~0u);
    last_key = k; last_vao = vao;
    GLES3_Bind_Array_Buffer(vbo);
    GLES3_Bind_Element_Buffer(ibo);
    gx_layout_misses++;
    return false; // the caller sets the attributes up, into this new VAO
}

void GLES3_Upload_Index_Data(GLuint ibo, const void* data, size_t lo, size_t hi, size_t total, bool allocate) {
    if (allocate) {
        // WebGL types a buffer by its first binding: it must be ELEMENT_ARRAY_BUFFER (a buffer
        // first bound to COPY_WRITE could never be an index buffer). Done in the default VAO.
        GLES3_Bind_Default_VAO();
        GLES3_Bind_Element_Buffer(ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)total, data, GL_DYNAMIC_DRAW);
        return;
    }
    if (g_CopyWriteBuffer != ibo) { glBindBuffer(GL_COPY_WRITE_BUFFER, ibo); g_CopyWriteBuffer = ibo; }
    glBufferSubData(GL_COPY_WRITE_BUFFER, (GLintptr)lo, (GLsizeiptr)(hi - lo), (const unsigned char*)data + lo);
}

void GLES3_Bind_Array_Buffer(GLuint buffer) {
    if (buffer != g_ArrayBuffer) { glBindBuffer(GL_ARRAY_BUFFER, buffer); g_ArrayBuffer = buffer; }
}

void GLES3_Bind_Element_Buffer(GLuint buffer) {
    if (buffer != g_ElementBuffer) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer); g_ElementBuffer = buffer; }
}

void GLES3_Delete_Buffer(GLuint* buffer) {
    if (!buffer || !*buffer) return;
    // A reused id must not find a VAO made for the old buffer.
    for (auto it = g_Layouts.begin(); it != g_Layouts.end();) {
        if (it->first.vbo != *buffer && it->first.ibo != *buffer) { ++it; continue; }
        if (g_BoundVAO == it->second) GLES3_Bind_Default_VAO();
        g_LayoutBase.erase(it->second);
        glDeleteVertexArrays(1, &it->second);
        it = g_Layouts.erase(it);
    }
    glDeleteBuffers(1, buffer); // also unbinds it from COPY_WRITE, and the id can be reused
    *buffer = 0;
    g_CopyWriteBuffer = ~0u;
    GLES3_Attrib_Shadow_Reset();
}

void GLES3_Attrib_Pointer(GLuint loc, GLint size, GLenum type, GLboolean norm, GLsizei stride, uintptr_t offset) {
    GLES3_AttribShadow& a = g_Attrib[loc];
    if (a.enabled != 1) { glEnableVertexAttribArray(loc); a.enabled = 1; a.buffer = ~0u; }
    if (a.buffer != g_ArrayBuffer || a.size != size || a.type != type || a.norm != norm ||
        a.stride != stride || a.offset != offset) {
        glVertexAttribPointer(loc, size, type, norm, stride, (const void*)offset);
        a.buffer = g_ArrayBuffer; a.size = size; a.type = type; a.norm = norm; a.stride = stride; a.offset = offset;
    }
}

void GLES3_Attrib_Constant(GLuint loc, float x, float y, float z, float w) {
    GLES3_AttribShadow& a = g_Attrib[loc];
    if (a.enabled != 0) {
        glDisableVertexAttribArray(loc); a.enabled = 0;
        a.c[0] = x + 1.0f; // force the constant below
    }
    if (a.c[0] != x || a.c[1] != y || a.c[2] != z || a.c[3] != w) {
        glVertexAttrib4f(loc, x, y, z, w);
        a.c[0] = x; a.c[1] = y; a.c[2] = z; a.c[3] = w;
    }
}

// ---------------------------------------------------------------------------
// ps.1.1 pixel shaders. The game's are short (terrain blend, flat terrain, roads,
// monochrome filter): tex, mov, add, sub, mul, mad, dp3, dp4, lrp with the usual source
// modifiers. Each is encoded for the fragment shader's interpreter (see u_PS there).
// ---------------------------------------------------------------------------
struct GLES3_PSProgram { int count; int data[16 * 8]; };
static std::vector<GLES3_PSProgram> g_PixelShaders;

// Register file index: r0-r1 0-1, t0-t3 2-5, v0-v1 6-7, c0-c7 8-15.
static int PS_Reg(unsigned int tok, bool& ok) {
    unsigned int type = (tok >> 28) & 7, num = tok & 0x7FF;
    if (type == 0 && num < 2) return (int)num;       // D3DSPR_TEMP
    if (type == 3 && num < 4) return 2 + (int)num;   // D3DSPR_TEXTURE
    if (type == 1 && num < 2) return 6 + (int)num;   // D3DSPR_INPUT
    if (type == 2 && num < 8) return 8 + (int)num;   // D3DSPR_CONST
    ok = false;
    return 0;
}

static int PS_Src(unsigned int tok, bool& ok) {
    int r = PS_Reg(tok, ok);
    unsigned int swz = (tok >> 16) & 0xFF, mod = (tok >> 24) & 0xF;
    int sw = swz == 0xE4 ? 0 : swz == 0xFF ? 1 : swz == 0xAA ? 2 : -1;
    if (sw < 0 || mod > 8) ok = false;
    return r | ((sw < 0 ? 0 : sw) << 5) | ((int)mod << 8);
}

unsigned int GLES3_Create_Pixel_Shader(const unsigned int* t) {
    if (!t || (t[0] & 0xFFFF0000u) != 0xFFFF0000u || (t[0] & 0xFFFF) > 0x0104) return 0;
    GLES3_PSProgram p = {};
    bool ok = true;
    for (unsigned int i = 1; ok && i < 512;) {
        unsigned int ins = t[i++], op = ins & 0xFFFF;
        if (op == 0xFFFF) break;                                     // end
        if (op == 0xFFFE) { i += (ins >> 16) & 0x7FFF; continue; }   // comment
        if (op == 0) continue;                                       // nop
        int nsrc;
        switch (op) {
            case 66: nsrc = 0; break;                                // tex
            case 1: nsrc = 1; break;                                 // mov
            case 2: case 3: case 5: case 8: case 9: nsrc = 2; break; // add sub mul dp3 dp4
            case 4: case 18: nsrc = 3; break;                        // mad lrp
            default: nsrc = -1; break;                               // texbem, def, cnd, ...
        }
        if (nsrc < 0 || p.count >= 16) { ok = false; break; }
        unsigned int dst = t[i++];
        int* d = &p.data[p.count++ * 8];
        if (op == 66) { d[0] = 66; continue; } // the stage samples are loaded up front
        d[0] = (int)op | (int)((dst >> 16) & 0xF) << 8 | (int)((dst >> 24) & 0xF) << 12
             | (int)((dst >> 20) & 1) << 16; // write mask, result shift, saturate
        d[1] = PS_Reg(dst, ok);
        for (int k = 0; k < nsrc; k++) d[2 + k] = PS_Src(t[i++], ok);
    }
    if (!ok || p.count == 0) return 0;
    g_PixelShaders.push_back(p);
    return (unsigned int)g_PixelShaders.size();
}

std::string GLES3_PS_Generate_GLSL(unsigned int handle) {
    if (handle == 0 || handle > g_PixelShaders.size()) return std::string();
    const GLES3_PSProgram& p = g_PixelShaders[handle - 1];
    static const char* reg[16] = { "r0", "r1", "t0", "t1", "t2", "t3", "v0", "v1",
        "u_PSConst[0]", "u_PSConst[1]", "u_PSConst[2]", "u_PSConst[3]",
        "u_PSConst[4]", "u_PSConst[5]", "u_PSConst[6]", "u_PSConst[7]" };
    auto src = [&](int e) {
        std::string x = reg[e & 15];
        int swz = (e >> 5) & 3, m = (e >> 8) & 15;
        if (swz == 1) x += ".aaaa"; else if (swz == 2) x += ".bbbb";
        switch (m) {
            case 1: return "(-" + x + ")";
            case 2: return "(" + x + " - 0.5)";
            case 3: return "(0.5 - " + x + ")";
            case 4: return "(" + x + " * 2.0 - 1.0)";
            case 5: return "(1.0 - " + x + " * 2.0)";
            case 6: return "(1.0 - " + x + ")";
            case 7: return "(" + x + " * 2.0)";
            case 8: return "(-" + x + " * 2.0)";
            default: return x;
        }
    };
    std::string g = "vec4 run_ps_gen() {\n vec4 r0 = vec4(0.0), r1 = vec4(0.0);\n"
                    " vec4 t0 = sample_stage(0), t1 = sample_stage(1), t2 = sample_stage(2), t3 = sample_stage(3);\n"
                    " vec4 v0 = v_Color, v1 = v_Specular;\n";
    for (int i = 0; i < p.count; i++) {
        const int* d = &p.data[i * 8];
        int op = d[0] & 255;
        if (op == 66) continue; // tex: t0-t3 sampled above
        std::string s0 = src(d[2]), s1 = src(d[3]), s2 = src(d[4]), e;
        switch (op) {
            case 1: e = s0; break;
            case 2: e = s0 + " + " + s1; break;
            case 3: e = s0 + " - " + s1; break;
            case 4: e = s0 + " * " + s1 + " + " + s2; break;
            case 8: e = "vec4(dot(" + s0 + ".rgb, " + s1 + ".rgb))"; break;
            case 9: e = "vec4(dot(" + s0 + ", " + s1 + "))"; break;
            case 18: e = "mix(" + s2 + ", " + s1 + ", " + s0 + ")"; break;
            default: e = s0 + " * " + s1; break;
        }
        int shift = (d[0] >> 12) & 15;
        const char* sc = shift == 1 ? " * 2.0" : shift == 2 ? " * 4.0" : shift == 15 ? " * 0.5" : "";
        const char* lim = ((d[0] >> 16) & 1) ? "0.0, 1.0" : "-1.0, 1.0"; // _sat, else ps.1.1 range
        int mask = (d[0] >> 8) & 15;
        std::string comps;
        for (int c = 0; c < 4; c++) if (mask & (1 << c)) comps += "rgba"[c];
        g += " { vec4 r = clamp((" + e + ")" + sc + ", " + lim + "); " + reg[d[1] & 15] +
             (comps.size() == 4 ? std::string(" = r;") : "." + comps + " = r." + comps + ";") + " }\n";
    }
    g += " return clamp(r0, 0.0, 1.0);\n}\n";
    return g;
}

void GLES3_Set_Pixel_Shader(unsigned int handle) {
    g_State.pixel_shader = handle <= g_PixelShaders.size() ? handle : 0;
}

void GLES3_Set_Pixel_Shader_Constants(unsigned int first, const float* data, unsigned int count) {
    for (unsigned int i = 0; data && i < count && first + i < 8; i++)
        memcpy(g_State.ps_const[first + i], data + i * 4, sizeof(float) * 4);
}

static void GLES3_Upload_Pixel_Shader(const GLES3_ProgramUniformLocations& locs, unsigned int handle) {
    const GLES3_PSProgram* p = handle ? &g_PixelShaders[handle - 1] : nullptr;
    if (locs.u_PSCount != -1) glUniform1i(locs.u_PSCount, p ? p->count : 0);
    if (p && locs.u_PS != -1) glUniform4iv(locs.u_PS, p->count * 2, p->data);
}

void GLES3_Enable_Light(unsigned int index, bool enable) {
    if (index >= 4) return;
    g_State.light_enabled[index] = enable;
    g_SceneDirty = true;
}

void GLES3_Set_Material(const GLES3_Material* mat) {
    memcpy(&g_State.material, mat, sizeof(GLES3_Material));
}


// ============================================================================
// DX8Wrapper API — Fog
// ============================================================================

void GLES3_Set_Fog(bool enable, float r, float g, float b,
                    float start, float end) {
    g_State.fog_enabled = enable;
    g_SceneDirty = true;
    g_State.fog_color[0] = r;
    g_State.fog_color[1] = g;
    g_State.fog_color[2] = b;
    g_State.fog_start = start;
    g_State.fog_end = end;
}


// ============================================================================
// DX8Wrapper API — Drawing
// ============================================================================

/*
** Apply_Pipeline_State — Called before every draw call.
** Uploads the current DX8 state to the emulation shader as uniforms.
** This is where the DX8 fixed-function pipeline gets translated to GLSL.
*/
// ============================================================================
// Ground cache
// ============================================================================
// On software and weak GPUs the terrain's base pass is about half of a frame, and in a
// strategy game the camera mostly stands still. So the pass is kept as an image (colour +
// depth) and copied back while nothing that feeds it has changed.
//
// The caller wraps its ground draws:
//     const bool gc = GLES3_Ground_Cache_Begin();
//     do { ...the draws... } while (gc && GLES3_Ground_Cache_Again());
// First time round the draws are not issued, only fingerprinted: all of the pipeline state
// (matrices, lights, material, stage setup, viewport), which textures and buffers they use
// and those objects' content serials. Then:
//   - same fingerprint as the cached image: copy it in, done;
//   - same as the previous frame's (the view just settled): draw again into the cache, copy;
//   - anything else (panning, a light on the ground, the shroud moving): draw again, plainly.
// The image is copied with glBlitFramebuffer, colour and depth, so the depth values are the
// rasteriser's own (a shader paste through gl_FragDepth came out slightly nearer and hid
// objects standing on the ground). A blit replaces the whole target, which is right because
// the scene draws the terrain first, on the frame's clear; the cache starts from that clear.
static struct {
    int mode = 0; // 0 idle, 1 fingerprinting, 2 drawing into the cache, 3 drawing plainly
    uint64_t probe = 0, last_probe = 0, cached = 0;
    bool valid = false, unsafe = false, checked = false;
    // Pan cache: the camera and look of the cached image, and of this frame's first ground draw.
    float view0[16] = {}, view1[16] = {}, proj1[16] = {}, plane_h = 0;
    uint64_t style0 = 0, style1 = 0;
    bool first = false, plane_ok = false;
    int slides = 0; // frames slid since the cached image was drawn
    GLuint fbo = 0, color = 0, depth = 0;
    int w = 0, h = 0, tw = 0, th = 0; // image size (-1: cache unusable here); this frame's target size
} g_GC;

static inline void GC_Mix(const void* data, size_t n) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = g_GC.probe;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; } // FNV-1a
    g_GC.probe = h;
}

void GLES3_Ground_Cache_Mix(unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e) {
    if (g_GC.mode != 1) return;
    const unsigned int v[5] = { a, b, c, d, e };
    GC_Mix(v, sizeof(v));
}

// One suppressed draw's fingerprint. Stencil writes can't be replayed from the image.
static void GC_Fingerprint(unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e) {
    if (g_State.render_states[GLES3_RS_STENCILENABLE] || g_State.pretransformed) g_GC.unsafe = true;
    if (g_GC.first) {
        g_GC.first = false;
        memcpy(g_GC.view1, g_State.view_matrix, sizeof(g_GC.view1));
        memcpy(g_GC.proj1, g_State.projection_matrix, sizeof(g_GC.proj1));
        // What the ground looks like apart from where the camera stands: projection, material,
        // lights, fog, stage setup, render states and viewport of its first draw.
        const uint64_t keep = g_GC.probe;
        g_GC.probe = 14695981039346656037ull;
        GC_Mix(g_State.projection_matrix, sizeof(g_State.projection_matrix));
        GC_Mix(&g_State.material, offsetof(GLES3_PipelineState, texture_stage_state) - offsetof(GLES3_PipelineState, material));
        GC_Mix(g_State.texture_stage_state, sizeof(g_State.texture_stage_state));
        GC_Mix(g_State.render_states, sizeof(g_State.render_states));
        GC_Mix(&g_State.viewport, sizeof(g_State.viewport));
        g_GC.style1 = g_GC.probe;
        g_GC.probe = keep;
    }
    GC_Mix(&g_State, offsetof(GLES3_PipelineState, active_program));
    unsigned int v[5 + 9] = { a, b, c, d, e };
    for (int i = 0; i < 8; i++) {
        const GLuint t = g_State.bound_textures[i];
        v[5 + i] = t < g_TexSerial.size() ? g_TexSerial[t] : 0;
    }
    v[13] = g_State.shroud_tex < g_TexSerial.size() ? g_TexSerial[g_State.shroud_tex] : 0;
    GC_Mix(v, sizeof(v));
}

static bool GC_Target() {
    if (g_GC.w < 0) return false;
    if (!g_GC.fbo) {
        glGenFramebuffers(1, &g_GC.fbo);
        glGenRenderbuffers(1, &g_GC.color);
        glGenRenderbuffers(1, &g_GC.depth);
    }
    if (g_GC.w != g_GC.tw || g_GC.h != g_GC.th) {
        g_GC.valid = false;
        glBindFramebuffer(GL_FRAMEBUFFER, g_GC.fbo);
        glBindRenderbuffer(GL_RENDERBUFFER, g_GC.color);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, g_GC.tw, g_GC.th);
        glBindRenderbuffer(GL_RENDERBUFFER, g_GC.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, g_GC.tw, g_GC.th); // the targets' own depth format: a depth blit needs a match
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_GC.color);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_GC.depth);
        const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, g_In3DPass ? g_Fbo : 0);
        if (!ok) { fprintf(stderr, "GLES3: ground cache framebuffer incomplete; cache off\n"); g_GC.w = -1; return false; }
        g_GC.w = g_GC.tw; g_GC.h = g_GC.th;
    }
    return true;
}

// Copies the cached ground (colour + depth) over the current target. False: this browser
// refused the copy (its canvas depth format differs), and the cache is switched off.
static bool GC_Copy() {
    const GLuint target = g_In3DPass ? g_Fbo : 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_GC.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    if (!g_GC.checked) while (glGetError() != GL_NO_ERROR) {}
    glBlitFramebuffer(0, 0, g_GC.w, g_GC.h, 0, 0, g_GC.w, g_GC.h, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    bool ok = true;
    if (!g_GC.checked) { // once: asking for errors stalls the pipeline
        g_GC.checked = true;
        ok = glGetError() == GL_NO_ERROR;
        if (!ok) { fprintf(stderr, "GLES3: ground cache copy refused; cache off\n"); g_GC.w = -1; }
    }
    if (scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    return ok;
}

// Pan cache. The height of the ground plane the slide assumes (the ground under the middle of
// the view); the caller gives it once a frame, before its draws.
void GLES3_Ground_Cache_Plane(float height, bool ok) { g_GC.plane_h = height; g_GC.plane_ok = ok; }

// The camera moved without turning: instead of drawing the ground, the cached image is moved to
// where a flat ground at plane_h would now appear. With no roll every screen row of such a
// ground is at one distance, so a row keeps its depth (the depth image is copied as it is) and
// its colour slides and scales along x by that row's amount: thin horizontal strips, one blit
// each. Slopes come out a few pixels off until the next real draw (every gxPanFrames frames).
// False: not a pure pan, or nothing usable; the caller draws.
static bool GC_Slide() {
    const float* V0 = g_GC.view0; const float* V1 = g_GC.view1; const float* P = g_GC.proj1;
    for (int i = 0; i < 12; i++) { if (i % 4 == 3) continue; if (fabsf(V0[i] - V1[i]) > 1e-5f) { gx_gc_why = 1; return false; } } // turned
    // D3D row vectors: camera = world * V. World up (0,0,1) in camera space is row 2.
    const float nx = V1[8], ny = V1[9], nz = V1[10];
    if (fabsf(nx) > 1e-3f || fabsf(ny) < 1e-3f) { gx_gc_why = 2; return false; } // rolled, or looking straight down the plane
    const float dx = V1[12] - V0[12], dy = V1[13] - V0[13], dz = V1[14] - V0[14]; // how ground points moved, camera space
    // The camera follows the terrain's height a little as it scrolls; the rows below allow for
    // that, but the copied depth doesn't, so only small changes pass (zooming does not).
    if (fabsf(dx * nx + dy * ny + dz * nz) > 1.5f) { gx_gc_why = 3; return false; }
    const float fx = P[0], fy = P[5];
    const float sg = P[11]; // +1: the camera looks along +z, -1: along -z (this engine)
    if (fx <= 0 || fy <= 0 || (sg != 1.0f && sg != -1.0f)) { gx_gc_why = 4; return false; } // not a perspective projection we know
    const float dzs = sg * dz;
    // The 3D view's rectangle inside the target (NDC -1..1 maps onto it).
    const int VX = g_GLViewport[0], VY = g_GLViewport[1], W = g_GLViewport[2], H = g_GLViewport[3];
    if (W < 16 || H < 16 || VX < 0 || VY < 0 || VX + W > g_GC.w || VY + H > g_GC.h) { gx_gc_why = 5; return false; }
    const float D = g_GC.plane_h + (V1[12] * nx + V1[13] * ny + V1[14] * nz); // plane: n . p = D, new camera
    const GLuint target = g_In3DPass ? g_Fbo : 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_GC.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, g_GC.w, g_GC.h, 0, 0, g_GC.w, g_GC.h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    // Source row (old image) and x mapping for a new-image row, both in NDC.
    auto row = [&](float y1, float& y0, float& z0, float& z1) -> bool {
        const float den = ny * y1 / fy + sg * nz;
        if (fabsf(den) < 1e-6f) return false;
        z1 = D / den;               // distance of the ground on this row now
        z0 = z1 - dzs;              // and when the image was drawn
        if (z1 <= 1.0f || z0 <= 1.0f) return false; // above the horizon / behind the camera
        y0 = fy * (y1 * z1 / fy - dy) / z0;
        return true;
    };
    const int S = H >= 600 ? 8 : 6; // strip height in pixels
    for (int a = 0; a < H; a += S) {
        const int b = a + S < H ? a + S : H;
        float ya, yb, yc, z0, z1, t0, t1;
        if (!row(2.0f * a / H - 1.0f, ya, t0, t1) || !row(2.0f * b / H - 1.0f, yb, t0, t1)) continue;
        if (!row(2.0f * (a + b) * 0.5f / H - 1.0f, yc, z0, z1)) continue;
        int sa = (int)floorf((ya * 0.5f + 0.5f) * H + 0.5f), sb = (int)floorf((yb * 0.5f + 0.5f) * H + 0.5f);
        if (sb <= sa) sb = sa + 1;
        if (sb <= 0 || sa >= H) continue; // came from outside the old image
        // x1 = (x0 * z0 + fx * dx) / z1 for x0 = -1 and +1
        const int xl = (int)floorf(((-z0 + fx * dx) / z1 * 0.5f + 0.5f) * W + 0.5f);
        const int xr = (int)floorf((( z0 + fx * dx) / z1 * 0.5f + 0.5f) * W + 0.5f);
        if (xr <= xl) continue;
        glBlitFramebuffer(VX, VY + sa, VX + W, VY + sb, VX + xl, VY + a, VX + xr, VY + b, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    if (scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    return true;
}

bool GLES3_Ground_Cache_Begin() {
    if (!(g_PageSwitches & 8) || g_GC.mode || g_GC.w < 0) return false;
    if (g_In3DPass) { g_GC.tw = g_FboW; g_GC.th = g_FboH; }
    else {
        // The drawing buffer's size, from the context: the canvas element can't be asked from
        // the game thread of the multithreaded build (Canvas_Size goes through the page).
        g_GC.tw = g_GC.th = 0;
        emscripten_webgl_get_drawing_buffer_size(emscripten_webgl_get_current_context(), &g_GC.tw, &g_GC.th);
    }
    if (g_GC.tw < 1 || g_GC.th < 1) return false;
    g_GC.mode = 1;
    g_GC.unsafe = false;
    g_GC.first = true;
    g_GC.probe = 14695981039346656037ull;
    const int v[3] = { g_GC.tw, g_GC.th, g_In3DPass ? 1 : 0 };
    GC_Mix(v, sizeof(v));
    GC_Mix(&g_FrameClear, sizeof(g_FrameClear)); // the image starts from the frame's clear
    return true;
}

// After the caller's draws: true = run them once more (this time they are really drawn).
bool GLES3_Ground_Cache_Again() {
    if (g_GC.mode == 1) {
        const uint64_t probe = g_GC.probe, last = g_GC.last_probe;
        g_GC.last_probe = probe;
        if (!g_GC.unsafe && g_GC.valid && probe == g_GC.cached && g_GC.w == g_GC.tw && g_GC.h == g_GC.th && GC_Copy()) {
            g_GC.mode = 0;
            gx_gc_hits++;
            return false;
        }
        const bool pan = (g_PageSwitches & 128) != 0;
        if (pan) gx_gc_why = g_GC.unsafe ? 20 : !g_GC.valid ? 21 : !g_GC.checked ? 22 : !g_GC.plane_ok ? 23 : g_GC.style1 != g_GC.style0 ? 24 : (g_GC.slides + 1 >= ((g_PageSwitches >> 8) & 15)) ? 25 : 0;
        if (pan && !g_GC.unsafe && g_GC.valid && g_GC.checked && g_GC.plane_ok && g_GC.style1 == g_GC.style0 &&
            g_GC.w == g_GC.tw && g_GC.h == g_GC.th && g_GC.slides + 1 < ((g_PageSwitches >> 8) & 15) && GC_Slide()) {
            g_GC.slides++;
            g_GC.mode = 0;
            gx_gc_slides++;
            return false;
        }
        // With the pan cache on every real draw goes into the cache, to be slid on the next frames.
        if (!g_GC.unsafe && (probe == last || pan) && GC_Target()) {
            // Start the image from the frame's clear, so the copy can replace the whole target.
            glBindFramebuffer(GL_FRAMEBUFFER, g_GC.fbo);
            const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
            if (scissor) glDisable(GL_SCISSOR_TEST);
            glClearColor(g_FrameClear.r, g_FrameClear.g, g_FrameClear.b, g_FrameClear.a);
            glClearDepthf(g_FrameClear.z);
            glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDepthMask(g_State.render_states[GLES3_RS_ZWRITEENABLE] ? GL_TRUE : GL_FALSE);
            if (scissor) glEnable(GL_SCISSOR_TEST);
            g_GC.valid = false;
            g_GC.cached = probe;
            memcpy(g_GC.view0, g_GC.view1, sizeof(g_GC.view0));
            g_GC.style0 = g_GC.style1;
            g_GC.slides = 0;
            g_GC.mode = 2;
            return true;
        }
        g_GC.mode = 3;
        gx_gc_plain++;
        return true;
    }
    if (g_GC.mode == 2) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_In3DPass ? g_Fbo : 0);
        g_GC.valid = true;
        gx_gc_captures++;
        if (!GC_Copy()) { g_GC.valid = false; g_GC.mode = 3; return true; } // copy refused: draw plainly
    }
    g_GC.mode = 0;
    return false;
}

static unsigned int g_ActiveSlot = 0;	// the slot the current program belongs to (it may be a fallback)
extern int gx_fallback_draws;	// background shader compiles (GLES3_ShaderManager::Init)
static void Apply_Pipeline_State() {
    if (g_State.shroud_tex) GLES3_Bind_Shroud_Unit();
    unsigned int hash = GLES3_ShaderManager::Hash_Pipeline_Config(g_State);
    GLuint program = GLES3_ShaderManager::Get_Program(hash, GLES3_ShaderManager::Can_Fall_Back(hash));
    if (!program && hash >= 1024) {	// still compiling in the background: the general shader of this setup
        hash = GLES3_ShaderManager::Fallback_Slot(hash);
        program = GLES3_ShaderManager::Get_Program(hash);
        gx_fallback_draws++;
    }
    g_ActiveSlot = hash;
    if (program != g_State.active_program) {
        glUseProgram(program);
        g_State.active_program = program;
    }
    GLES3_ShaderManager::Apply_Uniforms(hash, g_State);
}

/*
** Draw_Triangles — The main draw call. Maps to glDrawElements.
**
** Original DX8: IDirect3DDevice8::DrawIndexedPrimitive(
**   D3DPT_TRIANGLELIST, min_vertex, num_vertices,
**   start_index, primitive_count)
**
** OpenGL ES 3.0: glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, offset)
*/
void GLES3_Draw_Triangles(unsigned int prim_type,
                           unsigned int start_index,
                           unsigned int prim_count,
                           unsigned int min_vertex,
                           unsigned int num_vertices) {
    if (g_GC.mode == 1) { GC_Fingerprint(prim_type, start_index, prim_count, min_vertex, num_vertices); return; }
    Apply_Pipeline_State();

    GLenum gl_prim = GLES3_Helpers::PrimType_To_GL(prim_type);

    unsigned int index_count = 0;
    switch (prim_type) {
        case GLES3_PT_TRIANGLELIST:  index_count = prim_count * 3; break;
        case GLES3_PT_TRIANGLESTRIP: index_count = prim_count + 2; break;
        case GLES3_PT_TRIANGLEFAN:   index_count = prim_count + 2; break;
        case GLES3_PT_LINELIST:      index_count = prim_count * 2; break;
        case GLES3_PT_LINESTRIP:     index_count = prim_count + 1; break;
        case GLES3_PT_POINTLIST:     index_count = prim_count; break;
    }

    glDrawElements(gl_prim, index_count, GL_UNSIGNED_SHORT,
                   (const void*)(start_index * sizeof(unsigned short)));

    g_State.draw_calls++;
    g_State.triangles_drawn += prim_count;
}

// GPU skinning. One buffer of bone numbers (one unsigned short per vertex) per skinned model.
GLuint GLES3_Skin_Bone_Buffer(const unsigned short* bones, unsigned int count) {
    GLuint b = 0;
    glGenBuffers(1, &b);
    GLES3_Bind_Array_Buffer(b);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(unsigned short), bones, GL_STATIC_DRAW);
    return b;
}

// Skinned instances of one mesh: `palette` holds, per instance, `bones` 3x4 matrices (12 floats,
// row by row, translation in the 4th column). The vertex shader picks each vertex's bone matrix
// from a float texture (u_BoneTex, unit 5), so the CPU sends ~30 matrices a soldier instead of
// deforming and uploading every vertex. Called through DrawIndexedPrimitive like any draw.
void GLES3_Draw_Triangles_Skinned(unsigned int start_index, unsigned int prim_count,
                                  const float* palette, unsigned int bones, unsigned int instance_count,
                                  GLuint bone_buffer) {
    if (!instance_count || !bones) return;
    if (g_GC.mode == 1) { g_GC.unsafe = true; return; } // not expected inside the ground pass
    Apply_Pipeline_State();

    static GLuint tex = 0;
    static int tw = 0, th = 0;
    Active_Unit(5);
    if (!tex) glGenTextures(1, &tex);
    if (g_UnitTexture[5] != tex) { glBindTexture(GL_TEXTURE_2D, tex); g_UnitTexture[5] = tex; }
    if (g_UnitSampler[5] != 0) { glBindSampler(5, 0); g_UnitSampler[5] = 0; }
    const int w = (int)bones * 3, h = (int)instance_count;
    if (w > tw || h > th) {
        if (w > tw) tw = w;
        if (h > th) th = h < 128 ? 128 : h;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tw, th, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, palette);

    GLES3_Bind_Array_Buffer(bone_buffer);
    glEnableVertexAttribArray(13);
    glVertexAttribPointer(13, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(unsigned short), (const void*)0);

    // The two uniforms' locations, per program (looked up once each).
    struct Locs { GLint bones, tex; };
    static std::unordered_map<GLuint, Locs> locs;
    auto it = locs.find(g_State.active_program);
    if (it == locs.end()) {
        Locs l = { glGetUniformLocation(g_State.active_program, "u_SkinBones"), glGetUniformLocation(g_State.active_program, "u_BoneTex") };
        if (l.tex != -1) glUniform1i(l.tex, 5);
        it = locs.emplace(g_State.active_program, l).first;
    }
    if (it->second.bones != -1) glUniform1i(it->second.bones, (GLint)bones);

    glDrawElementsInstanced(GL_TRIANGLES, prim_count * 3, GL_UNSIGNED_SHORT,
                            (const void*)(start_index * sizeof(unsigned short)), instance_count);

    if (it->second.bones != -1) glUniform1i(it->second.bones, 0);
    glDisableVertexAttribArray(13);
    GLES3_Attrib_Shadow_Reset();
    g_State.draw_calls++;
    g_State.triangles_drawn += prim_count * instance_count;
}

void GLES3_Draw_Triangles_Instanced(unsigned int prim_type,
                                    unsigned int start_index,
                                    unsigned int prim_count,
                                    unsigned int min_vertex,
                                    unsigned int num_vertices,
                                    const float* world_matrices,
                                    unsigned int instance_count) {
    if (instance_count == 0) return;
    if (g_GC.mode == 1) { g_GC.unsafe = true; return; } // not expected inside the ground pass
    Apply_Pipeline_State();

    GLenum gl_prim = GLES3_Helpers::PrimType_To_GL(prim_type);

    unsigned int index_count = 0;
    switch (prim_type) {
        case GLES3_PT_TRIANGLELIST:  index_count = prim_count * 3; break;
        case GLES3_PT_TRIANGLESTRIP: index_count = prim_count + 2; break;
        case GLES3_PT_TRIANGLEFAN:   index_count = prim_count + 2; break;
        case GLES3_PT_LINELIST:      index_count = prim_count * 2; break;
        case GLES3_PT_LINESTRIP:     index_count = prim_count + 1; break;
        case GLES3_PT_POINTLIST:     index_count = prim_count; break;
    }

    // Transpose row-major matrices in world_matrices to column-major for WebGL
    float* col_major_matrices = (float*)malloc(instance_count * 16 * sizeof(float));
    for (unsigned int inst = 0; inst < instance_count; ++inst) {
        GLES3_Helpers::Matrix4x4_To_GL(world_matrices + inst * 16, col_major_matrices + inst * 16);
    }

    static GLuint g_InstanceVBO = 0;
    if (g_InstanceVBO == 0) {
        glGenBuffers(1, &g_InstanceVBO);
    }
    GLES3_Bind_Array_Buffer(g_InstanceVBO);
    glBufferData(GL_ARRAY_BUFFER, instance_count * 16 * sizeof(float), col_major_matrices, GL_STREAM_DRAW);
    free(col_major_matrices);

    // Setup instanced matrix attributes at layout locations 7, 8, 9, 10
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(7 + i);
        glVertexAttribPointer(7 + i, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), (const void*)(uintptr_t)(i * 4 * sizeof(float)));
        glVertexAttribDivisor(7 + i, 1);
    }

    // Attribute 12: each instance's stage 0 texture offset (tank treads scroll by their own
    // amount); without one the attribute is off and reads its constant (0, 0).
    static GLuint g_InstanceUVVBO = 0;
    if (gx_instance_uv) {
        if (g_InstanceUVVBO == 0) glGenBuffers(1, &g_InstanceUVVBO);
        GLES3_Bind_Array_Buffer(g_InstanceUVVBO);
        glBufferData(GL_ARRAY_BUFFER, instance_count * 2 * sizeof(float), gx_instance_uv, GL_STREAM_DRAW);
        glEnableVertexAttribArray(12);
        glVertexAttribPointer(12, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void*)0);
        glVertexAttribDivisor(12, 1);
    }

    // Get cached instancing uniform location
    GLint loc = GLES3_ShaderManager::Get_Instancing_Uniform_Location(g_State);
    if (loc != -1) {
        glUniform1i(loc, 1);
    }

    glDrawElementsInstanced(gl_prim, index_count, GL_UNSIGNED_SHORT,
                           (const void*)(start_index * sizeof(unsigned short)), instance_count);

    if (loc != -1) {
        glUniform1i(loc, 0);
    }
    for (int i = 0; i < 4; ++i) {
        glDisableVertexAttribArray(7 + i);
        glVertexAttribDivisor(7 + i, 0);
    }
    if (gx_instance_uv) {
        glDisableVertexAttribArray(12);
        glVertexAttribDivisor(12, 0);
    }

    // Attributes 7-10 were set directly above.
    GLES3_Attrib_Shadow_Reset();

    g_State.draw_calls++;
    g_State.triangles_drawn += prim_count * instance_count;
}


/*
** Draw (non-indexed) — Maps to glDrawArrays.
*/
void GLES3_Draw_Arrays(unsigned int prim_type,
                        unsigned int start_vertex,
                        unsigned int prim_count) {
    if (g_GC.mode == 1) { g_GC.unsafe = true; return; } // not expected inside the ground pass
    Apply_Pipeline_State();

    GLenum gl_prim = GLES3_Helpers::PrimType_To_GL(prim_type);
    unsigned int vertex_count = 0;
    switch (prim_type) {
        case GLES3_PT_TRIANGLELIST:  vertex_count = prim_count * 3; break;
        case GLES3_PT_TRIANGLESTRIP: vertex_count = prim_count + 2; break;
        default: vertex_count = prim_count; break;
    }

    glDrawArrays(gl_prim, start_vertex, vertex_count);

    g_State.draw_calls++;
    g_State.triangles_drawn += prim_count;
}


// ============================================================================
// DX8Wrapper API — Vertex/Index Buffer Management
// ============================================================================

GLES3_VertexBuffer* GLES3_VertexBuffer::Create(unsigned int size,
                                                 unsigned int stride,
                                                 unsigned int fvf,
                                                 bool dynamic) {
    auto* vb = new GLES3_VertexBuffer();
    vb->size = size;
    vb->stride = stride;
    vb->fvf = fvf;
    vb->is_dynamic = dynamic;
    vb->lock_ptr = nullptr;

    glGenBuffers(1, &vb->gl_buffer);
    GLES3_Bind_Array_Buffer(vb->gl_buffer);
    glBufferData(GL_ARRAY_BUFFER, size,
                 nullptr, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

    return vb;
}

void GLES3_VertexBuffer::Lock(void** data, unsigned int offset,
                               unsigned int lock_size) {
    if (!lock_ptr) {
        lock_ptr = malloc(size);
    }
    *data = (char*)lock_ptr + offset;
}

void GLES3_VertexBuffer::Unlock() {
    if (lock_ptr) {
        GLES3_Bind_Array_Buffer(gl_buffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, size, lock_ptr);
    }
}

void GLES3_VertexBuffer::Destroy() {
    if (gl_buffer) {
        GLES3_Delete_Buffer(&gl_buffer);
        gl_buffer = 0;
    }
    if (lock_ptr) {
        free(lock_ptr);
        lock_ptr = nullptr;
    }
    delete this;
}


GLES3_IndexBuffer* GLES3_IndexBuffer::Create(unsigned int size, bool dynamic) {
    auto* ib = new GLES3_IndexBuffer();
    ib->size = size;
    ib->index_count = size / sizeof(unsigned short);
    ib->is_dynamic = dynamic;
    ib->lock_ptr = nullptr;

    glGenBuffers(1, &ib->gl_buffer);
    GLES3_Bind_Element_Buffer(ib->gl_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, size,
                 nullptr, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

    return ib;
}

void GLES3_IndexBuffer::Lock(void** data, unsigned int offset,
                              unsigned int lock_size) {
    if (!lock_ptr) {
        lock_ptr = malloc(size);
    }
    *data = (char*)lock_ptr + offset;
}

void GLES3_IndexBuffer::Unlock() {
    if (lock_ptr) {
        GLES3_Bind_Element_Buffer(gl_buffer);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, size, lock_ptr);
    }
}

void GLES3_IndexBuffer::Destroy() {
    if (gl_buffer) {
        GLES3_Delete_Buffer(&gl_buffer);
        gl_buffer = 0;
    }
    if (lock_ptr) {
        free(lock_ptr);
        lock_ptr = nullptr;
    }
    delete this;
}


// ============================================================================
// Texture Format Conversion
// ============================================================================

GLenum GLES3_TextureConverter::DX8_Format_To_GL_Internal(unsigned int dx8_fmt) {
    // Map common DX8 formats (D3DFMT_*) to GL internal formats
    // These are the formats actually used by Generals/Zero Hour
    switch (dx8_fmt) {
        case 21: // D3DFMT_A8R8G8B8
            return GL_RGBA8;
        case 22: // D3DFMT_X8R8G8B8
            return GL_RGBA8; // treat as RGBA, ignore alpha
        case 23: // D3DFMT_R5G6B5
            return GL_RGB565;
        case 25: // D3DFMT_A1R5G5B5
            return GL_RGB5_A1;
        case 26: // D3DFMT_A4R4G4B4
            return GL_RGBA4;
        case 20: // D3DFMT_R8G8B8 (expanded to RGBA by To_GL_Pixels)
        case 50: // D3DFMT_L8
        case 51: // D3DFMT_A8L8
        case 28: // D3DFMT_A8
            return GL_RGBA8;
        case 827611204: // D3DFMT_DXT1 (FOURCC)
            return GL_COMPRESSED_RGB_S3TC_DXT1_EXT; // if available
        case 861165636: // D3DFMT_DXT3
            return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        case 894720068: // D3DFMT_DXT5
            return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        default:
            fprintf(stderr, "WARN: Unknown DX8 texture format %u, defaulting to RGBA8\n",
                    dx8_fmt);
            return GL_RGBA8;
    }
}

// ---------------------------------------------------------------------------
// To_GL_Pixels — D3D8 pixel memory to a GL source format + type.
//
// WebGL2 has no GL_TEXTURE_SWIZZLE_* (texParameteri fails with INVALID_ENUM), so
// channel orders GL can't read directly are converted on the CPU: BGRA/BGRX/BGR
// to RGBA, L8 / A8L8 / A8 to RGBA, and the 16-bit ARGB layouts to GL's RGBA ones.
// Returns `data` itself when no conversion is needed, else `tmp`'s storage.
// ---------------------------------------------------------------------------
static const void* To_GL_Pixels(unsigned int dx8_format, unsigned int width,
                                unsigned int height, const void* data,
                                std::vector<unsigned char>& tmp,
                                GLenum& src_fmt, GLenum& src_type) {
    const unsigned int n = width * height;
    const unsigned char* in = (const unsigned char*)data;
    src_fmt = GL_RGBA;
    src_type = GL_UNSIGNED_BYTE;
    switch (dx8_format) {
        case 23: // D3DFMT_R5G6B5 — same bit layout as GL_UNSIGNED_SHORT_5_6_5
            src_fmt = GL_RGB; src_type = GL_UNSIGNED_SHORT_5_6_5;
            return data;
        case 25: // D3DFMT_A1R5G5B5 → GL 5551 (alpha in the low bit)
        case 26: { // D3DFMT_A4R4G4B4 → GL 4444 (alpha in the low nibble)
            const bool a1 = dx8_format == 25;
            src_type = a1 ? GL_UNSIGNED_SHORT_5_5_5_1 : GL_UNSIGNED_SHORT_4_4_4_4;
            if (!data) return nullptr;
            tmp.resize(n * 2);
            const unsigned short* src = (const unsigned short*)data;
            unsigned short* dst = (unsigned short*)tmp.data();
            for (unsigned int i = 0; i < n; i++)
                dst[i] = a1 ? (unsigned short)((src[i] << 1) | (src[i] >> 15))
                            : (unsigned short)((src[i] << 4) | (src[i] >> 12));
            return tmp.data();
        }
        default: break;
    }
    // Everything else becomes RGBA8 bytes.
    if (!data) return nullptr;
    tmp.resize(n * 4);
    unsigned char* o = tmp.data();
    for (unsigned int i = 0; i < n; i++, o += 4) {
        switch (dx8_format) {
            case 20: { const unsigned char* p = in + i * 3;     // R8G8B8: B G R
                o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = 255; break; }
            case 22: { const unsigned char* p = in + i * 4;     // X8R8G8B8: B G R X
                o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = 255; break; }
            case 50: o[0] = o[1] = o[2] = in[i]; o[3] = 255; break;            // L8
            case 51: o[0] = o[1] = o[2] = in[i * 2]; o[3] = in[i * 2 + 1]; break; // A8L8: L A
            case 28: o[0] = o[1] = o[2] = 0; o[3] = in[i]; break;               // A8
            default: { const unsigned char* p = in + i * 4;     // A8R8G8B8 (and unknown): B G R A
                o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = p[3]; break; }
        }
    }
    return tmp.data();
}

// ---------------------------------------------------------------------------
// Create_Texture — Upload one mip level (level 0) of a D3D8 texture to GL.
//
// Uncompressed pixels go through To_GL_Pixels (channel order converted on the CPU).
//
// For DXT compressed textures glGenerateMipmap on a compressed base is
// unreliable; Upload_To_GL (d3d8.h) uploads each mip level separately and
// passes mip_levels=1 here to suppress the auto-generate call.
// ---------------------------------------------------------------------------
GLuint GLES3_TextureConverter::Create_Texture(unsigned int width,
                                               unsigned int height,
                                               unsigned int dx8_format,
                                               unsigned int mip_levels,
                                               const void* data) {
    // Uploads happen on whichever unit is active; put back what that unit had,
    // or another stage keeps sampling this texture.
    GLint prev_tex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    GLenum internal_fmt = DX8_Format_To_GL_Internal(dx8_format);

    bool is_compressed = (internal_fmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT  ||
                          internal_fmt == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
                          internal_fmt == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);

    if (is_compressed) {
        // WebGL2: WEBGL_compressed_texture_s3tc extension required.
        // The caller is responsible for uploading all mip levels if mip_levels > 1;
        // we only upload the base level here.
        unsigned int block_size = (internal_fmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT) ? 8 : 16;
        unsigned int data_size = ((width + 3) / 4) * ((height + 3) / 4) * block_size;
        // Set mip level storage range so the driver knows more levels will arrive
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (int)mip_levels - 1);
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, internal_fmt,
                               (GLsizei)width, (GLsizei)height, 0,
                               (GLsizei)data_size, data);
    } else {
        GLenum src_fmt, src_type;
        std::vector<unsigned char> temp_buffer;
        const void* upload_data = To_GL_Pixels(dx8_format, width, height, data,
                                               temp_buffer, src_fmt, src_type);
        glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt,
                     (GLsizei)width, (GLsizei)height, 0,
                     src_fmt, src_type, upload_data);

        // Generate mipmaps from the uploaded base level (uncompressed only)
        if (mip_levels != 1) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }
    }

    if (mip_levels != 1) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    GLES3_Register_Texture(tex, mip_levels > 1);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
    return tex;
}


// ---------------------------------------------------------------------------
// Update_Texture — Update one mip level (level 0) of an existing D3D8 texture in GL.
// ---------------------------------------------------------------------------
void GLES3_TextureConverter::Update_Texture(GLuint gl_tex_id,
                                            unsigned int width,
                                            unsigned int height,
                                            unsigned int dx8_format,
                                            unsigned int mip_levels,
                                            const void* data) {
    if (!gl_tex_id || !data) return;
    // Bound on the active unit and left there: the binding shadow records it, so the next
    // SetTexture on that unit rebinds only if it needs another texture (no glGet, no restore).
    GLES3_Bind_Texture_For_Upload(gl_tex_id);

    GLenum src_fmt, src_type;
    // Kept between calls (game thread only): textures updated every frame, like the fog of
    // war, no longer allocate a conversion buffer each time (item 45).
    static std::vector<unsigned char> temp_buffer;
    const void* upload_data = To_GL_Pixels(dx8_format, width, height, data,
                                           temp_buffer, src_fmt, src_type);

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    (GLsizei)width, (GLsizei)height,
                    src_fmt, src_type, upload_data);

    if (mip_levels > 1) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
}



// ============================================================================
// Shader Manager Implementation
// ============================================================================

// Background shader compiles (Settings switch asyncShaders, page switch bit 22). A first-time
// specialised shader could freeze a frame for seconds: the browser compiles when the game asks
// whether compiling worked (getShaderParameter was 12% of the battle benchmark's strikes, one
// frame 7.7 s). With KHR_parallel_shader_compile the compile runs in the background: until it
// is done, the draw uses the general shader of the same setup (the same picture; it is what
// the specialised-shaders switch falls back to), then the specialised one takes over.
// Shaders that fold the fog of war in are still compiled at once (the general one can't).
static bool g_ParallelCompile = false;
static const GLenum GX_COMPLETION_STATUS_KHR = 0x91B1;
struct GXPendingProgram { GLuint program, vs, fs; };
static std::unordered_map<unsigned int, GXPendingProgram> g_PendingPrograms;
int gx_async_programs = 0, gx_fallback_draws = 0;	// counters for the log

bool GLES3_ShaderManager::Init() {
    if (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_get_current_context())
        g_ParallelCompile = emscripten_webgl_enable_extension(ctx, "KHR_parallel_shader_compile");
    printf("GXSHADERS parallel compile %s\n", g_ParallelCompile ? "available" : "not available");
    memset(cached_programs, 0, sizeof(cached_programs));
    memset(cached_locations, -1, sizeof(cached_locations));
    // The fixed-function emulation shaders are compiled on first use
    // via Get_Program(). This avoids compiling unused variants.
    return true;
}

void GLES3_ShaderManager::Shutdown() {
    for (int i = 0; i < GLES3_PROGRAM_SLOTS; i++) {
        if (cached_programs[i]) {
            glDeleteProgram(cached_programs[i]);
            cached_programs[i] = 0;
        }
    }
}

GLuint GLES3_ShaderManager::Compile_Shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    if (g_ParallelCompile && (g_PageSwitches & 4194304)) return shader;	// checked when its program is linked

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "ERROR: Shader compilation failed:\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint GLES3_ShaderManager::Link_Program(GLuint vert, GLuint frag) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        fprintf(stderr, "ERROR: Shader program link failed:\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }

    // Shaders can be detached after linking
    glDetachShader(program, vert);
    glDetachShader(program, frag);
    return program;
}

unsigned int GLES3_ShaderManager::Hash_Pipeline_Config(
        const GLES3_PipelineState& state) {
    // Shader variant key: the states that change which code runs. Each variant is compiled
    // with matching #defines (dx8_fixedfunction_shaders.cpp), so the GPU only runs the code
    // that case needs; a weak or software GPU pays per pixel for every branch it keeps.
    unsigned int hash = 0;
    hash |= (state.lighting_enabled ? 1 : 0);
    hash |= (state.fog_enabled ? 2 : 0);
    hash |= (state.alpha_test_enabled ? 4 : 0);
    hash |= (state.pretransformed ? 8 : 0);
    hash |= (state.render_states[GLES3_RS_SPECULARENABLE] ? 16 : 0);
    if (state.pixel_shader) {
        hash |= 32; // a ps.1.1 program replaces the stages
        // Handles 1-15 get their own variant with the program compiled to GLSL; others
        // use the interpreter (GX_PSGEN 0).
        if (state.pixel_shader <= 15) hash |= state.pixel_shader << 6;
    } else {
        unsigned int stages = 0; // stages before the first DISABLE, as D3D runs them
        while (stages < 4 && state.texture_stage_state[stages][GLES3_TSS_COLOROP] > 1) stages++;
        hash |= stages << 6;
    }
    return Program_Slot(state, hash);
}

// Texture stage setups compiled to straight-line GLSL get their own program slot (1024 and up).
// Through the interpreter, a software or weak GPU runs every branch of every op, argument and
// texture lookup for every pixel; the UI and the shroud pass cost most of a low-end frame there.
static GLES3_FFKey g_SlotKey[GLES3_PROGRAM_SLOTS];
static int g_SlotCount = 1024;
int gx_ff_slots = 0; // GXLAYOUT log line
int GLES3_Free_Program_Slots() { return GLES3_PROGRAM_SLOTS - g_SlotCount; }
struct FFKeyHash {
    size_t operator()(const GLES3_FFKey& k) const {
        const unsigned char* b = (const unsigned char*)&k;
        size_t h = 2166136261u;
        for (size_t i = 0; i < sizeof(k); i++) h = (h ^ b[i]) * 16777619u;
        return h;
    }
};
struct FFKeyEq {
    bool operator()(const GLES3_FFKey& a, const GLES3_FFKey& b) const { return memcmp(&a, &b, sizeof(a)) == 0; }
};
static std::unordered_map<GLES3_FFKey, unsigned short, FFKeyHash, FFKeyEq> g_Slots;
// The slot for a stage setup; a new one gets the next free slot (out of slots: the
// interpreter variant, hash, still draws it right).
static unsigned int Intern_Slot(const GLES3_FFKey& k, unsigned int hash) {
    auto it = g_Slots.find(k);
    if (it != g_Slots.end()) return it->second;
    unsigned int slot = g_SlotCount < GLES3_PROGRAM_SLOTS ? (unsigned int)g_SlotCount++ : hash;
    if (slot >= 1024) { g_SlotKey[slot] = k; gx_ff_slots++; }
    g_Slots.emplace(k, (unsigned short)slot);
    return slot;
}

// Item 21 of the low-end plan. Programs compile the first time a draw needs one, which can
// stall a match (one 410 ms hitch measured mid-match; 30 programs, 528 ms in all over a lobby,
// a map load and 2 minutes of play). So every program this browser compiles is logged to
// /userdata/gx_shaders.bin (IDBFS, kept with Options.ini), and the next start compiles the
// logged ones before the menu (GLES3_Precompile_Programs). Keys and variant numbers belong to
// one build: the file starts with the build's date and time, and another build ignores it.
// Pixel-shader variants are left out: their handles are numbered as the game creates them.
struct GXShaderRecord { unsigned short hash; unsigned char has_key; GLES3_FFKey key; };
static std::vector<GXShaderRecord> g_ShaderLog;
static bool g_ShaderLogReplay = false;
static const char g_ShaderLogStamp[32] = "GXS1 " __DATE__ " " __TIME__;
static void Log_Program(unsigned int hash) {
    if (g_ShaderLogReplay) return;
    GXShaderRecord r;
    memset(&r, 0, sizeof(r));
    if (hash >= 1024) {
        r.has_key = 1;
        r.key = g_SlotKey[hash];
        if ((r.key.base_lo | r.key.base_hi << 8) & 32) return;
    } else {
        if (hash & 32) return;
        r.hash = (unsigned short)hash;
    }
    g_ShaderLog.push_back(r);
    if (FILE* f = fopen("/userdata/gx_shaders.bin", "wb")) { // a few KB, rewritten per new program
        fwrite(g_ShaderLogStamp, sizeof(g_ShaderLogStamp), 1, f);
        fwrite(g_ShaderLog.data(), sizeof(GXShaderRecord), g_ShaderLog.size(), f);
        fclose(f);
    }
}
void GLES3_Precompile_Programs() {
    FILE* f = fopen("/userdata/gx_shaders.bin", "rb");
    if (!f) return;
    char stamp[sizeof(g_ShaderLogStamp)];
    if (fread(stamp, sizeof(stamp), 1, f) != 1 || memcmp(stamp, g_ShaderLogStamp, sizeof(stamp)) != 0) {
        fclose(f);
        printf("GXSHADERS log from another build, ignored\n");
        return;
    }
    GXShaderRecord r;
    std::vector<GXShaderRecord> recs;
    while (fread(&r, sizeof(r), 1, f) == 1) recs.push_back(r);
    fclose(f);
    const double t0 = emscripten_get_now();
    int n = 0;
    g_ShaderLogReplay = true;
    for (const GXShaderRecord& rec : recs) {
        const unsigned int slot = rec.has_key ? Intern_Slot(rec.key, rec.key.base_lo | rec.key.base_hi << 8) : rec.hash;
        if (slot < GLES3_PROGRAM_SLOTS && GLES3_ShaderManager::Get_Program(slot)) n++;
    }
    g_ShaderLogReplay = false;
    g_ShaderLog = recs; // still logged: new programs are added to these
    printf("GXSHADERS precompiled %d of %d programs in %.0f ms\n", n, (int)recs.size(), emscripten_get_now() - t0);
}

unsigned int GLES3_ShaderManager::Program_Slot(const GLES3_PipelineState& state, unsigned int hash) {
    if (!(g_PageSwitches & 16)) return hash; // performance switch: the general (interpreting) shaders
    const bool ps = (hash & 32) != 0;
    const int stages = ps ? 0 : (int)((hash >> 6) & 7);
    const bool pre = (hash & 8) != 0;
    // Shroud fold: only when the draw's result is linear in its colour (alpha blending,
    // additive, opaque). A multiplicative draw (dst * src) is already right with the
    // shrouded destination.
    bool fold = false;
    if (state.shroud_tex && !pre) {
        auto linear = [](unsigned int f) { return f == 1 || f == 2 || f == 5 || f == 6; }; // ZERO ONE SRCALPHA INVSRCALPHA
        fold = !state.render_states[GLES3_RS_ALPHABLENDENABLE] ||
               (linear(state.render_states[GLES3_RS_SRCBLEND]) && linear(state.render_states[GLES3_RS_DESTBLEND]));
    }
    if (pre && stages == 0 && !state.alpha_test_enabled) return hash; // nothing to specialise
    GLES3_FFKey k;
    memset(&k, 0, sizeof(k));
    k.base_lo = hash & 255;
    k.base_hi = hash >> 8;
    if (state.alpha_test_enabled) k.afunc = state.alpha_func <= 8 ? state.alpha_func : 8;
    k.fold = fold ? 1 : 0;
    if (!pre) {
        // Vertex side: texture coordinates of the stages the variant runs (all four for a
        // pixel shader), light types and material colour sources, as the uniforms carried them.
        for (int s = 0; s < 4; s++) {
            if (!ps && s >= stages) break;
            unsigned int tci = state.texture_stage_state[s][GLES3_TSS_TEXCOORDINDEX];
            unsigned int ttf = state.texture_stage_state[s][GLES3_TSS_TEXTURETRANSFORMFLAGS];
            unsigned int mode = tci >> 16, count = ttf & 0xff;
            k.vtex[s] = (unsigned char)((mode <= 3 ? mode : 0) | (tci & 3) << 2 | (count <= 7 ? count : 7) << 4
                                        | ((ttf & 256) ? 128 : 0));
        }
        if (state.lighting_enabled) {
            for (int i = 0; i < 4; i++) {
                unsigned int t = state.lights[i].type;
                if (state.light_enabled[i]) k.lights |= (t >= 1 && t <= 3 ? t : 1) << (i * 2);
            }
            if (state.render_states[GLES3_RS_COLORVERTEX] && state.vertex_has_color) {
                if (state.render_states[GLES3_RS_AMBIENTMATERIALSOURCE] == 1) k.cvert |= 1;
                if (state.render_states[GLES3_RS_DIFFUSEMATERIALSOURCE] == 1) k.cvert |= 2;
                if (state.render_states[GLES3_RS_EMISSIVEMATERIALSOURCE] == 1) k.cvert |= 4;
            }
        }
    }
    auto clip = [](unsigned int v) { return (unsigned char)(v <= 255 ? v : 255); };
    for (int s = 0; s < stages; s++) {
        const unsigned int* t = state.texture_stage_state[s];
        if (state.bound_textures[s]) k.bound |= 1 << s;
        for (int c = 0; c < 2; c++) { // colour, then alpha
            unsigned int op = t[c ? GLES3_TSS_ALPHAOP : GLES3_TSS_COLOROP];
            if (c && op <= 1) { k.op[s][1] = 1; continue; } // alpha unchanged: args unused
            k.op[s][c] = clip(op);
            unsigned int a0 = t[c ? GLES3_TSS_ALPHAARG0 : GLES3_TSS_COLORARG0];
            unsigned int a1 = t[c ? GLES3_TSS_ALPHAARG1 : GLES3_TSS_COLORARG1];
            unsigned int a2 = t[c ? GLES3_TSS_ALPHAARG2 : GLES3_TSS_COLORARG2];
            // Only the arguments the op reads, so setups that differ in unused ones share a program.
            k.arg[s][c * 3 + 0] = (op == 25 || op == 26) ? (a0 & 63) : 0;
            k.arg[s][c * 3 + 1] = op == 3 ? 0 : (a1 & 63);
            k.arg[s][c * 3 + 2] = op == 2 ? 0 : (a2 & 63);
        }
    }
    static GLES3_FFKey last;
    static unsigned int last_slot = ~0u;
    if (last_slot != ~0u && memcmp(&k, &last, sizeof(k)) == 0) return last_slot;
    const unsigned int slot = Intern_Slot(k, hash);
    last = k;
    last_slot = slot;
    return slot;
}

GLint GLES3_ShaderManager::Get_Instancing_Uniform_Location(const GLES3_PipelineState &state) {
    (void)state;	// the program in use (Apply_Pipeline_State), which may be the general fallback
    return cached_locations[g_ActiveSlot].u_InstancingEnabled;
}

// Forward declaration — defined with extern "C" in dx8_fixedfunction_shaders.cpp
// Defined in dx8_fixedfunction_shaders.cpp. key: nullptr = the stages run by the interpreter.
std::string GLES3_Get_Vertex_Shader_Source(unsigned int config_hash, const GLES3_FFKey* key);
std::string GLES3_Get_Fragment_Shader_Source(unsigned int config_hash, const GLES3_FFKey* key);

void GLES3_ShaderManager::Populate_Locations(GLuint program, unsigned int hash) {
    GLES3_ProgramUniformLocations& locs = cached_locations[hash];
    locs.u_World = glGetUniformLocation(program, "u_World");
    locs.u_MatDiffuse = glGetUniformLocation(program, "u_MatDiffuse");
    locs.u_MatAmbient = glGetUniformLocation(program, "u_MatAmbient");
    locs.u_MatSpecular = glGetUniformLocation(program, "u_MatSpecular");
    locs.u_MatEmissive = glGetUniformLocation(program, "u_MatEmissive");
    locs.u_MatPower = glGetUniformLocation(program, "u_MatPower");
    locs.u_AlphaRef = glGetUniformLocation(program, "u_AlphaRef");
    locs.u_AlphaFunc = glGetUniformLocation(program, "u_AlphaFunc");

    for (int i = 0; i < 4; i++) {
        char name[32];
        snprintf(name, sizeof(name), "u_Texture%d", i);
        locs.u_Texture[i] = glGetUniformLocation(program, name);
    }

    locs.u_StageOps = glGetUniformLocation(program, "u_StageOps");
    locs.u_PSCount = glGetUniformLocation(program, "u_PSCount");
    locs.u_PS = glGetUniformLocation(program, "u_PS");
    locs.u_PSConst = glGetUniformLocation(program, "u_PSConst");
    locs.u_ColorVertex = glGetUniformLocation(program, "u_ColorVertex");
    locs.u_StageArgs = glGetUniformLocation(program, "u_StageArgs");
    locs.u_TexBound = glGetUniformLocation(program, "u_TexBound");
    locs.u_InstancingEnabled = glGetUniformLocation(program, "u_InstancingEnabled");
    locs.u_TexCoordIndex = glGetUniformLocation(program, "u_TexCoordIndex");
    locs.u_TexTransform = glGetUniformLocation(program, "u_TexTransform");
    locs.u_TexMatrix = glGetUniformLocation(program, "u_TexMatrix");
    locs.u_ShroudTex = glGetUniformLocation(program, "u_ShroudTex");
    locs.u_ShroudMatrix = glGetUniformLocation(program, "u_ShroudMatrix");
}


// A program compiling in the background: installed once the browser says it is done (no wait).
static bool Poll_Pending(unsigned int hash) {
    auto it = g_PendingPrograms.find(hash);
    if (it == g_PendingPrograms.end()) return false;
    GLint done = 0;
    glGetProgramiv(it->second.program, GX_COMPLETION_STATUS_KHR, &done);
    if (!done) return true;	// still compiling
    GXPendingProgram p = it->second;
    g_PendingPrograms.erase(it);
    GLint ok = 0;
    glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p.program, sizeof(log), nullptr, log);
        fprintf(stderr, "ERROR: Shader program link failed:\n%s\n", log);
        glDeleteProgram(p.program);
        return false;	// stays 0: the general shader keeps drawing it
    }
    glDetachShader(p.program, p.vs);
    glDetachShader(p.program, p.fs);
    GLES3_ShaderManager::Install_Program(hash, p.program);
    return false;
}

void GLES3_ShaderManager::Install_Program(unsigned int hash, GLuint program) {
    cached_programs[hash] = program;
    GLuint block = glGetUniformBlockIndex(program, "GXScene");
    if (block != GL_INVALID_INDEX) glUniformBlockBinding(program, block, 0);
    Populate_Locations(program, hash);
    Log_Program(hash);
}

// A specialised slot may draw with the general shader while it compiles, unless it folds the
// fog of war in (the general shader can't, and the separate fog pass was skipped for it).
bool GLES3_ShaderManager::Can_Fall_Back(unsigned int hash) {
    return hash >= 1024 && hash < GLES3_PROGRAM_SLOTS && !g_SlotKey[hash].fold;
}
unsigned int GLES3_ShaderManager::Fallback_Slot(unsigned int hash) {
    return g_SlotKey[hash].base_lo | (g_SlotKey[hash].base_hi << 8);
}

// The program for a slot, or 0 while it compiles in the background (async_ok) / if it failed.
GLuint GLES3_ShaderManager::Get_Program(unsigned int hash, bool async_ok) {

    if (cached_programs[hash] != 0) {
        return cached_programs[hash];
    }
    if (Poll_Pending(hash)) return 0;
    if (cached_programs[hash] != 0) return cached_programs[hash];
    // A variant that failed to compile stays 0; don't recompile it on every draw.
    static bool tried[GLES3_PROGRAM_SLOTS];
    if (tried[hash]) return 0;
    tried[hash] = true;

    // Compile shader variant for this pipeline configuration. Slots share compiled shaders:
    // a vertex shader per variant + vertex setup, a fragment shader per variant + stage setup.
    const GLES3_FFKey* key = hash >= 1024 ? &g_SlotKey[hash] : nullptr;
    unsigned int base = key ? (key->base_lo | (key->base_hi << 8)) : hash;
    unsigned long long vkey = base;
    if (key) vkey |= 1ull << 62 | (unsigned long long)key->vtex[0] << 10 | (unsigned long long)key->vtex[1] << 18
                  | (unsigned long long)key->vtex[2] << 26 | (unsigned long long)key->vtex[3] << 34
                  | (unsigned long long)key->lights << 42 | (unsigned long long)key->cvert << 50
                  | (unsigned long long)key->fold << 53;
    static std::unordered_map<unsigned long long, GLuint> vertex_shaders;
    GLuint& vs = vertex_shaders[vkey];
    if (!vs) vs = Compile_Shader(GL_VERTEX_SHADER, GLES3_Get_Vertex_Shader_Source(base, key).c_str());
    std::string fkey = key ? std::string((const char*)key, offsetof(GLES3_FFKey, vtex))
                           : "i" + std::to_string(base); // the fields before vtex are the fragment side
    static std::unordered_map<std::string, GLuint> fragment_shaders;
    GLuint& fs = fragment_shaders[fkey];
    if (!fs) fs = Compile_Shader(GL_FRAGMENT_SHADER, GLES3_Get_Fragment_Shader_Source(base, key).c_str());

    if (vs && fs && async_ok && hash >= 1024 && g_ParallelCompile && (g_PageSwitches & 4194304)) {
        // background: link now, check later (GLES3_ShaderManager::Poll_Program)
        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        g_PendingPrograms[hash] = { program, vs, fs };
        gx_async_programs++;
        return 0;
    }
    if (vs && fs) {
        cached_programs[hash] = Link_Program(vs, fs);
        if (cached_programs[hash] != 0) {
            GLuint block = glGetUniformBlockIndex(cached_programs[hash], "GXScene");
            if (block != GL_INVALID_INDEX) glUniformBlockBinding(cached_programs[hash], block, 0);
            Populate_Locations(cached_programs[hash], hash);
            Log_Program(hash);
        }
    }

    return cached_programs[hash];
}

// GXScene (dx8_fixedfunction_shaders.cpp) in std140 layout: every member is a vec4 or mat4.
struct GXSceneStd140 {
    float view[16], projection[16];
    float global_ambient[4], fog_color[4], fog_params[4], viewport_rect[4], texture_factor[4];
    struct { float diffuse[4], ambient[4], specular[4], position[4], direction[4], atten[4]; } lights[4];
};
static_assert(sizeof(GXSceneStd140) == 2 * 64 + 5 * 16 + 4 * 96, "GXScene std140 layout");

// Fill the scene block from the state and upload it when it changed: one bufferSubData
// shared by every program, instead of the same values re-sent to each program it switches to.
static void Upload_Scene_Block(const GLES3_PipelineState& state) {
    if (!g_SceneDirty) return; // set by every writer of these fields (all in this file)
    g_SceneDirty = false;
    static GLuint ubo = 0;
    static GXSceneStd140 sent;
    static bool have_sent = false;
    GXSceneStd140 b;
    memset(&b, 0, sizeof(b));
    memcpy(b.view, state.view_matrix, sizeof(b.view));
    memcpy(b.projection, state.projection_matrix, sizeof(b.projection));
    memcpy(b.global_ambient, state.global_ambient, sizeof(b.global_ambient));
    memcpy(b.fog_color, state.fog_color, sizeof(state.fog_color));
    b.fog_params[0] = state.fog_start;
    b.fog_params[1] = state.fog_end;
    b.fog_params[2] = g_LodBias;
    b.viewport_rect[0] = (float)state.viewport.x;
    b.viewport_rect[1] = (float)state.viewport.y;
    b.viewport_rect[2] = (float)(state.viewport.width ? state.viewport.width : 1);
    b.viewport_rect[3] = (float)(state.viewport.height ? state.viewport.height : 1);
    memcpy(b.texture_factor, state.texture_factor, sizeof(b.texture_factor));
    for (int i = 0; i < 4; i++) {
        if (!state.light_enabled[i]) continue; // type 0: off (values left 0 so they don't cause uploads)
        const GLES3_Light& l = state.lights[i];
        auto& o = b.lights[i];
        memcpy(o.diffuse, l.diffuse, sizeof(o.diffuse));
        memcpy(o.ambient, l.ambient, sizeof(o.ambient));
        memcpy(o.specular, l.specular, sizeof(o.specular));
        memcpy(o.position, l.position, sizeof(l.position));
        o.position[3] = l.range;
        memcpy(o.direction, l.direction, sizeof(l.direction));
        o.atten[0] = l.attenuation0;
        o.atten[1] = l.attenuation1;
        o.atten[2] = l.attenuation2;
        o.atten[3] = (float)(l.type >= 1 && l.type <= 3 ? l.type : 1);
    }
    if (!ubo) {
        glGenBuffers(1, &ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(b), &b, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo); // also leaves it bound to GL_UNIFORM_BUFFER
    } else if (!have_sent || memcmp(&b, &sent, sizeof(b)) != 0) {
        // Only the changed span (usually one matrix or one light).
        const unsigned char* x = (const unsigned char*)&b;
        const unsigned char* y = (const unsigned char*)&sent;
        size_t lo = 0, hi = sizeof(b);
        if (have_sent) {
            while (lo < hi && x[lo] == y[lo]) lo++;
            while (hi > lo && x[hi - 1] == y[hi - 1]) hi--;
            lo &= ~(size_t)15;
        }
        glBufferSubData(GL_UNIFORM_BUFFER, (GLintptr)lo, (GLsizeiptr)(hi - lo), x + lo);
    }
    sent = b;
    have_sent = true;
}

void GLES3_ShaderManager::Apply_Uniforms(unsigned int hash,
                                          const GLES3_PipelineState& state) {
    Upload_Scene_Block(state);
    const GLES3_ProgramUniformLocations& locs = cached_locations[hash];
    GLES3_CachedUniformState& cache = cached_uniform_states[hash];

    // Upload matrices (transposed for GL column-major order)
    float gl_matrix[16];

    GLES3_Helpers::Matrix4x4_To_GL(state.world_matrix, gl_matrix);
    if (locs.u_World != -1 && (!cache.initialized || memcmp(cache.world_matrix, gl_matrix, sizeof(gl_matrix)) != 0)) {
        glUniformMatrix4fv(locs.u_World, 1, GL_FALSE, gl_matrix);
        memcpy(cache.world_matrix, gl_matrix, sizeof(gl_matrix));
    }

    // Material
    if (locs.u_MatDiffuse != -1 && (!cache.initialized || memcmp(cache.material_diffuse, state.material.diffuse, sizeof(state.material.diffuse)) != 0)) {
        glUniform4fv(locs.u_MatDiffuse, 1, state.material.diffuse);
        memcpy(cache.material_diffuse, state.material.diffuse, sizeof(state.material.diffuse));
    }
    if (locs.u_MatAmbient != -1 && (!cache.initialized || memcmp(cache.material_ambient, state.material.ambient, sizeof(state.material.ambient)) != 0)) {
        glUniform4fv(locs.u_MatAmbient, 1, state.material.ambient);
        memcpy(cache.material_ambient, state.material.ambient, sizeof(state.material.ambient));
    }
    if (locs.u_MatSpecular != -1 && (!cache.initialized || memcmp(cache.material_specular, state.material.specular, sizeof(state.material.specular)) != 0)) {
        glUniform4fv(locs.u_MatSpecular, 1, state.material.specular);
        memcpy(cache.material_specular, state.material.specular, sizeof(state.material.specular));
    }
    if (locs.u_MatEmissive != -1 && (!cache.initialized || memcmp(cache.material_emissive, state.material.emissive, sizeof(state.material.emissive)) != 0)) {
        glUniform4fv(locs.u_MatEmissive, 1, state.material.emissive);
        memcpy(cache.material_emissive, state.material.emissive, sizeof(state.material.emissive));
    }
    if (locs.u_MatPower != -1 && (!cache.initialized || cache.material_power != state.material.power)) {
        glUniform1f(locs.u_MatPower, state.material.power);
        cache.material_power = state.material.power;
    }

    // Alpha test (emulated in shader)
    float at_ref = state.alpha_ref / 255.0f;
    if (locs.u_AlphaRef != -1 && (!cache.initialized || cache.alpha_ref != at_ref)) {
        glUniform1f(locs.u_AlphaRef, at_ref);
        cache.alpha_ref = at_ref;
    }
    if (locs.u_AlphaFunc != -1 && (!cache.initialized || cache.alpha_func != (int)state.alpha_func)) {
        glUniform1i(locs.u_AlphaFunc, state.alpha_func);
        cache.alpha_func = state.alpha_func;
    }

    // Texture samplers
    for (int i = 0; i < 4; i++) {
        if (locs.u_Texture[i] != -1 && (!cache.initialized)) {
            glUniform1i(locs.u_Texture[i], i);
        }
    }

    // Texture stage ops (stages 0-3), D3DRS_SPECULARENABLE and the pixel shader.
    {
        int ops[8];
        for (int i = 0; i < 4; i++) {
            ops[i * 2] = (int)state.texture_stage_state[i][GLES3_TSS_COLOROP];
            ops[i * 2 + 1] = (int)state.texture_stage_state[i][GLES3_TSS_ALPHAOP];
        }
        if (locs.u_StageOps != -1 && (!cache.initialized || memcmp(cache.stage_ops, ops, sizeof(ops)) != 0)) {
            glUniform1iv(locs.u_StageOps, 8, ops);
            memcpy(cache.stage_ops, ops, sizeof(ops));
        }
        if (!cache.initialized || cache.pixel_shader != state.pixel_shader) {
            GLES3_Upload_Pixel_Shader(locs, state.pixel_shader);
            cache.pixel_shader = state.pixel_shader;
        }
        if (locs.u_PSConst != -1 && (!cache.initialized || memcmp(cache.ps_const, state.ps_const, sizeof(state.ps_const)) != 0)) {
            glUniform4fv(locs.u_PSConst, 8, &state.ps_const[0][0]);
            memcpy(cache.ps_const, state.ps_const, sizeof(state.ps_const));
        }
    }

    // Color vertex mode
    int c_vertex = 0;
    if (state.render_states[GLES3_RS_COLORVERTEX] && state.vertex_has_color) {
        if (state.render_states[GLES3_RS_AMBIENTMATERIALSOURCE] == 1) c_vertex |= 1;
        if (state.render_states[GLES3_RS_DIFFUSEMATERIALSOURCE] == 1) c_vertex |= 2;
        if (state.render_states[GLES3_RS_EMISSIVEMATERIALSOURCE] == 1) c_vertex |= 4;
    }
    if (locs.u_ColorVertex != -1 && (!cache.initialized || cache.color_vertex != c_vertex)) {
        glUniform1i(locs.u_ColorVertex, c_vertex);
        cache.color_vertex = c_vertex;
    }

    {
        // Stages 0-3: COLORARG0/1/2 then ALPHAARG0/1/2 (D3DTA_* incl. modifier bits).
        int args[24];
        for (int st = 0; st < 4; st++) {
            const unsigned int* t = state.texture_stage_state[st];
            args[st * 6 + 0] = (int)t[GLES3_TSS_COLORARG0];
            args[st * 6 + 1] = (int)t[GLES3_TSS_COLORARG1];
            args[st * 6 + 2] = (int)t[GLES3_TSS_COLORARG2];
            args[st * 6 + 3] = (int)t[GLES3_TSS_ALPHAARG0];
            args[st * 6 + 4] = (int)t[GLES3_TSS_ALPHAARG1];
            args[st * 6 + 5] = (int)t[GLES3_TSS_ALPHAARG2];
        }
        if (locs.u_StageArgs != -1 && (!cache.initialized || memcmp(cache.stage_args, args, sizeof(args)) != 0)) {
            glUniform1iv(locs.u_StageArgs, 24, args);
            memcpy(cache.stage_args, args, sizeof(args));
        }
        int bound = 0;
        for (int st = 0; st < 4; st++) if (state.bound_textures[st]) bound |= 1 << st;
        if (locs.u_TexBound != -1 && (!cache.initialized || cache.tex_bound != bound)) {
            glUniform1i(locs.u_TexBound, bound);
            cache.tex_bound = bound;
        }
    }

    if (locs.u_InstancingEnabled != -1 && !cache.initialized) {
        glUniform1i(locs.u_InstancingEnabled, 0); // the instanced draw sets 1 and puts 0 back
    }

    // Texture coordinate generation and texture transforms (stages 0-3)
    {
        int tci[4], ttf[4];
        float tm[4][16];
        for (int i = 0; i < 4; i++) {
            tci[i] = (int)state.texture_stage_state[i][GLES3_TSS_TEXCOORDINDEX];
            ttf[i] = (int)state.texture_stage_state[i][GLES3_TSS_TEXTURETRANSFORMFLAGS];
            GLES3_Helpers::Matrix4x4_To_GL(state.texture_matrices[i], tm[i]);
        }
        if (locs.u_TexCoordIndex != -1 && (!cache.initialized || memcmp(cache.tex_coord_index, tci, sizeof(tci)) != 0)) {
            glUniform1iv(locs.u_TexCoordIndex, 4, tci);
            memcpy(cache.tex_coord_index, tci, sizeof(tci));
        }
        if (locs.u_TexTransform != -1 && (!cache.initialized || memcmp(cache.tex_transform, ttf, sizeof(ttf)) != 0)) {
            glUniform1iv(locs.u_TexTransform, 4, ttf);
            memcpy(cache.tex_transform, ttf, sizeof(ttf));
        }
        if (locs.u_TexMatrix != -1 && (!cache.initialized || memcmp(cache.tex_matrix, tm, sizeof(tm)) != 0)) {
            glUniformMatrix4fv(locs.u_TexMatrix, 4, GL_FALSE, &tm[0][0]);
            memcpy(cache.tex_matrix, tm, sizeof(tm));
        }
    }

    if (locs.u_ShroudTex != -1 && !cache.initialized) glUniform1i(locs.u_ShroudTex, 4);
    if (locs.u_ShroudMatrix != -1) {
        float sm[16];
        GLES3_Helpers::Matrix4x4_To_GL(state.shroud_matrix, sm);
        if (!cache.initialized || memcmp(cache.shroud_matrix, sm, sizeof(sm)) != 0) {
            glUniformMatrix4fv(locs.u_ShroudMatrix, 1, GL_FALSE, sm);
            memcpy(cache.shroud_matrix, sm, sizeof(sm));
        }
    }

    cache.initialized = true;
}



// ============================================================================
// Access to pipeline state (for external code that needs it)
// ============================================================================
GLES3_PipelineState* GLES3_Get_State() {
    return &g_State;
}

bool GLES3_Is_Initialized() {
    return g_Initialized;
}

#endif // __EMSCRIPTEN__

// ================================================================================================
// Flow fields on the GPU (AIPathfind.cpp gxFlowFieldPath, page switch bit 26 gpuFlowFields).
// The same fields as the CPU's Dial's-bucket Dijkstra, by relaxation: every pass, every walkable
// cell takes min(its distance, neighbour + 10 straight / 14 diagonal) with the A*'s corner rule.
// Integer only, so exact on every GPU and equal to the CPU's (shortest distances are unique).
// Batched: n fields are tiles of one RGBA32I texture with an unwalkable cell between tiles, so
// one set of passes and one read-back serve them all. Each pass also writes whether the cell
// changed (G); the passes run without checks (as many as the longest field needed so far) and
// the one read-back shows whether the last pass still changed anything: if so, more passes.
// Every GL state touched is saved and restored. Returns the passes run, or 0 (CPU then).
// ================================================================================================
extern "C" int gx_gpu_field_readbacks = 0;
extern "C" int GLES3_GPU_Flow_Fields(int n, int w, int h, const unsigned char* const* walks, const unsigned short* const* seeds, unsigned short* const* outs)
{
    static GLuint prog = 0, vao = 0, fbo = 0, tex[2] = {0, 0}, walkTex = 0;
    static int aw = 0, ah = 0, guess = 64;
    static GLint locD = -1, locW = -1, maxTex = 0;
    if (!prog) {
        const char* vs = "#version 300 es\nvoid main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }";
        const char* fs =
            "#version 300 es\nprecision highp float; precision highp int;\n"
            "uniform highp isampler2D u_d; uniform highp usampler2D u_w; out ivec4 o;\n"
            "const int INF = 65535;\n"
            "bool wk(ivec2 q, ivec2 sz){ return q.x >= 0 && q.y >= 0 && q.x < sz.x && q.y < sz.y && texelFetch(u_w, q, 0).r != 0u; }\n"
            "void main(){ ivec2 p = ivec2(gl_FragCoord.xy); ivec2 sz = textureSize(u_d, 0);\n"
            " int d = texelFetch(u_d, p, 0).r; if (!wk(p, sz)) { o = ivec4(d, 0, 0, 0); return; }\n"
            " int best = d;\n"
            " for (int n = 0; n < 8; ++n) {\n"
            "  ivec2 off = n == 0 ? ivec2(1,0) : n == 1 ? ivec2(0,1) : n == 2 ? ivec2(-1,0) : n == 3 ? ivec2(0,-1) : n == 4 ? ivec2(1,1) : n == 5 ? ivec2(-1,1) : n == 6 ? ivec2(-1,-1) : ivec2(1,-1);\n"
            "  ivec2 q = p + off; if (!wk(q, sz)) continue;\n"
            "  if (n >= 4 && !wk(ivec2(p.x, q.y), sz) && !wk(ivec2(q.x, p.y), sz)) continue;\n"
            "  int dq = texelFetch(u_d, q, 0).r; if (dq >= INF) continue;\n"
            "  int c = dq + (n < 4 ? 10 : 14); if (c < best) best = c;\n"
            " }\n"
            " o = ivec4(best, best != d ? 1 : 0, 0, 0); }";
        GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(v, 1, &vs, nullptr); glCompileShader(v);
        glShaderSource(f, 1, &fs, nullptr); glCompileShader(f);
        prog = glCreateProgram(); glAttachShader(prog, v); glAttachShader(prog, f); glLinkProgram(prog);
        GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) { char log[1024]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log); printf("GXFLOW gpu program failed: %s\n", log); glDeleteProgram(prog); prog = 0; return 0; }
        locD = glGetUniformLocation(prog, "u_d"); locW = glGetUniformLocation(prog, "u_w");
        glGenVertexArrays(1, &vao); glGenFramebuffers(1, &fbo);
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    }
    // atlas: tiles of (w+1) x (h+1), the extra row and column unwalkable
    const int tw = w + 1, th = h + 1;
    const int cols = std::max(1, std::min(n, (int)maxTex / tw)), rows = (n + cols - 1) / cols;
    const int W = cols * tw, H = rows * th;
    if (W > maxTex || H > maxTex) return 0;

    // save the state this touches
    GLint sFbo, sRead, sProg, sVao, sActive, sTex6, sTex7, sVp[4], sAlignU, sAlignP;
    GLboolean sBlend = glIsEnabled(GL_BLEND), sDepth = glIsEnabled(GL_DEPTH_TEST), sScissor = glIsEnabled(GL_SCISSOR_TEST),
              sStencil = glIsEnabled(GL_STENCIL_TEST), sCull = glIsEnabled(GL_CULL_FACE);
    GLboolean sMask[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &sFbo); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &sRead);
    glGetIntegerv(GL_CURRENT_PROGRAM, &sProg); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &sVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &sActive); glGetIntegerv(GL_VIEWPORT, sVp);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &sAlignU); glGetIntegerv(GL_PACK_ALIGNMENT, &sAlignP);
    glGetBooleanv(GL_COLOR_WRITEMASK, sMask);
    glActiveTexture(GL_TEXTURE6); glGetIntegerv(GL_TEXTURE_BINDING_2D, &sTex6);
    glActiveTexture(GL_TEXTURE7); glGetIntegerv(GL_TEXTURE_BINDING_2D, &sTex7);

    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ALIGNMENT, 4);

    if (aw != W || ah != H) {
        if (tex[0]) { glDeleteTextures(2, tex); glDeleteTextures(1, &walkTex); }
        glGenTextures(2, tex); glGenTextures(1, &walkTex);
        for (int i = 0; i < 2; ++i) {
            glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, tex[i]);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32I, W, H);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, walkTex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8UI, W, H);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        aw = W; ah = H;
    }
    std::vector<int> init((size_t)W * H * 4, 0);
    std::vector<unsigned char> wk((size_t)W * H, 0);
    for (size_t i = 0; i < (size_t)W * H; ++i) init[i * 4] = 65535;
    for (int t = 0; t < n; ++t) {
        const int ox = (t % cols) * tw, oy = (t / cols) * th;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t a = (size_t)(oy + y) * W + (ox + x), b = (size_t)y * w + x;
                init[a * 4] = seeds[t][b];
                wk[a] = walks[t][b];
            }
    }
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA_INTEGER, GL_INT, init.data());
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, walkTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RED_INTEGER, GL_UNSIGNED_BYTE, wk.data());

    glUseProgram(prog); glUniform1i(locD, 6); glUniform1i(locW, 7);
    glBindVertexArray(vao);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, W, H);
    std::vector<int> back((size_t)W * H * 4);
    int src = 0, passes = 0, todo = guess;
    bool done = false;
    while (!done && passes < W * H) {
        for (int k = 0; k < todo; ++k, ++passes) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[1 - src], 0);
            glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, tex[src]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            src = 1 - src;
        }
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[src], 0);
        glReadPixels(0, 0, W, H, GL_RGBA_INTEGER, GL_INT, back.data());
        gx_gpu_field_readbacks++;
        done = true;	// the last pass changed nothing: every distance is final
        for (size_t i = 0; i < (size_t)W * H; ++i) if (back[i * 4 + 1]) { done = false; break; }
        todo = 32;
    }
    if (passes > guess) guess = passes;	// next time run as many before the first read-back
    for (int t = 0; t < n; ++t) {
        const int ox = (t % cols) * tw, oy = (t / cols) * th;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int v = back[((size_t)(oy + y) * W + (ox + x)) * 4];
                outs[t][(size_t)y * w + x] = (unsigned short)(v > 65535 ? 65535 : v);
            }
    }

    // restore
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sFbo); glBindFramebuffer(GL_READ_FRAMEBUFFER, sRead);
    glUseProgram(sProg); glBindVertexArray(sVao);
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, sTex6);
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, sTex7);
    glActiveTexture(sActive);
    glViewport(sVp[0], sVp[1], sVp[2], sVp[3]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, sAlignU); glPixelStorei(GL_PACK_ALIGNMENT, sAlignP);
    glColorMask(sMask[0], sMask[1], sMask[2], sMask[3]);
    if (sBlend) glEnable(GL_BLEND); if (sDepth) glEnable(GL_DEPTH_TEST); if (sScissor) glEnable(GL_SCISSOR_TEST);
    if (sStencil) glEnable(GL_STENCIL_TEST); if (sCull) glEnable(GL_CULL_FACE);
    return done ? passes : 0;
}
