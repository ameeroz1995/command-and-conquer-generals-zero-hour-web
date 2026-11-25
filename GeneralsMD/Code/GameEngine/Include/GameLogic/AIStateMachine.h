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

// AIStateMachine.h
// Finite state machine encapsulation
// Author: Michael S. Booth, January 2002

#pragma once

#include "Lib/BaseType.h"
#include "GameLogic/GxComponents.h"	// web port (round 5)

#include "Common/AudioEventRTS.h"
#include "Common/GameMemory.h"
#include "Common/StateMachine.h"

#include "GameLogic/TerrainLogic.h"

// FORWARD DECLARATIONS ///////////////////////////////////////////////////////////////////////////
class AIGuardMachine;
class AIGuardRetaliateMachine;
class AITNGuardMachine;
class Weapon;
class Team;
class AIAttackState;
class AttackStateMachine;
class AIGroup;
class Squad;

//-----------------------------------------------------------------------------------------------------------


/**
 * The AI state IDs.
 * Each of these constants will be associated with an instance of a State class
 * in a given StateMachine.
 */
enum AIStateType CPP_11(: Int)
{
	AI_IDLE,
	AI_MOVE_TO,																///< move to the GoalObject or GoalPosition
	AI_FOLLOW_WAYPOINT_PATH_AS_TEAM,					///< follow a waypoint path as a team
	AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS,		///< follow a waypoint path
	AI_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT,		///< follow a waypoint path as a team
	AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS_EXACT,///< follow a waypoint path
	AI_FOLLOW_PATH,														///< follow a simple list of points
	AI_FOLLOW_EXITPRODUCTION_PATH,						///< the same, but only when exiting production facility
	AI_WAIT,
	AI_ATTACK_POSITION,												///< attempt to kill GoalPosition
	AI_ATTACK_OBJECT,													///< attempt to kill GoalObject
	AI_FORCE_ATTACK_OBJECT,										///< attempt to kill GoalObject, force fire on it.
	AI_ATTACK_AND_FOLLOW_OBJECT,							///< attempt to kill GoalObject, following it if necessary (and possible)
	AI_DEAD,
	AI_DOCK,																	///< dock with GoalObject, if GoalObject has a DockUpdate module
	AI_ENTER,																	///< move to GoalObject and "enter" it when close
	AI_GUARD,																	///< guard your current location
	AI_HUNT,																	///< seek and destroy behavior
	AI_WANDER,																///< Wander around following a waypoint path.
	AI_PANIC,																	///< Run around screaming following a waypoint path.
	AI_ATTACK_SQUAD,													///< Set the unit to attempt to kill all objects in goalSquad.
	AI_GUARD_TUNNEL_NETWORK,									///< Guard from inside a tunnel network.
	AI_GET_REPAIRED,													///< Get repaired at a repair depot
	AI_MOVE_OUT_OF_THE_WAY,										///< Move out of the way of another unit.
	AI_MOVE_AND_TIGHTEN,											///< Move in order to tighten up a formation.
	AI_MOVE_AND_EVACUATE,											///< Move to, then empty transport.
	AI_MOVE_AND_EVACUATE_AND_EXIT,						///< Move to, then empty transport.
	AI_MOVE_AND_DELETE,												///< Move to, then delete self.
	AI_ATTACK_AREA,														///< Attack units in an area.
	AI_HACK_INTERNET,													///< Hack internet for free money (no target required).
	AI_ATTACK_MOVE_TO,												///< Attack-move to a location
	AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS,	///< Attack-Follow down a waypoint path as individuals
	AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM,				///< Attack-Follow down a waypoint path as a team
	AI_FACE_OBJECT,
	AI_FACE_POSITION,
	AI_RAPPEL_INTO,														/**< rappel from current pos down to target object.
																								if target is building, will enter and kill lots of folks.
																								if target is null, will rappel to ground. */
	AI_COMBATDROP,														/**< attempt to send AI_RAPPEL_INTO to contents. */
	AI_EXIT,																	///< exit the obj, waiting if necessary
	AI_PICK_UP_CRATE,													///< Pick up a crate created by a kill.  jba.
	AI_MOVE_AWAY_FROM_REPULSORS,							///< Civilians are running away from repulsors. (enemies or dead civs, usually) jba
	AI_WANDER_IN_PLACE,												///< Civilians just wander around a spot, rather than along a path.
	AI_BUSY,																	///< This is a state that things will be in when they are busy doing random stuff that doesn't require AI interaction.
	AI_EXIT_INSTANTLY,												///< exit this obj, without waiting -- do it in the onEnter! This frame!
	AI_GUARD_RETALIATE,												///< attacks attacker but with restrictions (hybrid of attack and guard).
};

//-----------------------------------------------------------------------------------------------------------
// generically state transition conditions
extern Bool outOfWeaponRangeObject( State *thisState, void* userData );
extern Bool outOfWeaponRangePosition( State *thisState, void* userData );
extern Bool wantToSquishTarget( State *thisState, void* userData );

//-----------------------------------------------------------------------------------------------------------
/**
  The AI state machine.  This is used by AIUpdate to implement all of the
  commands in the AICommandInterface.

	NOTE NOTE NOTE NOTE NOTE

	Do NOT subclass this unless you want ALL of the states this machine possesses.
	If you only want SOME of the states, please make a new StateMachine, descended
	from StateMachine, NOT AIStateMachine. Thank you. (srj)

	NOTE NOTE NOTE NOTE NOTE

 */
class AIStateMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIStateMachine, "AIStateMachine" );

public:
	/**
	 * The implementation of this constructor defines the states
	 * used by this machine.
	 */
	AIStateMachine( Object *owner, AsciiString name );

	virtual void clear();
	virtual StateReturnType resetToDefaultState();
	virtual StateReturnType setState( StateID newStateID );

	/// @todo Rethink state parameter passing. Continuing in this fashion will have a pile of params in the machine (MSB)
	void setGoalPath( std::vector<Coord3D>* path );
	void addToGoalPath( const Coord3D *pathPoint );
	const Coord3D *getGoalPathPosition( Int i ) const;		///< return path position at index "i"
	Int getGoalPathSize() const { return m_goalPath.size(); }


	void setGoalWaypoint( const Waypoint *way );		///< move toward this waypoint, continue if connected
	const Waypoint *getGoalWaypoint();

	// All of these wind up storing a Squad, as it is the only one that can always be safely created.
	void setGoalTeam( const Team *team );
	void setGoalSquad( const Squad *squad );
	void setGoalAIGroup( const AIGroup *group );

	Squad *getGoalSquad();

	StateReturnType setTemporaryState( StateID newStateID, Int frameLimitCoount );			///< change the temporary state of the machine, and number of frames limit.
	StateID getTemporaryState() const {return m_temporaryState?m_temporaryState->getID():INVALID_STATE_ID;}

public:	// overrides.
	virtual StateReturnType updateStateMachine();				///< run one step of the machine
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getCurrentStateName() const ;
#endif

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	std::vector<Coord3D>	m_goalPath;					///< defines a simple path to follow
	const Waypoint *			m_goalWaypoint;
	Squad *								m_goalSquad;

	/** A temporary state to run for a while (usually AI_MOVE_OUT_OF_THE_WAY).
	Doesn't clear or reset the state machine, so it goes back to doing what it was doing.  jba. */
	State									*m_temporaryState;
	UnsignedInt						m_temporaryStateFramEnd; ///< Last frame to run m_temporaryState.
};


//-----------------------------------------------------------------------------------------------------------
class AttackStateMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AttackStateMachine, "AttackStateMachine" );

public:
// Attack states.
enum StateType CPP_11(: Int)
	{
		CHASE_TARGET,													///< Chase a moving target (optionally following it)
		APPROACH_TARGET,											///< Approach a non-moving target.
		AIM_AT_TARGET,												///< rotate to face GoalObject or GoalPosition
		FIRE_WEAPON,													///< fire the machine owner's current weapon
	};
	AttackStateMachine( Object *owner, AIAttackState* att, AsciiString name, Bool follow, Bool attackingObject, Bool forceAttacking );

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

//-----------------------------------------------------------------------------------------------------------
struct GxAIIdleStateRow;
class AIIdleState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIIdleState, "AIIdleState")
private:

	struct GxAIIdleStateRow *m_gxAIIdleStateRow;	///< web port (round 5): this unit's row in TheGxAIIdleStates
	friend struct GxAIIdleStateRow;
	inline struct GxAIIdleStateRow &gxRow() const;

	void doInitIdleState();

public:
	enum AIIdleTargetingType
	{
		LOOK_FOR_TARGETS,
		DO_NOT_LOOK_FOR_TARGETS
	};
	virtual Bool isIdle() const { return true; }
	AIIdleState( StateMachine *machine, AIIdleTargetingType shouldLookForTargets);
	virtual StateReturnType onEnter();
	virtual StateReturnType update();

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

};

// Web port (round 5, units are pure data): AIIdleState's per-unit state, one row per unit in TheGxAIIdleStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIIdleStateRow
{
	UnsignedShort initialSleepOffset;
	Bool shouldLookForTargets;
	Bool inited;
};
extern GxComponentTable<GxAIIdleStateRow> TheGxAIIdleStates;


inline GxAIIdleStateRow &AIIdleState::gxRow() const { return TheGxAIIdleStates.at(m_gxAIIdleStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Basic pathfinding and moving to a goal position.
 * Not for direct use (hence no associated state ID), but for deriving from.
 */
struct GxAIInternalMoveToStateRow;
class AIInternalMoveToState : public State
{
	MEMORY_POOL_GLUE_ABC(AIInternalMoveToState)
public:
	AIInternalMoveToState( StateMachine *machine, AsciiString name );	///< web port (round 5): defined in the .cpp


	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();

protected:

	void setAdjustsDestination(Bool b);
	Bool getAdjustsDestination() const;

	void setGoalPos(const Coord3D* pos);
	const Coord3D* getGoalPos() const;

	virtual Bool computePath();												///< compute the path

	void forceRepath();

private:
	void startMoveSound();

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	enum { MIN_REPATH_TIME = 10 };										///< minimum # of frames must elapse before re-pathing
protected:
	struct GxAIInternalMoveToStateRow *m_gxAIInternalMoveToStateRow;	///< web port (round 5): this unit's row in TheGxAIInternalMoveToStates
	friend struct GxAIInternalMoveToStateRow;
	inline struct GxAIInternalMoveToStateRow &gxRow() const;
	// web port (round 5): names for the two row fields that subclasses in other files (JetAIUpdate.cpp)
	// still use directly; they refer to this unit's row, they hold no data of their own
	Coord3D &m_goalPosition;
	Bool &m_waitingForPath;
private:
																										//   occasionally false for things like car bombs.
protected:
};

// Web port (round 5, units are pure data): AIInternalMoveToState's per-unit state, one row per unit in TheGxAIInternalMoveToStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIInternalMoveToStateRow
{
	Coord3D goalPosition;
	PathfindLayerEnum goalLayer;
	Coord3D pathGoalPosition;
	AudioHandle ambientPlayingHandle;
	UnsignedInt pathTimestamp;
	UnsignedInt blockedRepathTimestamp;
	Bool adjustDestinations;
	Bool waitingForPath;
	Bool tryOneMoreRepath;
};
extern GxComponentTable<GxAIInternalMoveToStateRow> TheGxAIInternalMoveToStates;


inline GxAIInternalMoveToStateRow &AIInternalMoveToState::gxRow() const { return TheGxAIInternalMoveToStates.at(m_gxAIInternalMoveToStateRow); }
inline void AIInternalMoveToState::setAdjustsDestination(Bool b)
{ gxRow().adjustDestinations = b; }
inline void AIInternalMoveToState::setGoalPos(const Coord3D* pos)
{ gxRow().goalPosition = *pos; }
inline const Coord3D* AIInternalMoveToState::getGoalPos() const
{ return &gxRow().goalPosition; }
inline void AIInternalMoveToState::forceRepath()
{
		gxRow().pathGoalPosition.x = gxRow().pathGoalPosition.y = gxRow().pathGoalPosition.z = -100.0f;
		gxRow().pathTimestamp = -MIN_REPATH_TIME;
	}

//-----------------------------------------------------------------------------------------------------------
/**
 * Move to the GoalPosition, or GoalObject.
 */
struct GxAIRappelStateRow;
class AIRappelState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIRappelState, "AIRappelState")
protected:
	struct GxAIRappelStateRow *m_gxAIRappelStateRow;	///< web port (round 5): this unit's row in TheGxAIRappelStates
	friend struct GxAIRappelStateRow;
	inline struct GxAIRappelStateRow &gxRow() const;
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

public:
	AIRappelState( StateMachine *machine );
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
};

// Web port (round 5, units are pure data): AIRappelState's per-unit state, one row per unit in TheGxAIRappelStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIRappelStateRow
{
	Real rappelRate;
	Real destZ;
	Bool targetIsBldg;
};
extern GxComponentTable<GxAIRappelStateRow> TheGxAIRappelStates;


inline GxAIRappelStateRow &AIRappelState::gxRow() const { return TheGxAIRappelStates.at(m_gxAIRappelStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Spin. Busy-like
 */
class AIBusyState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIBusyState, "AIBusyState")
public:
	AIBusyState( StateMachine *machine ) : State( machine, "AIBusyState" ) { }
	virtual StateReturnType onEnter() { return STATE_CONTINUE; }
	virtual void onExit( StateExitType status ) { }
	virtual StateReturnType update() { return STATE_CONTINUE; }
	virtual Bool isBusy() const { return true; }
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};

};
EMPTY_DTOR(AIBusyState)

//-----------------------------------------------------------------------------------------------------------
/**
 * Move to the GoalPosition, or GoalObject.
 */
struct GxAIMoveToStateRow;
class AIMoveToState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIMoveToState, "AIMoveToState")
protected:
	struct GxAIMoveToStateRow *m_gxAIMoveToStateRow;	///< web port (round 5): this unit's row in TheGxAIMoveToStates
	friend struct GxAIMoveToStateRow;
	inline struct GxAIMoveToStateRow &gxRow() const;
public:
	AIMoveToState( StateMachine *machine );
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
};

// Web port (round 5, units are pure data): AIMoveToState's per-unit state, one row per unit in TheGxAIMoveToStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIMoveToStateRow
{
	Bool isMoveTo;
};
extern GxComponentTable<GxAIMoveToStateRow> TheGxAIMoveToStates;


inline GxAIMoveToStateRow &AIMoveToState::gxRow() const { return TheGxAIMoveToStates.at(m_gxAIMoveToStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Moves out of the way of another object.
 */
class AIMoveOutOfTheWayState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIMoveOutOfTheWayState, "AIMoveOutOfTheWayState")
public:
	AIMoveOutOfTheWayState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIMoveOutOfTheWayState" ) { }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	virtual Bool computePath();												///< compute the path

};
EMPTY_DTOR(AIMoveOutOfTheWayState)

//-----------------------------------------------------------------------------------------------------------
/**
 * Moves toward goal pos to tighen up a formation.
 */
struct GxAIMoveAndTightenStateRow;
class AIMoveAndTightenState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIMoveAndTightenState, "AIMoveAndTightenState")
public:
	AIMoveAndTightenState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	AIMoveAndTightenState( StateMachine *machine, const char *name );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
protected:
	virtual Bool computePath();												///< compute the path
	struct GxAIMoveAndTightenStateRow *m_gxAIMoveAndTightenStateRow;	///< web port (round 5): this unit's row in TheGxAIMoveAndTightenStates
	friend struct GxAIMoveAndTightenStateRow;
	inline struct GxAIMoveAndTightenStateRow &gxRow() const;
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

// Web port (round 5, units are pure data): AIMoveAndTightenState's per-unit state, one row per unit in TheGxAIMoveAndTightenStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIMoveAndTightenStateRow
{
	Int okToRepathTimes;
	Bool checkForPath;
};
extern GxComponentTable<GxAIMoveAndTightenStateRow> TheGxAIMoveAndTightenStates;


inline GxAIMoveAndTightenStateRow &AIMoveAndTightenState::gxRow() const { return TheGxAIMoveAndTightenStates.at(m_gxAIMoveAndTightenStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Moves toward goal pos to get away from an enemy.
 */
class AIMoveAwayFromRepulsorsState : public AIMoveAndTightenState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIMoveAwayFromRepulsorsState, "AIMoveAwayFromRepulsorsState")
public:
	AIMoveAwayFromRepulsorsState( StateMachine *machine ) : AIMoveAndTightenState( machine, "AIMoveAwayFromRepulsors" ) { }
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	virtual Bool computePath();												///< compute the path

};
EMPTY_DTOR(AIMoveAwayFromRepulsorsState)

//-----------------------------------------------------------------------------------------------------------
/**
 * Compute a valid spot to fire on GoalObject, and move there.
 */
struct GxAIAttackApproachTargetStateRow;
class AIAttackApproachTargetState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIAttackApproachTargetState, "AIAttackApproachTargetState")
public:
	AIAttackApproachTargetState( StateMachine *machine, Bool follow, Bool attackingObject, Bool forceAttacking );	///< web port (round 5): defined in the .cpp

	virtual Bool isAttack() const { return TRUE; }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	virtual Bool computePath();												///< compute the path

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:

	enum { MIN_RECOMPUTE_TIME = 10 };

	StateReturnType updateInternal();

	struct GxAIAttackApproachTargetStateRow *m_gxAIAttackApproachTargetStateRow;	///< web port (round 5): this unit's row in TheGxAIAttackApproachTargetStates
	friend struct GxAIAttackApproachTargetStateRow;
	inline struct GxAIAttackApproachTargetStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIAttackApproachTargetState's per-unit state, one row per unit in TheGxAIAttackApproachTargetStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIAttackApproachTargetStateRow
{
	Coord3D prevVictimPos;
	UnsignedInt approachTimestamp;
	Bool follow;
	Bool isAttackingObject;
	Bool stopIfInRange;
	Bool isInitialApproach;
	Bool isForceAttacking;
};
extern GxComponentTable<GxAIAttackApproachTargetStateRow> TheGxAIAttackApproachTargetStates;


inline GxAIAttackApproachTargetStateRow &AIAttackApproachTargetState::gxRow() const { return TheGxAIAttackApproachTargetStates.at(m_gxAIAttackApproachTargetStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Chases a moving GoalObject.  If goal is not object, or not moving, or attacker lacks turret, exits with success.
 */
struct GxAIAttackPursueTargetStateRow;
class AIAttackPursueTargetState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIAttackPursueTargetState, "AIAttackPursueTargetState")
public:
	AIAttackPursueTargetState( StateMachine *machine, Bool follow, Bool attackingObject, Bool forceAttacking );	///< web port (round 5): defined in the .cpp

	virtual Bool isAttack() const { return TRUE; }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	virtual Bool computePath();												///< compute the path

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:

	enum { MIN_RECOMPUTE_TIME = 10 };

	StateReturnType updateInternal();

	struct GxAIAttackPursueTargetStateRow *m_gxAIAttackPursueTargetStateRow;	///< web port (round 5): this unit's row in TheGxAIAttackPursueTargetStates
	friend struct GxAIAttackPursueTargetStateRow;
	inline struct GxAIAttackPursueTargetStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIAttackPursueTargetState's per-unit state, one row per unit in TheGxAIAttackPursueTargetStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIAttackPursueTargetStateRow
{
	Coord3D prevVictimPos;
	UnsignedInt approachTimestamp;
	Bool follow;
	Bool isAttackingObject;
	Bool stopIfInRange;
	Bool isInitialApproach;
	Bool isForceAttacking;
};
extern GxComponentTable<GxAIAttackPursueTargetStateRow> TheGxAIAttackPursueTargetStates;


inline GxAIAttackPursueTargetStateRow &AIAttackPursueTargetState::gxRow() const { return TheGxAIAttackPursueTargetStates.at(m_gxAIAttackPursueTargetStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Pick up an upgrade crate after a kill.
 */
struct GxAIPickUpCrateStateRow;
class AIPickUpCrateState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIPickUpCrateState, "AIPickUpCrateState")
public:
	AIPickUpCrateState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	virtual Bool computePath();												///< compute the path

private:
	struct GxAIPickUpCrateStateRow *m_gxAIPickUpCrateStateRow;	///< web port (round 5): this unit's row in TheGxAIPickUpCrateStates
	friend struct GxAIPickUpCrateStateRow;
	inline struct GxAIPickUpCrateStateRow &gxRow() const;
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

// Web port (round 5, units are pure data): AIPickUpCrateState's per-unit state, one row per unit in TheGxAIPickUpCrateStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIPickUpCrateStateRow
{
	Int delayCounter;
};
extern GxComponentTable<GxAIPickUpCrateStateRow> TheGxAIPickUpCrateStates;


inline GxAIPickUpCrateStateRow &AIPickUpCrateState::gxRow() const { return TheGxAIPickUpCrateStates.at(m_gxAIPickUpCrateStateRow); }

//-------------------------------------------------------------------------------------------------
/**
 * Execute an Attack-Move
 */
 class AIAttackMoveToState : public AIMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIAttackMoveToState, "AIAttackMoveToState")
public:
	AIAttackMoveToState( StateMachine *machine );
	//virtual ~AIAttackMoveToState();

	virtual Bool isAttack() const { return m_attackMoveMachine ? m_attackMoveMachine->isInAttackState() : FALSE; }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif

protected:
	enum {ATTACK_RETRY_COUNT=5};
	enum {ATTACK_CLOSE_ENOUGH_CELLS=8};
	CommandSourceType	m_commandSrc;		// Original command source.  We switch to CMD_FROM_AI when auto-acquiring.
	StateMachine *m_attackMoveMachine;
	UnsignedInt		m_frameToSleepUntil;
	Int						m_retryCount;
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

//-----------------------------------------------------------------------------------------------------------
/**
 * Follow a waypoint path
 */
struct GxAIFollowWaypointPathStateRow;
class AIFollowWaypointPathState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIFollowWaypointPathState, "AIFollowWaypointPathState")
public:
	AIFollowWaypointPathState( StateMachine *machine, Bool asGroup );	///< web port (round 5): defined in the .cpp

	AIFollowWaypointPathState( StateMachine *machine, Bool asGroup, Bool isFollow);	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

protected:
	struct GxAIFollowWaypointPathStateRow *m_gxAIFollowWaypointPathStateRow;	///< web port (round 5): this unit's row in TheGxAIFollowWaypointPathStates
	friend struct GxAIFollowWaypointPathStateRow;
	inline struct GxAIFollowWaypointPathStateRow &gxRow() const;

	// this is necessary because we derive from FollowWaypointPathState, but we do not want to incur the
	// expense of checking RTTI to determine whether we are actually a FollowWaypointPathState or not

protected:
	void computeGoal(Bool useGroupOffsets);
	Real calcExtraPathDistance();
	const Waypoint *getNextWaypoint();
	Bool hasNextWaypoint();
};

// Web port (round 5, units are pure data): AIFollowWaypointPathState's per-unit state, one row per unit in TheGxAIFollowWaypointPathStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIFollowWaypointPathStateRow
{
	Coord2D groupOffset;
	Real angle;
	Int framesSleeping;
	const Waypoint* currentWaypoint;
	const Waypoint* priorWaypoint;
	Bool appendGoalPosition;
	Bool moveAsGroup;
	Bool isFollowWaypointPathState;
};
extern GxComponentTable<GxAIFollowWaypointPathStateRow> TheGxAIFollowWaypointPathStates;


inline GxAIFollowWaypointPathStateRow &AIFollowWaypointPathState::gxRow() const { return TheGxAIFollowWaypointPathStates.at(m_gxAIFollowWaypointPathStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Follow a waypoint path exactly
 */
struct GxAIFollowWaypointPathExactStateRow;
class AIFollowWaypointPathExactState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIFollowWaypointPathExactState, "AIFollowWaypointPathExactState")
public:
	AIFollowWaypointPathExactState( StateMachine *machine, Bool asGroup );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

protected:
	struct GxAIFollowWaypointPathExactStateRow *m_gxAIFollowWaypointPathExactStateRow;	///< web port (round 5): this unit's row in TheGxAIFollowWaypointPathExactStates
	friend struct GxAIFollowWaypointPathExactStateRow;
	inline struct GxAIFollowWaypointPathExactStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIFollowWaypointPathExactState's per-unit state, one row per unit in TheGxAIFollowWaypointPathExactStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIFollowWaypointPathExactStateRow
{
	const Waypoint* lastWaypoint;
	Bool moveAsGroup;
};
extern GxComponentTable<GxAIFollowWaypointPathExactStateRow> TheGxAIFollowWaypointPathExactStates;


inline GxAIFollowWaypointPathExactStateRow &AIFollowWaypointPathExactState::gxRow() const { return TheGxAIFollowWaypointPathExactStates.at(m_gxAIFollowWaypointPathExactStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Follow a waypoint path, attacking targets encountered along the way
 */
struct GxAIAttackFollowWaypointPathStateRow;
class AIAttackFollowWaypointPathState : public AIFollowWaypointPathState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIAttackFollowWaypointPathState, "AIAttackFollowWaypointPathState")
public:
	AIAttackFollowWaypointPathState( StateMachine *machine, Bool asGroup );
	//virtual ~AIAttackFollowWaypointPathState();

	virtual Bool isAttack() const;

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif

protected:
	// We can (and want to) use the attack-move-to logic for when to attack and when to do a normal attack.
	struct GxAIAttackFollowWaypointPathStateRow *m_gxAIAttackFollowWaypointPathStateRow;	///< web port (round 5): this unit's row in TheGxAIAttackFollowWaypointPathStates
	friend struct GxAIAttackFollowWaypointPathStateRow;
	inline struct GxAIAttackFollowWaypointPathStateRow &gxRow() const;
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

// Web port (round 5, units are pure data): AIAttackFollowWaypointPathState's per-unit state, one row per unit in TheGxAIAttackFollowWaypointPathStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIAttackFollowWaypointPathStateRow
{
	StateMachine* attackFollowMachine;
};
extern GxComponentTable<GxAIAttackFollowWaypointPathStateRow> TheGxAIAttackFollowWaypointPathStates;


inline GxAIAttackFollowWaypointPathStateRow &AIAttackFollowWaypointPathState::gxRow() const { return TheGxAIAttackFollowWaypointPathStates.at(m_gxAIAttackFollowWaypointPathStateRow); }
inline Bool AIAttackFollowWaypointPathState::isAttack() const
{ return gxRow().attackFollowMachine ? gxRow().attackFollowMachine->isInAttackState() : FALSE; }


//-----------------------------------------------------------------------------------------------------------
/**
 * Wanders around following a waypoint path.
 */
struct GxAIWanderStateRow;
class AIWanderState : public AIFollowWaypointPathState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIWanderState, "AIWanderState")
public:
	AIWanderState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

protected:
	struct GxAIWanderStateRow *m_gxAIWanderStateRow;	///< web port (round 5): this unit's row in TheGxAIWanderStates
	friend struct GxAIWanderStateRow;
	inline struct GxAIWanderStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIWanderState's per-unit state, one row per unit in TheGxAIWanderStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIWanderStateRow
{
	Int waitFrames;
	Int timer;
};
extern GxComponentTable<GxAIWanderStateRow> TheGxAIWanderStates;


inline GxAIWanderStateRow &AIWanderState::gxRow() const { return TheGxAIWanderStates.at(m_gxAIWanderStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Wanders around a point.
 */
struct GxAIWanderInPlaceStateRow;
class AIWanderInPlaceState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIWanderInPlaceState, "AIWanderInPlaceState")
public:
	AIWanderInPlaceState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

protected:
	void computeWanderGoal();
	struct GxAIWanderInPlaceStateRow *m_gxAIWanderInPlaceStateRow;	///< web port (round 5): this unit's row in TheGxAIWanderInPlaceStates
	friend struct GxAIWanderInPlaceStateRow;
	inline struct GxAIWanderInPlaceStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIWanderInPlaceState's per-unit state, one row per unit in TheGxAIWanderInPlaceStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIWanderInPlaceStateRow
{
	Coord3D origin;
	Int waitFrames;
	Int timer;
};
extern GxComponentTable<GxAIWanderInPlaceStateRow> TheGxAIWanderInPlaceStates;


inline GxAIWanderInPlaceStateRow &AIWanderInPlaceState::gxRow() const { return TheGxAIWanderInPlaceStates.at(m_gxAIWanderInPlaceStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Runs around screaming following a waypoint path.
 */
struct GxAIPanicStateRow;
class AIPanicState : public AIFollowWaypointPathState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIPanicState, "AIPanicState")
public:
	AIPanicState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

protected:
	struct GxAIPanicStateRow *m_gxAIPanicStateRow;	///< web port (round 5): this unit's row in TheGxAIPanicStates
	friend struct GxAIPanicStateRow;
	inline struct GxAIPanicStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIPanicState's per-unit state, one row per unit in TheGxAIPanicStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIPanicStateRow
{
	Int waitFrames;
	Int timer;
};
extern GxComponentTable<GxAIPanicStateRow> TheGxAIPanicStates;


inline GxAIPanicStateRow &AIPanicState::gxRow() const { return TheGxAIPanicStates.at(m_gxAIPanicStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Follow simple list of points.
 */
struct GxAIFollowPathStateRow;
class AIFollowPathState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIFollowPathState, "AIFollowPathState")
public:
	AIFollowPathState( StateMachine *machine, AsciiString name = "AIFollowPathState" );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();

protected:

	Int getCurPathIndex() const;
	void setAdjustFinalDestination(Bool b);
	void setAdjustFinalDestinationEvenIfNotDoingGroundMovement(Bool b);

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	struct GxAIFollowPathStateRow *m_gxAIFollowPathStateRow;	///< web port (round 5): this unit's row in TheGxAIFollowPathStates
	friend struct GxAIFollowPathStateRow;
	inline struct GxAIFollowPathStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIFollowPathState's per-unit state, one row per unit in TheGxAIFollowPathStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIFollowPathStateRow
{
	Int index;
	Bool adjustFinal;
	Bool adjustFinalOverride;
	Int retryCount;
};
extern GxComponentTable<GxAIFollowPathStateRow> TheGxAIFollowPathStates;


inline GxAIFollowPathStateRow &AIFollowPathState::gxRow() const { return TheGxAIFollowPathStates.at(m_gxAIFollowPathStateRow); }
inline Int AIFollowPathState::getCurPathIndex() const
{ return gxRow().index; }
inline void AIFollowPathState::setAdjustFinalDestination(Bool b)
{ gxRow().adjustFinal = b; }
inline void AIFollowPathState::setAdjustFinalDestinationEvenIfNotDoingGroundMovement(Bool b)
{ gxRow().adjustFinalOverride = b; }

//-----------------------------------------------------------------------------------------------------------
/**
 * Move to and unload transport.
 */
struct GxAIMoveAndEvacuateStateRow;
class AIMoveAndEvacuateState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIMoveAndEvacuateState, "AIMoveAndEvacuateState")
public:
	AIMoveAndEvacuateState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	struct GxAIMoveAndEvacuateStateRow *m_gxAIMoveAndEvacuateStateRow;	///< web port (round 5): this unit's row in TheGxAIMoveAndEvacuateStates
	friend struct GxAIMoveAndEvacuateStateRow;
	inline struct GxAIMoveAndEvacuateStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIMoveAndEvacuateState's per-unit state, one row per unit in TheGxAIMoveAndEvacuateStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIMoveAndEvacuateStateRow
{
	Coord3D origin;
};
extern GxComponentTable<GxAIMoveAndEvacuateStateRow> TheGxAIMoveAndEvacuateStates;


inline GxAIMoveAndEvacuateStateRow &AIMoveAndEvacuateState::gxRow() const { return TheGxAIMoveAndEvacuateStates.at(m_gxAIMoveAndEvacuateStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Move to and delete self.  Used for removing reinforcement transports.
 */
struct GxAIMoveAndDeleteStateRow;
class AIMoveAndDeleteState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIMoveAndDeleteState, "AIMoveAndDeleteState")
public:
	AIMoveAndDeleteState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

protected:
	struct GxAIMoveAndDeleteStateRow *m_gxAIMoveAndDeleteStateRow;	///< web port (round 5): this unit's row in TheGxAIMoveAndDeleteStates
	friend struct GxAIMoveAndDeleteStateRow;
	inline struct GxAIMoveAndDeleteStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIMoveAndDeleteState's per-unit state, one row per unit in TheGxAIMoveAndDeleteStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIMoveAndDeleteStateRow
{
	Bool appendGoalPosition;
};
extern GxComponentTable<GxAIMoveAndDeleteStateRow> TheGxAIMoveAndDeleteStates;


inline GxAIMoveAndDeleteStateRow &AIMoveAndDeleteState::gxRow() const { return TheGxAIMoveAndDeleteStates.at(m_gxAIMoveAndDeleteStateRow); }

//-----------------------------------------------------------------------------------------------------------
struct GxAIAttackAimAtTargetStateRow;
class AIAttackAimAtTargetState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIAttackAimAtTargetState, "AIAttackAimAtTargetState")
public:
	AIAttackAimAtTargetState( StateMachine *machine, Bool attackingObject, Bool forceAttacking );	///< web port (round 5): defined in the .cpp

	virtual Bool isAttack() const { return TRUE; }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	struct GxAIAttackAimAtTargetStateRow *m_gxAIAttackAimAtTargetStateRow;	///< web port (round 5): this unit's row in TheGxAIAttackAimAtTargetStates
	friend struct GxAIAttackAimAtTargetStateRow;
	inline struct GxAIAttackAimAtTargetStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIAttackAimAtTargetState's per-unit state, one row per unit in TheGxAIAttackAimAtTargetStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIAttackAimAtTargetStateRow
{
	Bool isAttackingObject;
	Bool canTurnInPlace;
	Bool setLocomotor;
	Bool isForceAttacking;
};
extern GxComponentTable<GxAIAttackAimAtTargetStateRow> TheGxAIAttackAimAtTargetStates;


inline GxAIAttackAimAtTargetStateRow &AIAttackAimAtTargetState::gxRow() const { return TheGxAIAttackAimAtTargetStates.at(m_gxAIAttackAimAtTargetStateRow); }

//-----------------------------------------------------------------------------------------------------------
class AIWaitState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIWaitState, "AIWaitState")
public:
	AIWaitState( StateMachine *machine ) : State( machine,"AIWaitState" ) { }
	virtual StateReturnType update();
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
};
EMPTY_DTOR(AIWaitState)

//-----------------------------------------------------------------------------------------------------------
class NotifyWeaponFiredInterface	// an ABC
{
public:
	virtual void notifyFired() = 0;
	virtual void notifyNewVictimChosen(Object* victim) = 0;
	virtual Bool isWeaponSlotOkToFire(WeaponSlotType wslot) const = 0;
	virtual Bool isAttackingObject() const = 0;
	virtual const Coord3D* getOriginalVictimPos() const = 0;
};

//-----------------------------------------------------------------------------------------------------------
class AIAttackFireWeaponState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIAttackFireWeaponState, "AIAttackFireWeaponState")
public:
	AIAttackFireWeaponState( StateMachine *machine, NotifyWeaponFiredInterface* att ) :
		State( machine, "AIAttackFireWeaponState" ),
		m_att(att)
	{
	}
	virtual Bool isAttack() const { return TRUE; }
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
	virtual StateReturnType onEnter();
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
private:
	NotifyWeaponFiredInterface *const m_att;		// this is NOT owned by us and should not be freed
};
EMPTY_DTOR(AIAttackFireWeaponState)

//-----------------------------------------------------------------------------------------------------------
class AttackExitConditionsInterface
{
public:
	virtual Bool shouldExit(const StateMachine* machine) const = 0;
};

//-----------------------------------------------------------------------------------------------------------
struct GxAIAttackStateRow;
class AIAttackState : public State, public NotifyWeaponFiredInterface
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIAttackState, "AIAttackState")
public:

	AIAttackState( StateMachine *machine, Bool follow, Bool attackingObject, Bool forceAttacking, AttackExitConditionsInterface* attackParameters);
	//~AIAttackState();

	virtual Bool isAttack() const { return TRUE; }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();

	virtual void notifyFired() { /* nothing */ }
	virtual void notifyNewVictimChosen(Object* victim);
	virtual const Coord3D* getOriginalVictimPos() const;
	virtual Bool isWeaponSlotOkToFire(WeaponSlotType wslot) const { return true; }
	virtual Bool isAttackingObject() const;
	virtual Bool isForceAttacking() const;
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:

	Bool chooseWeapon();

	struct GxAIAttackStateRow *m_gxAIAttackStateRow;	///< web port (round 5): this unit's row in TheGxAIAttackStates
	friend struct GxAIAttackStateRow;
	inline struct GxAIAttackStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIAttackState's per-unit state, one row per unit in TheGxAIAttackStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIAttackStateRow
{
	AttackStateMachine* attackMachine;
	AttackExitConditionsInterface* attackParameters;
	Team* victimTeam;
	Coord3D originalVictimPos;
	const Weapon* lockedWeaponOnEnter;
	Bool follow;
	Bool isAttackingObject;
	Bool isForceAttacking;
};
extern GxComponentTable<GxAIAttackStateRow> TheGxAIAttackStates;


inline GxAIAttackStateRow &AIAttackState::gxRow() const { return TheGxAIAttackStates.at(m_gxAIAttackStateRow); }
inline const Coord3D* AIAttackState::getOriginalVictimPos() const
{ return &gxRow().originalVictimPos; }
inline Bool AIAttackState::isAttackingObject() const
{ return gxRow().isAttackingObject; }
inline Bool AIAttackState::isForceAttacking() const
{ return gxRow().isForceAttacking; }


//-----------------------------------------------------------------------------------------------------------
struct GxAIAttackSquadStateRow;
class AIAttackSquadState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIAttackSquadState, "AIAttackSquadState")
public:
	AIAttackSquadState( StateMachine *machine, AttackExitConditionsInterface *attackParameters = nullptr);	///< web port (round 5): defined in the .cpp

	//~AIAttackSquadState();

	virtual Bool isAttack() const;
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
	Object *chooseVictim();
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif


protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	struct GxAIAttackSquadStateRow *m_gxAIAttackSquadStateRow;	///< web port (round 5): this unit's row in TheGxAIAttackSquadStates
	friend struct GxAIAttackSquadStateRow;
	inline struct GxAIAttackSquadStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIAttackSquadState's per-unit state, one row per unit in TheGxAIAttackSquadStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIAttackSquadStateRow
{
	StateMachine* attackSquadMachine;
};
extern GxComponentTable<GxAIAttackSquadStateRow> TheGxAIAttackSquadStates;


inline GxAIAttackSquadStateRow &AIAttackSquadState::gxRow() const { return TheGxAIAttackSquadStates.at(m_gxAIAttackSquadStateRow); }
inline Bool AIAttackSquadState::isAttack() const
{ return gxRow().attackSquadMachine ? gxRow().attackSquadMachine->isInAttackState() : FALSE; }


//-----------------------------------------------------------------------------------------------------------
class AIDeadState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIDeadState, "AIDeadState")
public:
	AIDeadState( StateMachine *machine ) : State( machine, "AIDeadState" ) { }
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
};
EMPTY_DTOR(AIDeadState)

//-----------------------------------------------------------------------------------------------------------
struct GxAIDockStateRow;
class AIDockState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIDockState, "AIDockState")
public:
	AIDockState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	//~AIDockState();
	virtual Bool isAttack() const;
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	struct GxAIDockStateRow *m_gxAIDockStateRow;	///< web port (round 5): this unit's row in TheGxAIDockStates
	friend struct GxAIDockStateRow;
	inline struct GxAIDockStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIDockState's per-unit state, one row per unit in TheGxAIDockStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIDockStateRow
{
	StateMachine* dockMachine;
	Bool usingPrecisionMovement;
};
extern GxComponentTable<GxAIDockStateRow> TheGxAIDockStates;


inline GxAIDockStateRow &AIDockState::gxRow() const { return TheGxAIDockStates.at(m_gxAIDockStateRow); }
inline Bool AIDockState::isAttack() const
{ return gxRow().dockMachine ? gxRow().dockMachine->isInAttackState() : FALSE; }

//-----------------------------------------------------------------------------------------------------------
/**
 * Move close to GoalObject and enter it.
 */
struct GxAIEnterStateRow;
class AIEnterState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIEnterState, "AIEnterState")
protected:
	struct GxAIEnterStateRow *m_gxAIEnterStateRow;	///< web port (round 5): this unit's row in TheGxAIEnterStates
	friend struct GxAIEnterStateRow;
	inline struct GxAIEnterStateRow &gxRow() const;
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
public:
	AIEnterState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
};

// Web port (round 5, units are pure data): AIEnterState's per-unit state, one row per unit in TheGxAIEnterStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIEnterStateRow
{
	ObjectID entryToClear;
};
extern GxComponentTable<GxAIEnterStateRow> TheGxAIEnterStates;


inline GxAIEnterStateRow &AIEnterState::gxRow() const { return TheGxAIEnterStates.at(m_gxAIEnterStateRow); }

//-----------------------------------------------------------------------------------------------------------
struct GxAIExitStateRow;
class AIExitState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIExitState, "AIExitState")
protected:
	struct GxAIExitStateRow *m_gxAIExitStateRow;	///< web port (round 5): this unit's row in TheGxAIExitStates
	friend struct GxAIExitStateRow;
	inline struct GxAIExitStateRow &gxRow() const;
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
public:
	AIExitState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
};

// Web port (round 5, units are pure data): AIExitState's per-unit state, one row per unit in TheGxAIExitStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIExitStateRow
{
	ObjectID entryToClear;
};
extern GxComponentTable<GxAIExitStateRow> TheGxAIExitStates;


inline GxAIExitStateRow &AIExitState::gxRow() const { return TheGxAIExitStates.at(m_gxAIExitStateRow); }

//-----------------------------------------------------------------------------------------------------------
struct GxAIExitInstantlyStateRow;
class AIExitInstantlyState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIExitInstantlyState, "AIExitInstantlyState")
protected:
	struct GxAIExitInstantlyStateRow *m_gxAIExitInstantlyStateRow;	///< web port (round 5): this unit's row in TheGxAIExitInstantlyStates
	friend struct GxAIExitInstantlyStateRow;
	inline struct GxAIExitInstantlyStateRow &gxRow() const;
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
public:
	AIExitInstantlyState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
};

// Web port (round 5, units are pure data): AIExitInstantlyState's per-unit state, one row per unit in TheGxAIExitInstantlyStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIExitInstantlyStateRow
{
	ObjectID entryToClear;
};
extern GxComponentTable<GxAIExitInstantlyStateRow> TheGxAIExitInstantlyStates;


inline GxAIExitInstantlyStateRow &AIExitInstantlyState::gxRow() const { return TheGxAIExitInstantlyStates.at(m_gxAIExitInstantlyStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Guard location
 */
struct GxAIGuardStateRow;
class AIGuardState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardState, "AIGuardState")
public:
	AIGuardState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	//~AIGuardState();
	virtual Bool isAttack() const;
	virtual Bool isGuardIdle() const;
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	struct GxAIGuardStateRow *m_gxAIGuardStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardStates
	friend struct GxAIGuardStateRow;
	inline struct GxAIGuardStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardState's per-unit state, one row per unit in TheGxAIGuardStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardStateRow
{
	AIGuardMachine* guardMachine;
};
extern GxComponentTable<GxAIGuardStateRow> TheGxAIGuardStates;


inline GxAIGuardStateRow &AIGuardState::gxRow() const { return TheGxAIGuardStates.at(m_gxAIGuardStateRow); }


//-----------------------------------------------------------------------------------------------------------
/**
 * Guard retaliate against object (hybrid attack / guard)
 */
struct GxAIGuardRetaliateStateRow;
class AIGuardRetaliateState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardRetaliateState, "AIGuardRetaliateState")
public:
	AIGuardRetaliateState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	//~AIGuardRetaliateState();
	virtual Bool isAttack() const;
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	struct GxAIGuardRetaliateStateRow *m_gxAIGuardRetaliateStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardRetaliateStates
	friend struct GxAIGuardRetaliateStateRow;
	inline struct GxAIGuardRetaliateStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardRetaliateState's per-unit state, one row per unit in TheGxAIGuardRetaliateStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardRetaliateStateRow
{
	AIGuardRetaliateMachine* guardRetaliateMachine;
};
extern GxComponentTable<GxAIGuardRetaliateStateRow> TheGxAIGuardRetaliateStates;


inline GxAIGuardRetaliateStateRow &AIGuardRetaliateState::gxRow() const { return TheGxAIGuardRetaliateStates.at(m_gxAIGuardRetaliateStateRow); }


//-----------------------------------------------------------------------------------------------------------
/**
 * Guard from inside a tunnel network.
 */
struct GxAITunnelNetworkGuardStateRow;
class AITunnelNetworkGuardState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AITunnelNetworkGuardState, "AITunnelNetworkGuardState")
public:
	AITunnelNetworkGuardState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	//~AIGuardState();
	virtual Bool isAttack() const;
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	struct GxAITunnelNetworkGuardStateRow *m_gxAITunnelNetworkGuardStateRow;	///< web port (round 5): this unit's row in TheGxAITunnelNetworkGuardStates
	friend struct GxAITunnelNetworkGuardStateRow;
	inline struct GxAITunnelNetworkGuardStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AITunnelNetworkGuardState's per-unit state, one row per unit in TheGxAITunnelNetworkGuardStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAITunnelNetworkGuardStateRow
{
	AITNGuardMachine* guardMachine;
};
extern GxComponentTable<GxAITunnelNetworkGuardStateRow> TheGxAITunnelNetworkGuardStates;


inline GxAITunnelNetworkGuardStateRow &AITunnelNetworkGuardState::gxRow() const { return TheGxAITunnelNetworkGuardStates.at(m_gxAITunnelNetworkGuardStateRow); }


//-----------------------------------------------------------------------------------------------------------
/**
 * Seek and destroy behavior
 */
struct GxAIHuntStateRow;
class AIHuntState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIHuntState, "AIHuntState")
public:
	AIHuntState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	//~AIHuntState();
	virtual Bool isAttack() const;
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	struct GxAIHuntStateRow *m_gxAIHuntStateRow;	///< web port (round 5): this unit's row in TheGxAIHuntStates
	friend struct GxAIHuntStateRow;
	inline struct GxAIHuntStateRow &gxRow() const;
	enum { ENEMY_SCAN_RATE = LOGICFRAMES_PER_SECOND };
};

// Web port (round 5, units are pure data): AIHuntState's per-unit state, one row per unit in TheGxAIHuntStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIHuntStateRow
{
	StateMachine* huntMachine;
	UnsignedInt nextEnemyScanTime;
};
extern GxComponentTable<GxAIHuntStateRow> TheGxAIHuntStates;


inline GxAIHuntStateRow &AIHuntState::gxRow() const { return TheGxAIHuntStates.at(m_gxAIHuntStateRow); }


//-----------------------------------------------------------------------------------------------------------
/**
 * Seek and destroy in area
 */
struct GxAIAttackAreaStateRow;
class AIAttackAreaState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIAttackAreaState, "AIAttackAreaState")
public:
	AIAttackAreaState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	//~AIAttackAreaState();
	virtual Bool isAttack() const;
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:
	struct GxAIAttackAreaStateRow *m_gxAIAttackAreaStateRow;	///< web port (round 5): this unit's row in TheGxAIAttackAreaStates
	friend struct GxAIAttackAreaStateRow;
	inline struct GxAIAttackAreaStateRow &gxRow() const;
	enum { ENEMY_SCAN_RATE = LOGICFRAMES_PER_SECOND };
};

// Web port (round 5, units are pure data): AIAttackAreaState's per-unit state, one row per unit in TheGxAIAttackAreaStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIAttackAreaStateRow
{
	StateMachine* attackMachine;
	UnsignedInt nextEnemyScanTime;
};
extern GxComponentTable<GxAIAttackAreaStateRow> TheGxAIAttackAreaStates;


inline GxAIAttackAreaStateRow &AIAttackAreaState::gxRow() const { return TheGxAIAttackAreaStates.at(m_gxAIAttackAreaStateRow); }
inline Bool AIAttackAreaState::isAttack() const
{ return gxRow().attackMachine ? gxRow().attackMachine->isInAttackState() : FALSE; }


//-----------------------------------------------------------------------------------------------------------
// Faces a position or an object (depending on goal)
//-----------------------------------------------------------------------------------------------------------
struct GxAIFaceStateRow;
class AIFaceState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIFaceState, "AIFaceState")
public:
	AIFaceState( StateMachine *machine, Bool obj );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	// snapshot interface .
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

protected:
	struct GxAIFaceStateRow *m_gxAIFaceStateRow;	///< web port (round 5): this unit's row in TheGxAIFaceStates
	friend struct GxAIFaceStateRow;
	inline struct GxAIFaceStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIFaceState's per-unit state, one row per unit in TheGxAIFaceStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIFaceStateRow
{
	Bool obj;
	Bool canTurnInPlace;
};
extern GxComponentTable<GxAIFaceStateRow> TheGxAIFaceStates;


inline GxAIFaceStateRow &AIFaceState::gxRow() const { return TheGxAIFaceStates.at(m_gxAIFaceStateRow); }

