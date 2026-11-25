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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/PerfTimer.h"
#include "Common/Team.h"
#include "Common/Xfer.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIGuardRetaliate.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"

// Web port (round 5, units are pure data): AIGuardRetaliateReturnState's per-unit state is a row of TheGxAIGuardRetaliateReturnStates;
// GXROW_AIGUARDRETALIATERETURNSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardRetaliateReturnStateRow> TheGxAIGuardRetaliateReturnStates;
#define GXROW_AIGUARDRETALIATERETURNSTATE (TheGxAIGuardRetaliateReturnStates.at(m_gxAIGuardRetaliateReturnStateRow))


// Web port (round 5, units are pure data): AIGuardRetaliateOuterState's per-unit state is a row of TheGxAIGuardRetaliateOuterStates;
// GXROW_AIGUARDRETALIATEOUTERSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardRetaliateOuterStateRow> TheGxAIGuardRetaliateOuterStates;
#define GXROW_AIGUARDRETALIATEOUTERSTATE (TheGxAIGuardRetaliateOuterStates.at(m_gxAIGuardRetaliateOuterStateRow))


// Web port (round 5, units are pure data): AIGuardRetaliateMachine's per-unit state is a row of TheGxAIGuardRetaliateMachines;
// GXROW_AIGUARDRETALIATEMACHINE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardRetaliateMachineRow> TheGxAIGuardRetaliateMachines;
#define GXROW_AIGUARDRETALIATEMACHINE (TheGxAIGuardRetaliateMachines.at(m_gxAIGuardRetaliateMachineRow))


// Web port (round 5, units are pure data): AIGuardRetaliateInnerState's per-unit state is a row of TheGxAIGuardRetaliateInnerStates;
// GXROW_AIGUARDRETALIATEINNERSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardRetaliateInnerStateRow> TheGxAIGuardRetaliateInnerStates;
#define GXROW_AIGUARDRETALIATEINNERSTATE (TheGxAIGuardRetaliateInnerStates.at(m_gxAIGuardRetaliateInnerStateRow))


// Web port (round 5, units are pure data): AIGuardRetaliateIdleState's per-unit state is a row of TheGxAIGuardRetaliateIdleStates;
// GXROW_AIGUARDRETALIATEIDLESTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardRetaliateIdleStateRow> TheGxAIGuardRetaliateIdleStates;
#define GXROW_AIGUARDRETALIATEIDLESTATE (TheGxAIGuardRetaliateIdleStates.at(m_gxAIGuardRetaliateIdleStateRow))


// Web port (round 5, units are pure data): AIGuardRetaliateAttackAggressorState's per-unit state is a row of TheGxAIGuardRetaliateAttackAggressorStates;
// GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardRetaliateAttackAggressorStateRow> TheGxAIGuardRetaliateAttackAggressorStates;
#define GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE (TheGxAIGuardRetaliateAttackAggressorStates.at(m_gxAIGuardRetaliateAttackAggressorStateRow))


const Real CLOSE_ENOUGH = (25.0f);


static Bool hasAttackedMeAndICanReturnFire( State *thisState, void* /*userData*/ )
{
	Object *obj = thisState->getMachineOwner();
	BodyModuleInterface *bmi = obj ? obj->getBodyModule() : nullptr;

	if (!(obj && bmi)) {
		return FALSE;
	}

	if (bmi->getClearableLastAttacker() == INVALID_ID) {
		return FALSE;
	}

	// K. It appears we have a valid aggressor. Find it, and determine if we can attack it, etc.
	Object *target = TheGameLogic->findObjectByID(bmi->getClearableLastAttacker());
	bmi->clearLastAttacker();

	// We use the clearable last attacker because we should continue attacking the guy. But if he
	// stops attacking us, then we want our timer to kick us off of him and make us go attack
	// other units instead.


	if (!target) {
		return FALSE;
	}

	if (obj->getRelationship(target) != ENEMIES) {
		return FALSE;
	}

	// This is a quick test on the target. It will be duplicated in getAbleToAttackSpecificObject,
	// but the payoff is worth the duplication.
	if (target->isEffectivelyDead()) {
		return FALSE;
	}

	//@todo: Get this out of here. Move it into the declaration of calling this function, or figure
	// out some way to call it less often.

	if (!obj->isAbleToAttack()) {
		return FALSE;
	}

	CanAttackResult result = obj->getAbleToAttackSpecificObject(ATTACK_NEW_TARGET, target, CMD_FROM_AI);
	if( result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
	{
		return TRUE;
	}
	return FALSE;
}

//-- ExitConditions -------------------------------------------------------------------------------
/**
 * This returns true if the conditions specified have been met, false otherwise.
 */
Bool GuardRetaliateExitConditions::shouldExit(const StateMachine* machine) const
{
	if (!machine->getGoalObject())
	{
		if (m_conditionsToConsider & ATTACK_ExitIfNoUnitFound)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	if (m_conditionsToConsider & ATTACK_ExitIfExpiredDuration)
	{
		if (TheGameLogic->getFrame() >= m_attackGiveUpFrame)
		{
			return true;
		}
	}

	if (m_conditionsToConsider & ATTACK_ExitIfOutsideRadius)
	{
		Coord3D deltaAggressor, myRange;
		Coord3D objPos = *machine->getGoalObject()->getPosition();
		Coord3D myPos = *machine->getOwner()->getPosition();
		deltaAggressor.x = objPos.x - m_center.x;
		deltaAggressor.y = objPos.y - m_center.y;
		deltaAggressor.z = 0; // BGC - when we search for a target we don't account for Z, so why should we here?
													// changing this fixed a crash where a GLARebelInfantry would be in GuardReturnState, find
													// a target that is within range, then not be able to attack because its actually out of range.
													// then it would look for a new target, get the same one, and proceed in an infinite recursive
													// loop that eventually blew the stack.

		Real guardRangeSqr = sqr( AIGuardRetaliateMachine::getStdGuardRange( machine->getOwner() ) );
		myRange.x = myPos.x - m_center.x;
		myRange.y = myPos.y - m_center.y;
		myRange.z = 0;

		if( deltaAggressor.lengthSqr() > m_radiusSqr )
		{
			//The aggressor is too far away now... give up retaliation.
			return TRUE;
		}
		if( myRange.lengthSqr() > guardRangeSqr )
		{
			//Never go beyond standard guard radius.
			return TRUE;
		}
	}

	return FALSE;
}


//-- AIGuardRetaliateMachine -------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
AIGuardRetaliateMachine::AIGuardRetaliateMachine( Object *owner ) : StateMachine(owner, "AIGuardRetaliateMachine")
{
	m_gxAIGuardRetaliateMachineRow = TheGxAIGuardRetaliateMachines.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIGUARDRETALIATEMACHINE.nemesisToAttack = INVALID_ID;

	GXROW_AIGUARDRETALIATEMACHINE.positionToGuard.zero();

	static const StateConditionInfo attackAggressors[] =
	{
		StateConditionInfo(hasAttackedMeAndICanReturnFire, AI_GUARD_RETALIATE_ATTACK_AGGRESSOR, nullptr),
		StateConditionInfo(nullptr, INVALID_STATE_ID, nullptr)
	};

	// order matters: first state is the default state.
// srj sez: I made "return" the start state, so that if ordered to guard a position
// that isn't the unit's current position, it moves to that position first.
	defineState( AI_GUARD_RETALIATE_ATTACK_AGGRESSOR, newInstance(AIGuardRetaliateAttackAggressorState)( this ), AI_GUARD_RETALIATE_RETURN, AI_GUARD_RETALIATE_RETURN );
	defineState( AI_GUARD_RETALIATE_RETURN,						newInstance(AIGuardRetaliateReturnState)( this ), AI_GUARD_RETALIATE_IDLE, AI_GUARD_RETALIATE_INNER, attackAggressors );
	defineState( AI_GUARD_RETALIATE_IDLE,							newInstance(AIGuardRetaliateIdleState)( this ), AI_GUARD_RETALIATE_INNER, EXIT_MACHINE_WITH_SUCCESS, attackAggressors );
	defineState( AI_GUARD_RETALIATE_INNER,						newInstance(AIGuardRetaliateInnerState)( this ), AI_GUARD_RETALIATE_OUTER, AI_GUARD_RETALIATE_OUTER );
	defineState( AI_GUARD_RETALIATE_OUTER,						newInstance(AIGuardRetaliateOuterState)( this ), AI_GUARD_RETALIATE_GET_CRATE, AI_GUARD_RETALIATE_GET_CRATE );
	defineState( AI_GUARD_RETALIATE_GET_CRATE,				newInstance(AIGuardRetaliatePickUpCrateState)( this ), AI_GUARD_RETALIATE_RETURN, AI_GUARD_RETALIATE_RETURN );
}

//--------------------------------------------------------------------------------------
AIGuardRetaliateMachine::~AIGuardRetaliateMachine()
{
	TheGxAIGuardRetaliateMachines.release( m_gxAIGuardRetaliateMachineRow );	// web port (round 5)
}

Bool AIGuardRetaliateMachine::isIdle() const
{
	if( getCurrentStateID() == AI_IDLE )
	{
		return TRUE;
	}
	return FALSE;
}

//--------------------------------------------------------------------------------------
/*static*/ Real AIGuardRetaliateMachine::getStdGuardRange(const Object* obj)
{
	Real visionRange = TheAI->getAdjustedVisionRangeForObject(obj,
		AI_VISIONFACTOR_OWNERTYPE | AI_VISIONFACTOR_MOOD | AI_VISIONFACTOR_GUARDINNER);

	return visionRange;
}

//--------------------------------------------------------------------------------------
Bool AIGuardRetaliateMachine::lookForInnerTarget()
{
	Object* owner = getOwner();
	if (!owner->isAbleToAttack())
	{
		return false;	// my, that was easy
	}

	// Check if team auto targets same victim.
	Object *teamVictim = nullptr;
	if (owner->getTeam()->getPrototype()->getTemplateInfo()->m_attackCommonTarget)
	{
		teamVictim = owner->getTeam()->getTeamTargetObject();
		if (teamVictim)
		{
			setNemesisID(teamVictim->getID());
			return true;	// Transitions to AIGuardRetaliateInnerState.
		}
	}

	PartitionFilterRelationship					f1(owner, PartitionFilterRelationship::ALLOW_ENEMIES);
	PartitionFilterPossibleToAttack			f2(ATTACK_NEW_TARGET, owner, CMD_FROM_AI);
	PartitionFilterSameMapStatus				filterMapStatus(owner);
	PartitionFilterRelationship					f5(owner, PartitionFilterRelationship::ALLOW_NEUTRAL);
	PartitionFilterPossibleToEnter			f6(owner, CMD_FROM_AI);
	PartitionFilterPossibleToHijack			f7(owner, CMD_FROM_AI);
	PartitionFilterRejectBuildings			f8( owner );

	PartitionFilter *filters[16];
	Int count = 0;

	// Enter Guard state
	if (owner->getTemplate()->isEnterGuard())
	{
		filters[count++] = &f6;

		// Hijack Guard state
		if (owner->getTemplate()->isHijackGuard())
		{
			filters[count++] = &f1;
			filters[count++] = &f7;
		}
		else
		{
			filters[count++] = &f5;
		}
	}
	// Attack Guard state
	else
	{
		filters[count++] = &f1;
		filters[count++] = &f2;
		filters[count++] = &f8; //Different than guard... we won't allow acquiring of structures (unless base defenses)
	}

	filters[count++] = &filterMapStatus;

	Real visionRange = AIGuardRetaliateMachine::getStdGuardRange(owner);

	filters[count++] = nullptr;

//	SimpleObjectIterator* iter = ThePartitionManager->iterateObjectsInRange(
//					&pos, visionRange, FROM_CENTER_2D, filters, ITER_SORTED_NEAR_TO_FAR);
//	MemoryPoolObjectHolder hold(iter);
//	Object* target = iter->first();
//
// srj sez: the above code is stupid and slow. since we only want the closest object,
// just ask for that; the above has to find ALL objects in range, but we ignore all
// but the first (closest).
//
	const Coord3D *pos = getPositionToGuard();
	Object* target = ThePartitionManager->getClosestObject(pos, visionRange, FROM_CENTER_2D, filters);
	if (target)
	{
		setNemesisID(target->getID());
		return true;	// Transitions to AIGuardRetaliateInnerState.
	}
	else
	{
		return false;
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateMachine::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateMachine::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 2;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	if (version>=2) {
		StateMachine::xfer(xfer);	// Forgot this in initial implementation.  jba.
	}

	xfer->xferObjectID(&GXROW_AIGUARDRETALIATEMACHINE.nemesisToAttack);
	xfer->xferCoord3D(&GXROW_AIGUARDRETALIATEMACHINE.positionToGuard);

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateMachine::loadPostProcess()
{
}

//-- AIGuardRetaliateInnerState ----------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateInnerState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateInnerState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateInnerState::loadPostProcess()
{
	onEnter();
}

// ------------------------------------------------------------------------------------------------
AIGuardRetaliateInnerState::~AIGuardRetaliateInnerState()
{
	deleteInstance(GXROW_AIGUARDRETALIATEINNERSTATE.attackState);
	deleteInstance(GXROW_AIGUARDRETALIATEINNERSTATE.enterState);
	TheGxAIGuardRetaliateInnerStates.release( m_gxAIGuardRetaliateInnerStateRow );	// web port (round 5)
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateInnerState::onEnter()
{
	// See if we try to enter the target
	if (getMachineOwner()->getTemplate()->isEnterGuard())
	{
		Object* nemesis = TheGameLogic->findObjectByID(getGuardMachine()->getNemesisID()) ;
		if (nemesis == nullptr)
		{
			DEBUG_LOG(("Unexpected null nemesis in AIGuardRetaliateInnerState."));
			return STATE_SUCCESS;
		}
		GXROW_AIGUARDRETALIATEINNERSTATE.enterState = newInstance(AIEnterState)(getMachine());

		GXROW_AIGUARDRETALIATEINNERSTATE.enterState->getMachine()->setGoalObject(nemesis);

		StateReturnType returnVal = GXROW_AIGUARDRETALIATEINNERSTATE.enterState->onEnter();
		if (returnVal == STATE_CONTINUE) {
			return STATE_CONTINUE;
		}
	}
	// Or try to destroy the target
	else
	{
		Coord3D pos = *getGuardMachine()->getPositionToGuard();
		Object* nemesis = TheGameLogic->findObjectByID(getGuardMachine()->getNemesisID()) ;
		if (nemesis == nullptr)
		{
			DEBUG_LOG(("Unexpected null nemesis in AIGuardRetaliateInnerState."));
			return STATE_SUCCESS;
		}
		GXROW_AIGUARDRETALIATEINNERSTATE.exitConditions.m_center = pos;
		GXROW_AIGUARDRETALIATEINNERSTATE.exitConditions.m_radiusSqr = sqr( 1.5f * AIGuardRetaliateMachine::getStdGuardRange( getMachineOwner() ) );
		GXROW_AIGUARDRETALIATEINNERSTATE.exitConditions.m_conditionsToConsider = (GuardRetaliateExitConditions::ATTACK_ExitIfOutsideRadius |
																								GuardRetaliateExitConditions::ATTACK_ExitIfNoUnitFound);

		GXROW_AIGUARDRETALIATEINNERSTATE.attackState = newInstance(AIAttackState)(getMachine(), false, true, false, &GXROW_AIGUARDRETALIATEINNERSTATE.exitConditions);

		GXROW_AIGUARDRETALIATEINNERSTATE.attackState->getMachine()->setGoalObject(nemesis);

		StateReturnType returnVal = GXROW_AIGUARDRETALIATEINNERSTATE.attackState->onEnter();
		if (returnVal == STATE_CONTINUE) {
			return STATE_CONTINUE;
		}
	}

	// if we had no one to attack, we were successful, so go to the next state.
	return STATE_SUCCESS;
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateInnerState::update()
{
	if (GXROW_AIGUARDRETALIATEINNERSTATE.attackState)
	{
		return GXROW_AIGUARDRETALIATEINNERSTATE.attackState->update();
	}
	else if (GXROW_AIGUARDRETALIATEINNERSTATE.enterState)
	{
		return GXROW_AIGUARDRETALIATEINNERSTATE.enterState->update();
	}

	return STATE_SUCCESS;
}

//--------------------------------------------------------------------------------------
void AIGuardRetaliateInnerState::onExit( StateExitType status )
{
	Object *obj = getMachineOwner();
	if (GXROW_AIGUARDRETALIATEINNERSTATE.attackState)
	{
		GXROW_AIGUARDRETALIATEINNERSTATE.attackState->onExit(status);
		deleteInstance(GXROW_AIGUARDRETALIATEINNERSTATE.attackState);
		GXROW_AIGUARDRETALIATEINNERSTATE.attackState = nullptr;
	}
	else if (GXROW_AIGUARDRETALIATEINNERSTATE.enterState)
	{
		GXROW_AIGUARDRETALIATEINNERSTATE.enterState->onExit(status);
		deleteInstance(GXROW_AIGUARDRETALIATEINNERSTATE.enterState);
		GXROW_AIGUARDRETALIATEINNERSTATE.enterState = nullptr;
	}

	if (obj->getTeam())
	{
		obj->getTeam()->setTeamTargetObject(nullptr); // clear the target.
	}
}

//-- AIGuardRetaliateOuterState ----------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateOuterState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateOuterState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateOuterState::loadPostProcess()
{						 AIGuardRetaliateOuterState
	onEnter();
}

// ------------------------------------------------------------------------------------------------
AIGuardRetaliateOuterState::~AIGuardRetaliateOuterState()
{
	deleteInstance(GXROW_AIGUARDRETALIATEOUTERSTATE.attackState);
	TheGxAIGuardRetaliateOuterStates.release( m_gxAIGuardRetaliateOuterStateRow );	// web port (round 5)
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateOuterState::onEnter()
{
	//if (getGuardMachine()->getGuardMode() == GUARDMODE_GUARD_WITHOUT_PURSUIT)
	//{
	//	// "patrol" mode does not follow targets outside the guard area.
	//	return STATE_SUCCESS;
	//}

	Coord3D pos = *getGuardMachine()->getPositionToGuard();

	Object* nemesis = TheGameLogic->findObjectByID(getGuardMachine()->getNemesisID()) ;
	if (nemesis == nullptr)
	{
		DEBUG_LOG(("Unexpected null nemesis in AIGuardRetaliateOuterState."));
		return STATE_SUCCESS;
	}
	Object *obj = getMachineOwner();

	Real range = TheAI->getAdjustedVisionRangeForObject(obj, AI_VISIONFACTOR_OWNERTYPE | AI_VISIONFACTOR_MOOD);

	GXROW_AIGUARDRETALIATEOUTERSTATE.exitConditions.m_center = pos;
	GXROW_AIGUARDRETALIATEOUTERSTATE.exitConditions.m_radiusSqr = sqr( 0.67f * (range + AIGuardRetaliateMachine::getStdGuardRange( getMachineOwner() )) );
	GXROW_AIGUARDRETALIATEOUTERSTATE.exitConditions.m_attackGiveUpFrame = TheGameLogic->getFrame() + TheAI->getAiData()->m_guardChaseUnitFrames;
	GXROW_AIGUARDRETALIATEOUTERSTATE.exitConditions.m_conditionsToConsider = (GuardRetaliateExitConditions::ATTACK_ExitIfExpiredDuration |
																								GuardRetaliateExitConditions::ATTACK_ExitIfOutsideRadius |
																								GuardRetaliateExitConditions::ATTACK_ExitIfNoUnitFound);

	GXROW_AIGUARDRETALIATEOUTERSTATE.attackState = newInstance(AIAttackState)(getMachine(), false, true, false, &GXROW_AIGUARDRETALIATEOUTERSTATE.exitConditions);
	GXROW_AIGUARDRETALIATEOUTERSTATE.attackState->getMachine()->setGoalObject(nemesis);

	StateReturnType returnVal = GXROW_AIGUARDRETALIATEOUTERSTATE.attackState->onEnter();
	if (returnVal == STATE_CONTINUE) {
		return STATE_CONTINUE;
	}

	// if we had no one to attack, we were successful, so go to the next state.
	return STATE_SUCCESS;
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateOuterState::update()
{
	if (GXROW_AIGUARDRETALIATEOUTERSTATE.attackState==nullptr) return STATE_SUCCESS;

	Object* goalObj = GXROW_AIGUARDRETALIATEOUTERSTATE.attackState->getMachineGoalObject();
	if (goalObj)
	{
		Coord3D deltaAggr;
		deltaAggr.x = GXROW_AIGUARDRETALIATEOUTERSTATE.exitConditions.m_center.x - goalObj->getPosition()->x;
		deltaAggr.y = GXROW_AIGUARDRETALIATEOUTERSTATE.exitConditions.m_center.y - goalObj->getPosition()->y;
		deltaAggr.z = GXROW_AIGUARDRETALIATEOUTERSTATE.exitConditions.m_center.z - goalObj->getPosition()->z;
		Real visionSqr = sqr(AIGuardRetaliateMachine::getStdGuardRange(getMachineOwner()));
		if (deltaAggr.lengthSqr() <= visionSqr)
		{
			// reset the counter
			GXROW_AIGUARDRETALIATEOUTERSTATE.exitConditions.m_attackGiveUpFrame = TheGameLogic->getFrame() + TheAI->getAiData()->m_guardChaseUnitFrames;
		}
	}

	return GXROW_AIGUARDRETALIATEOUTERSTATE.attackState->update();
}

//--------------------------------------------------------------------------------------
void AIGuardRetaliateOuterState::onExit( StateExitType status )
{
	if (GXROW_AIGUARDRETALIATEOUTERSTATE.attackState)
	{
		GXROW_AIGUARDRETALIATEOUTERSTATE.attackState->onExit(status);
		deleteInstance(GXROW_AIGUARDRETALIATEOUTERSTATE.attackState);
		GXROW_AIGUARDRETALIATEOUTERSTATE.attackState = nullptr;
	}
}

//-- AIGuardRetaliateReturnState ----------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateReturnState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateReturnState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedInt(&GXROW_AIGUARDRETALIATERETURNSTATE.nextReturnScanTime);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateReturnState::loadPostProcess()
{
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateReturnState::onEnter()
{
	UnsignedInt now = TheGameLogic->getFrame();
	GXROW_AIGUARDRETALIATERETURNSTATE.nextReturnScanTime = now + GameLogicRandomValue(0, TheAI->getAiData()->m_guardEnemyReturnScanRate);

// no, no, no, don't do this in onEnter, unless you like really slow maps. (srj)
//	if (getGuardMachine()->lookForInnerTarget())
//		return STATE_FAILURE; // early termination because we found a target.

	AIInternalMoveToState::gxRow().goalPosition = *getGuardMachine()->getPositionToGuard();

	AIUpdateInterface *ai = getMachineOwner()->getAIUpdateInterface();
	if (ai && ai->isDoingGroundMovement())
	{
		TheAI->pathfinder()->adjustDestination(getMachineOwner(), ai->getLocomotorSet(), &AIInternalMoveToState::gxRow().goalPosition);
	}
	setAdjustsDestination(true);
	return AIInternalMoveToState::onEnter();
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateReturnState::update()
{
	UnsignedInt now = TheGameLogic->getFrame();
	if (now >= GXROW_AIGUARDRETALIATERETURNSTATE.nextReturnScanTime)
	{
		GXROW_AIGUARDRETALIATERETURNSTATE.nextReturnScanTime = now + TheAI->getAiData()->m_guardEnemyReturnScanRate;
		if (getGuardMachine()->lookForInnerTarget())
			return STATE_FAILURE; // early termination because we found a target.
	}

	// Just let the return movement finish.
	return AIInternalMoveToState::update();
}

//--------------------------------------------------------------------------------------
void AIGuardRetaliateReturnState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );
}

//-- AIGuardRetaliateIdleState ----------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateIdleState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateIdleState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedInt(&GXROW_AIGUARDRETALIATEIDLESTATE.nextEnemyScanTime);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateIdleState::loadPostProcess()
{
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateIdleState::onEnter()
{
	// first time thru, use a random amount so that everyone doesn't scan on the same frame,
	// to avoid "spikes".
	UnsignedInt now = TheGameLogic->getFrame();
	GXROW_AIGUARDRETALIATEIDLESTATE.nextEnemyScanTime = now + GameLogicRandomValue(0, TheAI->getAiData()->m_guardEnemyScanRate);

	return STATE_CONTINUE;
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateIdleState::update()
{
	//DEBUG_LOG(("AIGuardRetaliateIdleState frame %d: %08lx",TheGameLogic->getFrame(),getMachineOwner()));

	UnsignedInt now = TheGameLogic->getFrame();
	if (now < GXROW_AIGUARDRETALIATEIDLESTATE.nextEnemyScanTime)
		return STATE_SLEEP(GXROW_AIGUARDRETALIATEIDLESTATE.nextEnemyScanTime - now);

	GXROW_AIGUARDRETALIATEIDLESTATE.nextEnemyScanTime = now + TheAI->getAiData()->m_guardEnemyScanRate;

#ifdef STATE_MACHINE_DEBUG
	//getMachine()->setDebugOutput(true);
#endif
	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAIUpdateInterface();
	// Check to see if we have created a crate we need to pick up.
	if (ai->getCrateID() != INVALID_ID)
	{
		getMachine()->setState(AI_GUARD_RETALIATE_GET_CRATE);
		return STATE_SLEEP(GXROW_AIGUARDRETALIATEIDLESTATE.nextEnemyScanTime - now);
	}

	// if anyone is in the inner area, return success.
	if (getGuardMachine()->lookForInnerTarget())
	{
		return STATE_SUCCESS;	// Transitions to AIGuardRetaliateInnerState.
	}
	else
	{
		return STATE_FAILURE;
	}

	return STATE_SLEEP(GXROW_AIGUARDRETALIATEIDLESTATE.nextEnemyScanTime - now);
}

//--------------------------------------------------------------------------------------
void AIGuardRetaliateIdleState::onExit( StateExitType status )
{

}

//-- AIGuardRetaliatePickUpCrateState ----------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AIGuardRetaliatePickUpCrateState::AIGuardRetaliatePickUpCrateState( StateMachine *machine )	: AIPickUpCrateState(machine)
{
#ifdef STATE_MACHINE_DEBUG
		setName("AIGuardRetaliatePickUpCrateState");
#endif
}
//--------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliatePickUpCrateState::onEnter()
{
	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAIUpdateInterface();

	// Check to see if we have created a crate we need to pick up.
	Object* crate = ai->checkForCrateToPickup();
	if (crate)
	{
		getMachine()->setGoalObject(crate);
		return AIPickUpCrateState::onEnter();
	}

	return STATE_SUCCESS; // no crate, so we're done.
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliatePickUpCrateState::update()
{
	return AIPickUpCrateState::update();
}

//--------------------------------------------------------------------------------------
void AIGuardRetaliatePickUpCrateState::onExit( StateExitType status )
{

}

//-- AIGuardRetaliateAttackAggressorState ------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AIGuardRetaliateAttackAggressorState::AIGuardRetaliateAttackAggressorState( StateMachine *machine ) : State( machine, "AIGuardRetaliateAttackAggressorState" )
{
	m_gxAIGuardRetaliateAttackAggressorStateRow = TheGxAIGuardRetaliateAttackAggressorStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

	GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState = nullptr;
}
#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIGuardRetaliateAttackAggressorState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/ ");
	if (GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState) name.concat(GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState->getName());
	else name.concat("*null GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState");
	return name;
}
#endif

// ------------------------------------------------------------------------------------------------
AIGuardRetaliateAttackAggressorState::~AIGuardRetaliateAttackAggressorState()
{
	deleteInstance(GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState);
	TheGxAIGuardRetaliateAttackAggressorStates.release( m_gxAIGuardRetaliateAttackAggressorStateRow );	// web port (round 5)
}

//-------------------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateAttackAggressorState::onEnter()
{
	Object *obj = getMachineOwner();
	ObjectID nemID = INVALID_ID;

	//Get the specified nemesis. If he doesn't exist... look at the object that damaged
	//us last.
	Object *nemesis = TheGameLogic->findObjectByID( getGuardMachine()->getNemesisID() );

	BodyModuleInterface *body = obj->getBodyModule();
	if( !nemesis && body && body->getLastDamageInfo()->in.m_sourceID && body->getLastDamageInfo()->in.m_damageType != DAMAGE_HEALING )
	{
		nemID = obj->getBodyModule()->getLastDamageInfo()->in.m_sourceID;
		nemesis = TheGameLogic->findObjectByID( nemID );
		if( nemesis && obj->getRelationship( nemesis ) == ENEMIES )
		{
			getGuardMachine()->setNemesisID(nemID);
		}
	}

	if( !nemesis )
	{
		DEBUG_LOG(("Unexpected null nemesis in AIGuardRetaliateAttackAggressorState."));
		return STATE_SUCCESS;
	}

	Coord3D pos = *getGuardMachine()->getPositionToGuard();

	Real range = TheAI->getAdjustedVisionRangeForObject(obj, AI_VISIONFACTOR_OWNERTYPE | AI_VISIONFACTOR_MOOD);

	GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.exitConditions.m_center = pos;
	GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.exitConditions.m_attackGiveUpFrame = TheGameLogic->getFrame() + TheAI->getAiData()->m_guardChaseUnitFrames;
	GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.exitConditions.m_radiusSqr = sqr( range + AIGuardRetaliateMachine::getStdGuardRange( obj ) );
	GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.exitConditions.m_conditionsToConsider = ( GuardRetaliateExitConditions::ATTACK_ExitIfExpiredDuration |
																						  GuardRetaliateExitConditions::ATTACK_ExitIfOutsideRadius |
																						  GuardRetaliateExitConditions::ATTACK_ExitIfNoUnitFound );

	GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState = newInstance(AIAttackState)(getMachine(), false, true, false, &GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.exitConditions);
	GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState->getMachine()->setGoalObject(nemesis);

	StateReturnType returnVal = GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState->onEnter();
	if (returnVal == STATE_CONTINUE) {
		return STATE_CONTINUE;
	}

	// if we had no one to attack, we were successful, so go to the next state.
	return STATE_SUCCESS;
}

//-------------------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateAttackAggressorState::update()
{
	if (GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState==nullptr) return STATE_SUCCESS;

	return GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState->update();
}

//-------------------------------------------------------------------------------------------------
void AIGuardRetaliateAttackAggressorState::onExit( StateExitType status )
{
	Object *obj = getMachineOwner();
	if (GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState)
	{
		GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState->onExit(status);
		deleteInstance(GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState);
		GXROW_AIGUARDRETALIATEATTACKAGGRESSORSTATE.attackState = nullptr;
	}

	if (obj->getTeam())
	{
		obj->getTeam()->setTeamTargetObject(nullptr); // clear the target.
	}
}

//-------------------------------------------------------------------------------------------------
void AIGuardRetaliateAttackAggressorState::crc( Xfer *xfer )
{

}

//-------------------------------------------------------------------------------------------------
void AIGuardRetaliateAttackAggressorState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

}

//-------------------------------------------------------------------------------------------------
void AIGuardRetaliateAttackAggressorState::loadPostProcess()
{
	onEnter();
}

AIGuardRetaliateIdleState::AIGuardRetaliateIdleState( StateMachine *machine ) : State( machine, "AIGuardRetaliateIdleState" )
{
	m_gxAIGuardRetaliateIdleStateRow = TheGxAIGuardRetaliateIdleStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
 }

AIGuardRetaliateIdleState::~AIGuardRetaliateIdleState()
{
	TheGxAIGuardRetaliateIdleStates.release( m_gxAIGuardRetaliateIdleStateRow );	// web port (round 5)
}

AIGuardRetaliateInnerState::AIGuardRetaliateInnerState( StateMachine *machine ) : State( machine, "AIGuardRetaliateInner" )
{
	m_gxAIGuardRetaliateInnerStateRow = TheGxAIGuardRetaliateInnerStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().attackState = 0;
		gxRow().enterState = 0;
	}

AIGuardRetaliateOuterState::AIGuardRetaliateOuterState( StateMachine *machine ) : State( machine, "AIGuardRetaliateOuter" )
{
	m_gxAIGuardRetaliateOuterStateRow = TheGxAIGuardRetaliateOuterStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().attackState = nullptr;
	}

AIGuardRetaliateReturnState::AIGuardRetaliateReturnState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIGuardRetaliateReturn" )
{
	m_gxAIGuardRetaliateReturnStateRow = TheGxAIGuardRetaliateReturnStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().nextReturnScanTime = 0;
	}

AIGuardRetaliateReturnState::~AIGuardRetaliateReturnState()
{
	TheGxAIGuardRetaliateReturnStates.release( m_gxAIGuardRetaliateReturnStateRow );	// web port (round 5)
}
