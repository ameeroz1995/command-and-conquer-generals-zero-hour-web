/*
** GeneralsX Web Port — Main Loop Adapter Implementation
**
** Bridges the game's traditional while-loop to Emscripten's
** requestAnimationFrame-based callback system.
**
** LICENSE: GPL-3.0
*/

#if defined(__EMSCRIPTEN__)

#include "gles3_mainloop.h"
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>
#include <unistd.h>
#include <malloc.h>
#include <cstdint>
#include <emscripten/heap.h>
#include <cmath>

// ============================================================================
// Internal state
// ============================================================================
static struct {
    WebFrameCallback frame_callback = nullptr;
    bool shutdown_requested = false;
    bool paused = false;

    // Timing
    double start_time_ms = 0.0;
    double last_frame_time_ms = 0.0;
    float delta_time_ms = 16.667f;  // ~60fps default
    unsigned int frame_count = 0;

    // FPS calculation (rolling average)
    float fps = 60.0f;
    float fps_accumulator = 0.0f;
    unsigned int fps_frame_count = 0;
    double fps_last_update = 0.0;

    // Loading
    WebMainLoop::LoadingProgressCallback loading_callback = nullptr;

    // Visibility
    WebVisibility::VisibilityCallback visibility_callback = nullptr;
} g_Loop;

// ============================================================================
// WebTiming
// ============================================================================
namespace WebTiming {

double Now_Ms() {
    return emscripten_get_now();
}

void Yield_Ms(int ms) {
    // No-op to avoid Asyncify dependency
}

} // namespace WebTiming

// Steady 30 fps (item 19 of the low-end plan). A match that can't hold 60 but runs at 33-50
// fps alternates long and short frames, which looks worse than an even 30. Then draw on every
// second display refresh (EM_TIMING_RAF 2; 33 ms timeouts when not on rAF) once the rate has
// stayed there for 5 s. Below ~33 fps some frames take over 33 ms, miss their slot and wait for
// the next pair, so pacing would make it slower: it starts only above 33 and drops back to 60 at
// once under 27. Every 30 s it tries 60 again.
// After such a drop it waits 1 minute before pacing again, doubling each time (up to 8 min), so a
// machine that swings between 15 and 45 fps doesn't flip back and forth (seen on the low-end
// setup: 4-5 switches a run). Called every ~500 ms with the measured rate.
// gx_in_match is set by EmscriptenMain. The page's setting "Steady 30 fps" (or ?pace=0, for
// measuring) turns pacing off; read about once a second with the other page switches.
int gx_in_match = 0;
static void GX_PaceFrames(float fps) {
    static int halved = 0, steady = 0;
    static double since = 0, blockedUntil = 0, backoff = 60000.0;
    extern int GLES3_Page_Switches();
    const bool enabled = (GLES3_Page_Switches() & 4) != 0;
    if (!enabled && !halved) return;
    int mode = 0, value = 0;
    emscripten_get_main_loop_timing(&mode, &value);
    const double now = WebTiming::Now_Ms();
    int want = halved;
    if (!enabled || !gx_in_match) { want = 0; steady = 0; }
    else if (!halved) {
        steady = (fps >= 33.0f && fps <= 50.0f && now >= blockedUntil) ? steady + 1 : 0;
        if (steady >= 10) want = 1;
    }
    else if (fps < 27.0f) {
        want = 0; steady = 0;
        blockedUntil = now + backoff;
        if (backoff < 480000.0) backoff *= 2;
    }
    else if (now - since > 30000.0) { want = 0; steady = 0; }
    if (want == halved) return;
    halved = want;
    since = now;
    if (mode == EM_TIMING_RAF) emscripten_set_main_loop_timing(EM_TIMING_RAF, halved ? 2 : 1);
    else emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, halved ? 33 : 16);
    printf("GXPACE %s (%.1f fps)\n", halved ? "30" : "60", fps);
}

// ============================================================================
// Per-frame callback — called by Emscripten's requestAnimationFrame
// ============================================================================
extern "C" { void (*gx_frame_hook)(double work_ms) = nullptr; }
static double gx_last_work_ms = 0;

static void emscripten_main_loop_callback() {
    if (g_Loop.shutdown_requested) {
        emscripten_cancel_main_loop();
        return;
    }

    if (g_Loop.paused) {
        // Still running the loop but skipping game logic
        // This keeps the browser tab responsive
        return;
    }

    // Calculate delta time
    double now = WebTiming::Now_Ms();
    if (g_Loop.last_frame_time_ms > 0.0) {
        g_Loop.delta_time_ms = (float)(now - g_Loop.last_frame_time_ms);
    }
    g_Loop.last_frame_time_ms = now;

    // Clamp delta time to prevent spiral of death after tab switch
    // (browser pauses rAF when tab is hidden; first frame back has huge dt)
    if (g_Loop.delta_time_ms > 100.0f) {
        g_Loop.delta_time_ms = 16.667f;  // pretend it was one frame
    }

    // Update FPS counter (every ~500ms)
    g_Loop.fps_accumulator += g_Loop.delta_time_ms;
    g_Loop.fps_frame_count++;
    if (g_Loop.fps_accumulator >= 500.0f) {
        g_Loop.fps = (float)g_Loop.fps_frame_count / (g_Loop.fps_accumulator / 1000.0f);
        g_Loop.fps_accumulator = 0.0f;
        g_Loop.fps_frame_count = 0;
        GX_PaceFrames(g_Loop.fps);
    }

    // Run game frame
    if (g_Loop.frame_callback) {
        double work_start = WebTiming::Now_Ms();
        bool keep_running = g_Loop.frame_callback(g_Loop.delta_time_ms);
        if (!keep_running) {
            g_Loop.shutdown_requested = true;
        }
        // CPU time per frame on whichever thread runs the game, logged every 5 s
        // (GXWORK lines; the benchmark scripts compare builds with it).
        static double work_sum = 0, work_t0 = 0; static int work_n = 0;
        double work_end = WebTiming::Now_Ms();
        gx_last_work_ms = work_end - work_start;
        work_sum += work_end - work_start; work_n++;
        // Average over the last second, for the page's frame-rate overlay (Module.gxWorkMs).
        { extern double gx_work_ms_1s; static double s1 = 0, t1 = 0; static int n1 = 0;
          s1 += work_end - work_start; n1++;
          if (t1 == 0) t1 = work_end;
          if (work_end - t1 >= 1000.0) { gx_work_ms_1s = s1 / n1; s1 = 0; n1 = 0; t1 = work_end; } }
        if (work_t0 == 0) work_t0 = work_end;
        if (work_end - work_t0 >= 5000.0) {
            printf("GXWORK ms=%.2f frames=%d\n", work_sum / work_n, work_n);
            // Heap in use (top of the sbrk heap) vs. the wasm memory size, for sizing the
            // fixed heap of the worker build; GXMEM lines.
            // live = bytes malloc has handed out and not freed (mallinfo); top = the heap's high
            // water mark (sbrk), which never comes down; of = the wasm memory size.
            printf("GXMEM live=%u MB top=%u MB of %u MB\n", (unsigned)(mallinfo().uordblks >> 20),
                   (unsigned)((uintptr_t)sbrk(0) >> 20), (unsigned)(emscripten_get_heap_size() >> 20));
            extern int gx_layout_misses, gx_layout_clears, gx_ff_slots, gx_async_programs, gx_fallback_draws;
            printf("GXLAYOUT new=%d flushes=%d ffprograms=%d async=%d fallbackdraws=%d\n", gx_layout_misses, gx_layout_clears, gx_ff_slots, gx_async_programs, gx_fallback_draws);
            work_sum = 0; work_n = 0; work_t0 = work_end;
        }
    }

    // The benchmark (Main/WebBenchmark.cpp) records every frame and drives its scene from here.
    if (gx_frame_hook) gx_frame_hook(gx_last_work_ms);
    g_Loop.frame_count++;
}

// GeneralsX @feature 09/11/2026 Browsers stop requestAnimationFrame in a hidden tab, and a lockstep
// LAN game then stalls for every player. The page calls this from a Web Worker timer (not
// throttled when hidden) while a networked game's tab is hidden.
extern "C" EMSCRIPTEN_KEEPALIVE void GX_TickHidden() {
#ifdef __EMSCRIPTEN_PTHREADS__
    if (GX_OffGameThread()) { // the page's timer; the frame runs on the game thread
        emscripten_dispatch_to_thread_async(GX_GameThread, EM_FUNC_SIG_V, (void *)GX_TickHidden, nullptr);
        return;
    }
#endif
    emscripten_main_loop_callback();
}

// ============================================================================
// WebMainLoop
// ============================================================================
namespace WebMainLoop {

void Start(WebFrameCallback frame_cb, int target_fps,
           bool simulate_infinite_loop) {
    g_Loop.frame_callback = frame_cb;
    g_Loop.shutdown_requested = false;
    g_Loop.start_time_ms = WebTiming::Now_Ms();
    g_Loop.last_frame_time_ms = g_Loop.start_time_ms;
    g_Loop.frame_count = 0;
    g_Loop.fps = (target_fps > 0) ? (float)target_fps : 60.0f;

    fprintf(stderr, "INFO: Starting Emscripten main loop (target FPS: %s)\n",
            target_fps == 0 ? "vsync" : std::to_string(target_fps).c_str());

    // 0 = use requestAnimationFrame (sync to display refresh)
    // simulate_infinite_loop = true means main() won't return
    emscripten_set_main_loop(emscripten_main_loop_callback,
                              target_fps,
                              simulate_infinite_loop ? 1 : 0);
}

void Request_Shutdown() {
    fprintf(stderr, "INFO: Shutdown requested\n");
    g_Loop.shutdown_requested = true;
}

bool Is_Shutdown_Requested() {
    return g_Loop.shutdown_requested;
}

double Get_Elapsed_Time_Ms() {
    return WebTiming::Now_Ms() - g_Loop.start_time_ms;
}

float Get_Delta_Time_Ms() {
    return g_Loop.delta_time_ms;
}

float Get_FPS() {
    return g_Loop.fps;
}

unsigned int Get_Frame_Count() {
    return g_Loop.frame_count;
}

void Pause() {
    g_Loop.paused = true;
    fprintf(stderr, "INFO: Main loop paused\n");
}

void Resume() {
    if (g_Loop.paused) {
        g_Loop.paused = false;
        // Reset timing to avoid huge delta on resume
        g_Loop.last_frame_time_ms = WebTiming::Now_Ms();
        fprintf(stderr, "INFO: Main loop resumed\n");
    }
}

bool Is_Paused() {
    return g_Loop.paused;
}

void Set_Loading_Callback(LoadingProgressCallback cb) {
    g_Loop.loading_callback = cb;
}

void Run_Loading_Phase(LoadStartCallback load_func,
                        LoadReadyCallback ready_func) {
    if (load_func) load_func();

    // Non-yielding poll if needed (Run_Loading_Phase is not actively called in game loop)
    while (ready_func && !ready_func()) {
        if (g_Loop.loading_callback) {
            g_Loop.loading_callback(0.5f, "Loading game assets...");
        }
    }
}

} // namespace WebMainLoop

// ============================================================================
// WebVisibility — pause game when tab is hidden
// ============================================================================
namespace WebVisibility {

static EM_BOOL visibility_change_callback(int eventType,
                                            const EmscriptenVisibilityChangeEvent* event,
                                            void* userData) {
    bool is_visible = !event->hidden;

    if (is_visible) {
        WebMainLoop::Resume();
    } else {
        WebMainLoop::Pause();
    }

    if (g_Loop.visibility_callback) {
        g_Loop.visibility_callback(is_visible);
    }

    return EM_TRUE;
}

void Set_Callback(VisibilityCallback cb) {
    g_Loop.visibility_callback = cb;
}

void Init() {
    emscripten_set_visibilitychange_callback(nullptr, EM_TRUE,
                                              visibility_change_callback);
    fprintf(stderr, "INFO: Tab visibility monitoring active\n");
}

} // namespace WebVisibility

#endif // __EMSCRIPTEN__
