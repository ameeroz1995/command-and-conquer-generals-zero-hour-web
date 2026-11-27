#pragma once
// Web port: the AI lane's hash-map sweep switches (exact: same game, plain Settings switches).
// #4, #5, #13 are fifth-word bits 21-23; #14, #15 are sixth-word bits 0-1 (sixth-word ranges:
// ai 0-3, collide 4-7, query 8-11, combat 12-15, paths 16-19).
#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches5();
extern int GLES3_Page_Switches6();
#else
#ifndef GX_SWITCHES56_FALLBACK
#define GX_SWITCHES56_FALLBACK
static inline int GLES3_Page_Switches5() { return 0; }
static inline int GLES3_Page_Switches6() { return 0; }
#endif
#endif
enum GxSweepSwitch
{
	GXSW_PRIO_ARRAY   = 0x100 | 21,	///< #4 attackPrioArray (fifth word bit 21): attack priority by template ID (ScriptEngine.cpp)
	GXSW_STATE_ARRAY  = 0x100 | 22,	///< #5 stateArray (fifth word bit 22): StateMachine state lookup by ID in an array (StateMachine.cpp)
	GXSW_DISABLED_POP = 0x100 | 23,	///< #13 disabledPop (fifth word bit 23): disabled list removal by stored index (GameLogic.cpp)
	GXSW_PASS_POS     = 0x200 | 0,	///< #14 passPos (sixth word bit 0): update pass removal by stored position (GameLogic.cpp)
	GXSW_PASS_KEY     = 0x200 | 1,	///< #15 passKeyArray (sixth word bit 1): pass sort keys and kinds by NameKey in arrays (GameLogic.cpp)
	GXSW_FAST_REMOVE  = 0x200 | 3,	///< #55 fastRemove (sixth word bit 3): unit group and radar removal in O(1) (AIGroup.cpp, Radar.cpp)
	GXSW_TEAM_ONE_WALK = 0x200 | 20,	///< #3 teamOneWalk (sixth word bit 20): one factory walk per unit type (AIPlayer.cpp)
	GXSW_LIGHT_ITER   = 0x200 | 21,	///< lightIter (sixth word bit 21): terrain vertex lighting uses a lights iterator on the stack (BaseHeightMap.cpp)
	GXSW_SCAN_LEAN    = 0x200 | 29,	///< scanLean (sixth word bit 29): batchScan collects only modules due this tick and tests the mood frame first (GameLogic.cpp, AIUpdate.cpp)
	GXSW_AUDIO_NAMES  = 0x200 | 22,	///< audioNames (sixth word bit 22): sound file names built once per event and sound (AudioEventRTS.cpp)
	GXSW_ATTACK_REUSE = 0x200 | 23,	///< attackReuse (sixth word bit 23): AIAttackState keeps its attack state machine between attacks (AIStates.cpp)
	GXSW_SUPPLY_ONCE  = 0x200 | 2,	///< #54 supplyOnce (sixth word bit 2): AI supply search collects warehouses once (AIPlayer.cpp)
};
inline bool gxSweepOn(int sw)
{
	const int w = (sw & 0x200) ? GLES3_Page_Switches6() : GLES3_Page_Switches5();
	return (w >> (sw & 31)) & 1;
}
