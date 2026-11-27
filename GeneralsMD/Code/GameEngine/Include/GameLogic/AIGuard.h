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

// FILE: AIGuard.h
/*---------------------------------------------------------------------------*/
/* EA Pacific                                                                */
/* Confidential Information	                                                 */
/* Copyright (C) 2001 - All Rights Reserved                                  */
/* DO NOT DISTRIBUTE                                                         */
/*---------------------------------------------------------------------------*/
/* Project:    RTS3                                                          */
/* File name:  AIGuard.h                                                     */
/* Created:    John K. McDonald, Jr., 3/29/2002                              */
/* Desc:       // Define Guard states for AI                                 */
/* Revision History:                                                         */
/*		3/29/2002 : Initial creation                                           */
/*---------------------------------------------------------------------------*/

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////
#include "Common/GameMemory.h"
#include "Common/GxComponentPool.h"	// web port (round 5)
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/AI.h"

// DEFINES ////////////////////////////////////////////////////////////////////
// TYPE DEFINES ///////////////////////////////////////////////////////////////
enum
{
	// prevent collisions with other states that we might use, (namely AI_IDLE)
	AI_GUARD_INNER = 5000,					///< Attack anything within this area till death
	AI_GUARD_IDLE,									///< Wait till something shows up to attack.
	AI_GUARD_OUTER,									///< Attack anything within this area that has been aggressive, until the timer expires
	AI_GUARD_RETURN,								///< Restore to a position within the inner circle
	AI_GUARD_GET_CRATE,							///< Pick up a crate from an enemy we killed.
	AI_GUARD_ATTACK_AGGRESSOR,			///< Attack something that attacked me (that I can attack)
};

//--------------------------------------------------------------------------------------
struct GxExitConditionsRow;
class ExitConditions : public AttackExitConditionsInterface
{
public:
	virtual ~ExitConditions();	///< web port (round 5): releases the row

public:

	enum ExitConditionsEnum
	{
		ATTACK_ExitIfOutsideRadius		= 0x01,
		ATTACK_ExitIfExpiredDuration	= 0x02,
		ATTACK_ExitIfNoUnitFound			= 0x04
	};

	GxRow<struct GxExitConditionsRow> m_gxExitConditionsRow;	///< web port (round 5): this unit's row in TheGxExitConditionss
	friend struct GxExitConditionsRow;
	inline struct GxExitConditionsRow &gxRow() const;

	ExitConditions();	///< web port (round 5): defined in the .cpp


	virtual Bool shouldExit(const StateMachine* machine) const;
};

// Web port (round 5, units are pure data): ExitConditions's per-unit state, one row per unit in TheGxExitConditionss;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxExitConditionsRow
{
	Int conditionsToConsider;
	Coord3D center;
	Real radiusSqr;
	UnsignedInt attackGiveUpFrame;
};


inline GxExitConditionsRow &ExitConditions::gxRow() const { return const_cast<GxExitConditionsRow &>(*m_gxExitConditionsRow); }



// FORWARD DECLARATIONS ///////////////////////////////////////////////////////

//--------------------------------------------------------------------------------------
struct GxAIGuardMachineRow;
class AIGuardMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIGuardMachine, "AIGuardMachinePool" );

private:
	GxRow<struct GxAIGuardMachineRow> m_gxAIGuardMachineRow;	///< web port (round 5): this unit's row in TheGxAIGuardMachines
	friend struct GxAIGuardMachineRow;
	inline struct GxAIGuardMachineRow &gxRow() const;

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
	AIGuardMachine( Object *owner );
	Object* findTargetToGuardByID();
	void setTargetToGuard( const Object *object );

	const Coord3D * getPositionToGuard() const;
	void setTargetPositionToGuard( const Coord3D *pos);

	const PolygonTrigger * getAreaToGuard() const;
	void setAreaToGuard( const PolygonTrigger *area);

	void setNemesisID(ObjectID id);
	ObjectID getNemesisID() const;

	GuardMode getGuardMode() const;
	void setGuardMode(GuardMode guardMode);

	Bool lookForInnerTarget();

	static Real getStdGuardRange(const Object* obj);
};

// Web port (round 5, units are pure data): AIGuardMachine's per-unit state, one row per unit in TheGxAIGuardMachines;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardMachineRow
{
	ObjectID targetToGuard;
	const PolygonTrigger * areaToGuard;
	Coord3D positionToGuard;
	ObjectID nemesisToAttack;
	GuardMode guardMode;
};


inline GxAIGuardMachineRow &AIGuardMachine::gxRow() const { return const_cast<GxAIGuardMachineRow &>(*m_gxAIGuardMachineRow); }
inline const Coord3D * AIGuardMachine::getPositionToGuard() const
{ return &gxRow().positionToGuard; }
inline const PolygonTrigger * AIGuardMachine::getAreaToGuard() const
{ return gxRow().areaToGuard; }
inline Object* AIGuardMachine::findTargetToGuardByID()
{ return TheGameLogic->findObjectByID(gxRow().targetToGuard); }
inline void AIGuardMachine::setTargetToGuard( const Object *object )
{ gxRow().targetToGuard = object ? object->getID() : INVALID_ID; }
inline void AIGuardMachine::setTargetPositionToGuard( const Coord3D *pos)
{ gxRow().positionToGuard = *pos; }
inline void AIGuardMachine::setAreaToGuard( const PolygonTrigger *area)
{ gxRow().areaToGuard = area; }
inline void AIGuardMachine::setNemesisID(ObjectID id)
{ gxRow().nemesisToAttack = id; }
inline ObjectID AIGuardMachine::getNemesisID() const
{ return gxRow().nemesisToAttack; }
inline GuardMode AIGuardMachine::getGuardMode() const
{ return gxRow().guardMode; }
inline void AIGuardMachine::setGuardMode(GuardMode guardMode)
{ gxRow().guardMode = guardMode; }


//--------------------------------------------------------------------------------------
struct GxAIGuardInnerStateRow;
class AIGuardInnerState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardInnerState, "AIGuardInnerState")
public:
	AIGuardInnerState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual Bool isAttack() const;
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AIGuardMachine* getGuardMachine() { return (AIGuardMachine*)getMachine(); }

	GxRow<struct GxAIGuardInnerStateRow> m_gxAIGuardInnerStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardInnerStates
	friend struct GxAIGuardInnerStateRow;
	inline struct GxAIGuardInnerStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardInnerState's per-unit state, one row per unit in TheGxAIGuardInnerStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardInnerStateRow
{
	ExitConditions exitConditions;
	AIAttackState* attackState;
	AIEnterState* enterState;
};


inline GxAIGuardInnerStateRow &AIGuardInnerState::gxRow() const { return const_cast<GxAIGuardInnerStateRow &>(*m_gxAIGuardInnerStateRow); }
inline Bool AIGuardInnerState::isAttack() const
{ return gxRow().attackState ? gxRow().attackState->isAttack() : FALSE; }


//--------------------------------------------------------------------------------------
struct GxAIGuardIdleStateRow;
class AIGuardIdleState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardIdleState, "AIGuardIdleState")
public:
	AIGuardIdleState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual Bool isAttack() const { return FALSE; }
	virtual Bool isGuardIdle() const { return TRUE; }
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AIGuardMachine* getGuardMachine() { return (AIGuardMachine*)getMachine(); }

	GxRow<struct GxAIGuardIdleStateRow> m_gxAIGuardIdleStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardIdleStates
	friend struct GxAIGuardIdleStateRow;
	inline struct GxAIGuardIdleStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardIdleState's per-unit state, one row per unit in TheGxAIGuardIdleStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardIdleStateRow
{
	UnsignedInt nextEnemyScanTime;
	Coord3D guardeePos;
};


inline GxAIGuardIdleStateRow &AIGuardIdleState::gxRow() const { return const_cast<GxAIGuardIdleStateRow &>(*m_gxAIGuardIdleStateRow); }

//--------------------------------------------------------------------------------------
struct GxAIGuardOuterStateRow;
class AIGuardOuterState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardOuterState, "AIGuardOuterState")
public:
	AIGuardOuterState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual Bool isAttack() const;
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AIGuardMachine* getGuardMachine() { return (AIGuardMachine*)getMachine(); }

	GxRow<struct GxAIGuardOuterStateRow> m_gxAIGuardOuterStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardOuterStates
	friend struct GxAIGuardOuterStateRow;
	inline struct GxAIGuardOuterStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardOuterState's per-unit state, one row per unit in TheGxAIGuardOuterStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardOuterStateRow
{
	ExitConditions exitConditions;
	AIAttackState* attackState;
};


inline GxAIGuardOuterStateRow &AIGuardOuterState::gxRow() const { return const_cast<GxAIGuardOuterStateRow &>(*m_gxAIGuardOuterStateRow); }
inline Bool AIGuardOuterState::isAttack() const
{ return gxRow().attackState ? gxRow().attackState->isAttack() : FALSE; }


//--------------------------------------------------------------------------------------
struct GxAIGuardReturnStateRow;
class AIGuardReturnState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardReturnState, "AIGuardReturnState")
private:
	AIGuardMachine* getGuardMachine() { return (AIGuardMachine*)getMachine(); }
public:
	AIGuardReturnState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual Bool isAttack() const { return FALSE; }
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	GxRow<struct GxAIGuardReturnStateRow> m_gxAIGuardReturnStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardReturnStates
	friend struct GxAIGuardReturnStateRow;
	inline struct GxAIGuardReturnStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardReturnState's per-unit state, one row per unit in TheGxAIGuardReturnStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardReturnStateRow
{
	UnsignedInt nextReturnScanTime;
};


inline GxAIGuardReturnStateRow &AIGuardReturnState::gxRow() const { return const_cast<GxAIGuardReturnStateRow &>(*m_gxAIGuardReturnStateRow); }

//--------------------------------------------------------------------------------------
class AIGuardPickUpCrateState : public AIPickUpCrateState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardPickUpCrateState, "AIGuardPickUpCrateState")
public:
	AIGuardPickUpCrateState( StateMachine *machine );
	virtual Bool isAttack() const { return FALSE; }
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
};
EMPTY_DTOR(AIGuardPickUpCrateState)

//--------------------------------------------------------------------------------------
struct GxAIGuardAttackAggressorStateRow;
class AIGuardAttackAggressorState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AIGuardAttackAggressorState, "AIGuardAttackAggressorState")
public:
	AIGuardAttackAggressorState( StateMachine *machine );
	virtual Bool isAttack() const;
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AIGuardMachine* getGuardMachine() { return (AIGuardMachine*)getMachine(); }
	GxRow<struct GxAIGuardAttackAggressorStateRow> m_gxAIGuardAttackAggressorStateRow;	///< web port (round 5): this unit's row in TheGxAIGuardAttackAggressorStates
	friend struct GxAIGuardAttackAggressorStateRow;
	inline struct GxAIGuardAttackAggressorStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AIGuardAttackAggressorState's per-unit state, one row per unit in TheGxAIGuardAttackAggressorStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIGuardAttackAggressorStateRow
{
	ExitConditions exitConditions;
	AIAttackState* attackState;
};


inline GxAIGuardAttackAggressorStateRow &AIGuardAttackAggressorState::gxRow() const { return const_cast<GxAIGuardAttackAggressorStateRow &>(*m_gxAIGuardAttackAggressorStateRow); }
inline Bool AIGuardAttackAggressorState::isAttack() const
{ return gxRow().attackState ? gxRow().attackState->isAttack() : FALSE; }


//--------------------------------------------------------------------------------------
