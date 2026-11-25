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

// FILE: AIGuardRetaliate.h
/*---------------------------------------------------------------------------*/
/* Electronic Arts Los Angeles                                               */
/* Confidential Information	                                                 */
/* Copyright (C) 2003 - All Rights Reserved                                  */
/* DO NOT DISTRIBUTE                                                         */
/*---------------------------------------------------------------------------*/
/* Project:    RTS3                                                          */
/* File name:  AIGuardRetaliate.h                                            */
/* Created:    Kris Morness July 2003                                        */
/* Desc:       // Define Guard Retaliation states for AI                     */
/*---------------------------------------------------------------------------*/

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////
#include "Common/GameMemory.h"
#include "GameLogic/GxComponents.h"	// web port (round 5)
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/AI.h"

// DEFINES ////////////////////////////////////////////////////////////////////
// TYPE DEFINES ///////////////////////////////////////////////////////////////
enum
{
	// prevent collisions with other states that we might use, (namely AI_IDLE)
	AI_GUARD_RETALIATE_INNER = 5000,					///< Attack anything within this area till death
	AI_GUARD_RETALIATE_IDLE,									///< Wait till something shows up to attack.
	AI_GUARD_RETALIATE_OUTER,									///< Attack anything within this area that has been aggressive, until the timer expires
	AI_GUARD_RETALIATE_RETURN,								///< Restore to a position within the inner circle
	AI_GUARD_RETALIATE_GET_CRATE,							///< Pick up a crate from an enemy we killed.
	AI_GUARD_RETALIATE_ATTACK_AGGRESSOR,			///< Attack something that attacked me (that I can attack)
};

//--------------------------------------------------------------------------------------
struct GxGuardRetaliateExitConditionsRow;
class GuardRetaliateExitConditions : public AttackExitConditionsInterface
{
public:
	virtual ~GuardRetaliateExitConditions();	///< web port (round 5): releases the row

public:

	enum ExitConditionsEnum
	{
		ATTACK_ExitIfOutsideRadius		= 0x01,
		ATTACK_ExitIfExpiredDuration	= 0x02,
		ATTACK_ExitIfNoUnitFound			= 0x04
	};

	struct GxGuardRetaliateExitConditionsRow *m_gxGuardRetaliateExitConditionsRow;	///< web port (round 5): this unit's row in TheGxGuardRetaliateExitConditionss
	friend struct GxGuardRetaliateExitConditionsRow;
	inline struct GxGuardRetaliateExitConditionsRow &gxRow() const;

	GuardRetaliateExitConditions();	///< web port (round 5): defined in the .cpp


	virtual Bool shouldExit(const StateMachine* machine) const;
};

// Web port (round 5, units are pure data): GuardRetaliateExitConditions's per-unit state, one row per unit in TheGxGuardRetaliateExitConditionss;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxGuardRetaliateExitConditionsRow
{
	Int conditionsToConsider;
	Coord3D center;
	Real radiusSqr;
	UnsignedInt attackGiveUpFrame;
};
extern GxComponentTable<GxGuardRetaliateExitConditionsRow> TheGxGuardRetaliateExitConditionss;


inline GxGuardRetaliateExitConditionsRow &GuardRetaliateExitConditions::gxRow() const { return TheGxGuardRetaliateExitConditionss.at(m_gxGuardRetaliateExitConditionsRow); }



// FORWARD DECLARATIONS ///////////////////////////////////////////////////////

//--------------------------------------------------------------------------------------
struct GxAIGuardRetaliateMachineRow;
class AIGuardRetaliateMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIGuardRetaliateMachine, "AIGuardRetaliateMachinePool" );

private:
	struct GxAIGuardRetaliateMachineRow *m_gxAIGuardRetaliateMachineRow;	///< web port (round 5): this unit's row in TheGxAIGuardRetaliateMachines
	friend struct GxAIGuardRetaliateMachineRow;
	inline struct GxAIGuardRetaliateMachineRow &gxRow() const;

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

public:
	/**
	 * The implementation of this constructor defines the states
	 * used by this machine.
	 */
	AIGuardRetaliateMachine( Object *owner );

	virtual Bool isIdle() const;

	const Coord3D * getPositionToGuard() const;
	void setTargetPositionToGuard( const Coord3D *pos);

	void setNemesisID(ObjectID id);
	ObjectID getNemesisID() const;

	Bool lookForInnerTarget();

	static Real getStdGuardRange(const Object* obj);
};

// Web port (round 5, units are pure data): AIGuardRetaliateMachine's per-unit state, one row per unit in TheGxAIGuardRetaliateMachines;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardRetaliateMachineRow
{
	Coord3D positionToGuard;
	ObjectID nemesisToAttack;
};
extern GxComponentTable<GxAIGuardRetaliateMachineRow> TheGxAIGuardRetaliateMachines;


inline GxAIGuardRetaliateMachineRow &AIGuardRetaliateMachine::gxRow() const { return TheGxAIGuardRetaliateMachines.at(m_gxAIGuardRetaliateMachineRow); }
inline const Coord3D * AIGuardRetaliateMachine::getPositionToGuard() const
{ return &gxRow().positionToGuard; }
inline void AIGuardRetaliateMachine::setTargetPositionToGuard( const Coord3D *pos)
{ gxRow().positionToGuard = *pos; }
inline void AIGuardRetaliateMachine::setNemesisID(ObjectID id)
{ gxRow().nemesisToAttack = id; }
inline ObjectID AIGuardRetaliateMachine::getNemesisID() const
{ return gxRow().nemesisToAttack; }


//--------------------------------------------------------------------------------------
struct GxAIGuardRetaliateInnerStateRow;
class AIGuardRetaliateInnerState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardRetaliateInnerState, "AIGuardRetaliateInnerState")
public:
	AIGuardRetaliateInnerState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AIGuardRetaliateMachine* getGuardMachine() { return (AIGuardRetaliateMachine*)getMachine(); }

	struct GxAIGuardRetaliateInnerStateRow *m_gxAIGuardRetaliateInnerStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardRetaliateInnerStates
	friend struct GxAIGuardRetaliateInnerStateRow;
	inline struct GxAIGuardRetaliateInnerStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardRetaliateInnerState's per-unit state, one row per unit in TheGxAIGuardRetaliateInnerStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardRetaliateInnerStateRow
{
	GuardRetaliateExitConditions exitConditions;
	AIAttackState* attackState;
	AIEnterState* enterState;
};
extern GxComponentTable<GxAIGuardRetaliateInnerStateRow> TheGxAIGuardRetaliateInnerStates;


inline GxAIGuardRetaliateInnerStateRow &AIGuardRetaliateInnerState::gxRow() const { return TheGxAIGuardRetaliateInnerStates.at(m_gxAIGuardRetaliateInnerStateRow); }


//--------------------------------------------------------------------------------------
struct GxAIGuardRetaliateIdleStateRow;
class AIGuardRetaliateIdleState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardRetaliateIdleState, "AIGuardRetaliateIdleState")
public:
	AIGuardRetaliateIdleState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AIGuardRetaliateMachine* getGuardMachine() { return (AIGuardRetaliateMachine*)getMachine(); }

	struct GxAIGuardRetaliateIdleStateRow *m_gxAIGuardRetaliateIdleStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardRetaliateIdleStates
	friend struct GxAIGuardRetaliateIdleStateRow;
	inline struct GxAIGuardRetaliateIdleStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardRetaliateIdleState's per-unit state, one row per unit in TheGxAIGuardRetaliateIdleStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardRetaliateIdleStateRow
{
	UnsignedInt nextEnemyScanTime;
	Coord3D guardeePos;
};
extern GxComponentTable<GxAIGuardRetaliateIdleStateRow> TheGxAIGuardRetaliateIdleStates;


inline GxAIGuardRetaliateIdleStateRow &AIGuardRetaliateIdleState::gxRow() const { return TheGxAIGuardRetaliateIdleStates.at(m_gxAIGuardRetaliateIdleStateRow); }

//--------------------------------------------------------------------------------------
struct GxAIGuardRetaliateOuterStateRow;
class AIGuardRetaliateOuterState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardRetaliateOuterState, "AIGuardRetaliateOuterState")
public:
	AIGuardRetaliateOuterState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AIGuardRetaliateMachine* getGuardMachine() { return (AIGuardRetaliateMachine*)getMachine(); }

	struct GxAIGuardRetaliateOuterStateRow *m_gxAIGuardRetaliateOuterStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardRetaliateOuterStates
	friend struct GxAIGuardRetaliateOuterStateRow;
	inline struct GxAIGuardRetaliateOuterStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardRetaliateOuterState's per-unit state, one row per unit in TheGxAIGuardRetaliateOuterStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardRetaliateOuterStateRow
{
	GuardRetaliateExitConditions exitConditions;
	AIAttackState* attackState;
};
extern GxComponentTable<GxAIGuardRetaliateOuterStateRow> TheGxAIGuardRetaliateOuterStates;


inline GxAIGuardRetaliateOuterStateRow &AIGuardRetaliateOuterState::gxRow() const { return TheGxAIGuardRetaliateOuterStates.at(m_gxAIGuardRetaliateOuterStateRow); }


//--------------------------------------------------------------------------------------
struct GxAIGuardRetaliateReturnStateRow;
class AIGuardRetaliateReturnState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardRetaliateReturnState, "AIGuardRetaliateReturnState")
private:
	AIGuardRetaliateMachine* getGuardMachine() { return (AIGuardRetaliateMachine*)getMachine(); }
public:
	AIGuardRetaliateReturnState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	struct GxAIGuardRetaliateReturnStateRow *m_gxAIGuardRetaliateReturnStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardRetaliateReturnStates
	friend struct GxAIGuardRetaliateReturnStateRow;
	inline struct GxAIGuardRetaliateReturnStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardRetaliateReturnState's per-unit state, one row per unit in TheGxAIGuardRetaliateReturnStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardRetaliateReturnStateRow
{
	UnsignedInt nextReturnScanTime;
};
extern GxComponentTable<GxAIGuardRetaliateReturnStateRow> TheGxAIGuardRetaliateReturnStates;


inline GxAIGuardRetaliateReturnStateRow &AIGuardRetaliateReturnState::gxRow() const { return TheGxAIGuardRetaliateReturnStates.at(m_gxAIGuardRetaliateReturnStateRow); }

//--------------------------------------------------------------------------------------
class AIGuardRetaliatePickUpCrateState : public AIPickUpCrateState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardRetaliatePickUpCrateState, "AIGuardRetaliatePickUpCrateState")
public:
	AIGuardRetaliatePickUpCrateState( StateMachine *machine );
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
};
EMPTY_DTOR(AIGuardRetaliatePickUpCrateState)

//--------------------------------------------------------------------------------------
struct GxAIGuardRetaliateAttackAggressorStateRow;
class AIGuardRetaliateAttackAggressorState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardRetaliateAttackAggressorState, "AIGuardRetaliateAttackAggressorState")
public:
	AIGuardRetaliateAttackAggressorState( StateMachine *machine );
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const ;
#endif
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AIGuardRetaliateMachine* getGuardMachine() { return (AIGuardRetaliateMachine*)getMachine(); }
	struct GxAIGuardRetaliateAttackAggressorStateRow *m_gxAIGuardRetaliateAttackAggressorStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardRetaliateAttackAggressorStates
	friend struct GxAIGuardRetaliateAttackAggressorStateRow;
	inline struct GxAIGuardRetaliateAttackAggressorStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardRetaliateAttackAggressorState's per-unit state, one row per unit in TheGxAIGuardRetaliateAttackAggressorStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardRetaliateAttackAggressorStateRow
{
	GuardRetaliateExitConditions exitConditions;
	AIAttackState* attackState;
};
extern GxComponentTable<GxAIGuardRetaliateAttackAggressorStateRow> TheGxAIGuardRetaliateAttackAggressorStates;


inline GxAIGuardRetaliateAttackAggressorStateRow &AIGuardRetaliateAttackAggressorState::gxRow() const { return TheGxAIGuardRetaliateAttackAggressorStates.at(m_gxAIGuardRetaliateAttackAggressorStateRow); }


//--------------------------------------------------------------------------------------
