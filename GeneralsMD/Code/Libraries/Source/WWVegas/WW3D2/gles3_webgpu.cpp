/*
** GeneralsX Web Port — WebGPU drawing (see gles3_webgpu.h, docs/web-port/WEBGPU_PLAN.md)
**
** C++ side: draw-time resolution of the GLES3 state tracker into pipelines (WGSL generated
** here, always specialised), uniform slices, texture bind groups and draw records.
** JS side (EM_JS below, state on Module.gxw): the GPU objects and the replay of the records.
**
** LICENSE: GPL-3.0
*/
#if defined(__EMSCRIPTEN__)
#include "gles3_webgpu.h"
#include "gles3_wrapper.h"
#include "gles3_fvf.h"
#include <emscripten.h>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>

GLES3_PipelineState* GLES3_Get_State();
const int* GLES3_PS_Program(unsigned int handle, int* count); // gles3_wrapper.cpp

// ============================================================================
// JS side
// ============================================================================
EM_JS(int, gxw_js_init, (), {
  var g = Module.gxGPU;
  if (!g || !g.device) return 0;
  var dev = g.device, cv = Module.canvas || document.getElementById('canvas');
  var ctx = cv && cv.getContext('webgpu');
  if (!ctx) { console.error('GXW: no webgpu canvas context'); return 0; }
  var fmt = navigator.gpu.getPreferredCanvasFormat();
  ctx.configure({ device: dev, format: fmt, alphaMode: 'opaque' });
  var U = GPUBufferUsage, T = GPUTextureUsage, S = GPUShaderStage;
  var W = Module.gxw = { dev: dev, ctx: ctx, fmt: fmt, cv: cv, bufs: [null], texs: [null], views: [null], smp: {},
    bgs: [null], pipes: [null], mods: new Map(), depth: null, depthView: null, dw: 0, dh: 0, uniSize: 0, uni: null, bg0: null,
    streamSize: 0, stream: null, cur: null, curView: null, errors: 0, vp: null,
    clr: { c: 0, z: 0, r: 0, g: 0, b: 0, a: 1, d: 1, s: 0 }, offs: new Uint32Array(3), frames: 0 };
  W.bgl0 = dev.createBindGroupLayout({ entries: [0, 1, 2].map(function (b) {
    return { binding: b, visibility: S.VERTEX | S.FRAGMENT, buffer: { type: 'read-only-storage' } }; }) });
  var e1 = [];
  for (var b = 0; b < 4; b++) e1.push({ binding: b, visibility: S.FRAGMENT, texture: {} });
  for (var b = 4; b < 8; b++) e1.push({ binding: b, visibility: S.FRAGMENT, sampler: {} });
  W.bgl1 = dev.createBindGroupLayout({ entries: e1 });
  W.layout = dev.createPipelineLayout({ bindGroupLayouts: [W.bgl0, W.bgl1] });
  W.white = dev.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: T.TEXTURE_BINDING | T.COPY_DST });
  dev.queue.writeTexture({ texture: W.white }, new Uint8Array([255, 255, 255, 255]), { bytesPerRow: 4 }, [1, 1]);
  W.whiteView = W.white.createView();
  dev.addEventListener('uncapturederror', function (e) { if (W.errors++ < 30) console.error('GXW error: ' + e.error.message); });
  dev.lost.then(function (i) { console.error('GXW device lost: ' + i.message); });
  Module.gxWebGPU = 1;
  return 1;
});

EM_JS(int, gxw_js_buffer_create, (int size), {
  var W = Module.gxw;
  var b = W.dev.createBuffer({ size: size, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
  W.bufs.push(b);
  return W.bufs.length - 1;
});
EM_JS(void, gxw_js_buffer_write, (int id, int off, const void* ptr, int len), {
  var W = Module.gxw;
  W.dev.queue.writeBuffer(W.bufs[id], off, HEAPU8, ptr, len);
});
EM_JS(void, gxw_js_buffer_destroy, (int id), {
  var W = Module.gxw, b = W.bufs[id];
  if (b) { b.destroy(); W.bufs[id] = null; }
});

// fmt: 0 rgba8unorm, 1 bc1, 2 bc2, 3 bc3
EM_JS(int, gxw_js_tex_create, (int w, int h, int levels, int fmt), {
  var W = Module.gxw;
  var f = ['rgba8unorm', 'bc1-rgba-unorm', 'bc2-rgba-unorm', 'bc3-rgba-unorm'][fmt];
  try {
    var t = W.dev.createTexture({ size: [w, h], mipLevelCount: levels, format: f,
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    W.texs.push(t); W.views.push(t.createView());
  } catch (e) { console.error('GXW texture ' + w + 'x' + h + ' ' + f + ': ' + e.message); W.texs.push(null); W.views.push(null); }
  return W.texs.length - 1;
});
EM_JS(void, gxw_js_tex_write, (int id, int level, int w, int h, const void* ptr, int len, int bpr, int rows), {
  var W = Module.gxw, t = W.texs[id];
  if (!t) return;
  W.dev.queue.writeTexture({ texture: t, mipLevel: level }, HEAPU8.subarray(ptr, ptr + len),
    { bytesPerRow: bpr, rowsPerImage: rows }, { width: w, height: h });
});
EM_JS(void, gxw_js_tex_destroy, (int id), {
  var W = Module.gxw, t = W.texs[id];
  if (t) t.destroy();
  W.texs[id] = null; W.views[id] = null;
});

// Sampler key: addrU (2 bits) | addrV << 2 | mag linear << 4 | min linear << 5 | mip << 6 (0 none, 1 nearest, 2 linear)
EM_JS(int, gxw_js_bindgroup, (int t0, int t1, int t2, int t3, int s0, int s1, int s2, int s3), {
  var W = Module.gxw, tx = [t0, t1, t2, t3], sk = [s0, s1, s2, s3], ent = [];
  var am = ['repeat', 'repeat', 'mirror-repeat', 'clamp-to-edge'];
  for (var i = 0; i < 4; i++) ent.push({ binding: i, resource: (tx[i] && W.views[tx[i]]) || W.whiteView });
  for (var i = 0; i < 4; i++) {
    var k = sk[i], s = W.smp[k];
    if (!s) {
      var mip = (k >> 6) & 3;
      s = W.smp[k] = W.dev.createSampler({ addressModeU: am[k & 3], addressModeV: am[(k >> 2) & 3],
        magFilter: (k & 16) ? 'linear' : 'nearest', minFilter: (k & 32) ? 'linear' : 'nearest',
        mipmapFilter: mip === 2 ? 'linear' : 'nearest', lodMaxClamp: mip ? 32 : 0 });
    }
    ent.push({ binding: 4 + i, resource: s });
  }
  W.bgs.push(W.dev.createBindGroup({ layout: W.bgl1, entries: ent }));
  return W.bgs.length - 1;
});
EM_JS(void, gxw_js_bindgroup_free, (int id), { Module.gxw.bgs[id] = null; });

// desc (ints): stride, nattr, 8 x (loc, fmt, offset), topology, stripfmt, cull, blend, src, dst,
// depth test, depth write, depth func, stencil, sfunc, sfail, szfail, spass, sread, swrite, bias
EM_JS(int, gxw_js_pipeline, (const char* wgsl, const int* d), {
  var W = Module.gxw, code = UTF8ToString(wgsl), q = d >> 2, I = HEAP32;
  var mod = W.mods.get(code);
  if (!mod) {
    mod = W.dev.createShaderModule({ code: code });
    W.mods.set(code, mod);
    if (mod.getCompilationInfo) mod.getCompilationInfo().then(function (ci) {
      ci.messages.forEach(function (m) { if (m.type === 'error' && W.errors++ < 30) console.error('GXW WGSL ' + m.lineNum + ': ' + m.message + '\n' + code); });
    });
  }
  var vf = [null, 'float32', 'float32x2', 'float32x3', 'float32x4', 'unorm8x4'];
  var attrs = [], n = I[q + 1];
  for (var i = 0; i < n; i++) attrs.push({ shaderLocation: I[q + 2 + i * 3], format: vf[I[q + 3 + i * 3]], offset: I[q + 4 + i * 3] });
  var o = q + 26;
  var topo = ['triangle-list', 'triangle-strip', 'line-list', 'line-strip', 'point-list'][I[o]];
  var bf = [null, 'zero', 'one', 'src', 'one-minus-src', 'src-alpha', 'one-minus-src-alpha', 'dst-alpha',
            'one-minus-dst-alpha', 'dst', 'one-minus-dst', 'src-alpha-saturated'];
  var cmp = [null, 'never', 'less', 'equal', 'less-equal', 'greater', 'not-equal', 'greater-equal', 'always'];
  var sop = [null, 'keep', 'zero', 'replace', 'increment-clamp', 'decrement-clamp', 'invert', 'increment-wrap', 'decrement-wrap'];
  var target = { format: W.fmt, writeMask: GPUColorWrite.RED | GPUColorWrite.GREEN | GPUColorWrite.BLUE };
  if (I[o + 3]) {
    var src = bf[I[o + 4]] || 'one', dst = bf[I[o + 5]] || 'zero';
    var srcA = src === 'src-alpha-saturated' ? 'one' : src; // not allowed for alpha in some implementations
    target.blend = { color: { srcFactor: src, dstFactor: dst, operation: 'add' }, alpha: { srcFactor: srcA, dstFactor: dst, operation: 'add' } };
  }
  var st = I[o + 9] ? { compare: cmp[I[o + 10]] || 'always', failOp: sop[I[o + 11]] || 'keep', depthFailOp: sop[I[o + 12]] || 'keep', passOp: sop[I[o + 13]] || 'keep' }
                    : { compare: 'always', failOp: 'keep', depthFailOp: 'keep', passOp: 'keep' };
  var desc = {
    layout: W.layout,
    vertex: { module: mod, entryPoint: 'vs_main', buffers: [{ arrayStride: I[q], attributes: attrs }] },
    fragment: { module: mod, entryPoint: 'fs_main', targets: [target] },
    primitive: { topology: topo, frontFace: 'cw', cullMode: ['none', 'front', 'back'][I[o + 2]] },
    depthStencil: { format: 'depth24plus-stencil8', depthWriteEnabled: !!I[o + 7], depthCompare: I[o + 6] ? (cmp[I[o + 8]] || 'less-equal') : 'always',
      stencilFront: st, stencilBack: st, stencilReadMask: I[o + 9] ? I[o + 14] & 255 : 255, stencilWriteMask: I[o + 9] ? I[o + 15] & 255 : 0,
      depthBias: I[o + 16], depthBiasSlopeScale: I[o + 16] ? -1 : 0 }
  };
  if (I[o + 1]) desc.primitive.stripIndexFormat = I[o + 1] === 2 ? 'uint32' : 'uint16';
  try { W.pipes.push(W.dev.createRenderPipeline(desc)); }
  catch (e) { console.error('GXW pipeline: ' + e.message); W.pipes.push(null); }
  return W.pipes.length - 1;
});

// Replays records: 1 viewport (8 ints), 2 clear (8 ints), 3 draw (16 ints). Then submits.
// uni: scenes, draws and extras one after the other at 256-aligned offsets (lens in bytes, each > 0).
EM_JS(void, gxw_js_flush, (const int* cmd, int n, const void* uni, int sLen, int dLen, int xLen, const void* str, int strLen, int present), {
  var W = Module.gxw, dev = W.dev, q = dev.queue, U = GPUBufferUsage;
  var dOff = (sLen + 255) & ~255, xOff = (dOff + dLen + 255) & ~255, uniLen = xOff + xLen;
  if (uniLen > W.uniSize || !W.uni) {
    var sz = Math.max(uniLen, W.uniSize * 2, 1 << 20);
    if (W.uni) W.uni.destroy();
    W.uni = dev.createBuffer({ size: sz, usage: U.STORAGE | U.COPY_DST }); W.uniSize = sz;
  }
  q.writeBuffer(W.uni, 0, HEAPU8, uni, uniLen);
  W.bg0 = dev.createBindGroup({ layout: W.bgl0, entries: [
    { binding: 0, resource: { buffer: W.uni, offset: 0, size: sLen } },
    { binding: 1, resource: { buffer: W.uni, offset: dOff, size: dLen } },
    { binding: 2, resource: { buffer: W.uni, offset: xOff, size: xLen } }] });
  if (strLen > W.streamSize || !W.stream) {
    var sz2 = Math.max(strLen, W.streamSize * 2, 1 << 20);
    if (W.stream) W.stream.destroy();
    W.stream = dev.createBuffer({ size: sz2, usage: U.VERTEX | U.INDEX | U.COPY_DST }); W.streamSize = sz2;
  }
  if (strLen) q.writeBuffer(W.stream, 0, HEAPU8, str, strLen);
  if (!W.cur) {
    W.cur = W.ctx.getCurrentTexture(); W.curView = W.cur.createView();
    if (W.cur.width !== W.dw || W.cur.height !== W.dh) {
      if (W.depth) W.depth.destroy();
      W.dw = W.cur.width; W.dh = W.cur.height;
      W.depth = dev.createTexture({ size: [W.dw, W.dh], format: 'depth24plus-stencil8', usage: GPUTextureUsage.RENDER_ATTACHMENT });
      W.depthView = W.depth.createView();
    }
  }
  var I = HEAP32, F = HEAPF32, i = cmd >> 2, end = i + n;
  var enc = dev.createCommandEncoder(), pass = null, C = W.clr;
  var pipe = -1, bg = -1, vb = -2, vbo = -1, ib = -2, ibo = -1, ibf = 0, sref = -1;
  function setVp() {
    var v = W.vp; if (!v) return;
    var x = Math.max(0, Math.min(v[0], W.dw)), y = Math.max(0, Math.min(v[1], W.dh));
    var w = Math.max(0, Math.min(v[2], W.dw - x)), h = Math.max(0, Math.min(v[3], W.dh - y));
    pass.setViewport(x, y, w, h, Math.max(0, Math.min(1, v[4])), Math.max(0, Math.min(1, v[5])));
  }
  function begin() {
    pass = enc.beginRenderPass({
      colorAttachments: [{ view: W.curView, loadOp: C.c ? 'clear' : 'load', storeOp: 'store', clearValue: { r: C.r, g: C.g, b: C.b, a: 1 } }],
      depthStencilAttachment: { view: W.depthView, depthLoadOp: C.z ? 'clear' : 'load', depthStoreOp: 'store', depthClearValue: C.d,
        stencilLoadOp: C.z ? 'clear' : 'load', stencilStoreOp: 'store', stencilClearValue: C.s } });
    C.c = 0; C.z = 0;
    pipe = -1; bg = -1; vb = -2; vbo = -1; ib = -2; ibo = -1; sref = -1;
    pass.setBindGroup(0, W.bg0);
    setVp();
  }
  while (i < end) {
    var t = I[i];
    if (t === 3) {
      if (!pass) begin();
      var p = I[i + 1];
      if (p && W.pipes[p]) {
        if (p !== pipe) { pass.setPipeline(W.pipes[p]); pipe = p; }
        var g = I[i + 2];
        if (g !== bg) { pass.setBindGroup(1, W.bgs[g]); bg = g; }
        var v = I[i + 3], vo = I[i + 4];
        if (v !== vb || vo !== vbo) { pass.setVertexBuffer(0, v ? W.bufs[v] : W.stream, vo); vb = v; vbo = vo; }
        var r = I[i + 14];
        if (r !== sref) { pass.setStencilReference(r); sref = r; }
        var x = I[i + 5];
        if (x >= 0) {
          var xo = I[i + 6], xf = I[i + 7];
          if (x !== ib || xo !== ibo || xf !== ibf) { pass.setIndexBuffer(x ? W.bufs[x] : W.stream, xf === 2 ? 'uint32' : 'uint16', xo); ib = x; ibo = xo; ibf = xf; }
          pass.drawIndexed(I[i + 8], 1, I[i + 9], I[i + 10], I[i + 11]);
        } else {
          pass.draw(I[i + 8], 1, I[i + 9], I[i + 11]);
        }
      }
      i += 16;
    } else if (t === 1) {
      W.vp = [F[i + 1], F[i + 2], F[i + 3], F[i + 4], F[i + 5], F[i + 6]];
      if (pass) setVp();
      i += 8;
    } else if (t === 2) {
      if (pass) { pass.end(); pass = null; }
      var f = I[i + 1];
      if (f & 1) { C.c = 1; C.r = F[i + 2]; C.g = F[i + 3]; C.b = F[i + 4]; C.a = F[i + 5]; }
      if (f & 2) { C.z = 1; C.d = F[i + 6]; C.s = I[i + 7] & 255; }
      i += 8;
    } else { console.error('GXW bad record ' + t); break; }
  }
  if (!pass && (C.c || C.z)) begin();
  if (pass) pass.end();
  q.submit([enc.finish()]);
  if (present) { W.cur = null; W.curView = null; W.frames++; }
});

// ============================================================================
// C++ side
// ============================================================================
static bool g_On = false;
bool GXW_On() { return g_On; }

static uint32_t g_Epoch = 1; // flushes so far: a resource read by a draw of this epoch is "in flight"
struct GXW_BufUse { uint32_t epoch = 0; size_t lo = 0, hi = 0, size = 0; };
static std::vector<GXW_BufUse> g_Bufs(1);
static std::vector<uint32_t> g_TexEpoch(1);
static std::vector<std::vector<uint64_t>> g_TexGroups(1); // bind-group keys using the texture
static std::vector<int32_t> g_Cmd;
static std::vector<uint8_t> g_Uni, g_Stream; // g_Uni: scenes | draws | extras, packed at flush
static std::vector<uint8_t> g_Scenes, g_Draws, g_Extras;
int gxw_flushes = 0, gxw_hazard_flushes = 0, gxw_draws = 0, gxw_pipelines = 0, gxw_skipped = 0; // log counters

static void Flush(bool present) {
    if (!g_Cmd.empty() || present) {
        // Every binding needs at least one element.
        if (g_Scenes.empty()) g_Scenes.resize(592, 0);
        if (g_Draws.empty()) g_Draws.resize(160, 0);
        if (g_Extras.empty()) g_Extras.resize(448, 0);
        const size_t d_off = (g_Scenes.size() + 255) & ~(size_t)255, x_off = (d_off + g_Draws.size() + 255) & ~(size_t)255;
        g_Uni.assign(x_off + g_Extras.size(), 0);
        memcpy(g_Uni.data(), g_Scenes.data(), g_Scenes.size());
        memcpy(g_Uni.data() + d_off, g_Draws.data(), g_Draws.size());
        memcpy(g_Uni.data() + x_off, g_Extras.data(), g_Extras.size());
        gxw_js_flush(g_Cmd.data(), (int)g_Cmd.size(), g_Uni.data(), (int)g_Scenes.size(), (int)g_Draws.size(), (int)g_Extras.size(),
                     g_Stream.data(), (int)g_Stream.size(), present ? 1 : 0);
    }
    g_Cmd.clear(); g_Stream.clear(); g_Scenes.clear(); g_Draws.clear(); g_Extras.clear();
    g_Epoch++;
    gxw_flushes++;
}

bool GXW_Init() {
    if (!EM_ASM_INT({ return Module.gxGPU && Module.gxGPU.device ? 1 : 0; })) return false;
    g_On = gxw_js_init() != 0;
    fprintf(stderr, "INFO: WebGPU drawing %s\n", g_On ? "on" : "failed, WebGL2 instead");
    return g_On;
}

void GXW_Present() { if (g_On) Flush(true); }

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------
unsigned GXW_Buffer_Create(size_t size, bool) {
    const size_t sz = (size + 3) & ~(size_t)3;
    unsigned id = (unsigned)gxw_js_buffer_create((int)(sz ? sz : 4));
    if (id >= g_Bufs.size()) g_Bufs.resize(id + 1);
    g_Bufs[id] = GXW_BufUse();
    g_Bufs[id].size = sz;
    return id;
}

void GXW_Buffer_Write(unsigned id, size_t lo, size_t hi, const uint8_t* data) {
    if (!id || id >= g_Bufs.size() || hi <= lo) return;
    GXW_BufUse& u = g_Bufs[id];
    lo &= ~(size_t)3;
    hi = (hi + 3) & ~(size_t)3;
    if (hi > u.size) hi = u.size;
    if (hi <= lo) return;
    if (u.epoch == g_Epoch && lo < u.hi && hi > u.lo) { gxw_hazard_flushes++; Flush(false); }
    gxw_js_buffer_write((int)id, (int)lo, data + lo, (int)(hi - lo));
}

void GXW_Buffer_Destroy(unsigned id) {
    if (!id || id >= g_Bufs.size()) return;
    if (g_Bufs[id].epoch == g_Epoch) Flush(false);
    gxw_js_buffer_destroy((int)id);
    g_Bufs[id] = GXW_BufUse();
}

static void Use_Buffer(unsigned id, size_t lo, size_t hi) {
    if (!id || id >= g_Bufs.size()) return;
    GXW_BufUse& u = g_Bufs[id];
    if (u.epoch != g_Epoch) { u.epoch = g_Epoch; u.lo = lo; u.hi = hi; return; }
    if (lo < u.lo) u.lo = lo;
    if (hi > u.hi) u.hi = hi;
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------
static bool Is_DXT(unsigned f) { return f == 827611204u || f == 861165636u || f == 894720068u; }
static int DXT_Code(unsigned f) { return f == 827611204u ? 1 : f == 861165636u ? 2 : 3; }

struct GXW_BGKey { uint32_t tex[4]; uint8_t smp[4]; };
struct GXW_BGHash { size_t operator()(const GXW_BGKey& k) const {
    size_t h = 1469598103934665603ull; const uint8_t* b = (const uint8_t*)&k;
    for (size_t i = 0; i < sizeof(k); i++) h = (h ^ b[i]) * 1099511628211ull; return h; } };
struct GXW_BGEq { bool operator()(const GXW_BGKey& a, const GXW_BGKey& b) const { return memcmp(&a, &b, sizeof(a)) == 0; } };
static std::unordered_map<GXW_BGKey, uint32_t, GXW_BGHash, GXW_BGEq> g_BindGroups;
static std::vector<uint8_t> g_TexBC(1); // 1: the GPU texture is BC (block upload)
static std::vector<GXW_BGKey> g_BGKeys(1); // by bind-group id

unsigned GXW_Texture_Create(unsigned w, unsigned h, unsigned levels, unsigned dx8_format) {
    int code = 0;
    // BC textures need a block-aligned size; others (and a page without BC) are unpacked.
    if (Is_DXT(dx8_format) && (w % 4) == 0 && (h % 4) == 0 && EM_ASM_INT({ return Module.gxGPU.bc ? 1 : 0; }))
        code = DXT_Code(dx8_format);
    unsigned id = (unsigned)gxw_js_tex_create((int)w, (int)h, (int)(levels ? levels : 1), code);
    if (id >= g_TexEpoch.size()) { g_TexEpoch.resize(id + 1, 0); g_TexGroups.resize(id + 1); }
    g_TexEpoch[id] = 0;
    if (id >= g_TexBC.size()) g_TexBC.resize(id + 1, 0);
    g_TexBC[id] = code != 0;
    GLES3_Register_Texture(id, levels > 1);
    return id;
}

// D3D pixels -> RGBA8 (same conversions as the GL path's To_GL_Pixels, all to 8 bits).
static void To_RGBA8(unsigned f, unsigned w, unsigned h, const uint8_t* in, std::vector<uint8_t>& out) {
    const size_t n = (size_t)w * h;
    out.resize(n * 4);
    uint8_t* o = out.data();
    const uint16_t* s16 = (const uint16_t*)in;
    for (size_t i = 0; i < n; i++, o += 4) {
        switch (f) {
            case 23: { uint16_t v = s16[i]; o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
                       o[2] = (uint8_t)((v & 31) * 255 / 31); o[3] = 255; break; }
            case 25: { uint16_t v = s16[i]; o[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
                       o[2] = (uint8_t)((v & 31) * 255 / 31); o[3] = (v & 0x8000) ? 255 : 0; break; }
            case 26: { uint16_t v = s16[i]; o[0] = (uint8_t)(((v >> 8) & 15) * 17); o[1] = (uint8_t)(((v >> 4) & 15) * 17);
                       o[2] = (uint8_t)((v & 15) * 17); o[3] = (uint8_t)(((v >> 12) & 15) * 17); break; }
            case 20: { const uint8_t* p = in + i * 3; o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = 255; break; }
            case 22: { const uint8_t* p = in + i * 4; o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = 255; break; }
            case 50: o[0] = o[1] = o[2] = in[i]; o[3] = 255; break;
            case 51: o[0] = o[1] = o[2] = in[i * 2]; o[3] = in[i * 2 + 1]; break;
            case 28: o[0] = o[1] = o[2] = 0; o[3] = in[i]; break;
            default: { const uint8_t* p = in + i * 4; o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = p[3]; break; }
        }
    }
}

// DXT block -> RGBA8 (for textures WebGPU can't take as BC: not 4-aligned, or no BC feature).
static void Decode_DXT(unsigned f, unsigned w, unsigned h, const uint8_t* in, std::vector<uint8_t>& out) {
    out.assign((size_t)w * h * 4, 0);
    const unsigned bw = (w + 3) / 4, bh = (h + 3) / 4, bs = f == 827611204u ? 8 : 16;
    for (unsigned by = 0; by < bh; by++) for (unsigned bx = 0; bx < bw; bx++) {
        const uint8_t* b = in + (by * bw + bx) * bs;
        const uint8_t* cb = bs == 16 ? b + 8 : b;
        unsigned c0 = cb[0] | cb[1] << 8, c1 = cb[2] | cb[3] << 8;
        uint8_t pal[4][4];
        auto rgb = [](unsigned c, uint8_t* p) { p[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31); p[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63); p[2] = (uint8_t)((c & 31) * 255 / 31); p[3] = 255; };
        rgb(c0, pal[0]); rgb(c1, pal[1]);
        for (int k = 0; k < 3; k++) {
            if (c0 > c1 || bs == 16) { pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3); pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3); }
            else { pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2); pal[3][k] = 0; }
        }
        pal[2][3] = 255; pal[3][3] = (c0 > c1 || bs == 16) ? 255 : 0;
        const unsigned bits = cb[4] | cb[5] << 8 | cb[6] << 16 | (unsigned)cb[7] << 24;
        uint8_t alpha[16];
        if (f == 861165636u) { for (int p = 0; p < 16; p++) alpha[p] = (uint8_t)(((b[p / 2] >> ((p & 1) * 4)) & 15) * 17); }
        else if (f == 894720068u) {
            uint8_t a[8] = { b[0], b[1] };
            if (a[0] > a[1]) for (int k = 1; k < 7; k++) a[k + 1] = (uint8_t)(((7 - k) * a[0] + k * a[1]) / 7);
            else { for (int k = 1; k < 5; k++) a[k + 1] = (uint8_t)(((5 - k) * a[0] + k * a[1]) / 5); a[6] = 0; a[7] = 255; }
            uint64_t ab = 0; for (int k = 0; k < 6; k++) ab |= (uint64_t)b[2 + k] << (8 * k);
            for (int p = 0; p < 16; p++) alpha[p] = a[(ab >> (3 * p)) & 7];
        } else for (int p = 0; p < 16; p++) alpha[p] = 255;
        for (int p = 0; p < 16; p++) {
            unsigned x = bx * 4 + (p & 3), y = by * 4 + (p >> 2);
            if (x >= w || y >= h) continue;
            const uint8_t* c = pal[(bits >> (2 * p)) & 3];
            uint8_t* o = &out[((size_t)y * w + x) * 4];
            o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = bs == 16 ? alpha[p] : c[3];
        }
    }
}


static void Tex_Hazard(unsigned id) {
    if (id < g_TexEpoch.size() && g_TexEpoch[id] == g_Epoch) { gxw_hazard_flushes++; Flush(false); }
}

void GXW_Texture_Write(unsigned id, unsigned level, unsigned w, unsigned h, unsigned f, const void* pixels, bool build_mips) {
    if (!id || !pixels) return;
    Tex_Hazard(id);
    static std::vector<uint8_t> rgba, next;
    const bool bc = Is_DXT(f) && id < g_TexBC.size() && g_TexBC[id];
    if (bc) {
        const unsigned bs = f == 827611204u ? 8 : 16, bw = (w + 3) / 4, bh = (h + 3) / 4;
        gxw_js_tex_write((int)id, (int)level, (int)(bw * 4), (int)(bh * 4), pixels, (int)(bw * bh * bs), (int)(bw * bs), (int)bh);
        return;
    }
    if (Is_DXT(f)) Decode_DXT(f, w, h, (const uint8_t*)pixels, rgba);
    else To_RGBA8(f, w, h, (const uint8_t*)pixels, rgba);
    gxw_js_tex_write((int)id, (int)level, (int)w, (int)h, rgba.data(), (int)rgba.size(), (int)(w * 4), (int)h);
    if (!build_mips) return;
    // WebGPU has no generateMipmap: box-filter the chain on the CPU.
    unsigned cw = w, ch = h;
    for (unsigned lv = level + 1; cw > 1 || ch > 1; lv++) {
        const unsigned nw = cw > 1 ? cw / 2 : 1, nh = ch > 1 ? ch / 2 : 1;
        next.resize((size_t)nw * nh * 4);
        for (unsigned y = 0; y < nh; y++) for (unsigned x = 0; x < nw; x++) {
            const unsigned x0 = x * 2 < cw ? x * 2 : cw - 1, x1 = x * 2 + 1 < cw ? x * 2 + 1 : x0;
            const unsigned y0 = y * 2 < ch ? y * 2 : ch - 1, y1 = y * 2 + 1 < ch ? y * 2 + 1 : y0;
            for (int c = 0; c < 4; c++)
                next[((size_t)y * nw + x) * 4 + c] = (uint8_t)((rgba[((size_t)y0 * cw + x0) * 4 + c] + rgba[((size_t)y0 * cw + x1) * 4 + c] +
                                                                rgba[((size_t)y1 * cw + x0) * 4 + c] + rgba[((size_t)y1 * cw + x1) * 4 + c] + 2) / 4);
        }
        gxw_js_tex_write((int)id, (int)lv, (int)nw, (int)nh, next.data(), (int)next.size(), (int)(nw * 4), (int)nh);
        rgba.swap(next);
        cw = nw; ch = nh;
    }
}

void GXW_Texture_Destroy(unsigned id) {
    if (!id || id >= g_TexEpoch.size()) return;
    if (g_TexEpoch[id] == g_Epoch) Flush(false);
    for (uint64_t k : g_TexGroups[id]) {
        // k: bind-group id; drop it if it is still the cached group of its key
        const uint32_t bg = (uint32_t)k;
        if (bg < g_BGKeys.size()) {
            auto it = g_BindGroups.find(g_BGKeys[bg]);
            if (it != g_BindGroups.end() && it->second == bg) { g_BindGroups.erase(it); gxw_js_bindgroup_free((int)bg); }
        }
    }
    g_TexGroups[id].clear();
    GLES3_Unregister_Texture(id);
    gxw_js_tex_destroy((int)id);
}


// ---------------------------------------------------------------------------
// Shader key and WGSL
// ---------------------------------------------------------------------------
struct GXW_SKey {
    uint8_t flags;      // 1 light, 2 fog, 4 alpha test, 8 pretransformed, 16 specular, 32 pixel shader
    uint8_t stages, ps, afunc, bound;
    uint8_t op[4][2], arg[4][6], vtex[4], lights, cvert;
    uint8_t posw, normal, color, spec, tcn[4]; // vertex inputs (tcn: float count, 0 absent)
};
struct GXW_PKey {
    GXW_SKey s;
    uint16_t stride; uint8_t off_pos, off_nrm, off_col, off_spc, off_tc[4];
    uint8_t topo, stripfmt, cull, blend, src, dst, ztest, zwrite, zfunc, sten, sfunc, sfail, szfail, spass, sread, swrite;
    int16_t bias;
};
template <class K> struct GXW_Hash { size_t operator()(const K& k) const {
    size_t h = 1469598103934665603ull; const uint8_t* b = (const uint8_t*)&k;
    for (size_t i = 0; i < sizeof(k); i++) h = (h ^ b[i]) * 1099511628211ull; return h; } };
template <class K> struct GXW_Eq { bool operator()(const K& a, const K& b) const { return memcmp(&a, &b, sizeof(K)) == 0; } };
static std::unordered_map<GXW_PKey, uint32_t, GXW_Hash<GXW_PKey>, GXW_Eq<GXW_PKey>> g_Pipes;
static std::unordered_map<GXW_SKey, std::string, GXW_Hash<GXW_SKey>, GXW_Eq<GXW_SKey>> g_WGSL;

static const char* WGSL_COMMON =
    "struct Light { diffuse: vec4f, ambient: vec4f, specular: vec4f, position: vec4f, direction: vec4f, atten: vec4f };\n"
    "struct Scene { view: mat4x4f, proj: mat4x4f, ambient: vec4f, fogColor: vec4f, fogParams: vec4f, viewport: vec4f, tfactor: vec4f, lights: array<Light, 4> };\n"
    "struct Draw { world: mat4x4f, matDiffuse: vec4f, matAmbient: vec4f, matSpecular: vec4f, matEmissive: vec4f, misc: vec4f, idx: vec4u };\n"
    "struct Extra { texMatrix: array<mat4x4f, 4>, psConst: array<vec4f, 8>, shroud: mat4x4f };\n"
    "@group(0) @binding(0) var<storage, read> SC: array<Scene>;\n"
    "@group(0) @binding(1) var<storage, read> DR: array<Draw>;\n"
    "@group(0) @binding(2) var<storage, read> XS: array<Extra>;\n"
    "@group(1) @binding(0) var t0: texture_2d<f32>;\n@group(1) @binding(1) var t1: texture_2d<f32>;\n"
    "@group(1) @binding(2) var t2: texture_2d<f32>;\n@group(1) @binding(3) var t3: texture_2d<f32>;\n"
    "@group(1) @binding(4) var s0: sampler;\n@group(1) @binding(5) var s1: sampler;\n"
    "@group(1) @binding(6) var s2: sampler;\n@group(1) @binding(7) var s3: sampler;\n"
    "struct VOut { @builtin(position) pos: vec4f, @location(0) color: vec4f, @location(1) spec: vec4f, @location(2) tc0: vec3f,\n"
    "  @location(3) tc1: vec3f, @location(4) tc2: vec3f, @location(5) tc3: vec3f, @location(6) fog: f32, @location(7) @interpolate(flat) di: u32 };\n";

static std::string N(int i) { return std::string(1, (char)('0' + i)); }

static std::string FF_Arg(int a) { // D3DTA_* as a WGSL expression (same rules as FF_Arg in dx8_fixedfunction_shaders.cpp)
    int sel = a & 15;
    std::string v = sel == 0 ? "diffuse" : sel == 2 ? "tex" : sel == 3 ? "S.tfactor" : sel == 4 ? "i.spec" : "cur";
    if (a & 16) v = "(vec4f(1.0) - " + v + ")";
    if (a & 32) v = "vec4f((" + v + ").a)";
    return v;
}
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
        case 14: return "mix(" + a2 + ", " + a1 + ", S.tfactor.a)";
        case 15: return "(" + a1 + " + " + a2 + " * (1.0 - tex.a))";
        case 16: return "mix(" + a2 + ", " + a1 + ", cur.a)";
        case 18: return "(" + a1 + " + (" + a1 + ").a * " + a2 + ")";
        case 19: return "(" + a1 + " * " + a2 + " + (" + a1 + ").a)";
        case 20: return "((1.0 - (" + a1 + ").a) * " + a2 + " + " + a1 + ")";
        case 21: return "((vec4f(1.0) - " + a1 + ") * " + a2 + " + (" + a1 + ").a)";
        case 22: case 23: return a2;
        case 24: return "vec4f(4.0 * dot((" + a1 + ").rgb - 0.5, (" + a2 + ").rgb - 0.5))";
        case 25: return "(" + a0 + " + " + a1 + " * " + a2 + ")";
        case 26: return "mix(" + a2 + ", " + a1 + ", " + a0 + ")";
        default: return "(" + a1 + " * " + a2 + ")";
    }
}
static std::string Sample(const GXW_SKey& k, int s) {
    if (!(k.bound & (1 << s))) return "vec4f(1.0)";
    return "textureSample(t" + N(s) + ", s" + N(s) + ", i.tc" + N(s) + ".xy / i.tc" + N(s) + ".z)";
}

static std::string Make_WGSL(const GXW_SKey& k) {
    const bool light = k.flags & 1, fog = k.flags & 2, atest = k.flags & 4, pre = k.flags & 8, spec = k.flags & 16, ps = k.flags & 32;
    std::string s = WGSL_COMMON;
    s += "struct VIn {\n  @location(0) pos: vec" + std::string(k.posw ? "4" : "3") + "f,\n";
    if (k.normal) s += "  @location(1) nrm: vec3f,\n";
    if (k.color) s += "  @location(2) col: vec4f,\n";
    for (int t = 0; t < 4; t++) if (k.tcn[t])
        s += "  @location(" + std::to_string(3 + t) + ") tc" + N(t) + ": " + (k.tcn[t] == 1 ? "f32" : "vec" + N(k.tcn[t]) + "f") + ",\n";
    if (k.spec) s += "  @location(11) spc: vec4f,\n";
    s += "};\n@vertex fn vs_main(i: VIn, @builtin(instance_index) di: u32) -> VOut {\n  var o: VOut;\n  o.di = di;\n"
         "  let si = DR[di].idx.x;\n  let xi = DR[di].idx.y;\n";
    s += std::string("  let aPos = vec4f(i.pos.xyz, ") + (k.posw ? "i.pos.w" : "1.0") + ");\n";
    s += std::string("  let aNormal = ") + (k.normal ? "i.nrm" : "vec3f(0.0, 0.0, 1.0)") + ";\n";
    s += std::string("  let vc = ") + (k.color ? "i.col.bgra" : "vec4f(1.0)") + ";\n";
    for (int t = 0; t < 4; t++)
        s += "  let a" + N(t) + " = " + (k.tcn[t] == 0 ? std::string("vec2f(0.0)") : k.tcn[t] == 1 ? "vec2f(i.tc" + N(t) + ", 0.0)" : "i.tc" + N(t) + ".xy") + ";\n";
    s += std::string("  o.spec = ") + (k.spec ? "i.spc.bgra" : "vec4f(0.0)") + ";\n  o.color = vc;\n  o.fog = 1.0;\n";
    s += "  o.tc0 = vec3f(0.0, 0.0, 1.0); o.tc1 = o.tc0; o.tc2 = o.tc0; o.tc3 = o.tc0;\n";
    if (pre) {
        s += "  let w = select(1.0, 1.0 / aPos.w, aPos.w > 0.0);\n"
             "  let p = (aPos.xy + 0.5 - S.viewport.xy) / S.viewport.zw;\n"
             "  o.pos = vec4f(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, aPos.z, 1.0) * w;\n"
             "  o.tc0 = vec3f(a0, 1.0); o.tc1 = vec3f(a1, 1.0); o.tc2 = vec3f(a2, 1.0); o.tc3 = vec3f(a3, 1.0);\n";
    } else {
        s += "  let worldPos = D.world * vec4f(aPos.xyz, 1.0);\n  let viewPos = S.view * worldPos;\n  o.pos = S.proj * viewPos;\n"
             "  let wm3 = mat3x3f(D.world[0].xyz, D.world[1].xyz, D.world[2].xyz);\n"
             "  let vm3 = mat3x3f(S.view[0].xyz, S.view[1].xyz, S.view[2].xyz);\n"
             "  let eyeNormal = normalize(vm3 * (wm3 * aNormal));\n";
        for (int t = 0; t < 4; t++) {
            if (!ps && t >= k.stages) break;
            const int v = k.vtex[t], mode = v & 3, set = (v >> 2) & 3, count = (v >> 4) & 7;
            const bool proj = (v & 128) != 0;
            s += "  {\n    let c = ";
            s += mode == 1 ? "vec4f(eyeNormal, 1.0)" : mode == 2 ? "vec4f(viewPos.xyz, 1.0)"
               : mode == 3 ? "vec4f(reflect(normalize(viewPos.xyz), eyeNormal), 1.0)" : "vec4f(a" + N(set) + ", 1.0, 0.0)";
            s += ";\n";
            if (count == 0) s += "    o.tc" + N(t) + " = vec3f(c.xy, 1.0);\n";
            else {
                s += "    let tt = X.texMatrix[" + N(t) + "] * c;\n    o.tc" + N(t) + " = vec3f(tt.xy, ";
                s += !proj ? "1.0" : count == 4 ? "tt.w" : count == 3 ? "tt.z" : "tt.y";
                s += ");\n";
            }
            s += "  }\n";
        }
        if (light) {
            const int cv = k.cvert;
            s += std::string("  var totalDiffuse = ") + ((cv & 4) ? "vc" : "D.matEmissive") + ";\n";
            s += std::string("  let matAmb = ") + ((cv & 1) ? "vc" : "D.matAmbient") + ";\n";
            s += std::string("  let matDiff = ") + ((cv & 2) ? "vc" : "D.matDiffuse") + ";\n";
            s += "  var lightAmbient = S.ambient;\n  var totalSpecular = vec4f(0.0);\n"
                 "  let camPos = -(transpose(vm3) * S.view[3].xyz);\n"
                 "  let toEye = normalize(camPos - worldPos.xyz);\n"
                 "  let worldNormal = normalize(wm3 * aNormal);\n";
            for (int l = 0; l < 4; l++) {
                const int type = (k.lights >> (l * 2)) & 3;
                if (!type) continue;
                s += "  {\n    let L = S.lights[" + N(l) + "];\n    var lightDir = vec3f(0.0, 0.0, 1.0);\n    var att = 1.0;\n    var lit = true;\n";
                if (type == 3) s += "    lightDir = normalize(-L.direction.xyz);\n";
                else {
                    s += "    let toLight = L.position.xyz - worldPos.xyz;\n    let dist = length(toLight);\n"
                         "    lit = !(dist > L.position.w);\n"
                         "    if (dist > 0.0) { lightDir = toLight / dist; }\n"
                         "    att = 1.0 / (L.atten.x + L.atten.y * dist + L.atten.z * dist * dist);\n";
                    if (type == 2) s += "    att = att * smoothstep(0.0, 1.0, dot(-lightDir, normalize(L.direction.xyz)));\n";
                }
                s += "    if (lit) {\n      let NdotL = max(dot(worldNormal, lightDir), 0.0);\n"
                     "      totalDiffuse += matDiff * L.diffuse * NdotL * att;\n      lightAmbient += L.ambient * att;\n";
                if (spec) s += "      if (NdotL > 0.0) {\n        let NdotH = max(dot(worldNormal, normalize(lightDir + toEye)), 0.0);\n"
                               "        totalSpecular += L.specular * pow(NdotH, max(D.misc.x, 0.0001)) * att;\n      }\n";
                s += "    }\n  }\n";
            }
            s += "  o.color = vec4f(saturate(totalDiffuse + matAmb * lightAmbient).rgb, matDiff.a);\n"
                 "  o.spec = saturate(D.matSpecular * totalSpecular);\n";
        }
        if (fog) s += "  if (S.fogParams.y > S.fogParams.x) { o.fog = clamp((S.fogParams.y - abs(viewPos.z)) / (S.fogParams.y - S.fogParams.x), 0.0, 1.0); }\n";
    }
    s += "  return o;\n}\n";

    // Fragment
    s += "@fragment fn fs_main(i: VOut) -> @location(0) vec4f {\n  let di = i.di;\n  let si = DR[di].idx.x;\n  let xi = DR[di].idx.y;\n"
         "  let diffuse = i.color;\n  var color = i.color;\n";
    if (ps) {
        int count = 0;
        const int* d = GLES3_PS_Program(k.ps, &count);
        static const char* reg[16] = { "r0", "r1", "t0_", "t1_", "t2_", "t3_", "v0", "v1", "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7" };
        s += "  var r0 = vec4f(0.0); var r1 = vec4f(0.0);\n";
        for (int t = 0; t < 4; t++) s += "  var t" + N(t) + "_ = " + Sample(k, t) + ";\n";
        s += "  var v0 = i.color; var v1 = i.spec;\n";
        for (int c = 0; c < 8; c++) s += "  var c" + N(c) + " = X.psConst[" + N(c) + "];\n";
        auto src = [&](int e) {
            std::string x = reg[e & 15];
            int swz = (e >> 5) & 3, m = (e >> 8) & 15;
            if (swz == 1) x = "vec4f(" + x + ".a)"; else if (swz == 2) x = "vec4f(" + x + ".b)";
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
        for (int n = 0; d && n < count; n++) {
            const int* q = d + n * 8;
            const int op = q[0] & 255;
            if (op == 66) continue;
            std::string s0 = src(q[2]), s1 = src(q[3]), s2 = src(q[4]), e;
            switch (op) {
                case 1: e = s0; break;
                case 2: e = s0 + " + " + s1; break;
                case 3: e = s0 + " - " + s1; break;
                case 4: e = s0 + " * " + s1 + " + " + s2; break;
                case 8: e = "vec4f(dot(" + s0 + ".rgb, " + s1 + ".rgb))"; break;
                case 9: e = "vec4f(dot(" + s0 + ", " + s1 + "))"; break;
                case 18: e = "mix(" + s2 + ", " + s1 + ", " + s0 + ")"; break;
                default: e = s0 + " * " + s1; break;
            }
            const int shift = (q[0] >> 12) & 15;
            const char* sc = shift == 1 ? " * 2.0" : shift == 2 ? " * 4.0" : shift == 15 ? " * 0.5" : "";
            const char* lo = ((q[0] >> 16) & 1) ? "0.0" : "-1.0";
            const int mask = (q[0] >> 8) & 15;
            const std::string dst = reg[q[1] & 15];
            s += "  { let r = clamp((" + e + ")" + sc + ", vec4f(" + lo + "), vec4f(1.0)); " + dst + " = select(" + dst + ", r, vec4<bool>(" +
                 ((mask & 1) ? "true" : "false") + ", " + ((mask & 2) ? "true" : "false") + ", " + ((mask & 4) ? "true" : "false") + ", " +
                 ((mask & 8) ? "true" : "false") + ")); }\n";
        }
        s += "  color = saturate(r0);\n";
    } else {
        for (int st = 0; st < k.stages; st++) {
            const uint8_t* a = k.arg[st];
            s += "  {\n    let cur = color;\n    let tex = " + Sample(k, st) + ";\n";
            s += "    let rgb = saturate(" + FF_Op(k.op[st][0], FF_Arg(a[1]), FF_Arg(a[2]), FF_Arg(a[0])) + ").rgb;\n";
            if (k.op[st][1] > 1) {
                auto x = [](int v) { return "vec4f(" + FF_Arg(v) + ".a)"; };
                s += "    let al = saturate(" + FF_Op(k.op[st][1], x(a[4]), x(a[5]), x(a[3])) + ").a;\n";
                s += "    color = vec4f(rgb, al);\n";
            } else s += "    color = vec4f(rgb, cur.a);\n";
            s += "  }\n";
        }
        if (spec) s += "  color = vec4f(saturate(color.rgb + i.spec.rgb), color.a);\n";
    }
    if (atest) {
        const char* c = nullptr;
        switch (k.afunc) {
            case 1: c = "false"; break;
            case 2: c = "color.a < D.misc.y"; break;
            case 3: c = "abs(color.a - D.misc.y) < 0.004"; break;
            case 4: c = "color.a <= D.misc.y"; break;
            case 5: c = "color.a > D.misc.y"; break;
            case 6: c = "abs(color.a - D.misc.y) >= 0.004"; break;
            case 7: c = "color.a >= D.misc.y"; break;
            default: break; // always
        }
        if (c) s += std::string("  if (!(") + c + ")) { discard; }\n";
    }
    if (fog) s += "  color = vec4f(mix(S.fogColor.rgb, color.rgb, i.fog), color.a);\n";
    s += "  return color;\n}\n";
    // The generated code reads S. (scene), D. (draw) and X. (extra): indexed storage arrays.
    std::string out;
    out.reserve(s.size() + 1024);
    const size_t body = strlen(WGSL_COMMON);
    out.append(s, 0, body);
    for (size_t p = body; p < s.size(); p++) {
        const char c = s[p];
        const bool start = p == 0 || !(isalnum((unsigned char)s[p - 1]) || s[p - 1] == '_');
        if (start && p + 1 < s.size() && s[p + 1] == '.' && (c == 'S' || c == 'D' || c == 'X')) {
            out += c == 'S' ? "SC[si]" : c == 'D' ? "DR[di]" : "XS[xi]";
            continue;
        }
        out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Draw-time state resolution
// ---------------------------------------------------------------------------
static void Shader_Key(const GLES3_PipelineState& st, unsigned fvf, GXW_SKey& k) {
    memset(&k, 0, sizeof(k));
    const unsigned* rs = st.render_states;
    const bool pre = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    if (st.lighting_enabled && !pre) k.flags |= 1;
    if (st.fog_enabled && !pre) k.flags |= 2;
    if (st.alpha_test_enabled) k.flags |= 4;
    if (pre) k.flags |= 8;
    if (rs[GLES3_RS_SPECULARENABLE]) k.flags |= 16;
    const bool ps = st.pixel_shader != 0;
    if (ps) { k.flags |= 32; k.ps = (uint8_t)st.pixel_shader; }
    else while (k.stages < 4 && st.texture_stage_state[k.stages][GLES3_TSS_COLOROP] > 1) k.stages++;
    if (st.alpha_test_enabled) k.afunc = (uint8_t)(st.alpha_func <= 8 ? st.alpha_func : 8);
    for (int s = 0; s < 4; s++) if (st.bound_textures[s] && (ps || s < k.stages)) k.bound |= 1 << s;
    if (!pre) {
        for (int s = 0; s < 4; s++) {
            if (!ps && s >= k.stages) break;
            unsigned tci = st.texture_stage_state[s][GLES3_TSS_TEXCOORDINDEX];
            unsigned ttf = st.texture_stage_state[s][GLES3_TSS_TEXTURETRANSFORMFLAGS];
            unsigned mode = tci >> 16, count = ttf & 0xff;
            k.vtex[s] = (uint8_t)((mode <= 3 ? mode : 0) | (tci & 3) << 2 | (count <= 7 ? count : 7) << 4 | ((ttf & 256) ? 128 : 0));
        }
        if (k.flags & 1) {
            for (int i = 0; i < 4; i++) {
                unsigned t = st.lights[i].type;
                if (st.light_enabled[i]) k.lights |= (t >= 1 && t <= 3 ? t : 1) << (i * 2);
            }
            if (rs[GLES3_RS_COLORVERTEX] && (fvf & D3DFVF_DIFFUSE)) {
                if (rs[GLES3_RS_AMBIENTMATERIALSOURCE] == 1) k.cvert |= 1;
                if (rs[GLES3_RS_DIFFUSEMATERIALSOURCE] == 1) k.cvert |= 2;
                if (rs[GLES3_RS_EMISSIVEMATERIALSOURCE] == 1) k.cvert |= 4;
            }
        }
    }
    auto clip = [](unsigned v) { return (uint8_t)(v <= 255 ? v : 255); };
    for (int s = 0; s < k.stages; s++) {
        const unsigned* t = st.texture_stage_state[s];
        for (int c = 0; c < 2; c++) {
            unsigned op = t[c ? GLES3_TSS_ALPHAOP : GLES3_TSS_COLOROP];
            if (c && op <= 1) { k.op[s][1] = 1; continue; }
            k.op[s][c] = clip(op);
            unsigned a0 = t[c ? GLES3_TSS_ALPHAARG0 : GLES3_TSS_COLORARG0];
            unsigned a1 = t[c ? GLES3_TSS_ALPHAARG1 : GLES3_TSS_COLORARG1];
            unsigned a2 = t[c ? GLES3_TSS_ALPHAARG2 : GLES3_TSS_COLORARG2];
            k.arg[s][c * 3 + 0] = (op == 25 || op == 26) ? (a0 & 63) : 0;
            k.arg[s][c * 3 + 1] = op == 3 ? 0 : (a1 & 63);
            k.arg[s][c * 3 + 2] = op == 2 ? 0 : (a2 & 63);
        }
    }
    // Vertex inputs
    const unsigned pos = fvf & D3DFVF_POSITION_MASK;
    k.posw = pos == D3DFVF_XYZRHW;
    k.normal = (fvf & D3DFVF_NORMAL) != 0;
    k.color = (fvf & D3DFVF_DIFFUSE) != 0;
    k.spec = (fvf & D3DFVF_SPECULAR) != 0;
    const unsigned tc = GLES3_FVFDecoder::Get_Tex_Count(fvf);
    for (unsigned t = 0; t < 4; t++) k.tcn[t] = t < tc ? (uint8_t)GLES3_FVFDecoder::Get_Tex_Coord_Size(fvf, t) : 0;
}

static uint32_t Pipeline(const GXW_SKey& sk, unsigned fvf, unsigned stride, unsigned topo, unsigned stripfmt) {
    const GLES3_PipelineState& st = *GLES3_Get_State();
    const unsigned* rs = st.render_states;
    GXW_PKey k;
    memset(&k, 0, sizeof(k));
    k.s = sk;
    static GLES3_FVFDecoder dec;
    static unsigned dec_fvf = ~0u;
    if (dec_fvf != fvf) { dec.Decode(fvf); dec_fvf = fvf; }
    k.stride = (uint16_t)stride;
    k.off_nrm = (uint8_t)dec.Get_Normal_Offset(); k.off_col = (uint8_t)dec.Get_Diffuse_Offset();
    k.off_spc = (uint8_t)dec.Get_Specular_Offset();
    for (int t = 0; t < 4; t++) k.off_tc[t] = (uint8_t)dec.Get_Tex_Offset(t);
    k.topo = (uint8_t)topo; k.stripfmt = (uint8_t)stripfmt;
    const unsigned cull = rs[GLES3_RS_CULLMODE];
    k.cull = cull == 1 ? 0 : cull == 2 ? 1 : 2;
    if (rs[GLES3_RS_ALPHABLENDENABLE]) { k.blend = 1; k.src = (uint8_t)rs[GLES3_RS_SRCBLEND]; k.dst = (uint8_t)rs[GLES3_RS_DESTBLEND]; }
    k.ztest = rs[GLES3_RS_ZENABLE] != 0;
    k.zwrite = k.ztest && rs[GLES3_RS_ZWRITEENABLE] != 0;
    k.zfunc = (uint8_t)(rs[GLES3_RS_ZFUNC] ? rs[GLES3_RS_ZFUNC] : 4);
    if (rs[GLES3_RS_STENCILENABLE]) {
        k.sten = 1; k.sfunc = (uint8_t)rs[GLES3_RS_STENCILFUNC]; k.sfail = (uint8_t)rs[GLES3_RS_STENCILFAIL];
        k.szfail = (uint8_t)rs[GLES3_RS_STENCILZFAIL]; k.spass = (uint8_t)rs[GLES3_RS_STENCILPASS];
        k.sread = (uint8_t)rs[GLES3_RS_STENCILMASK]; k.swrite = (uint8_t)rs[GLES3_RS_STENCILWRITEMASK];
    }
    if (topo <= 1 && rs[GLES3_RS_ZBIAS] > 0) k.bias = (int16_t)(-(int)rs[GLES3_RS_ZBIAS] * 2);

    static GXW_PKey last;
    static uint32_t last_id = 0;
    if (last_id && memcmp(&k, &last, sizeof(k)) == 0) return last_id;
    auto it = g_Pipes.find(k);
    uint32_t id;
    if (it != g_Pipes.end()) id = it->second;
    else {
        auto sit = g_WGSL.find(k.s);
        if (sit == g_WGSL.end()) sit = g_WGSL.emplace(k.s, Make_WGSL(k.s)).first;
        int d[48] = {};
        d[0] = (int)stride;
        int n = 0;
        auto attr = [&](int loc, int fmt, int off) { d[2 + n * 3] = loc; d[3 + n * 3] = fmt; d[4 + n * 3] = off; n++; };
        attr(0, k.s.posw ? 4 : 3, 0);
        if (k.s.normal) attr(1, 3, k.off_nrm);
        if (k.s.color) attr(2, 5, k.off_col);
        for (int t = 0; t < 4; t++) if (k.s.tcn[t]) attr(3 + t, k.s.tcn[t], k.off_tc[t]);
        if (k.s.spec) attr(11, 5, k.off_spc);
        d[1] = n;
        int* o = d + 26;
        o[0] = k.topo; o[1] = k.stripfmt; o[2] = k.cull; o[3] = k.blend; o[4] = k.src; o[5] = k.dst;
        o[6] = k.ztest; o[7] = k.zwrite; o[8] = k.zfunc; o[9] = k.sten; o[10] = k.sfunc; o[11] = k.sfail;
        o[12] = k.szfail; o[13] = k.spass; o[14] = k.sread; o[15] = k.swrite; o[16] = k.bias;
        id = (uint32_t)gxw_js_pipeline(sit->second.c_str(), d);
        g_Pipes.emplace(k, id);
        gxw_pipelines++;
    }
    last = k; last_id = id;
    return id;
}

static uint32_t Sampler_Key(const GLES3_PipelineState& st, int s) {
    const unsigned* t = st.texture_stage_state[s];
    unsigned u = t[GLES3_TSS_ADDRESSU], v = t[GLES3_TSS_ADDRESSV];
    unsigned mag = t[GLES3_TSS_MAGFILTER], mn = t[GLES3_TSS_MINFILTER], mip = t[GLES3_TSS_MIPFILTER];
    if (mn == 0) mn = 1;
    const bool mips = GLES3_Texture_Has_Mipmaps(st.bound_textures[s]);
    return (u <= 3 ? u : 1) | (v <= 3 ? v : 1) << 2 | (mag != 1 ? 16 : 0) | (mn != 1 ? 32 : 0) | (mips ? (mip > 2 ? 2 : mip) : 0) << 6;
}

static uint32_t Bind_Group() {
    const GLES3_PipelineState& st = *GLES3_Get_State();
    GXW_BGKey k;
    memset(&k, 0, sizeof(k));
    for (int s = 0; s < 4; s++) {
        k.tex[s] = st.bound_textures[s];
        k.smp[s] = (uint8_t)(k.tex[s] ? Sampler_Key(st, s) : 0);
        if (k.tex[s] && k.tex[s] < g_TexEpoch.size()) g_TexEpoch[k.tex[s]] = g_Epoch;
    }
    static GXW_BGKey last;
    static uint32_t last_id = 0;
    if (last_id && memcmp(&k, &last, sizeof(k)) == 0) return last_id;
    auto it = g_BindGroups.find(k);
    uint32_t id;
    if (it != g_BindGroups.end()) id = it->second;
    else {
        id = (uint32_t)gxw_js_bindgroup((int)k.tex[0], (int)k.tex[1], (int)k.tex[2], (int)k.tex[3], k.smp[0], k.smp[1], k.smp[2], k.smp[3]);
        g_BindGroups.emplace(k, id);
        if (id >= g_BGKeys.size()) g_BGKeys.resize(id + 1);
        g_BGKeys[id] = k;
        for (int s = 0; s < 4; s++) if (k.tex[s] && k.tex[s] < g_TexGroups.size()) g_TexGroups[k.tex[s]].push_back(id);
    }
    last = k; last_id = id;
    return id;
}

// Uniform slices, appended to the per-flush ring (256-byte aligned, padded to the binding size)
// unless identical to the last one of their kind this flush.
struct GXW_Slice { std::vector<uint8_t> last; uint32_t idx = 0, epoch = 0; };
// Appends an element to arr unless it equals the last one appended this flush; returns its index.
static uint32_t Append(GXW_Slice& sl, std::vector<uint8_t>& arr, const void* p, size_t n) {
    if (sl.epoch == g_Epoch && sl.last.size() == n && memcmp(sl.last.data(), p, n) == 0) return sl.idx;
    const size_t off = arr.size();
    arr.resize(off + n);
    memcpy(arr.data() + off, p, n);
    sl.last.assign((const uint8_t*)p, (const uint8_t*)p + n);
    sl.idx = (uint32_t)(off / n); sl.epoch = g_Epoch;
    return sl.idx;
}

struct GXW_Scene {
    float view[16], proj[16], ambient[4], fog_color[4], fog_params[4], viewport[4], tfactor[4];
    struct { float diffuse[4], ambient[4], specular[4], position[4], direction[4], atten[4]; } lights[4];
};
struct GXW_DrawBlock { float world[16], diffuse[4], ambient[4], specular[4], emissive[4], misc[4]; uint32_t idx[4]; };
static_assert(sizeof(GXW_DrawBlock) == 160, "Draw stride");
struct GXW_Extra { float tex[4][16], ps[8][4], shroud[16]; };

static_assert(sizeof(GXW_Scene) == 592 && sizeof(GXW_Extra) == 448, "storage strides");
static void Uniforms(const GXW_SKey& sk, uint32_t& o0, uint32_t& o1, uint32_t& o2) {
    const GLES3_PipelineState& st = *GLES3_Get_State();
    static GXW_Slice s0, s1, s2;
    GXW_Scene sc;
    memset(&sc, 0, sizeof(sc));
    memcpy(sc.view, st.view_matrix, 64); memcpy(sc.proj, st.projection_matrix, 64);
    memcpy(sc.ambient, st.global_ambient, 16);
    memcpy(sc.fog_color, st.fog_color, 12);
    sc.fog_params[0] = st.fog_start; sc.fog_params[1] = st.fog_end;
    sc.viewport[0] = (float)st.viewport.x; sc.viewport[1] = (float)st.viewport.y;
    sc.viewport[2] = (float)(st.viewport.width ? st.viewport.width : 1); sc.viewport[3] = (float)(st.viewport.height ? st.viewport.height : 1);
    memcpy(sc.tfactor, st.texture_factor, 16);
    for (int i = 0; i < 4; i++) {
        if (!st.light_enabled[i]) continue;
        const GLES3_Light& l = st.lights[i];
        auto& o = sc.lights[i];
        memcpy(o.diffuse, l.diffuse, 16); memcpy(o.ambient, l.ambient, 16); memcpy(o.specular, l.specular, 16);
        memcpy(o.position, l.position, 12); o.position[3] = l.range;
        memcpy(o.direction, l.direction, 12);
        o.atten[0] = l.attenuation0; o.atten[1] = l.attenuation1; o.atten[2] = l.attenuation2;
        o.atten[3] = (float)(l.type >= 1 && l.type <= 3 ? l.type : 1);
    }
    const uint32_t si = Append(s0, g_Scenes, &sc, sizeof(sc));
    GXW_DrawBlock db;
    memcpy(db.world, st.world_matrix, 64);
    memcpy(db.diffuse, st.material.diffuse, 16); memcpy(db.ambient, st.material.ambient, 16);
    memcpy(db.specular, st.material.specular, 16); memcpy(db.emissive, st.material.emissive, 16);
    db.misc[0] = st.material.power; db.misc[1] = st.alpha_ref / 255.0f; db.misc[2] = db.misc[3] = 0;
    bool extra = (sk.flags & 32) != 0;
    for (int s = 0; s < 4 && !extra; s++) if ((sk.vtex[s] >> 4) & 7) extra = true;
    if (extra) {
        GXW_Extra x;
        memset(&x, 0, sizeof(x));
        for (int s = 0; s < 4; s++) memcpy(x.tex[s], st.texture_matrices[s], 64);
        memcpy(x.ps, st.ps_const, sizeof(x.ps));
        memcpy(x.shroud, st.shroud_matrix, 64);
        o2 = Append(s2, g_Extras, &x, sizeof(x));
    } else o2 = s2.epoch == g_Epoch ? s2.idx : 0;
    db.idx[0] = si; db.idx[1] = o2; db.idx[2] = db.idx[3] = 0;
    o0 = si;
    o1 = Append(s1, g_Draws, &db, sizeof(db)); // the draw's index: its first instance
}

static uint32_t Stream(const void* p, size_t n) {
    size_t off = (g_Stream.size() + 3) & ~(size_t)3;
    g_Stream.resize(off + ((n + 3) & ~(size_t)3), 0);
    memcpy(g_Stream.data() + off, p, n);
    return (uint32_t)off;
}

static unsigned Topo(unsigned prim) { return prim == 5 ? 1 : prim == 2 ? 2 : prim == 3 ? 3 : prim == 1 ? 4 : 0; }
static unsigned Count(unsigned prim, unsigned n) {
    switch (prim) { case 4: return n * 3; case 5: case 6: return n + 2; case 2: return n * 2; case 3: return n + 1; default: return n; }
}

static void Record(uint32_t pipe, uint32_t bg, unsigned vb, uint32_t vbo, int ib, uint32_t ibo, int ibf,
                   unsigned count, unsigned first, int base, uint32_t o0, uint32_t o1, uint32_t o2) {
    const int32_t r[16] = { 3, (int32_t)pipe, (int32_t)bg, (int32_t)vb, (int32_t)vbo, ib, (int32_t)ibo, ibf,
                            (int32_t)count, (int32_t)first, base, (int32_t)o1, (int32_t)o0, (int32_t)o2,
                            (int32_t)(GLES3_Get_State()->render_states[GLES3_RS_STENCILREF] & 255), 0 };
    g_Cmd.insert(g_Cmd.end(), r, r + 16);
    gxw_draws++;
}

// Fan -> list indices (one fan of n + 2 vertices given by idx16/idx32 or sequential from `start`).
static uint32_t Fan_Indices(unsigned prim_count, const uint8_t* idx, bool i32, unsigned start, unsigned& count, int& fmt) {
    static std::vector<uint32_t> out;
    out.clear();
    auto at = [&](unsigned j) -> uint32_t { return idx ? (i32 ? ((const uint32_t*)idx)[j] : ((const uint16_t*)idx)[j]) : start + j; };
    for (unsigned t = 0; t < prim_count; t++) { out.push_back(at(0)); out.push_back(at(t + 1)); out.push_back(at(t + 2)); }
    count = (unsigned)out.size();
    fmt = 2;
    return Stream(out.data(), out.size() * 4);
}

void GXW_Draw_Indexed(unsigned prim, unsigned prim_count, unsigned fvf, unsigned stride,
                      unsigned vb, size_t vb_size, unsigned base_vertex, unsigned min_vertex,
                      unsigned num_vertices, unsigned ib, unsigned start_index,
                      const uint8_t* ib_cpu, size_t ib_size) {
    if (!fvf || !prim_count) { gxw_skipped++; return; }
    unsigned count = Count(prim, prim_count);
    if ((size_t)(start_index + count) * 2 > ib_size) { gxw_skipped++; return; }
    int ibf = 1;
    int ibid = (int)ib;
    uint32_t ibo = start_index * 2, first = 0;
    if (prim == 6) { ibid = 0; ibo = Fan_Indices(prim_count, ib_cpu + start_index * 2, false, 0, count, ibf); }
    else { ibo = 0; first = start_index; Use_Buffer(ib, (size_t)start_index * 2, (size_t)(start_index + count) * 2); }
    size_t lo = (size_t)(base_vertex + min_vertex) * stride, hi = (size_t)(base_vertex + min_vertex + num_vertices) * stride;
    if (hi > vb_size) hi = vb_size;
    Use_Buffer(vb, lo, hi);
    GXW_SKey sk;
    Shader_Key(*GLES3_Get_State(), fvf, sk);
    const uint32_t pipe = Pipeline(sk, fvf, stride, prim == 6 ? 0 : Topo(prim), (prim == 5 || prim == 3) ? 1 : 0);
    uint32_t o0, o1, o2;
    Uniforms(sk, o0, o1, o2);
    Record(pipe, Bind_Group(), vb, 0, ibid, ibo, ibf, count, first, (int)base_vertex, o0, o1, o2);
}

void GXW_Draw_Arrays(unsigned prim, unsigned prim_count, unsigned fvf, unsigned stride, unsigned vb, unsigned start_vertex) {
    if (!fvf || !prim_count) { gxw_skipped++; return; }
    unsigned count = Count(prim, prim_count);
    Use_Buffer(vb, (size_t)start_vertex * stride, (size_t)(start_vertex + count) * stride);
    GXW_SKey sk;
    Shader_Key(*GLES3_Get_State(), fvf, sk);
    uint32_t o0, o1, o2;
    if (prim == 6) {
        int fmt; unsigned n;
        const uint32_t ibo = Fan_Indices(prim_count, nullptr, false, start_vertex, n, fmt);
        const uint32_t pipe = Pipeline(sk, fvf, stride, 0, 0);
        Uniforms(sk, o0, o1, o2);
        Record(pipe, Bind_Group(), vb, 0, 0, ibo, fmt, n, 0, 0, o0, o1, o2);
        return;
    }
    const uint32_t pipe = Pipeline(sk, fvf, stride, Topo(prim), 0);
    Uniforms(sk, o0, o1, o2);
    Record(pipe, Bind_Group(), vb, 0, -1, 0, 0, count, start_vertex, 0, o0, o1, o2);
}

void GXW_Draw_UP(unsigned prim, unsigned prim_count, unsigned fvf, unsigned stride,
                 const void* vertices, unsigned num_vertices, const void* indices, bool index32) {
    if (!fvf || !prim_count || !vertices) { gxw_skipped++; return; }
    unsigned count = Count(prim, prim_count);
    const unsigned nv = indices ? num_vertices : count;
    const uint32_t vbo = Stream(vertices, (size_t)nv * stride);
    GXW_SKey sk;
    Shader_Key(*GLES3_Get_State(), fvf, sk);
    uint32_t o0, o1, o2;
    int fmt = index32 ? 2 : 1;
    if (prim == 6) {
        unsigned n;
        const uint32_t ibo = Fan_Indices(prim_count, (const uint8_t*)indices, index32, 0, n, fmt);
        const uint32_t pipe = Pipeline(sk, fvf, stride, 0, 0);
        Uniforms(sk, o0, o1, o2);
        Record(pipe, Bind_Group(), 0, vbo, 0, ibo, fmt, n, 0, 0, o0, o1, o2);
        return;
    }
    const uint32_t pipe = Pipeline(sk, fvf, stride, Topo(prim), (prim == 5 || prim == 3) ? (index32 ? 2 : 1) : 0);
    Uniforms(sk, o0, o1, o2);
    if (indices) {
        const uint32_t ibo = Stream(indices, (size_t)count * (index32 ? 4 : 2));
        Record(pipe, Bind_Group(), 0, vbo, 0, ibo, fmt, count, 0, 0, o0, o1, o2);
    } else {
        Record(pipe, Bind_Group(), 0, vbo, -1, 0, 0, count, 0, 0, o0, o1, o2);
    }
}

void GXW_Clear(bool color, bool zs, float r, float g, float b, float a, float z, unsigned stencil) {
    if (!color && !zs) return;
    int32_t rec[8];
    rec[0] = 2; rec[1] = (color ? 1 : 0) | (zs ? 2 : 0);
    memcpy(&rec[2], &r, 4); memcpy(&rec[3], &g, 4); memcpy(&rec[4], &b, 4); memcpy(&rec[5], &a, 4); memcpy(&rec[6], &z, 4);
    rec[7] = (int32_t)stencil;
    g_Cmd.insert(g_Cmd.end(), rec, rec + 8);
}

void GXW_Viewport(int x, int y, int w, int h, float min_z, float max_z) {
    static float last[6] = { -1, -1, -1, -1, -1, -1 };
    const float v[6] = { (float)x, (float)y, (float)w, (float)h, min_z, max_z };
    if (memcmp(v, last, sizeof(v)) == 0) return;
    memcpy(last, v, sizeof(v));
    int32_t rec[8] = { 1 };
    memcpy(&rec[1], v, sizeof(v));
    g_Cmd.insert(g_Cmd.end(), rec, rec + 8);
}
#endif
