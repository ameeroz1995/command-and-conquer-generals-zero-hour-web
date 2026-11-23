/*
** GeneralsX Web Port — Embedded Fixed-Function Emulation Shaders
**
** Provides the GLSL ES 3.0 shader sources that emulate DX8's fixed-function
** pipeline. The shader manager (GLES3_ShaderManager) calls these functions
** to get shader source for compilation.
**
** DESIGN:
** Rather than shipping separate .vert/.frag files (which would need to be
** loaded via Emscripten's filesystem), we embed the shader sources as C
** string literals. This eliminates async file loading and ensures shaders
** are always available.
**
** The pipeline config hash determines which features to enable:
**   bit 0: lighting enabled
**   bit 1: fog enabled
**   bit 2: alpha test enabled
**   bits 3-6: texture stage active (one bit per stage 0-3)
**
** For v1, we use a single universal shader that handles all configurations
** via uniform booleans. This is simpler and still performant since Generals
** only uses ~10-20 unique pipeline configurations per scene. GPU branch
** prediction handles the uniform-based branching efficiently on modern
** hardware (especially since entire draw calls share the same branch path).
**
** BGRA VERTEX COLOR SWIZZLE:
** DX8 stores vertex colors as BGRA (Blue in low byte). Since WebGL2 doesn't
** support GL_BGRA format in vertex attributes, we read the color as RGBA
** from the vertex buffer and swizzle .r↔.b in the vertex shader.
**
** LICENSE: GPL-3.0
*/

#if defined(__EMSCRIPTEN__)
#include <string>
#include <cstring>
#include <cstdio>
#include "gles3_wrapper.h"
#include <emscripten/em_asm.h>


// Scene uniforms shared by every program (GLES3_ShaderManager::Apply_Uniforms, binding 0):
// uploaded once when they change instead of per program, since variants switch programs often.
#define GX_SCENE_BLOCK \
"struct Light {\n" \
"    vec4 diffuse;\n" \
"    vec4 ambient;\n" \
"    vec4 specular;\n" \
"    vec4 position;  // xyz, w = range (point/spot: no light beyond it)\n" \
"    vec4 direction; // xyz\n" \
"    vec4 atten;     // attenuation0-2, w = type: 0 off, 1 point, 2 spot, 3 directional\n" \
"};\n" \
"layout(std140) uniform GXScene {\n" \
"    mat4 u_View;\n" \
"    mat4 u_Projection;\n" \
"    vec4 u_GlobalAmbient;\n" \
"    vec4 u_FogColor;     // rgb\n" \
"    vec4 u_FogParams;    // start, end, lod bias (log2(drawing buffer / back buffer), <= 0)\n" \
"    vec4 u_ViewportRect; // x, y, width, height in pixels\n" \
"    vec4 u_TextureFactor;\n" \
"    Light u_Lights[4];\n" \
"};\n"

// ============================================================================
// Vertex Shader — DX8 Fixed-Function Pipeline Emulation
// ============================================================================
static const char* g_VertexShaderSource = R"GLSL(#version 300 es
/*
** DX8 Fixed-Function Pipeline Emulation — Vertex Shader
** Handles: transform, per-vertex lighting, fog, tex coord passthrough
** BGRA vertex color swizzle (DX8 uses BGRA byte order)
*/
precision highp float;
)GLSL" GX_SCENE_BLOCK R"GLSL(
// Vertex attributes (matching FVF decoder attribute locations)
layout(location = 0) in vec4 a_Position;  // xyz (w defaults to 1), or XYZRHW
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Color;       // DX8 DIFFUSE (BGRA byte order in memory)
layout(location = 3) in vec2 a_TexCoord0;
layout(location = 4) in vec2 a_TexCoord1;
layout(location = 5) in vec2 a_TexCoord2;
layout(location = 6) in vec2 a_TexCoord3;
layout(location = 7) in mat4 a_InstanceWorld; // Instanced matrix layout locations 7, 8, 9, 10
layout(location = 11) in vec4 a_Specular;     // DX8 SPECULAR (BGRA), 0 when the FVF has none
layout(location = 12) in vec2 a_InstanceUV;   // per instance: added to the stage 0 texture coordinate (batched tank treads)

// Transform matrices (view and projection are in GXScene)
uniform mat4 u_World;
uniform bool u_InstancingEnabled;

// Per-stage texture coordinates, as D3D8 fixed function builds them:
// D3DTSS_TEXCOORDINDEX (low bits: UV set, high 16 bits: D3DTSS_TCI_* generation mode),
// D3DTSS_TEXTURETRANSFORMFLAGS (D3DTTFF_COUNTn | D3DTTFF_PROJECTED) and D3DTS_TEXTUREn.
// The shroud, cloud shadows and projected decals use camera-space generation.
uniform int u_TexCoordIndex[4];
uniform int u_TexTransform[4];
uniform mat4 u_TexMatrix[4];

// Material
uniform vec4 u_MatDiffuse;
uniform vec4 u_MatAmbient;
uniform vec4 u_MatSpecular;
uniform vec4 u_MatEmissive;
uniform float u_MatPower;

// Which material colours come from the vertex colour (D3DRS_COLORVERTEX with
// D3DRS_*MATERIALSOURCE = D3DMCS_COLOR1 and a diffuse vertex colour):
// bit 0 ambient, bit 1 diffuse, bit 2 emissive. Otherwise the material is used.
uniform int u_ColorVertex;
#if GX_VGEN
#define GX_CVX GX_CV
vec3 gx_tc0(vec3 eyePos, vec3 eyeNormal); // generated per vertex setup, appended after main
vec3 gx_tc1(vec3 eyePos, vec3 eyeNormal);
vec3 gx_tc2(vec3 eyePos, vec3 eyeNormal);
vec3 gx_tc3(vec3 eyePos, vec3 eyeNormal);
void gx_lights(vec3 worldNormal, vec3 worldPos, vec3 toEye, vec4 matDiff,
               inout vec4 totalDiffuse, inout vec4 lightAmbient, inout vec4 totalSpecular);
#else
#define GX_CVX u_ColorVertex
#endif


// Outputs to fragment shader
out vec4 v_Color;
out vec4 v_Specular;
out vec3 v_TexCoord0; // xy, and the divisor for D3DTTFF_PROJECTED (else 1)
out vec3 v_TexCoord1;
out vec3 v_TexCoord2;
out vec3 v_TexCoord3;
out float v_FogFactor;
#if GX_SHROUD
uniform mat4 u_ShroudMatrix; // camera-space position -> shroud uv (GLES3_Set_Shroud_Fold)
out vec2 v_ShroudUV;
#endif

vec3 stage_texcoord(int i, vec3 eyePos, vec3 eyeNormal) {
    int tci = u_TexCoordIndex[i];
    int mode = tci >> 16;
    vec4 c;
    if (mode == 1) c = vec4(eyeNormal, 1.0);                              // CAMERASPACENORMAL
    else if (mode == 2) c = vec4(eyePos, 1.0);                            // CAMERASPACEPOSITION
    else if (mode == 3) c = vec4(reflect(normalize(eyePos), eyeNormal), 1.0); // REFLECTIONVECTOR
    else {
        int set = tci & 3;
        vec2 uv = set == 0 ? a_TexCoord0 : set == 1 ? a_TexCoord1 : set == 2 ? a_TexCoord2 : a_TexCoord3;
        c = vec4(uv, 1.0, 0.0); // D3D puts the 2D translation in the matrix's third row
    }
    int count = u_TexTransform[i] & 0xff;
    if (count == 0) return vec3(c.xy, 1.0);
    vec4 t = u_TexMatrix[i] * c;
    float q = 1.0;
    if ((u_TexTransform[i] & 256) != 0) q = count == 4 ? t.w : count == 3 ? t.z : t.y;
    return vec3(t.xy, q);
}

void main() {
    vec4 vertexColor = a_Color.bgra;
    if (GX_PRE) {
        float w = a_Position.w > 0.0 ? 1.0 / a_Position.w : 1.0;
        // D3D pixel centres are at integer coordinates, GL's at +0.5.
        vec2 p = (a_Position.xy + 0.5 - u_ViewportRect.xy) / u_ViewportRect.zw;
        gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, a_Position.z * 2.0 - 1.0, 1.0) * w;
        v_Color = vertexColor;
        v_Specular = a_Specular.bgra;
        v_TexCoord0 = vec3(a_TexCoord0, 1.0);
        v_TexCoord1 = vec3(a_TexCoord1, 1.0);
        v_TexCoord2 = vec3(a_TexCoord2, 1.0);
        v_TexCoord3 = vec3(a_TexCoord3, 1.0);
        v_FogFactor = 1.0;
        return;
    }
    mat4 worldMat = u_World;
    if (u_InstancingEnabled) {
        worldMat = a_InstanceWorld;
    }
    // Transform position through World * View * Projection
    vec4 worldPos = worldMat * vec4(a_Position.xyz, 1.0);
    vec4 viewPos = u_View * worldPos;
    gl_Position = u_Projection * viewPos;
    // Map D3D projection depth [0, w] to OpenGL ES clip space depth [-w, w]
    gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;

    // BGRA → RGBA swizzle for vertex color
    // DX8 stores vertex colors as BGRA in memory. When read as GL_UNSIGNED_BYTE
    // with GL_RGBA format, the bytes map to: R=B, G=G, B=R, A=A
    // We need to swap R and B channels.

    vec3 eyeNormal = normalize(mat3(u_View) * (mat3(worldMat) * a_Normal));
    // Only the stages this variant runs (a pixel shader may read all four); the conditions
    // are compile-time constants, so the others cost nothing per vertex.
    const vec3 unused = vec3(0.0, 0.0, 1.0);
#if GX_VGEN
    v_TexCoord0 = (GX_PS || GX_STAGES > 0) ? gx_tc0(viewPos.xyz, eyeNormal) : unused;
    v_TexCoord1 = (GX_PS || GX_STAGES > 1) ? gx_tc1(viewPos.xyz, eyeNormal) : unused;
    v_TexCoord2 = (GX_PS || GX_STAGES > 2) ? gx_tc2(viewPos.xyz, eyeNormal) : unused;
    v_TexCoord3 = (GX_PS || GX_STAGES > 3) ? gx_tc3(viewPos.xyz, eyeNormal) : unused;
#else
    v_TexCoord0 = (GX_PS || GX_STAGES > 0) ? stage_texcoord(0, viewPos.xyz, eyeNormal) : unused;
    v_TexCoord1 = (GX_PS || GX_STAGES > 1) ? stage_texcoord(1, viewPos.xyz, eyeNormal) : unused;
    v_TexCoord2 = (GX_PS || GX_STAGES > 2) ? stage_texcoord(2, viewPos.xyz, eyeNormal) : unused;
    v_TexCoord3 = (GX_PS || GX_STAGES > 3) ? stage_texcoord(3, viewPos.xyz, eyeNormal) : unused;
#endif
    if (u_InstancingEnabled) v_TexCoord0.xy += a_InstanceUV;
#if GX_SHROUD
    v_ShroudUV = (u_ShroudMatrix * vec4(viewPos.xyz, 1.0)).xy;
#endif

    // Compute lighting
    if (GX_LIGHT) {
        mat3 worldNormalMatrix = mat3(worldMat);
        vec3 worldNormal = normalize(worldNormalMatrix * a_Normal);

        // Start with emissive material color
        vec4 totalDiffuse = (GX_CVX & 4) != 0 ? vertexColor : u_MatEmissive;

        // Global ambient contribution
        vec4 matAmb = (GX_CVX & 1) != 0 ? vertexColor : u_MatAmbient;
        vec4 matDiff = (GX_CVX & 2) != 0 ? vertexColor : u_MatDiffuse;
        vec4 lightAmbient = u_GlobalAmbient; // D3D: global + sum of attenuated light ambients
        vec4 totalSpecular = vec4(0.0);
        // Camera position in world space, for the local-viewer half vector.
        vec3 camPos = -transpose(mat3(u_View)) * u_View[3].xyz;
        vec3 toEye = normalize(camPos - worldPos.xyz);

        // Per-light contribution
#if GX_VGEN
        gx_lights(worldNormal, worldPos.xyz, toEye, matDiff, totalDiffuse, lightAmbient, totalSpecular);
#else
        for (int i = 0; i < 4; i++) {
            Light L = u_Lights[i];
            int type = int(L.atten.w);
            if (type == 0) continue;

            vec3 lightDir;
            float attenuation = 1.0;

            if (type == 3) {
                // Directional light — direction is towards the light source
                lightDir = normalize(-L.direction.xyz);
            } else if (type == 1) {
                // Point light — direction from vertex to light
                vec3 toLight = L.position.xyz - worldPos.xyz;
                float dist = length(toLight);
                if (dist > L.position.w) continue;
                lightDir = dist > 0.0 ? toLight / dist : vec3(0.0, 0.0, 1.0);
                attenuation = 1.0 / (L.atten.x
                    + L.atten.y * dist
                    + L.atten.z * dist * dist);
            } else {
                // Spot light — simplified as point with direction check
                vec3 toLight = L.position.xyz - worldPos.xyz;
                float dist = length(toLight);
                if (dist > L.position.w) continue;
                lightDir = dist > 0.0 ? toLight / dist : vec3(0.0, 0.0, 1.0);
                attenuation = 1.0 / (L.atten.x
                    + L.atten.y * dist
                    + L.atten.z * dist * dist);
                // Spot cone falloff
                float cosAngle = dot(-lightDir, normalize(L.direction.xyz));
                attenuation *= smoothstep(0.0, 1.0, cosAngle);
            }

            float NdotL = max(dot(worldNormal, lightDir), 0.0);

            totalDiffuse += matDiff * L.diffuse * NdotL * attenuation;
            lightAmbient += L.ambient * attenuation;
            if (GX_SPEC && NdotL > 0.0) {
                float NdotH = max(dot(worldNormal, normalize(lightDir + toEye)), 0.0);
                totalSpecular += L.specular * pow(NdotH, max(u_MatPower, 0.0001)) * attenuation;
            }
        }
#endif

        v_Color = clamp(totalDiffuse + matAmb * lightAmbient, 0.0, 1.0);
        v_Color.a = matDiff.a;
        v_Specular = clamp(u_MatSpecular * totalSpecular, 0.0, 1.0);
    } else {
        // No lighting — use vertex color directly (UI, 2D elements, terrain decals)
        v_Color = vertexColor;
        v_Specular = a_Specular.bgra;
    }

    // Fog factor (linear fog in eye space)
    if (GX_FOG && u_FogParams.y > u_FogParams.x) {
        float eyeDist = abs(viewPos.z); // D3D vertex fog uses eye-space z (no RANGEFOGENABLE)
        v_FogFactor = clamp((u_FogParams.y - eyeDist) / (u_FogParams.y - u_FogParams.x), 0.0, 1.0);
    } else {
        v_FogFactor = 1.0;
    }
}
)GLSL";


// ============================================================================
// Fragment Shader — DX8 Fixed-Function Pipeline Emulation
// ============================================================================
static const char* g_FragmentShaderSource = R"GLSL(#version 300 es
/*
** DX8 Fixed-Function Pipeline Emulation — Fragment Shader
** Handles: multi-texture blending, alpha test, fog
*/
precision highp float;
)GLSL" GX_SCENE_BLOCK R"GLSL(
// Inputs from vertex shader
in vec4 v_Color;
in vec3 v_TexCoord0;
in vec3 v_TexCoord1;
in vec3 v_TexCoord2;
in vec3 v_TexCoord3;
in float v_FogFactor;

// Texture samplers
uniform sampler2D u_Texture0;
uniform sampler2D u_Texture1;
uniform sampler2D u_Texture2;
uniform sampler2D u_Texture3;


// Alpha test emulation (removed in GLES3, emulated here)
uniform float u_AlphaRef;         // 0.0 - 1.0 (normalized from DX8's 0-255)
uniform int u_AlphaFunc;          // D3DCMPFUNC enum value


// Texture stages 0-3, as D3D8 fixed function runs them. u_StageOps: COLOROP, ALPHAOP per
// stage (D3DTEXTUREOP). u_StageArgs: COLORARG0/1/2, ALPHAARG0/1/2 per stage (D3DTA_*: low
// 4 bits select, 0x10 COMPLEMENT, 0x20 ALPHAREPLICATE). Bit n of u_TexBound: stage n has a
// texture (D3D reads an unbound stage's texture as opaque white).
uniform int u_StageOps[8];
uniform int u_StageArgs[24];
uniform int u_TexBound;


in vec4 v_Specular; // added after the stages when D3DRS_SPECULARENABLE (GX_SPEC)
#if GX_SHROUD
uniform sampler2D u_ShroudTex; // unit 4
in vec2 v_ShroudUV;
#endif

// Optional render scale: sampling with u_FogParams.z as the bias picks the mip level the
// full-size frame would, so a smaller buffer doesn't drop to tiny mips (the terrain atlas's
// tile borders bleed there) and textures stay sharp.
#define u_LodBias u_FogParams.z

// A ps.1.1 pixel shader set with SetPixelShader replaces the stages (u_PSCount > 0).
// Two ivec4 per instruction (encoded by GLES3_Create_Pixel_Shader):
//   [2i]   = (opcode | writemask << 8 | shift << 12 | saturate << 16, dst, src0, src1)
//   [2i+1] = (src2, 0, 0, 0)
// A register is 0-1 r0-r1, 2-5 t0-t3, 6-7 v0-v1, 8-15 c0-c7; a source adds swizzle << 5
// (1 = .a, 2 = .b replicate) and modifier << 8 (D3DSPSM_*).
uniform int u_PSCount;
uniform ivec4 u_PS[32];
uniform vec4 u_PSConst[8];

// Output
out vec4 fragColor;

// Alpha test comparison. A program from GLES3_FF_Generate_GLSL has the function as a constant
// (GX_AFUNC), so only its own comparison is compiled.
#ifdef GX_AFUNC
#define GX_AF GX_AFUNC
#else
#define GX_AF u_AlphaFunc
#endif
bool alpha_test(float alpha) {
    // D3DCMPFUNC values
    if (GX_AF == 1) return false;                             // NEVER
    if (GX_AF == 2) return alpha < u_AlphaRef;                // LESS
    if (GX_AF == 3) return abs(alpha - u_AlphaRef) < 0.004;   // EQUAL
    if (GX_AF == 4) return alpha <= u_AlphaRef;               // LESSEQUAL
    if (GX_AF == 5) return alpha > u_AlphaRef;                // GREATER
    if (GX_AF == 6) return abs(alpha - u_AlphaRef) >= 0.004;  // NOTEQUAL
    if (GX_AF == 7) return alpha >= u_AlphaRef;               // GREATEREQUAL
    if (GX_AF == 8) return true;                              // ALWAYS
    return true;
}

vec4 sample_stage(int s) {
    if ((u_TexBound & (1 << s)) == 0) return vec4(1.0);
    if (s == 0) return texture(u_Texture0, v_TexCoord0.xy / v_TexCoord0.z, u_LodBias);
    if (s == 1) return texture(u_Texture1, v_TexCoord1.xy / v_TexCoord1.z, u_LodBias);
    if (s == 2) return texture(u_Texture2, v_TexCoord2.xy / v_TexCoord2.z, u_LodBias);
    return texture(u_Texture3, v_TexCoord3.xy / v_TexCoord3.z, u_LodBias);
}

vec4 stage_arg(int a, vec4 diffuse, vec4 current, vec4 tex) {
    int sel = a & 15;
    vec4 v = sel == 0 ? diffuse : sel == 2 ? tex : sel == 3 ? u_TextureFactor
           : sel == 4 ? v_Specular : current; // 1 CURRENT; 5 TEMP as CURRENT
    if ((a & 16) != 0) v = vec4(1.0) - v;
    if ((a & 32) != 0) v = vec4(v.a);
    return v;
}

// One D3DTEXTUREOP on all four channels; the caller keeps .rgb (colour) or .a (alpha).
vec4 tex_op(int op, vec4 a1, vec4 a2, vec4 a0, vec4 diffuse, vec4 current, vec4 tex) {
    vec4 r;
    if (op == 2) r = a1;                                   // SELECTARG1
    else if (op == 3) r = a2;                              // SELECTARG2
    else if (op == 5) r = a1 * a2 * 2.0;                   // MODULATE2X
    else if (op == 6) r = a1 * a2 * 4.0;                   // MODULATE4X
    else if (op == 7) r = a1 + a2;                         // ADD
    else if (op == 8) r = a1 + a2 - 0.5;                   // ADDSIGNED
    else if (op == 9) r = (a1 + a2 - 0.5) * 2.0;           // ADDSIGNED2X
    else if (op == 10) r = a1 - a2;                        // SUBTRACT
    else if (op == 11) r = a1 + a2 - a1 * a2;              // ADDSMOOTH
    else if (op == 12) r = mix(a2, a1, diffuse.a);         // BLENDDIFFUSEALPHA
    else if (op == 13) r = mix(a2, a1, tex.a);             // BLENDTEXTUREALPHA
    else if (op == 14) r = mix(a2, a1, u_TextureFactor.a); // BLENDFACTORALPHA
    else if (op == 15) r = a1 + a2 * (1.0 - tex.a);        // BLENDTEXTUREALPHAPM
    else if (op == 16) r = mix(a2, a1, current.a);         // BLENDCURRENTALPHA
    else if (op == 18) r = a1 + a1.a * a2;                 // MODULATEALPHA_ADDCOLOR
    else if (op == 19) r = a1 * a2 + a1.a;                 // MODULATECOLOR_ADDALPHA
    else if (op == 20) r = (1.0 - a1.a) * a2 + a1;         // MODULATEINVALPHA_ADDCOLOR
    else if (op == 21) r = (1.0 - a1) * a2 + a1.a;         // MODULATEINVCOLOR_ADDALPHA
    else if (op == 22 || op == 23) r = a2;                 // BUMPENVMAP*: no bump, pass on
    else if (op == 24) r = vec4(4.0 * dot(a1.rgb - 0.5, a2.rgb - 0.5)); // DOTPRODUCT3
    else if (op == 25) r = a0 + a1 * a2;                   // MULTIPLYADD
    else if (op == 26) r = mix(a2, a1, a0);                // LERP
    else r = a1 * a2;                                      // MODULATE, PREMODULATE
    return clamp(r, 0.0, 1.0);                             // D3D saturates every stage
}

vec4 ps_src(vec4 R[16], int s) {
    vec4 v = R[s & 15];
    int swz = (s >> 5) & 3;
    if (swz == 1) v = vec4(v.a); else if (swz == 2) v = vec4(v.b);
    int m = (s >> 8) & 15;
    if (m == 1) v = -v;                          // NEG
    else if (m == 2) v = v - 0.5;                // BIAS
    else if (m == 3) v = 0.5 - v;                // BIASNEG
    else if (m == 4) v = v * 2.0 - 1.0;          // SIGN
    else if (m == 5) v = 1.0 - v * 2.0;          // SIGNNEG
    else if (m == 6) v = 1.0 - v;                // COMP
    else if (m == 7) v = v * 2.0;                // X2
    else if (m == 8) v = -v * 2.0;               // X2NEG
    return v;
}

vec4 run_pixel_shader() {
    vec4 R[16];
    R[0] = vec4(0.0); R[1] = vec4(0.0);
    for (int i = 0; i < 4; i++) R[2 + i] = sample_stage(i);
    R[6] = v_Color; R[7] = v_Specular;
    for (int i = 0; i < 8; i++) R[8 + i] = u_PSConst[i];
    for (int i = 0; i < 16; i++) {
        if (i >= u_PSCount) break;
        ivec4 a = u_PS[2 * i];
        int op = a.x & 255;
        if (op == 66) continue; // tex: the stage samples are already in t0-t3
        vec4 s0 = ps_src(R, a.z), s1 = ps_src(R, a.w), s2 = ps_src(R, u_PS[2 * i + 1].x);
        vec4 r;
        if (op == 1) r = s0;                          // mov
        else if (op == 2) r = s0 + s1;                // add
        else if (op == 3) r = s0 - s1;                // sub
        else if (op == 4) r = s0 * s1 + s2;           // mad
        else if (op == 8) r = vec4(dot(s0.rgb, s1.rgb)); // dp3
        else if (op == 9) r = vec4(dot(s0, s1));      // dp4
        else if (op == 18) r = mix(s2, s1, s0);       // lrp
        else r = s0 * s1;                             // mul
        int shift = (a.x >> 12) & 15;
        if (shift == 1) r *= 2.0; else if (shift == 2) r *= 4.0; else if (shift == 15) r *= 0.5;
        r = ((a.x >> 16) & 1) != 0 ? clamp(r, 0.0, 1.0) : clamp(r, -1.0, 1.0); // ps.1.1 range
        int mask = (a.x >> 8) & 15;
        R[a.y] = mix(R[a.y], r, bvec4((mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0, (mask & 8) != 0));
    }
    return clamp(R[0], 0.0, 1.0);
}

#if GX_PSGEN
vec4 run_ps_gen(); // generated from the ps.1.1 program, appended after main
#endif
#if GX_FFGEN
vec4 run_ff(vec4 diffuse); // the texture stages as straight-line code, appended after main
#endif

void main() {
    vec4 diffuse = v_Color;
    vec4 color = v_Color; // CURRENT: the diffuse colour at stage 0

    if (GX_PS) {
#if GX_PSGEN
        color = run_ps_gen();
#else
        color = run_pixel_shader();
#endif
    } else {
#if GX_FFGEN
        color = run_ff(diffuse);
#else
        for (int s = 0; s < GX_STAGES; s++) {
            int cop = u_StageOps[s * 2], aop = u_StageOps[s * 2 + 1];
            if (cop <= 1) break; // D3DTOP_DISABLE ends the chain here
            vec4 tex = sample_stage(s);
            int b = s * 6;
            vec4 c0 = stage_arg(u_StageArgs[b + 0], diffuse, color, tex);
            vec4 c1 = stage_arg(u_StageArgs[b + 1], diffuse, color, tex);
            vec4 c2 = stage_arg(u_StageArgs[b + 2], diffuse, color, tex);
            vec3 rgb = tex_op(cop, c1, c2, c0, diffuse, color, tex).rgb;
            if (aop > 1) {
                vec4 x0 = vec4(stage_arg(u_StageArgs[b + 3], diffuse, color, tex).a);
                vec4 x1 = vec4(stage_arg(u_StageArgs[b + 4], diffuse, color, tex).a);
                vec4 x2 = vec4(stage_arg(u_StageArgs[b + 5], diffuse, color, tex).a);
                color.a = tex_op(aop, x1, x2, x0, diffuse, color, tex).a;
            }
            color.rgb = rgb;
        }
#endif
        if (GX_SPEC) color.rgb = clamp(color.rgb + v_Specular.rgb, 0.0, 1.0);
    }

    // --- Alpha test ---
    if (GX_ATEST && !alpha_test(color.a)) {
        discard;
    }

    // --- Fog blending ---
    if (GX_FOG) {
        color.rgb = mix(u_FogColor.rgb, color.rgb, v_FogFactor);
    }

    fragColor = color;
#if GX_SHROUD
    // The shroud pass multiplied the frame by the shroud texture after everything at
    // terrain depth was drawn; for draws linear in their colour, doing it per draw is the same.
    fragColor.rgb *= texture(u_ShroudTex, v_ShroudUV).rgb;
#endif
}
)GLSL";


// ============================================================================
// Public API — called by GLES3_ShaderManager::Get_Program()
// ============================================================================

/*
** For v1 we return the same universal shader for all configurations.
** The shader uses uniform booleans to handle different pipeline states,
** which is efficient because the GPU evaluates all fragments in a draw call
** with the same branch path (uniform-based branching is essentially free).
**
** If profiling shows this is a bottleneck, v2 can generate specialized
** shader variants based on the config hash (e.g., separate shaders for
** lit vs unlit, fogged vs unfogged, 1-texture vs 2-texture).
*/

extern "C" {

} // extern "C"

// Each variant (GLES3_ShaderManager::Hash_Pipeline_Config) gets #defines right after the
// #version line; the shaders test them instead of uniforms, so the compiler drops the code a
// variant never runs. key: the setup to compile in (stages and alpha test for the fragment
// shader, texture coordinates, lights and colour sources for the vertex shader), or nullptr.
static std::string Variant_Source(const char* base, unsigned int h, bool is_fragment, const GLES3_FFKey* key) {
    const char* body = strchr(base, '\n') + 1; // after "#version 300 es"
    char defs[384];
    auto b = [](bool v) { return v ? "true" : "false"; }; // GLSL has no int -> bool
    bool ps = (h & 32) != 0;
    unsigned int psgen = ps ? (h >> 6) & 15 : 0; // ps.1.1 handle compiled to GLSL
    int stages = ps ? 0 : (int)((h >> 6) & 7);
    bool ffgen = is_fragment && key && stages > 0;
    int n = snprintf(defs, sizeof(defs),
             "#version 300 es\n#define GX_LIGHT %s\n#define GX_FOG %s\n#define GX_ATEST %s\n"
             "#define GX_PRE %s\n#define GX_SPEC %s\n#define GX_PS %s\n#define GX_STAGES %d\n"
             "#define GX_PSGEN %d\n#define GX_FFGEN %d\n#define GX_VGEN %d\n#define GX_SHROUD %d\n",
             b(h & 1), b(h & 2), b(h & 4), b(h & 8), b(h & 16), b(ps), stages,
             is_fragment && psgen ? 1 : 0, ffgen ? 1 : 0, !is_fragment && key ? 1 : 0, key && key->fold ? 1 : 0);
    if (is_fragment && key && key->afunc) snprintf(defs + n, sizeof(defs) - n, "#define GX_AFUNC %d\n", key->afunc);
    if (!is_fragment && key) snprintf(defs + n, sizeof(defs) - n, "#define GX_CV %d\n", key->cvert);
    std::string s = std::string(defs) + body;
    // Item 9 of the low-end plan: on weak machines (window.gxLowEnd) the fragment shader does
    // its colour maths at mediump, which integrated and mobile GPUs run at 16 bits, faster.
    // The scene block (shared with the vertex shader, so its precision must match) stays
    // highp, and so do texture coordinates and the shroud uv: at mediump a 1024-texel texture
    // would lose sub-texel position. ?mediump=0 / =1 forces it off / on for testing. Software
    // renderers compute 32 bits either way.
    static int mediump = -1;
    if (mediump < 0) mediump = MAIN_THREAD_EM_ASM_INT({
        try { const q = location.search; if (q.indexOf('mediump=1') >= 0) return 1; if (q.indexOf('mediump=0') >= 0) return 0; } catch (e) {}
        if (typeof window === 'undefined') return 0;
        var o = window.GeneralsX && window.GeneralsX.opt; // performance switch: Lower shader precision
        return (o ? o.mediump : window.gxLowEnd) ? 1 : 0; });
    if (is_fragment && mediump) {
        const char* marks[][2] = {
            {"// Inputs from vertex shader\n", "precision mediump float;\n// Inputs from vertex shader\n"},
            {"in vec3 v_TexCoord0;", "in highp vec3 v_TexCoord0;"}, {"in vec3 v_TexCoord1;", "in highp vec3 v_TexCoord1;"},
            {"in vec3 v_TexCoord2;", "in highp vec3 v_TexCoord2;"}, {"in vec3 v_TexCoord3;", "in highp vec3 v_TexCoord3;"},
            {"in vec2 v_ShroudUV;", "in highp vec2 v_ShroudUV;"}};
        for (auto& m : marks) {
            size_t at = s.find(m[0]);
            if (at != std::string::npos) s.replace(at, strlen(m[0]), m[1]);
        }
    }
    if (is_fragment && psgen) s += GLES3_PS_Generate_GLSL(psgen);
    if (ffgen) s += GLES3_FF_Generate_GLSL(*key);
    if (!is_fragment && key) s += GLES3_VS_Generate_GLSL(*key);
    return s;
}

std::string GLES3_Get_Vertex_Shader_Source(unsigned int config_hash, const GLES3_FFKey* key) {
    return Variant_Source(g_VertexShaderSource, config_hash, false, key);
}

std::string GLES3_Get_Fragment_Shader_Source(unsigned int config_hash, const GLES3_FFKey* key) {
    return Variant_Source(g_FragmentShaderSource, config_hash, true, key);
}

// The vertex side of one setup as straight-line GLSL: texture coordinates per stage (same
// rules as stage_texcoord) and the lights unrolled with their types fixed (same maths as the
// loop in main). Matrices, colours and ranges still come from uniforms.
std::string GLES3_VS_Generate_GLSL(const GLES3_FFKey& k) {
    std::string s = "\n";
    for (int i = 0; i < 4; i++) {
        const int v = k.vtex[i], mode = v & 3, set = (v >> 2) & 3, count = (v >> 4) & 7;
        const bool proj = (v & 128) != 0;
        const std::string n(1, (char)('0' + i));
        s += "vec3 gx_tc" + n + "(vec3 eyePos, vec3 eyeNormal) {\n    vec4 c = ";
        s += mode == 1 ? "vec4(eyeNormal, 1.0)" : mode == 2 ? "vec4(eyePos, 1.0)"
           : mode == 3 ? "vec4(reflect(normalize(eyePos), eyeNormal), 1.0)"
           : "vec4(a_TexCoord" + std::string(1, (char)('0' + set)) + ", 1.0, 0.0)";
        s += ";\n";
        if (count == 0) {
            s += "    return vec3(c.xy, 1.0);\n}\n";
        } else {
            s += "    vec4 t = u_TexMatrix[" + n + "] * c;\n    return vec3(t.xy, ";
            s += !proj ? "1.0" : count == 4 ? "t.w" : count == 3 ? "t.z" : "t.y";
            s += ");\n}\n";
        }
    }
    s += "void gx_lights(vec3 worldNormal, vec3 worldPos, vec3 toEye, vec4 matDiff,\n"
         "               inout vec4 totalDiffuse, inout vec4 lightAmbient, inout vec4 totalSpecular) {\n";
    for (int i = 0; i < 4; i++) {
        const int type = (k.lights >> (i * 2)) & 3;
        if (type == 0) continue;
        s += "    {\n        Light L = u_Lights[" + std::string(1, (char)('0' + i)) + "];\n"
             "        vec3 lightDir;\n        float attenuation = 1.0;\n        bool lit = true;\n";
        if (type == 3) {
            s += "        lightDir = normalize(-L.direction.xyz);\n";
        } else {
            s += "        vec3 toLight = L.position.xyz - worldPos;\n"
                 "        float dist = length(toLight);\n"
                 "        lit = !(dist > L.position.w);\n"
                 "        lightDir = dist > 0.0 ? toLight / dist : vec3(0.0, 0.0, 1.0);\n"
                 "        attenuation = 1.0 / (L.atten.x + L.atten.y * dist + L.atten.z * dist * dist);\n";
            if (type == 2)
                s += "        attenuation *= smoothstep(0.0, 1.0, dot(-lightDir, normalize(L.direction.xyz)));\n";
        }
        s += "        if (lit) {\n"
             "            float NdotL = max(dot(worldNormal, lightDir), 0.0);\n"
             "            totalDiffuse += matDiff * L.diffuse * NdotL * attenuation;\n"
             "            lightAmbient += L.ambient * attenuation;\n"
             "            if (GX_SPEC && NdotL > 0.0) {\n"
             "                float NdotH = max(dot(worldNormal, normalize(lightDir + toEye)), 0.0);\n"
             "                totalSpecular += L.specular * pow(NdotH, max(u_MatPower, 0.0001)) * attenuation;\n"
             "            }\n        }\n    }\n";
    }
    return s + "}\n";
}

// One D3D texture stage argument (D3DTA_*) as a GLSL expression; same rules as stage_arg().
static std::string FF_Arg(int a) {
    int sel = a & 15;
    std::string v = sel == 0 ? "diffuse" : sel == 2 ? "tex" : sel == 3 ? "u_TextureFactor"
                  : sel == 4 ? "v_Specular" : "cur"; // 1 CURRENT; 5 TEMP as CURRENT
    if (a & 16) v = "(vec4(1.0) - " + v + ")";
    if (a & 32) v = "vec4((" + v + ").a)";
    return v;
}

// One D3DTEXTUREOP as a GLSL expression; same formulas as tex_op().
static std::string FF_Op(int op, const std::string& a1, const std::string& a2, const std::string& a0) {
    switch (op) {
        case 2: return a1;
        case 3: return a2;
        case 5: return "(" + a1 + " * " + a2 + " * 2.0)";
        case 6: return "(" + a1 + " * " + a2 + " * 4.0)";
        case 7: return "(" + a1 + " + " + a2 + ")";
        case 8: return "(" + a1 + " + " + a2 + " - 0.5)";
        case 9: return "((" + a1 + " + " + a2 + " - 0.5) * 2.0)";
        case 10: return "(" + a1 + " - " + a2 + ")";
        case 11: return "(" + a1 + " + " + a2 + " - " + a1 + " * " + a2 + ")";
        case 12: return "mix(" + a2 + ", " + a1 + ", diffuse.a)";
        case 13: return "mix(" + a2 + ", " + a1 + ", tex.a)";
        case 14: return "mix(" + a2 + ", " + a1 + ", u_TextureFactor.a)";
        case 15: return "(" + a1 + " + " + a2 + " * (1.0 - tex.a))";
        case 16: return "mix(" + a2 + ", " + a1 + ", cur.a)";
        case 18: return "(" + a1 + " + (" + a1 + ").a * " + a2 + ")";
        case 19: return "(" + a1 + " * " + a2 + " + (" + a1 + ").a)";
        case 20: return "((1.0 - (" + a1 + ").a) * " + a2 + " + " + a1 + ")";
        case 21: return "((vec4(1.0) - " + a1 + ") * " + a2 + " + (" + a1 + ").a)";
        case 22: case 23: return a2;
        case 24: return "vec4(4.0 * dot((" + a1 + ").rgb - 0.5, (" + a2 + ").rgb - 0.5))";
        case 25: return "(" + a0 + " + " + a1 + " * " + a2 + ")";
        case 26: return "mix(" + a2 + ", " + a1 + ", " + a0 + ")";
        default: return "(" + a1 + " * " + a2 + ")"; // MODULATE, PREMODULATE
    }
}

// The texture stages of one setup as straight-line GLSL: run_ff(), called by main instead of
// the interpreter loop. Each stage reads the colour before it (cur) for both its colour and
// alpha, and D3D saturates every stage, as in the loop.
std::string GLES3_FF_Generate_GLSL(const GLES3_FFKey& k) {
    std::string s = "\nvec4 run_ff(vec4 diffuse) {\n    vec4 color = diffuse;\n";
    for (int st = 0; st < 4 && k.op[st][0] > 1; st++) {
        const char n = (char)('0' + st);
        s += "    {\n        vec4 cur = color;\n        vec4 tex = ";
        s += (k.bound & (1 << st))
            ? std::string("texture(u_Texture") + n + ", v_TexCoord" + n + ".xy / v_TexCoord" + n + ".z, u_LodBias);\n"
            : std::string("vec4(1.0);\n");
        const unsigned char* a = k.arg[st];
        s += "        color.rgb = clamp(" + FF_Op(k.op[st][0], FF_Arg(a[1]), FF_Arg(a[2]), FF_Arg(a[0])) + ", 0.0, 1.0).rgb;\n";
        if (k.op[st][1] > 1) {
            auto x = [](int v) { return "vec4(" + FF_Arg(v) + ".a)"; };
            s += "        color.a = clamp(" + FF_Op(k.op[st][1], x(a[4]), x(a[5]), x(a[3])) + ", 0.0, 1.0).a;\n";
        }
        s += "    }\n";
    }
    return s + "    return color;\n}\n";
}

#endif // __EMSCRIPTEN__
