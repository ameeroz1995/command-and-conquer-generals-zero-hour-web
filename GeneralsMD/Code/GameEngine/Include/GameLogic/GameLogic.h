/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: GameLogic.h //////////////////////////////////////////////////////////////////////////////
// GameLogic singleton class - defines interface to GameLogic methods and objects
// Author: Michael S. Booth, October 2000
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/GameCommon.h"	// ensure we get DUMP_PERF_STATS, or not
#include "Common/GameType.h"
#include "Common/Snapshot.h"
#include "Common/STLTypedefs.h"
#include "Common/ObjectStatusTypes.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameLogic/Module/UpdateModule.h"	// needed for DIRECT_UPDATEMODULE_ACCESS

/*
	At one time, we distinguished between sleepy and nonsleepy
	update modules, and kept a separate list for each. however,
	now that the bulk of update modules are sleepy, profiling shows
	that there is no real advantage to having the separate list,
	so to simplify the world, I am removing it. If ALLOW_NONSLEEPY_UPDATES
	is still undefined when we ship, please just nuke all the undefed
	code. (srj)
*/
#define NO_ALLOW_NONSLEEPY_UPDATES

// forward declarations
class AudioEventRTS;
class Object;
class Drawable;
class Player;
class ThingTemplate;
class Team;
class CommandList;
class GameMessage;
class LoadScreen;
class WindowLayout;
class TerrainLogic;
class GhostObjectManager;
class CommandButton;
enum BuildableStatus CPP_11(: Int);


typedef const CommandButton* ConstCommandButtonPtr;

// What kind of game we're in.
enum GameMode CPP_11(: Int)
{
	GAME_SINGLE_PLAYER,
	GAME_LAN,
	GAME_SKIRMISH,
	GAME_REPLAY,
	GAME_SHELL,
	GAME_INTERNET,
	GAME_NONE
};

enum
{
	CRC_CACHED,
	CRC_RECALC
};


/// Function pointers for use by GameLogic callback functions.
typedef void (*GameLogicFuncPtr)( Object *obj, void *userData );
//typedef std::hash_map<ObjectID, Object *, rts::hash<ObjectID>, rts::equal_to<ObjectID> > ObjectPtrHash;
//typedef ObjectPtrHash::const_iterator ObjectPtrIter;

typedef std::vector<Object*> ObjectPtrVector;

// ------------------------------------------------------------------------------------------------
/**
 * The implementation of GameLogic
 */
class GameLogic : public SubsystemInterface, public Snapshot
{

public:

	GameLogic();
	virtual ~GameLogic();

	// subsystem methods
	virtual void init();															///< Initialize or re-initialize the instance
	virtual void reset();															///< Reset the logic system
	virtual void update();														///< update the world

	void preUpdate();

#if defined(RTS_DEBUG)
	Int getNumberSleepyUpdates() const {return m_sleepyUpdates.size();} //For profiling, so not in Release.
#endif
	void processCommandList( CommandList *list );		///< process the command list

	void prepareNewGame( GameMode gameMode, GameDifficulty diff, Int rankPoints );						///< prepare for new game

	void logicMessageDispatcher( GameMessage *msg,
																			 void *userData );	///< Logic command list processing

	void registerObject( Object *obj );							///< Given an object, register it with the GameLogic and give it a unique ID

	// Web port (round 5 addendum, kernels): a system registers a kernel and an apply step for a
	// GxUnitStore behaviour bit and an update phase (PHASE_INITIAL..PHASE_FINAL). Every frame, at the
	// start of that phase (before any of the phase's module updates, with or without updatePasses),
	// the slots of TheGxUnitStore.pass(bit) in object-ID order are handed to the kernel as one range
	// [0, n) (later: split over workers), then to apply on the main thread.
	// kernel: writes only its own units' rows; no allocation, events, RNG or other globals.
	// apply: the side effects (events, other objects, RNG), in object-ID order.
	typedef void (*GxKernelFn)( const Int *slots, Int begin, Int end );
	typedef void (*GxApplyFn)( const Int *slots, Int count );
	static void gxRegisterKernel( Int storeBit, Int phase, GxKernelFn kernel, GxApplyFn apply );
	// Both kernel hooks run only while the match rule updatePasses is on and kindKernelsOff is not
	// (decided by the host / the replay header; the kindKernels setting, fourth word bit 7, sets it). Per-kind batch hook: for an update-module kind (its
	// module name key), the updatePasses dispatch hands the kind's every-frame modules, in object-ID
	// order, to kernel(mods, 0, n) and then apply(mods, n), right before that kind's per-module
	// update loop. Only under updatePasses (the modules are in pass lists only then), so a module's
	// update() must still do its job when the kernel did not run.
	typedef void (*GxKindKernelFn)( UpdateModulePtr *mods, Int begin, Int end );
	typedef void (*GxKindApplyFn)( UpdateModulePtr *mods, Int count );
	static void gxRegisterKindKernel( NameKeyType kind, GxKindKernelFn kernel, GxKindApplyFn apply );
	// TRUE while match rule updatePasses is on and the "off" rule offRule (GXRULE_KIND_KERNELS_OFF,
	// GXRULE_CONTAIN_KERNELS_OFF) is not: kernels follow the match rules, so recordings replay either way
	static Bool gxKernelSwitch( Int offRule );
	void gxNoteDisabled( Object *obj );	///< web port: obj may now be disabled (switch disabledList)

	void addObjectToLookupTable( Object *obj );			///< add object ID to hash lookup table
	void removeObjectFromLookupTable( Object *obj );///< remove object ID from hash lookup table

	/// @todo Change this to refer to a Region3D as an extent of the world
	void setWidth( Real width );										///< Sets the width of the world
	Real getWidth();													///< Returns the width of the world
	void setHeight( Real height );									///< Sets the height of the world
	Real getHeight();													///< Returns the height of the world

	Bool isInGameLogicUpdate() const { return m_isInUpdate; }
	Bool hasUpdated() const { return m_hasUpdated; } ///< Returns true if the logic frame has advanced in the current client/render update
	UnsignedInt getFrame();										///< Returns the current simulation frame number
	UnsignedInt getCRC( Int mode = CRC_CACHED, AsciiString deepCRCFileName = AsciiString::TheEmptyString );		///< Returns the CRC

	void setObjectIDCounter( ObjectID nextObjID ) { m_nextObjID = nextObjID; }
	ObjectID getObjectIDCounter() { return m_nextObjID; }

	//-----------------------------------------------------------------------------------------------
	void setBuildableStatusOverride(const ThingTemplate* tt, BuildableStatus bs);
	Bool findBuildableStatusOverride(const ThingTemplate* tt, BuildableStatus& bs) const;

	void setControlBarOverride(const AsciiString& commandSetName, Int slot, ConstCommandButtonPtr commandButton);
	Bool findControlBarOverride(const AsciiString& commandSetName, Int slot, ConstCommandButtonPtr& commandButton) const;

	//-----------------------------------------------------------------------------------------------
	/// create an object given the thing template. (Only for use by ThingFactory.)
	Object *friend_createObject( const ThingTemplate *thing, const ObjectStatusMaskType &objectStatusMask, Team *team );
	void destroyObject( Object *obj );							///< Mark object as destroyed for later deletion
	Object *findObjectByID( ObjectID id );								///< Given an ObjectID, return a pointer to the object.
 	Object *getFirstObject();									///< Returns the "first" object in the world. When used with the object method "getNextObject()", all objects in the world can be iterated.
	ObjectID allocateObjectID();							///< Returns a new unique object id

	// super hack
	void startNewGame( Bool loadSaveGame );
	void loadMapINI( AsciiString mapName );

	void updateLoadProgress( Int progress );
	void deleteLoadScreen();

	//Kris: Cut setGameLoading() and replaced with setLoadingMap() and setLoadingSave() -- reason: nomenclature
	//void setGameLoading( Bool loading ) { m_loadingScene = loading; }
	void setLoadingMap( Bool loading ) { m_loadingMap = loading; }
	void setLoadingSave( Bool loading ) { m_loadingSave = loading; }
	void setClearingGameData( Bool clearing ) { m_clearingGameData = clearing; }

	void setGameMode( GameMode mode );
	GameMode getGameMode();

	Bool isInGame(); // Includes Shell Game
	Bool isInLanGame();
	Bool isInSinglePlayerGame();
	Bool isInSkirmishGame();
	Bool isInReplayGame();
	Bool isInInternetGame();
	Bool isInShellGame();
	Bool isInMultiplayerGame();
	Bool isInInteractiveGame() const;

	static Bool isInInteractiveGame(GameMode mode) { return mode != GAME_NONE && mode != GAME_SHELL; }

	//Kris: Cut isLoadingGame() and replaced with isLoadingMap() and isLoadingSave() -- reason: nomenclature
	//Bool isLoadingGame() const { return m_loadingScene; }		// This is the old function that isn't very clear on it's definition.
	Bool isLoadingMap() const { return m_loadingMap; }			// Whenever a map is in the process of loading.
	Bool isLoadingSave() const { return m_loadingSave; }		// Whenever a saved game is in the process of loading.
	Bool isClearingGameData() const { return m_clearingGameData; }

	void enableScoring(Bool score) { m_isScoringEnabled = score; }
	Bool isScoringEnabled() const { return m_isScoringEnabled; }

	void setShowBehindBuildingMarkers(Bool b) { m_showBehindBuildingMarkers = b; }
	Bool getShowBehindBuildingMarkers() const { return m_showBehindBuildingMarkers; }

	void setDrawIconUI(Bool b) { m_drawIconUI = b; }
	Bool getDrawIconUI() const { return m_drawIconUI; }

	void setShowDynamicLOD(Bool b) { m_showDynamicLOD = b; }
	Bool getShowDynamicLOD() const { return m_showDynamicLOD; }

	void setHulkMaxLifetimeOverride(Int b) { m_scriptHulkMaxLifetimeOverride = b; }
	Int getHulkMaxLifetimeOverride() const { return m_scriptHulkMaxLifetimeOverride; }

	Bool isIntroMoviePlaying();

	void updateObjectsChangedTriggerAreas() {m_frameObjectsChangedTriggerAreas = m_frame;}
	UnsignedInt getFrameObjectsChangedTriggerAreas() {return m_frameObjectsChangedTriggerAreas;}

	void exitGame();
	void clearGameData(Bool showScoreScreen = TRUE);														///< Clear the game data
	void closeWindows();

	void sendObjectCreated( Object *obj );
	void sendObjectDestroyed( Object *obj );

	void bindObjectAndDrawable(Object* obj, Drawable* draw);

	void setGamePausedInFrame( UnsignedInt frame, Bool disableLogicTimeScale );
	UnsignedInt getGamePauseFrame() const { return m_pauseFrame; }
	void setGamePaused( Bool paused, Bool pauseMusic = TRUE, Bool pauseInput = TRUE );
	Bool isGamePaused();
	Bool getInputEnabledMemory() const { return m_inputEnabledMemory; }

	void processProgress(Int playerId, Int percentage);
	void processProgressComplete(Int playerId);
	Bool isProgressComplete();
	void timeOutGameStart();
	void initTimeOutValues();
	UnsignedInt getObjectCount();

	Int getRankLevelLimit() const { return m_rankLevelLimit; }
	void setRankLevelLimit(Int limit)
	{
		if (limit < 1) limit = 1;
		m_rankLevelLimit = limit;
	}

	// We need to allow access to this, because on a restartGame, we need to restart with the settings we started with
	Int getRankPointsToAddAtGameStart() const { return m_rankPointsToAddAtGameStart; }

  UnsignedShort getSuperweaponRestriction() const; ///< Get any optional limits on superweapons
  void setSuperweaponRestriction();

#ifdef DUMP_PERF_STATS
	void getAIMetricsStatistics( UnsignedInt *numAI, UnsignedInt *numMoving, UnsignedInt *numAttacking, UnsignedInt *numWaitingForPath, UnsignedInt *overallFailedPathfinds );
	void resetOverallFailedPathfinds() { m_overallFailedPathfinds = 0; }
	void incrementOverallFailedPathfinds() { m_overallFailedPathfinds++; }
	UnsignedInt getOverallFailedPathfinds() const { return m_overallFailedPathfinds; }
#endif

	// NOTE: selectObject and deselectObject should be called *only* by logical things, NEVER by the
	// client. These will cause the client to select or deselect the object, if affectClient is true.
	// If createToSelection is TRUE, this object causes a new group to be selected.
	void selectObject(Object *obj, Bool createNewSelection, PlayerMaskType playerMask, Bool affectClient = FALSE);
	void deselectObject(Object *obj, PlayerMaskType playerMask, Bool affectClient = FALSE);

	// this should be called only by UpdateModule, thanks.
	void friend_awakenUpdateModule(Object* obj, UpdateModulePtr update, UnsignedInt whenToWakeUp);

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:

	void updateDisplayBusyState();

	void pauseGameLogic(Bool paused);
	void pauseGameSound(Bool paused);
	void pauseGameMusic(Bool paused);
	void pauseGameInput(Bool paused);

	void pushSleepyUpdate(UpdateModulePtr u);
	UpdateModulePtr peekSleepyUpdate() const;
	void popSleepyUpdate();
	void eraseSleepyUpdate(Int i);
	void rebalanceSleepyUpdate(Int i);
	Int rebalanceParentSleepyUpdate(Int i);
	Int rebalanceChildSleepyUpdate(Int i);
	void remakeSleepyUpdate();
	Int gx4UpSleepyUpdate(Int i);	///< web port (match rule heap4): 4-ary sift up by stored key
	Int gx4DownSleepyUpdate(Int i);	///< web port (match rule heap4): 4-ary sift down by stored key
	void gx4VerifySleepyUpdate();	///< verify mode: the 4-ary queue's order and indexes
	void validateSleepyUpdate() const;

	static void createOptimizedTree(const ThingTemplate *thingTemplate, Coord3D *pos, Real angle);

private:

	/**
		overrides to thing template buildable status. doesn't really belong here,
		but has to go somewhere. (srj)
	*/
	typedef std::hash_map< AsciiString, BuildableStatus, rts::hash<AsciiString>, rts::equal_to<AsciiString> > BuildableMap;
	BuildableMap m_thingTemplateBuildableOverrides;

	/**
		overrides to control bars. doesn't really belong here, but has to go somewhere. (srj)
	*/
	typedef std::hash_map< AsciiString, ConstCommandButtonPtr, rts::hash<AsciiString>, rts::equal_to<AsciiString> > ControlBarOverrideMap;
	ControlBarOverrideMap m_controlBarOverrides;

	Real m_width, m_height;																	///< Dimensions of the world
	UnsignedInt m_frame;																		///< Simulation frame number

	// CRC cache system -----------------------------------------------------------------------------
	UnsignedInt	m_CRC;																			///< Cache of previous CRC value
	std::map<Int, UnsignedInt> m_cachedCRCs;								///< CRCs we've seen this frame
	Bool m_shouldValidateCRCs;															///< Should we validate CRCs this frame?
	//-----------------------------------------------------------------------------------------------
	//Bool m_loadingScene;
	Bool m_loadingMap;
	Bool m_loadingSave;
	Bool m_clearingGameData;

	Bool m_isInUpdate;
	Bool m_hasUpdated;

	Int m_rankPointsToAddAtGameStart;

	Bool m_isScoringEnabled;
	Bool m_showBehindBuildingMarkers;	//used by designers to override the user setting for cinematics
	Bool m_drawIconUI;
	Bool m_showDynamicLOD;	//used by designers to override the user setting for cinematics
	Int m_scriptHulkMaxLifetimeOverride;	///< Scripts can change the lifetime of a hulk -- defaults to off (-1) in frames.

	/// @todo remove this hack
	Bool m_startNewGame;
	WindowLayout *m_background;

	Object* m_objList;																			///< All of the objects in the world.
//	ObjectPtrHash m_objHash;																///< Used for ObjectID lookups
	ObjectPtrVector m_objVector;

	// this is a vector, but is maintained as a priority queue.
	// never modify it directly; please use the proper access methods.
	// (for an excellent discussion of priority queues, please see:
	// http://dogma.net/markn/articles/pq_stl/priority.htm)
	std::vector<UpdateModulePtr> m_sleepyUpdates;
	// Web port (match rule 131072, updatePasses; third word bit 9): modules that ask to run every
	// frame leave the heap and run in one flat pass per update phase, grouped by behaviour (AI kinds
	// first) and then by object ID. An entry's module has index-in-logic -2 while it is in a pass.
	struct GxPassEntry { unsigned long long key; UpdateModulePtr u; };
	std::vector<GxPassEntry> m_gxPass[4];
	std::vector<GxPassEntry> m_gxPassJoin;
	std::vector<GxPassEntry> m_gxMergeBuf;	///< mergeBuf: gxPassMergeJoin's sort and merge buffer, kept across ticks
	void gxPassRemove(UpdateModulePtr u);
	void gxPassFlushToHeap();
	void gxPassMerge();
	void gxPassMergeJoin();
	// The dispatch table (round 4, same rule): one behaviour bit (GxUnitStore, bits 32-63) per
	// module kind that runs every frame. Its pass walks TheGxUnitStore.pass(bit) in object-ID order
	// and calls that kind's module of each unit (modById). Kind members have index-in-logic -4; a
	// second module of the same kind on one unit, or kinds past the 32nd, use the lists above (-2).
	struct GxKind { NameKeyType nk; unsigned long long order; Int bit; Int phase; Bool isAI; std::vector<UpdateModulePtr> modById; };
	std::vector<GxKind> m_gxKinds;
	std::vector<Int> m_gxKindOrder;	///< m_gxKinds indexes in dispatch order (AI kinds first, then name hash)
	std::vector<Int> m_gxKindByNk;	///< web port (switch passKeyArray): kind index by NameKey, -1 none
	Int gxKindOf(UpdateModulePtr u, Bool create);
	/// per phase, per object ID: the unit's every-frame modules without a kind bit of their own
	/// (kinds past the 28th, or a second module of one kind); behaviour bit 60 + phase. Index -5.
	std::vector<std::vector<UpdateModulePtr>> m_gxMiscById[4];
	void gxKindsClear(Bool toHeap);
	// Web port (switch disabledList, third word bit 8): the objects that may be disabled, so the
	// end-of-tick check visits them instead of walking every object. Order = object-list order
	// (descending m_gxListSeq). Rebuilt from the list after a reset or a load.
	std::vector<Object *> m_gxDisabledObjs;
	UnsignedInt m_gxListSeqNext = 0;
	Bool m_gxDisabledListValid = false;
	void gxCheckDisabledObjects();
	Bool m_gxHeap4 = FALSE;	///< web port (match rule heap4): the update queue is a 4-ary heap (switched by a rebuild before the first update of a game)
	std::vector<UnsignedInt> m_gxSleepyKey;	///< web port (switch sleepyKeys): each entry's priority, kept beside m_sleepyUpdates so the heap compares without touching the modules

#ifdef ALLOW_NONSLEEPY_UPDATES
	// this is a plain old list, not a pq.
	std::list<UpdateModulePtr> m_normalUpdates;
#endif

	UpdateModulePtr					 m_curUpdateModule;

	ObjectPointerList m_objectsToDestroy;										///< List of things that need to be destroyed at end of frame

	ObjectID m_nextObjID;																		///< For allocating object id's

	void processDestroyList();												///< Destroy all pending objects on the destroy list

	void destroyAllObjectsImmediate();											///< destroy, and process destroy list immediately

	/// factory for TheTerrainLogic, called from init()
	virtual TerrainLogic *createTerrainLogic();
	virtual GhostObjectManager *createGhostObjectManager();

	GameMode m_gameMode;
	Int m_rankLevelLimit;
  UnsignedShort m_superweaponRestriction;

	LoadScreen *getLoadScreen( Bool loadSaveGame );
	LoadScreen *m_loadScreen;

	UnsignedInt m_pauseFrame;
	Bool m_gamePaused;
	Bool m_pauseSound;
	Bool m_pauseMusic;
	Bool m_pauseInput;
	Bool m_inputEnabledMemory;// Latches used to remember what to restore to after we unpause
	Bool m_mouseVisibleMemory;
	Bool m_logicTimeScaleEnabledMemory;

	Bool m_progressComplete[MAX_SLOTS];
	enum { PROGRESS_COMPLETE_TIMEOUT = 60000 };							///< Timeout we wait for when we've completed our Load
	Int m_progressCompleteTimeout[MAX_SLOTS];
	void testTimeOut();
	void lastHeardFrom( Int playerId );
	Bool m_forceGameStartByTimeOut;													///< If we timeout someone we're waiting to load, set this flag to start the game

#ifdef DUMP_PERF_STATS
	UnsignedInt m_overallFailedPathfinds;
#endif

	UnsignedInt m_frameObjectsChangedTriggerAreas;					///< Last frame objects moved into/outof trigger areas, or were created/destroyed. jba.

	// ----------------------------------------------------------------------------------------------
	struct ObjectTOCEntry
	{
		AsciiString name;
		UnsignedShort id;
	};
	typedef std::list< ObjectTOCEntry > ObjectTOCList;
	typedef ObjectTOCList::iterator ObjectTOCListIterator;
	ObjectTOCList m_objectTOC;															///< the object TOC
	void addTOCEntry( AsciiString name, UnsignedShort id ); ///< add a new name/id TOC pair
	ObjectTOCEntry *findTOCEntryByName( AsciiString name );	///< find ObjectTOC by name
	ObjectTOCEntry *findTOCEntryById( UnsignedShort id );		///< find ObjectTOC by id
	void xferObjectTOC( Xfer *xfer );												///< save/load object TOC for current state of map
	void prepareLogicForObjectLoad();									///< prepare engine for object data from game file

};

// INLINE /////////////////////////////////////////////////////////////////////////////////////////
inline void GameLogic::setWidth( Real width ) { m_width = width; }
inline Real GameLogic::getWidth() { return m_width; }
inline void GameLogic::setHeight( Real height ) { m_height = height; }
inline Real GameLogic::getHeight() { return m_height; }
inline UnsignedInt GameLogic::getFrame() { return m_frame; }

inline Bool GameLogic::isInGame() { return m_gameMode != GAME_NONE; }
inline GameMode GameLogic::getGameMode() { return m_gameMode; }
inline Bool GameLogic::isInLanGame() { return (m_gameMode == GAME_LAN); }
inline Bool GameLogic::isInSkirmishGame() { return (m_gameMode == GAME_SKIRMISH); }
inline Bool GameLogic::isInMultiplayerGame() { return (m_gameMode == GAME_LAN) || (m_gameMode == GAME_INTERNET) ; }
inline Bool GameLogic::isInInteractiveGame() const { return isInInteractiveGame(m_gameMode); }
inline Bool GameLogic::isInReplayGame() { return (m_gameMode == GAME_REPLAY); }
inline Bool GameLogic::isInInternetGame() { return (m_gameMode == GAME_INTERNET); }
inline Bool GameLogic::isInShellGame() { return (m_gameMode == GAME_SHELL); }
inline UnsignedShort GameLogic::getSuperweaponRestriction() const { return m_superweaponRestriction; }

inline Object* GameLogic::findObjectByID( ObjectID id )
{
	if( id == INVALID_ID )
		return nullptr;

//	ObjectPtrHash::iterator it = m_objHash.find(id);
//	if (it == m_objHash.end())
//		return nullptr;
//
//	return (*it).second;
	if( (size_t)id < m_objVector.size() )
		return m_objVector[(size_t)id];

	return nullptr;
}



// the singleton
extern GameLogic *TheGameLogic;
