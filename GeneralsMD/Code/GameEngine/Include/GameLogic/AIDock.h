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

// AIDock.h
// Docking behavior
// Author: Michael S. Booth, February 2002

#pragma once

#include "Common/GameMemory.h"
#include "GameLogic/GxComponents.h"	// web port (round 5)
#include "GameLogic/AIStateMachine.h"

/**
 * The states of the Docking state machine.
 */
enum
{
	AI_DOCK_APPROACH,									///< given a queue pos, move to it
	AI_DOCK_WAIT_FOR_CLEARANCE,				///< wait for dock to give us enter clearance
	AI_DOCK_ADVANCE_POSITION,					///< Advance in approach position as line moves forward
	AI_DOCK_MOVE_TO_ENTRY,						///< move to the dock entrance
	AI_DOCK_MOVE_TO_DOCK,							///< move to the actual dock position
	AI_DOCK_PROCESS_DOCK,							///< invoke the dock's action until it is done
	AI_DOCK_MOVE_TO_EXIT,							///< move to the dock exit, can exit the dock machine
	AI_DOCK_MOVE_TO_RALLY						  ///< Move to rally if desired, exit the dock machine no matter what
};

//-----------------------------------------------------------------------------------------------------------
/**
 * The docking state machine.
 */
struct GxAIDockMachineRow;
class AIDockMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIDockMachine, "AIDockMachinePool" );

public:
	/**
	 * The implementation of this constructor defines the states
	 * used by this machine.
	 */
	AIDockMachine( Object *owner );

	static Bool ableToAdvance( State *thisState, void* userData ); // Condition for scooting forward in line while waiting
	virtual void halt(); ///< Stops the state machine & disables it in preparation for deleting it.

	struct GxAIDockMachineRow *m_gxAIDockMachineRow;	///< web port (round 5): this unit's row in TheGxAIDockMachines
	friend struct GxAIDockMachineRow;
	inline struct GxAIDockMachineRow &gxRow() const;

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

};

// Web port (round 5, units are pure data): AIDockMachine's per-unit state, one row per unit in TheGxAIDockMachines;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIDockMachineRow
{
	Int approachPosition;
};
extern GxComponentTable<GxAIDockMachineRow> TheGxAIDockMachines;


inline GxAIDockMachineRow &AIDockMachine::gxRow() const { return TheGxAIDockMachines.at(m_gxAIDockMachineRow); }


// Please do not use these states in some other machine.  I know that wouldn't even make sense, but they
// cast their getMachine to an AIDock machine to store stuff across states so you'd crash.
//-----------------------------------------------------------------------------------------------------------
class AIDockApproachState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIDockApproachState, "AIDockApproachState")
public:
	AIDockApproachState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIDockApproachState" ) { }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess(){};
};
EMPTY_DTOR(AIDockApproachState)

//-----------------------------------------------------------------------------------------------------------
struct GxAIDockWaitForClearanceStateRow;
class AIDockWaitForClearanceState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIDockWaitForClearanceState, "AIDockWaitForClearanceState")
public:
	AIDockWaitForClearanceState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	struct GxAIDockWaitForClearanceStateRow *m_gxAIDockWaitForClearanceStateRow;	///< web port (round 5): this unit's row in TheGxAIDockWaitForClearanceStates
	friend struct GxAIDockWaitForClearanceStateRow;
	inline struct GxAIDockWaitForClearanceStateRow &gxRow() const;
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess(){};
};

// Web port (round 5, units are pure data): AIDockWaitForClearanceState's per-unit state, one row per unit in TheGxAIDockWaitForClearanceStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIDockWaitForClearanceStateRow
{
	UnsignedInt enterFrame;
};
extern GxComponentTable<GxAIDockWaitForClearanceStateRow> TheGxAIDockWaitForClearanceStates;


inline GxAIDockWaitForClearanceStateRow &AIDockWaitForClearanceState::gxRow() const { return TheGxAIDockWaitForClearanceStates.at(m_gxAIDockWaitForClearanceStateRow); }

//-----------------------------------------------------------------------------------------------------------
class AIDockAdvancePositionState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIDockAdvancePositionState, "AIDockAdvancePositionState")
public:
	AIDockAdvancePositionState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIDockApproachState" ) { }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
};
EMPTY_DTOR(AIDockAdvancePositionState)

//-----------------------------------------------------------------------------------------------------------
class AIDockMoveToEntryState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIDockMoveToEntryState, "AIDockMoveToEntryState")
public:
	AIDockMoveToEntryState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIDockMoveToEntryState" ) { }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
};
EMPTY_DTOR(AIDockMoveToEntryState)

//-----------------------------------------------------------------------------------------------------------
class AIDockMoveToDockState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIDockMoveToDockState, "AIDockMoveToDockState")
public:
	AIDockMoveToDockState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIDockMoveToDockState" ) { }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
};
EMPTY_DTOR(AIDockMoveToDockState)

//-----------------------------------------------------------------------------------------------------------
class AIDockMoveToRallyState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIDockMoveToRallyState, "AIDockMoveToRallyState")
public:
	AIDockMoveToRallyState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIDockMoveToRallyState" ) { }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
};
EMPTY_DTOR(AIDockMoveToRallyState)

//-----------------------------------------------------------------------------------------------------------
struct GxAIDockProcessDockStateRow;
class AIDockProcessDockState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIDockProcessDockState, "AIDockProcessDockState")
public:
	AIDockProcessDockState( StateMachine *machine );
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();

	void setNextDockActionFrame();//This puts a delay between callings of Action to tweak the speed of docking.
	struct GxAIDockProcessDockStateRow *m_gxAIDockProcessDockStateRow;	///< web port (round 5): this unit's row in TheGxAIDockProcessDockStates
	friend struct GxAIDockProcessDockStateRow;
	inline struct GxAIDockProcessDockStateRow &gxRow() const;
	// complete a Action a few frames sooner than you would have: It does not need to be saved.
	Object* findMyDrone();

protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};

private:

};

// Web port (round 5, units are pure data): AIDockProcessDockState's per-unit state, one row per unit in TheGxAIDockProcessDockStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIDockProcessDockStateRow
{
	UnsignedInt nextDockActionFrame;
	ObjectID droneID;
};
extern GxComponentTable<GxAIDockProcessDockStateRow> TheGxAIDockProcessDockStates;


inline GxAIDockProcessDockStateRow &AIDockProcessDockState::gxRow() const { return TheGxAIDockProcessDockStates.at(m_gxAIDockProcessDockStateRow); }

//-----------------------------------------------------------------------------------------------------------
class AIDockMoveToExitState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIDockMoveToExitState, "AIDockMoveToExitState")
public:
	AIDockMoveToExitState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIDockMoveToExitState" ) { }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
};
EMPTY_DTOR(AIDockMoveToExitState)

//-----------------------------------------------------------------------------------------------------------
