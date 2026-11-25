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

// FILE: AITNGuard.h
/*---------------------------------------------------------------------------*/
/* EA Pacific                                                                */
/* Confidential Information	                                                 */
/* Copyright (C) 2001 - All Rights Reserved                                  */
/* DO NOT DISTRIBUTE                                                         */
/*---------------------------------------------------------------------------*/
/* Project:    RTS3                                                          */
/* File name:  AITNGuard.h                                                     */
/* Created:    John K. McDonald, Jr., 3/29/2002                              */
/* Desc:       // Define Guard states for AI                                 */
/* Revision History:                                                         */
/*		3/29/2002 : Initial creation                                           */
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
	AI_TN_GUARD_INNER = 5000,					///< Attack anything within this area till death
	AI_TN_GUARD_IDLE,									///< Wait till something shows up to attack.
	AI_TN_GUARD_OUTER,									///< Attack anything within this area that has been aggressive, until the timer expires
	AI_TN_GUARD_RETURN,								///< Restore to a position within the inner circle
	AI_TN_GUARD_GET_CRATE,							///< Pick up a crate from an enemy we killed.
	AI_TN_GUARD_ATTACK_AGGRESSOR,			///< Attack something that attacked me (that I can attack)
};

//--------------------------------------------------------------------------------------
struct GxTunnelNetworkExitConditionsRow;
class TunnelNetworkExitConditions : public AttackExitConditionsInterface
{
public:
	virtual ~TunnelNetworkExitConditions();	///< web port (round 5): releases the row

public:

	struct GxTunnelNetworkExitConditionsRow *m_gxTunnelNetworkExitConditionsRow;	///< web port (round 5): this unit's row in TheGxTunnelNetworkExitConditionss
	friend struct GxTunnelNetworkExitConditionsRow;
	inline struct GxTunnelNetworkExitConditionsRow &gxRow() const;

	TunnelNetworkExitConditions();	///< web port (round 5): defined in the .cpp


	virtual Bool shouldExit(const StateMachine* machine) const;
};

// Web port (round 5, units are pure data): TunnelNetworkExitConditions's per-unit state, one row per unit in TheGxTunnelNetworkExitConditionss;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxTunnelNetworkExitConditionsRow
{
	UnsignedInt attackGiveUpFrame;
};
extern GxComponentTable<GxTunnelNetworkExitConditionsRow> TheGxTunnelNetworkExitConditionss;


inline GxTunnelNetworkExitConditionsRow &TunnelNetworkExitConditions::gxRow() const { return TheGxTunnelNetworkExitConditionss.at(m_gxTunnelNetworkExitConditionsRow); }



// FORWARD DECLARATIONS ///////////////////////////////////////////////////////

//--------------------------------------------------------------------------------------
struct GxAITNGuardMachineRow;
class AITNGuardMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AITNGuardMachine, "AITNGuardMachinePool" );

private:
	struct GxAITNGuardMachineRow *m_gxAITNGuardMachineRow;	///< web port (round 5): this unit's row in TheGxAITNGuardMachines
	friend struct GxAITNGuardMachineRow;
	inline struct GxAITNGuardMachineRow &gxRow() const;

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
	AITNGuardMachine( Object *owner );

	const Coord3D * getPositionToGuard() const;
	void setTargetPositionToGuard( const Coord3D *pos);

	void setNemesisID(ObjectID id);
	ObjectID getNemesisID() const;

	GuardMode getGuardMode() const;
	void setGuardMode(GuardMode guardMode);

	Bool lookForInnerTarget();

	static Real getStdGuardRange(const Object* obj);
};

// Web port (round 5, units are pure data): AITNGuardMachine's per-unit state, one row per unit in TheGxAITNGuardMachines;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAITNGuardMachineRow
{
	Coord3D positionToGuard;
	ObjectID nemesisToAttack;
	GuardMode guardMode;
};
extern GxComponentTable<GxAITNGuardMachineRow> TheGxAITNGuardMachines;


inline GxAITNGuardMachineRow &AITNGuardMachine::gxRow() const { return TheGxAITNGuardMachines.at(m_gxAITNGuardMachineRow); }
inline const Coord3D * AITNGuardMachine::getPositionToGuard() const
{ return &gxRow().positionToGuard; }
inline void AITNGuardMachine::setTargetPositionToGuard( const Coord3D *pos)
{ gxRow().positionToGuard = *pos; }
inline void AITNGuardMachine::setNemesisID(ObjectID id)
{ gxRow().nemesisToAttack = id; }
inline ObjectID AITNGuardMachine::getNemesisID() const
{ return gxRow().nemesisToAttack; }
inline GuardMode AITNGuardMachine::getGuardMode() const
{ return gxRow().guardMode; }
inline void AITNGuardMachine::setGuardMode(GuardMode guardMode)
{ gxRow().guardMode = guardMode; }


//--------------------------------------------------------------------------------------
struct GxAITNGuardInnerStateRow;
class AITNGuardInnerState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AITNGuardInnerState, "AITNGuardInnerState")
public:
	AITNGuardInnerState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AITNGuardMachine* getGuardMachine() { return (AITNGuardMachine*)getMachine(); }

	struct GxAITNGuardInnerStateRow *m_gxAITNGuardInnerStateRow;	///< web port (round 5): this unit's row in TheGxAITNGuardInnerStates
	friend struct GxAITNGuardInnerStateRow;
	inline struct GxAITNGuardInnerStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AITNGuardInnerState's per-unit state, one row per unit in TheGxAITNGuardInnerStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAITNGuardInnerStateRow
{
	TunnelNetworkExitConditions exitConditions;
	Bool scanForEnemy;
	AIAttackState* attackState;
};
extern GxComponentTable<GxAITNGuardInnerStateRow> TheGxAITNGuardInnerStates;


inline GxAITNGuardInnerStateRow &AITNGuardInnerState::gxRow() const { return TheGxAITNGuardInnerStates.at(m_gxAITNGuardInnerStateRow); }


//--------------------------------------------------------------------------------------
struct GxAITNGuardIdleStateRow;
class AITNGuardIdleState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AITNGuardIdleState, "AITNGuardIdleState")
public:
	AITNGuardIdleState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AITNGuardMachine* getGuardMachine() { return (AITNGuardMachine*)getMachine(); }

	struct GxAITNGuardIdleStateRow *m_gxAITNGuardIdleStateRow;	///< web port (round 5): this unit's row in TheGxAITNGuardIdleStates
	friend struct GxAITNGuardIdleStateRow;
	inline struct GxAITNGuardIdleStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AITNGuardIdleState's per-unit state, one row per unit in TheGxAITNGuardIdleStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAITNGuardIdleStateRow
{
	UnsignedInt nextEnemyScanTime;
	Coord3D guardeePos;
};
extern GxComponentTable<GxAITNGuardIdleStateRow> TheGxAITNGuardIdleStates;


inline GxAITNGuardIdleStateRow &AITNGuardIdleState::gxRow() const { return TheGxAITNGuardIdleStates.at(m_gxAITNGuardIdleStateRow); }

//--------------------------------------------------------------------------------------
struct GxAITNGuardOuterStateRow;
class AITNGuardOuterState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AITNGuardOuterState, "AITNGuardOuterState")
public:
	AITNGuardOuterState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AITNGuardMachine* getGuardMachine() { return (AITNGuardMachine*)getMachine(); }

	struct GxAITNGuardOuterStateRow *m_gxAITNGuardOuterStateRow;	///< web port (round 5): this unit's row in TheGxAITNGuardOuterStates
	friend struct GxAITNGuardOuterStateRow;
	inline struct GxAITNGuardOuterStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AITNGuardOuterState's per-unit state, one row per unit in TheGxAITNGuardOuterStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAITNGuardOuterStateRow
{
	TunnelNetworkExitConditions exitConditions;
	AIAttackState* attackState;
};
extern GxComponentTable<GxAITNGuardOuterStateRow> TheGxAITNGuardOuterStates;


inline GxAITNGuardOuterStateRow &AITNGuardOuterState::gxRow() const { return TheGxAITNGuardOuterStates.at(m_gxAITNGuardOuterStateRow); }


//--------------------------------------------------------------------------------------
struct GxAITNGuardReturnStateRow;
class AITNGuardReturnState : public AIEnterState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AITNGuardReturnState, "AITNGuardReturnState")
private:
	AITNGuardMachine* getGuardMachine() { return (AITNGuardMachine*)getMachine(); }
public:
	AITNGuardReturnState( StateMachine *machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );

protected:
	struct GxAITNGuardReturnStateRow *m_gxAITNGuardReturnStateRow;	///< web port (round 5): this unit's row in TheGxAITNGuardReturnStates
	friend struct GxAITNGuardReturnStateRow;
	inline struct GxAITNGuardReturnStateRow &gxRow() const;

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

// Web port (round 5, units are pure data): AITNGuardReturnState's per-unit state, one row per unit in TheGxAITNGuardReturnStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAITNGuardReturnStateRow
{
	UnsignedInt nextReturnScanTime;
};
extern GxComponentTable<GxAITNGuardReturnStateRow> TheGxAITNGuardReturnStates;


inline GxAITNGuardReturnStateRow &AITNGuardReturnState::gxRow() const { return TheGxAITNGuardReturnStates.at(m_gxAITNGuardReturnStateRow); }

//--------------------------------------------------------------------------------------
class AITNGuardPickUpCrateState : public AIPickUpCrateState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AITNGuardPickUpCrateState, "AITNGuardPickUpCrateState")
public:
	AITNGuardPickUpCrateState( StateMachine *machine );
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
};
EMPTY_DTOR(AITNGuardPickUpCrateState)

//--------------------------------------------------------------------------------------
struct GxAITNGuardAttackAggressorStateRow;
class AITNGuardAttackAggressorState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(AITNGuardAttackAggressorState, "AITNGuardAttackAggressorState")
public:
	AITNGuardAttackAggressorState( StateMachine *machine );
	virtual StateReturnType onEnter();
	virtual StateReturnType update();
	virtual void onExit( StateExitType status );
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	AITNGuardMachine* getGuardMachine() { return (AITNGuardMachine*)getMachine(); }
	struct GxAITNGuardAttackAggressorStateRow *m_gxAITNGuardAttackAggressorStateRow;	///< web port (round 5): this unit's row in TheGxAITNGuardAttackAggressorStates
	friend struct GxAITNGuardAttackAggressorStateRow;
	inline struct GxAITNGuardAttackAggressorStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): AITNGuardAttackAggressorState's per-unit state, one row per unit in TheGxAITNGuardAttackAggressorStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAITNGuardAttackAggressorStateRow
{
	TunnelNetworkExitConditions exitConditions;
	AIAttackState* attackState;
};
extern GxComponentTable<GxAITNGuardAttackAggressorStateRow> TheGxAITNGuardAttackAggressorStates;


inline GxAITNGuardAttackAggressorStateRow &AITNGuardAttackAggressorState::gxRow() const { return TheGxAITNGuardAttackAggressorStates.at(m_gxAITNGuardAttackAggressorStateRow); }


//--------------------------------------------------------------------------------------
