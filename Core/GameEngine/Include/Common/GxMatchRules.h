// Web port: match rules. Switches that change the simulation (so every player and every replay
// must agree on them) go through gxMatchRule(). In a network game or a replay the rules are the
// ones the host's Settings chose, carried in the game options ("GX=" in GameInfoToAsciiString,
// so they reach every player and the replay header) and fixed when the match starts. In single
// player they are the local Settings switches, read live.
#pragma once

enum GxMatchRule
{
	GXRULE_IGNORE_UNITS   = 1,	///< ignoreUnitsInPaths: routes planned around terrain and buildings only (AIPathfind.cpp)
	GXRULE_FLOW_FIELDS    = 2,	///< flowFields: shared route maps for long ground moves (AIPathfind.cpp)
	GXRULE_FAST_INV_SQRT  = 4,	///< fastInvSqrt: Quake-style 1/sqrt (wwmath.h)
	GXRULE_LAZY_SCANS     = 8,	///< lazyScans: idle enemy scans that found nothing wait longer (AIUpdate.cpp)
	// 16: was pathBudget (a smaller per-frame route search budget): measured, no gain, removed
	GXRULE_COARSE_START   = 32,	///< coarseStart: move along the zone-level route while the full one is queued (AIUpdate.cpp)
	GXRULE_SHARED_ROUTES  = 64,	///< sharedRoutes: units sent to one taken point plan to free spots near it (AIUpdate.cpp)
	GXRULE_ROUTE_BUDGET   = 128,	///< routeBudget: half the route-search work per frame (AIPathfind.cpp)
	GXRULE_COLLIDE_EVERY2 = 256,	///< collideStagger: ground units look for touching units every 2nd frame (PartitionManager.cpp)
	GXRULE_COLLIDE_EVERY3 = 512,	///< collideStagger3: every 3rd frame (PartitionManager.cpp)
	GXRULE_MOVE_AFTER_TICK = 32768,	///< moveAfterTick: every unit's locomotion in one pass after the update queue (AIUpdate.cpp)
	GXRULE_GROUP_ROUTE    = 1024,	///< groupRoute: a group order plans one route; members follow offset copies (AIGroup.cpp, AIUpdate.cpp)
	GXRULE_ROUTE_QUEUE    = 1048576,	///< routeQueue: requests never dropped when the queue is full; an order's burst released 64 a frame, coarse starts 32 a frame; no closest-spot search starts in the second half of a frame's budget (AIPathfind.cpp)
	GXRULE_ROUTE_CACHE    = 2097152,	///< routeCache: a recent route from the same start area to the same goal area is reused, shifted (AIPathfind.cpp)
	GXRULE_ROUTE_REFINE   = 4194304,	///< routeRefine: long moves search in detail only the next ~30 cells, then follow the zone-level route and extend; closest-spot searches stop at 2,000 cells once they have a spot (AIPathfind.cpp, AIStates.cpp)
	GXRULE_FLOW_COVER     = 8388608,	///< flowCover: flow fields also for moves over 8 cells (AIPathfind.cpp)
	GXRULE_STORE_PAIRS    = 65536,	///< storePairs: collision broad phase over the unit store's cells (PartitionManager.cpp)
	GXRULE_PHYS_BULK      = 2048,	///< physBulk: the whole physics step of all units in one pass over the unit store (PhysicsUpdate.cpp)
	GXRULE_SCAN_MANAGER   = 262144,	///< scanManager: range searches as a manager: per-tick store lists, scan pass, per-tick result cache (PartitionManager.cpp)
	GXRULE_SPATIAL_PAIRS  = 8192,	///< pairOffer: touching-pair search drops clearly-apart neighbours when offered (PartitionManager.cpp)
	GXRULE_WEAPON_PASS    = 16384,	///< weaponPass: armed objects' weapon status kept by one end-of-tick pass in object-ID order (ObjectWeaponStatusHelper.cpp)
	GXRULE_UPDATE_PASSES  = 131072,	///< updatePasses: every-frame update modules run as flat passes per phase, by behaviour then object ID (GameLogic.cpp)
	GXRULE_DAMAGE_QUEUE   = 524288,	///< damageQueue: weapon damage queued during the tick and applied in one pass in victim-ID order (Weapon.cpp)
};

unsigned int gxLocalMatchRules();		///< the rules the local Settings switches ask for
void gxStartMatchRules();				///< at the start of every game: fix the rules for a network game or replay
bool gxMatchRule(unsigned int rule);	///< is this rule on in the game being played?
unsigned int gxCurrentMatchRules();	///< all rules in force now (for logs)

