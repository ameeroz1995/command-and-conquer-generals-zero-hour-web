# WebGPU drawing (branch perf/webgpu)

Goal: the D3D8 shim draws through WebGPU instead of WebGL2, behind the Settings switch
`webgpu` (off by default; WebGL2 stays the fallback). Page loads with the switch on and
`navigator.gpu` present get a GPUDevice before the game starts (`Module.gxGPU`).

## Measured first (scratchpad drawbench, 3,000 game-like draws, 150 programs, 200 textures)
GPU-complete frame time, RX 580, Edge, ANGLE-Vulkan flags for both:
- WebGL2: 21.4 ms
- WebGPU, encoded every frame: 16.7 ms (-22%)
- WebGPU, render bundle replayed: 8.2 ms (-62%)
SwiftShader WebGPU: far slower than WebGL (frames > 2 s) -> software GPUs stay on WebGL.

## Design
Interception is at the GLES3 layer and the d3d8.h device, not at GL calls:
- State setters (render states, transforms, texture stage states, lights, material) only fill
  g_State when WebGPU is on (no GL calls). Everything is resolved at draw time.
- Draw = pipeline id (shader key + vertex layout + blend/depth/stencil/cull/topology) +
  uniform slices (scene block, draw block, extra block; appended to a per-frame ring only when
  they changed) + texture bind group (4 stages + shroud, cached by key) + vertex/index buffer
  ids, offsets and counts. Draw records are 16 ints in a wasm array; one JS call replays a
  batch into a GPURenderPassEncoder (gx_webgpu.js).
- Buffers keep their CPU copies (d3d8.h). Uploads go straight to queue.writeBuffer (a snapshot
  at call time). Hazard: a write into a byte range that a draw recorded since the last submit
  first flushes and submits the recorded draws (per-buffer used range [lo, hi)). Same for
  texture writes (whole texture). UP draws and uniforms use per-frame ring buffers.
- Clears: before any draw in the pass -> loadOp clear; else end the pass and begin one with
  loadOp clear (the GL path clears the whole target too).
- Shaders: WGSL generated in C++ (gles3_webgpu_shaders.cpp), always specialised (port of
  GLES3_FF_Generate_GLSL / GLES3_VS_Generate_GLSL / GLES3_PS_Generate_GLSL). Depth stays in
  D3D's [0, 1] (no z*2-w). Missing vertex attributes are constants in the shader.
- Textures: rgba8unorm (16-bit D3D formats expanded on the CPU), bc1/bc2/bc3 with the
  texture-compression-bc feature (else decoded on the CPU). Mip levels of uncompressed
  textures: CPU box filter at upload (WebGPU has no generateMipmap).
- Triangle fans become lists (indices built on the CPU into the ring).

## Milestones
1. Boots and draws menus + battle; off while WebGPU is on: render scale, ground cache, pan
   cache, shroud fold, GPU skinning, unit batching (instancing), benchmark thumbnails.
   Screenshots vs WebGL; benchmark A/B (same flags both sides).
2. Render bundles: cache a bundle per pass segment whose draw records match last frame's.
3. Bring back the features of step 1 on WebGPU (instancing, skinning, fold, render scale,
   readback for thumbnails), each measured.

## Results (2026-11-25, RX 580, Edge with WebGPU flags on both sides, battle benchmark)
Step 1 works: menus, battle and the 5-map tour draw through WebGPU; pictures match WebGL
(the blocky smoke edges are in both).
- With the WebGL-only features off on both sides (same 3,200-4,400 draws a frame):
  first version -12% fps (setBindGroup with a new dynamic offset per draw was 6.4% of the
  frame); with per-draw data in storage arrays read by instance_index (one setBindGroup per
  pass): armies +1%, strikes +5%; tour run: all phases within +-2%.
- Against WebGL with its batching on (live settings): -7..-9%, because WebGPU still draws
  every unit part separately (2x the draws). Porting unit batching and GPU skinning is next.

## Draw counts and multi-part draws (2026-11-25, WebGL, RX 580)
GXCAT log, strikes frame: rigid parts 1,500-2,350 draws, see-through 200-680, interface ~200,
soldiers 100-160, terrain/water/shadows/tracks/roads < 70 together. Why rigid parts stayed single
(GXINST): one-of-a-kind parts 650-1,600, different lighting 100-380, own material settings up to
1,100 at peaks; the 128 instance cap was never reached.
- multiDraw (switch, off): every rigid part of one material group in the same light in one
  WEBGL_multi_draw call, matrix per gl_DrawID. Quiet machine: draws -22..-30%, fps -4..+3% (noise),
  screenshots clean in 10 runs. Under heavy machine load (load 35-47) ~7 of 10 runs showed stray
  polygons covering the view; drawing the same groups one by one never did, so it is a timing
  issue in the browser's multi-draw path; kept off.
- multiLights (switch, off, needs multiDraw): parts in different light join, each with its own
  lights from the per-draw texture. Draws only -1..-3% more, uploads +22..39%, fps +1%.
Conclusion: at these counts draw calls are not what limits the frame on this machine.
