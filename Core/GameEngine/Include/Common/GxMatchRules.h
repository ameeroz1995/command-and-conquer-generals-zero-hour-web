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
	GXRULE_PHYS_BULK      = 2048,	///< physBulk: forces + velocity integrated for all units in one pass at the start of the physics phase (PhysicsUpdate.cpp)
};

unsigned int gxLocalMatchRules();		///< the rules the local Settings switches ask for
void gxStartMatchRules();				///< at the start of every game: fix the rules for a network game or replay
bool gxMatchRule(unsigned int rule);	///< is this rule on in the game being played?
unsigned int gxCurrentMatchRules();	///< all rules in force now (for logs)
