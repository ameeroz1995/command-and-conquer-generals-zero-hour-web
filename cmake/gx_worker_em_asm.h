// Forced include for the GX_WEB_WORKER build (main() on a pthread, canvas transferred).
// The game's EM_ASM blocks touch the page (window, document, Web Audio, the shell's
// progress UI, the JS FS, which lives on the main thread), so run them there. Code that
// must stay on the game thread uses EM_JS (unaffected) or guards on
// __EMSCRIPTEN_PTHREADS__ (canvas sizing in d3d8.h).
#pragma once
#include <emscripten/em_asm.h>
#undef EM_ASM
#define EM_ASM(code, ...) MAIN_THREAD_EM_ASM(code, ##__VA_ARGS__)
#undef EM_ASM_
#define EM_ASM_(code, ...) MAIN_THREAD_EM_ASM_INT(code, ##__VA_ARGS__)
#undef EM_ASM_INT
#define EM_ASM_INT(code, ...) MAIN_THREAD_EM_ASM_INT(code, ##__VA_ARGS__)
#undef EM_ASM_DOUBLE
#define EM_ASM_DOUBLE(code, ...) MAIN_THREAD_EM_ASM_DOUBLE(code, ##__VA_ARGS__)

// Entry points the page calls (network, input, audio events, resize, hidden-tab ticks)
// arrive on the main thread; the game state they touch lives on the game thread. They
// re-send themselves there with emscripten_dispatch_to_thread_async, which runs between
// frames when the game thread returns to its event loop.
#ifdef __cplusplus
#include <pthread.h>
#include <emscripten/threading.h>
extern "C" pthread_t GX_GameThread; // set at the top of main() (EmscriptenMain.cpp)
static inline bool GX_OffGameThread() {
  return GX_GameThread && !pthread_equal(pthread_self(), GX_GameThread);
}
#endif
#undef EM_ASM_PTR
#define EM_ASM_PTR(code, ...) MAIN_THREAD_EM_ASM_PTR(code, ##__VA_ARGS__)
