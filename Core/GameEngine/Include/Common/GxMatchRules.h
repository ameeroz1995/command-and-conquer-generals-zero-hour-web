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
	GXRULE_PATH_BUDGET    = 16,	///< pathBudget: smaller per-frame route search budget (AIPathfind.cpp)
};

unsigned int gxLocalMatchRules();		///< the rules the local Settings switches ask for
void gxStartMatchRules();				///< at the start of every game: fix the rules for a network game or replay
bool gxMatchRule(unsigned int rule);	///< is this rule on in the game being played?
unsigned int gxCurrentMatchRules();	///< all rules in force now (for logs)
