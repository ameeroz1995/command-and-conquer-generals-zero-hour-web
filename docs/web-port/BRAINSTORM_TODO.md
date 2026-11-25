# Brainstorm to-do (speed ideas we talked about, not started)

Each item: what it is, why, how we'd check it. Owner = who picks it up when it starts.

## Browser overhead (rendering chat)
- [ ] **Multi-threaded build GL hand-off.** Check whether the mt build proxies every GL call to the
  browser main thread (PROXY_TO_PTHREAD without OffscreenCanvas). If so, draw from the game thread via
  OffscreenCanvas. Likely the biggest single item. Check: GL calls per frame + fps, mt off/on.
- [ ] **No sync GL calls in the frame loop.** Find getError / getParameter / readPixels / any call that
  waits for the GPU during a frame, remove or move to load time. Check: Chrome trace, frame time.
- [ ] **Fewer state calls per draw.** Skip repeated state; uniform buffers instead of many uniform calls;
  VAOs everywhere. Check: GL calls per frame (bench "GL calls" column).
- [ ] **Bigger draws.** Multi-draw (in testing), then WebGPU (step 1 committed). Check: draws per frame.
- [ ] **Fixed big heap in the mt build.** A growable heap with threads makes memory access slower;
  start with a large fixed heap (RAM is cheap). Check: A/B mt.
- [ ] **Build flags.** -O3, link-time optimisation, wasm SIMD if not on yet. Check: A/B.
- [ ] **ANGLE backend tip for players.** ANGLE on Vulkan vs OpenGL in Edge/Chrome on Linux; a
  player-side setting, document it if it helps. Check: same build, two flags.

## Decided against (for the record)
- Rewrite in plain JavaScript: slower than the current WebAssembly, GC pauses, huge rewrite.
- Move to a JS game engine (Three.js / Babylon / PlayCanvas): only drawing could gain, and the
  WebGPU + multi-draw work gets that inside the current engine; the simulation would get slower.
