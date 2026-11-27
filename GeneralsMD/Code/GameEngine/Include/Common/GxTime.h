// Web port (2026-11-25, rule "fps is not the measure"): load-independent work counters and per-tick
// timers, for A/B runs on the same replay.
//
// Timing mode: fourth switch word bit 16 (tickTiming, Settings, default off). Off, every hook is one
// switch test. On:
// - GXT_LOGIC: GameLogic::update wall time (GameEngine::update), GXT_FRAME: whole frame interval;
// - each system adds its own time (GxTimeScope) and work counters (gxCount);
// - every 300 logic ticks one line "GXTIME ..." with per-tick averages since the start, the frame p99 and
//   the count of frames over 100 ms.
// Agents add their ids to the enums (keep the names short, they are printed).
#pragma once

#include <vector>
#include <algorithm>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern int GLES3_Page_Switches4();
#else
#ifndef GX_SWITCHES4_FALLBACK
#define GX_SWITCHES4_FALLBACK
static inline int GLES3_Page_Switches4() { return 0; }
#endif
static inline double emscripten_get_now() { return 0.0; }
#endif

enum GxTimeId { GXT_LOGIC, GXT_FRAME, GXT_SEARCH, GXT_FOG_REVEAL, GXT_SNAP, GXT_PATCH, GXT_PART_UPDATE, GXT_CELLS, GXT_PAIRS, GXT_BATCH_CULL, GXT_TIME_COUNT };
enum GxCountId
{
	GXC_SEARCHES, GXC_SEARCH_CELLS, GXC_SEARCH_ENTRIES, GXC_SEARCH_UNITS, GXC_SIMD_CULLED,
	GXC_FOG_CIRCLES, GXC_FOG_CELLS, GXC_FOG_STATUS_CALLS, GXC_FOG_RECOMPUTES, GXC_FOG_KEPT,
	GXC_PAIRS_TESTED, GXC_PAIRS_SIMD_CULLED, GXC_SNAP_BUILDS, GXC_SNAP_PATCHES, GXC_CELL_UPDATES, GXC_BATCH_SEEKERS, GXC_ALLOCS, GXC_NAME_LOOKUPS,
	GXC_COUNT
};

extern unsigned long long g_gxAllocs;	///< game memory blocks handed out (GameMemory.cpp)

struct GxTimeState
{
	double ms[GXT_TIME_COUNT] = {};
	unsigned long long n[GXC_COUNT] = {};
	unsigned long long ticks = 0;
	std::vector<float> frames;	///< frame intervals (ms)
	double lastFrame = 0.0;
	int depth[GXT_TIME_COUNT] = {};	///< nesting guard: only the outermost scope of an id is timed
	unsigned long long allocs[GXT_TIME_COUNT] = {};	///< game memory blocks allocated inside each timer's outermost scope
};

inline GxTimeState &gxTime() { static GxTimeState s; return s; }
inline bool gxTimingOn() { return (GLES3_Page_Switches4() & 65536) != 0; }
inline void gxCount(GxCountId id, unsigned long long k = 1) { if (gxTimingOn()) gxTime().n[id] += k; }

struct GxTimeScope
{
	GxTimeId id; double t0; bool on; unsigned long long a0;
	explicit GxTimeScope(GxTimeId i) : id(i), t0(0.0), on(gxTimingOn() && gxTime().depth[i]++ == 0), a0(g_gxAllocs) { if (on) t0 = emscripten_get_now(); }
	~GxTimeScope()
	{
		if (!gxTimingOn()) return;
		GxTimeState &s = gxTime();
		if (on) { s.ms[id] += emscripten_get_now() - t0; s.allocs[id] += g_gxAllocs - a0; }
		if (s.depth[id] > 0) --s.depth[id];
	}
};

/// GameEngine::update, at the start of each frame
inline void gxTimeFrameStart()
{
	if (!gxTimingOn()) return;
	GxTimeState &s = gxTime();
	const double now = emscripten_get_now();
	if (s.lastFrame > 0.0) { s.frames.push_back((float)(now - s.lastFrame)); s.ms[GXT_FRAME] += now - s.lastFrame; }
	s.lastFrame = now;
}

/// after each logic tick
inline void gxTimeTickEnd()
{
	if (!gxTimingOn()) return;
	GxTimeState &s = gxTime();
	if (++s.ticks % 300) return;
	static const char *tn[GXT_TIME_COUNT] = { "logic", "frame", "search", "fogReveal", "snapBuild", "snapPatch", "partUpdate", "cellsTouched", "pairOffers", "bsCull" };
	static const char *cn[GXC_COUNT] = { "searches", "searchCells", "searchEntries", "searchUnits", "simdCulled",
		"fogCircles", "fogCells", "fogStatusCalls", "fogRecomputes", "fogKept", "pairsTested", "pairsSimdCulled", "snapBuilds", "snapPatches", "cellUpdates", "bsSeekers", "allocs", "nameLookups" };
	std::vector<float> f = s.frames;
	float p99 = 0.0f; int over100 = 0;
	if (!f.empty())
	{
		std::sort(f.begin(), f.end());
		p99 = f[(size_t)(0.99 * (f.size() - 1))];
		for (size_t i = 0; i < f.size(); ++i) if (f[i] > 100.0f) ++over100;
	}
	char buf[2048]; int k = snprintf(buf, sizeof(buf), "GXTIME ticks %llu:", s.ticks);
	for (int i = 0; i < GXT_TIME_COUNT; ++i) k += snprintf(buf + k, sizeof(buf) - k, " %s %.3f ms/tick;", tn[i], s.ms[i] / s.ticks);
	for (int i = 0; i < GXC_COUNT; ++i) k += snprintf(buf + k, sizeof(buf) - k, " %s %.1f/tick;", cn[i], (double)s.n[i] / s.ticks);
	k += snprintf(buf + k, sizeof(buf) - k, " allocs in");
	for (int i = 0; i < GXT_TIME_COUNT; ++i) if (i != GXT_LOGIC && i != GXT_FRAME) k += snprintf(buf + k, sizeof(buf) - k, " %s %.1f/tick;", tn[i], (double)s.allocs[i] / s.ticks);
	snprintf(buf + k, sizeof(buf) - k, " frames %d, p99 %.1f ms, over 100 ms %d", (int)f.size(), p99, over100);
	printf("%s\n", buf);
}
