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

enum GxTimeId { GXT_LOGIC, GXT_FRAME, GXT_SEARCH, GXT_FOG_REVEAL, GXT_MOVE, GXT_MOVE_KERNEL, GXT_AI_LOOP, GXT_AI_KERNELS, 
	GXT_SPLASH, GXT_ESTIMATE, GXT_RELOAD,	// combat
	GXT_PHYSICS, GXT_COLLIDE_BUILD, GXT_COLLIDE_PROCESS,	// collide
	GXT_CLIENT, GXT_CONTROLBAR,	// combat: GameClient::update, ControlBar::update (selection UI)
	GXT_PATHFIND, GXT_LOCO, GXT_SNAP, GXT_PATCH, GXT_PART_UPDATE, GXT_CELLS, GXT_PAIRS,	// paths / ai / query
	GXT_TIME_COUNT };
enum GxCountId
{
	GXC_SEARCHES, GXC_SEARCH_CELLS, GXC_SEARCH_ENTRIES, GXC_SEARCH_UNITS, GXC_SIMD_CULLED,
	GXC_FOG_CIRCLES, GXC_FOG_CELLS, GXC_FOG_STATUS_CALLS, GXC_FOG_RECOMPUTES, GXC_FOG_KEPT,
	GXC_MOVE_UNITS, GXC_MOVE_KERNEL_POINTS, GXC_MOVE_KERNEL_TAKEN, GXC_MOVE_APPLY_POINTS, GXC_HORDE_SEARCHES,
	GXC_AI_MODULE_CALLS, GXC_AI_KERNEL_SLOTS,
	// combat: splash searches / victims / store slots scanned / kept by the SIMD pass; damage estimates,
	// damage-table and bonus-table hits and misses; weapon rows visited / changed by the reload kernel
	GXC_SPLASHES, GXC_SPLASH_VICTIMS, GXC_SPLASH_SCANNED, GXC_SPLASH_NEAR,
	GXC_ESTIMATES, GXC_TABLE_HITS, GXC_TABLE_MISSES, GXC_BONUS_HITS, GXC_BONUS_MISSES,
	GXC_RELOAD_ROWS, GXC_RELOAD_CHANGED, GXC_HEIGHTDIE_REDONE,
	GXC_PHYS_UNITS, GXC_COLLIDE_SEEKERS, GXC_PAIR_CELLS, GXC_PAIR_TESTS, GXC_PAIRS, GXC_ALLOCS,
	// combat: game memory blocks allocated inside weapon fire (includes the damage it deals), damage
	// dealing (splash lists included), damage estimates for target choice, projectile updates
	GXC_ALLOC_FIRE, GXC_ALLOC_DAMAGE, GXC_ALLOC_ESTIMATE, GXC_ALLOC_PROJECTILE, GXC_ALLOC_FX, GXC_ALLOC_SPAWN, GXC_EVENT_WALK, GXC_ASSIST_ASKS, GXC_BUILD_MORE,
	GXC_PATH_FINDS, GXC_PATH_CELLS, GXC_LOCO_CALLS, GXC_CPOP_CALLS, GXC_CPOP_SIMD_SEGS,
	GXC_MOOD_SCANS, GXC_BATCH_SCANNED, GXC_BATCH_USED, GXC_BATCH_CALLS,
	GXC_PAIRS_TESTED, GXC_PAIRS_SIMD_CULLED, GXC_SNAP_BUILDS, GXC_SNAP_PATCHES, GXC_CELL_UPDATES,
	GXC_COUNT
};

struct GxTimeState
{
	double ms[GXT_TIME_COUNT] = {};
	unsigned long long n[GXC_COUNT] = {};
	unsigned long long ticks = 0;
	std::vector<float> frames;	///< frame intervals (ms)
	double lastFrame = 0.0;
	int depth[GXT_TIME_COUNT] = {};	///< nesting guard: only the outermost scope of an id is timed
};

inline GxTimeState &gxTime() { static GxTimeState s; return s; }
inline bool gxTimingOn() { return (GLES3_Page_Switches4() & 65536) != 0; }
inline void gxCount(GxCountId id, unsigned long long k = 1) { if (gxTimingOn()) gxTime().n[id] += k; }

struct GxTimeScope
{
	GxTimeId id; double t0; bool on;
	explicit GxTimeScope(GxTimeId i) : id(i), t0(0.0), on(gxTimingOn() && gxTime().depth[i]++ == 0) { if (on) t0 = emscripten_get_now(); }
	~GxTimeScope()
	{
		if (!gxTimingOn()) return;
		GxTimeState &s = gxTime();
		if (on) s.ms[id] += emscripten_get_now() - t0;
		if (s.depth[id] > 0) --s.depth[id];
	}
};

/// game memory blocks allocated while the outermost scope of `id` is open (g_gxAllocs, GameMemory.cpp)
struct GxAllocScope
{
	GxCountId id; unsigned long long a0; bool on;
	static int *depth() { static int d[GXC_COUNT] = {}; return d; }
	static unsigned long long allocs() { extern unsigned long long g_gxAllocs; return g_gxAllocs; }
	explicit GxAllocScope(GxCountId i) : id(i), a0(0), on(gxTimingOn() && depth()[i]++ == 0) { if (on) a0 = allocs(); }
	~GxAllocScope()
	{
		if (!gxTimingOn()) return;
		if (on) gxTime().n[id] += allocs() - a0;
		if (depth()[id] > 0) --depth()[id];
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
	static const char *tn[GXT_TIME_COUNT] = { "logic", "frame", "search", "fogReveal", "move", "moveKernel", "aiLoop", "aiKernels", "splash", "estimate", "reload", "physics", "collideBuild", "collideProcess", "client", "controlBar", "pathfind", "loco", "snapBuild", "snapPatch", "partUpdate", "cellsTouched", "pairOffers" };
	static const char *cn[GXC_COUNT] = { "searches", "searchCells", "searchEntries", "searchUnits", "simdCulled",
		"fogCircles", "fogCells", "fogStatusCalls", "fogRecomputes", "fogKept",
		"moveUnits", "moveKernelPoints", "moveKernelTaken", "moveApplyPoints", "hordeSearches",
		"aiModuleCalls", "aiKernelSlots",
		"splashes", "splashVictims", "splashScanned", "splashNear",
		"estimates", "tableHits", "tableMisses", "bonusHits", "bonusMisses", "reloadRows", "reloadChanged", "heightDieRedone",
		"physUnits", "collideSeekers", "pairCells", "pairTests", "pairs", "allocs",
		"allocFire", "allocDamage", "allocEstimate", "allocProjectile", "allocFx", "allocSpawn", "eventWalkObjects", "assistAsks", "buildMoreChecks",
		"pathFinds", "pathCells", "locoCalls", "cpopCalls", "cpopSimdSegs",
		"moodScans", "batchScanned", "batchUsed", "batches",
		"pairsTested", "pairsSimdCulled", "snapBuilds", "snapPatches", "cellUpdates" };
	std::vector<float> f = s.frames;
	float p99 = 0.0f; int over100 = 0;
	if (!f.empty())
	{
		std::sort(f.begin(), f.end());
		p99 = f[(size_t)(0.99 * (f.size() - 1))];
		for (size_t i = 0; i < f.size(); ++i) if (f[i] > 100.0f) ++over100;
	}
	char buf[4096]; int k = snprintf(buf, sizeof(buf), "GXTIME ticks %llu:", s.ticks);
	for (int i = 0; i < GXT_TIME_COUNT; ++i) k += snprintf(buf + k, sizeof(buf) - k, " %s %.3f ms/tick;", tn[i], s.ms[i] / s.ticks);
	for (int i = 0; i < GXC_COUNT; ++i) k += snprintf(buf + k, sizeof(buf) - k, " %s %.1f/tick;", cn[i], (double)s.n[i] / s.ticks);
	snprintf(buf + k, sizeof(buf) - k, " frames %d, p99 %.1f ms, over 100 ms %d", (int)f.size(), p99, over100);
	printf("%s\n", buf);
}
