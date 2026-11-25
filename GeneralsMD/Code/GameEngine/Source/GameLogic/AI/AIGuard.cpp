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


// FILE: AIGuard.cpp
/*---------------------------------------------------------------------------*/
/* EA Pacific                                                                */
/* Confidential Information	                                                 */
/* Copyright (C) 2001 - All Rights Reserved                                  */
/* DO NOT DISTRIBUTE                                                         */
/*---------------------------------------------------------------------------*/
/* Project:    RTS3                                                          */
/* File name:  AIGuard.cpp                                                   */
/* Created:    John K. McDonald, Jr., 3/29/2002                              */
/* Desc:       // Set up guard states for AI                                 */
/* Revision History:                                                         */
/*		3/29/2002 : Initial creation                                           */
/*---------------------------------------------------------------------------*/
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/PerfTimer.h"
#include "Common/Team.h"
#include "Common/Xfer.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIGuard.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"

// Web port (round 5, units are pure data): ExitConditions's per-unit state is a row of TheGxExitConditionss;
// GXROW_EXITCONDITIONS is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxExitConditionsRow> TheGxExitConditionss;
#define GXROW_EXITCONDITIONS (TheGxExitConditionss.at(m_gxExitConditionsRow))


// Web port (round 5, units are pure data): AIGuardReturnState's per-unit state is a row of TheGxAIGuardReturnStates;
// GXROW_AIGUARDRETURNSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardReturnStateRow> TheGxAIGuardReturnStates;
#define GXROW_AIGUARDRETURNSTATE (TheGxAIGuardReturnStates.at(m_gxAIGuardReturnStateRow))


// Web port (round 5, units are pure data): AIGuardOuterState's per-unit state is a row of TheGxAIGuardOuterStates;
// GXROW_AIGUARDOUTERSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardOuterStateRow> TheGxAIGuardOuterStates;
#define GXROW_AIGUARDOUTERSTATE (TheGxAIGuardOuterStates.at(m_gxAIGuardOuterStateRow))


// Web port (round 5, units are pure data): AIGuardMachine's per-unit state is a row of TheGxAIGuardMachines;
// GXROW_AIGUARDMACHINE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardMachineRow> TheGxAIGuardMachines;
#define GXROW_AIGUARDMACHINE (TheGxAIGuardMachines.at(m_gxAIGuardMachineRow))


// Web port (round 5, units are pure data): AIGuardInnerState's per-unit state is a row of TheGxAIGuardInnerStates;
// GXROW_AIGUARDINNERSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardInnerStateRow> TheGxAIGuardInnerStates;
#define GXROW_AIGUARDINNERSTATE (TheGxAIGuardInnerStates.at(m_gxAIGuardInnerStateRow))


// Web port (round 5, units are pure data): AIGuardIdleState's per-unit state is a row of TheGxAIGuardIdleStates;
// GXROW_AIGUARDIDLESTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardIdleStateRow> TheGxAIGuardIdleStates;
#define GXROW_AIGUARDIDLESTATE (TheGxAIGuardIdleStates.at(m_gxAIGuardIdleStateRow))


// Web port (round 5, units are pure data): AIGuardAttackAggressorState's per-unit state is a row of TheGxAIGuardAttackAggressorStates;
// GXROW_AIGUARDATTACKAGGRESSORSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardAttackAggressorStateRow> TheGxAIGuardAttackAggressorStates;
#define GXROW_AIGUARDATTACKAGGRESSORSTATE (TheGxAIGuardAttackAggressorStates.at(m_gxAIGuardAttackAggressorStateRow))


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
Bool ExitConditions::shouldExit(const StateMachine* machine) const
{
	if (!machine->getGoalObject())
	{
		if (GXROW_EXITCONDITIONS.conditionsToConsider & ATTACK_ExitIfNoUnitFound)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	if (GXROW_EXITCONDITIONS.conditionsToConsider & ATTACK_ExitIfExpiredDuration)
	{
		if (TheGameLogic->getFrame() >= GXROW_EXITCONDITIONS.attackGiveUpFrame)
		{
			return true;
		}
	}

	if (GXROW_EXITCONDITIONS.conditionsToConsider & ATTACK_ExitIfOutsideRadius)
	{
		Coord3D deltaAggressor;
		Coord3D objPos = *machine->getGoalObject()->getPosition();
		deltaAggressor.x = objPos.x - GXROW_EXITCONDITIONS.center.x;
		deltaAggressor.y = objPos.y - GXROW_EXITCONDITIONS.center.y;
//	deltaAggressor.z = objPos.z - GXROW_EXITCONDITIONS.center.z;
		deltaAggressor.z = 0; // BGC - when we search for a target we don't account for Z, so why should we here?
													// changing this fixed a crash where a GLARebelInfantry would be in GuardReturnState, find
													// a target that is within range, then not be able to attack because its actually out of range.
													// then it would look for a new target, get the same one, and proceed in an infinite recursive
													// loop that eventually blew the stack.

		if (deltaAggressor.lengthSqr() > GXROW_EXITCONDITIONS.radiusSqr)
		{
			return true;
		}
	}

	return false;
}


//-- AIGuardMachine -------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
AIGuardMachine::AIGuardMachine( Object *owner ) : StateMachine(owner, "AIGuardMachine")
{
	m_gxAIGuardMachineRow = TheGxAIGuardMachines.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIGUARDMACHINE.targetToGuard = INVALID_ID;
	GXROW_AIGUARDMACHINE.areaToGuard = nullptr;
	GXROW_AIGUARDMACHINE.nemesisToAttack = INVALID_ID;
	GXROW_AIGUARDMACHINE.guardMode = GUARDMODE_NORMAL;

	GXROW_AIGUARDMACHINE.positionToGuard.zero();

	static const StateConditionInfo attackAggressors[] =
	{
		StateConditionInfo(hasAttackedMeAndICanReturnFire, AI_GUARD_ATTACK_AGGRESSOR, nullptr),
		StateConditionInfo(nullptr, INVALID_STATE_ID, nullptr)
	};

	// order matters: first state is the default state.
// srj sez: I made "return" the start state, so that if ordered to guard a position
// that isn't the unit's current position, it moves to that position first.
	//Kris: Except that guard return is more like an attack move, and will acquire targets while moving there.
	//This breaks deployAI units because they have to completely unpack before realizing that there is a target in range.
	//So I'm making AI_GUARD_INNER the first state.
	defineState( AI_GUARD_INNER,						newInstance(AIGuardInnerState)( this ), AI_GUARD_OUTER, AI_GUARD_OUTER, attackAggressors );
	defineState( AI_GUARD_RETURN,						newInstance(AIGuardReturnState)( this ), AI_GUARD_IDLE, AI_GUARD_INNER, attackAggressors );
	defineState( AI_GUARD_IDLE,							newInstance(AIGuardIdleState)( this ), AI_GUARD_INNER, AI_GUARD_RETURN, attackAggressors );
	defineState( AI_GUARD_OUTER,						newInstance(AIGuardOuterState)( this ), AI_GUARD_GET_CRATE, AI_GUARD_GET_CRATE );
	defineState( AI_GUARD_GET_CRATE,				newInstance(AIGuardPickUpCrateState)( this ), AI_GUARD_RETURN, AI_GUARD_RETURN );
	defineState( AI_GUARD_ATTACK_AGGRESSOR, newInstance(AIGuardAttackAggressorState)( this ), AI_GUARD_INNER, AI_GUARD_INNER );
}

//--------------------------------------------------------------------------------------
AIGuardMachine::~AIGuardMachine()
{
	TheGxAIGuardMachines.release( m_gxAIGuardMachineRow );	// web port (round 5)
}

//--------------------------------------------------------------------------------------
/*static*/ Real AIGuardMachine::getStdGuardRange(const Object* obj)
{
	Real visionRange = TheAI->getAdjustedVisionRangeForObject(obj,
		AI_VISIONFACTOR_OWNERTYPE | AI_VISIONFACTOR_MOOD | AI_VISIONFACTOR_GUARDINNER);

	return visionRange;
}

//--------------------------------------------------------------------------------------
Bool AIGuardMachine::lookForInnerTarget()
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
			return true;	// Transitions to AIGuardInnerState.
		}
	}

	Object* targetToGuard = findTargetToGuardByID();
	Coord3D pos = targetToGuard ? *targetToGuard->getPosition() : *getPositionToGuard();

	const PolygonTrigger*								area = getAreaToGuard();
	PartitionFilterRelationship					f1(owner, PartitionFilterRelationship::ALLOW_ENEMIES);
	PartitionFilterPossibleToAttack			f2(ATTACK_NEW_TARGET, owner, CMD_FROM_AI);
	PartitionFilterSameMapStatus				filterMapStatus(owner);
	PartitionFilterPolygonTrigger				f3(area);
	PartitionFilterIsFlying							f4;
	PartitionFilterRelationship					f5(owner, PartitionFilterRelationship::ALLOW_NEUTRAL);
	PartitionFilterPossibleToEnter			f6(owner, CMD_FROM_AI);
	PartitionFilterPossibleToHijack			f7(owner, CMD_FROM_AI);

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
	}

	filters[count++] = &filterMapStatus;

	Real visionRange = AIGuardMachine::getStdGuardRange(owner);

	if (area)
	{
		UnsignedInt checkFrame = TheGameLogic->getFrameObjectsChangedTriggerAreas()+TheAI->getAiData()->m_guardEnemyScanRate;
		if (TheGameLogic->getFrame()>checkFrame) {
			return false;
		}
		filters[count++] = &f3;
		visionRange = area->getRadius();
		area->getCenterPoint(&pos);
	}

	if (getGuardMode() == GUARDMODE_GUARD_FLYING_UNITS_ONLY)
	{
		// only consider flying targets
		filters[count++] = &f4;
	}

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
	Object* target = ThePartitionManager->getClosestObject(&pos, visionRange, FROM_CENTER_2D, filters);
	if (target)
	{
		setNemesisID(target->getID());
		return true;	// Transitions to AIGuardInnerState.
	}
	else
	{
		return false;
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardMachine::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardMachine::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 2;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	if (version>=2) {
		StateMachine::xfer(xfer);	// Forgot this in initial implementation.  jba.
	}

	xfer->xferObjectID(&GXROW_AIGUARDMACHINE.targetToGuard);
	xfer->xferObjectID(&GXROW_AIGUARDMACHINE.nemesisToAttack);
	xfer->xferCoord3D(&GXROW_AIGUARDMACHINE.positionToGuard);

	AsciiString triggerName;
	if (GXROW_AIGUARDMACHINE.areaToGuard) triggerName = GXROW_AIGUARDMACHINE.areaToGuard->getTriggerName();
	xfer->xferAsciiString(&triggerName);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (triggerName.isNotEmpty()) {
			GXROW_AIGUARDMACHINE.areaToGuard = TheTerrainLogic->getTriggerAreaByName(triggerName);
		}
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardMachine::loadPostProcess()
{
}

//-- AIGuardInnerState ----------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardInnerState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardInnerState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardInnerState::loadPostProcess()
{
	onEnter();
}

// ------------------------------------------------------------------------------------------------
AIGuardInnerState::~AIGuardInnerState()
{
	deleteInstance(GXROW_AIGUARDINNERSTATE.attackState);
	deleteInstance(GXROW_AIGUARDINNERSTATE.enterState);
	TheGxAIGuardInnerStates.release( m_gxAIGuardInnerStateRow );	// web port (round 5)
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardInnerState::onEnter()
{
	// See if we try to enter the target
	if (getMachineOwner()->getTemplate()->isEnterGuard())
	{
		Object* nemesis = TheGameLogic->findObjectByID(getGuardMachine()->getNemesisID()) ;
		if (nemesis == nullptr)
		{
			DEBUG_LOG(("Unexpected null nemesis in AIGuardInnerState."));
			return STATE_SUCCESS;
		}
		GXROW_AIGUARDINNERSTATE.enterState = newInstance(AIEnterState)(getMachine());

		GXROW_AIGUARDINNERSTATE.enterState->getMachine()->setGoalObject(nemesis);

		StateReturnType returnVal = GXROW_AIGUARDINNERSTATE.enterState->onEnter();
		if (returnVal == STATE_CONTINUE) {
			return STATE_CONTINUE;
		}
	}
	// Or try to destroy the target
	else
	{
		Object* targetToGuard = getGuardMachine()->findTargetToGuardByID();
		Coord3D pos = targetToGuard ? *targetToGuard->getPosition() : *getGuardMachine()->getPositionToGuard();
		Object* nemesis = TheGameLogic->findObjectByID(getGuardMachine()->getNemesisID()) ;
		if (nemesis == nullptr)
		{
			DEBUG_LOG(("Unexpected null nemesis in AIGuardInnerState."));
			return STATE_SUCCESS;
		}
		GXROW_AIGUARDINNERSTATE.exitConditions.gxRow().center = pos;
		GXROW_AIGUARDINNERSTATE.exitConditions.gxRow().radiusSqr = sqr(AIGuardMachine::getStdGuardRange(getMachineOwner()));
		GXROW_AIGUARDINNERSTATE.exitConditions.gxRow().conditionsToConsider = (ExitConditions::ATTACK_ExitIfOutsideRadius |
																								ExitConditions::ATTACK_ExitIfNoUnitFound);

		GXROW_AIGUARDINNERSTATE.attackState = newInstance(AIAttackState)(getMachine(), false, true, false, &GXROW_AIGUARDINNERSTATE.exitConditions);

		GXROW_AIGUARDINNERSTATE.attackState->getMachine()->setGoalObject(nemesis);

		StateReturnType returnVal = GXROW_AIGUARDINNERSTATE.attackState->onEnter();
		if (returnVal == STATE_CONTINUE) {
			return STATE_CONTINUE;
		}
	}

	// if we had no one to attack, we were successful, so go to the next state.
	return STATE_SUCCESS;
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardInnerState::update()
{
	if (GXROW_AIGUARDINNERSTATE.attackState)
	{
		// if the position has moved (IE we're guarding an object), move with it.
		Object* targetToGuard = getGuardMachine()->findTargetToGuardByID();
		if (targetToGuard)
		{
			GXROW_AIGUARDINNERSTATE.exitConditions.gxRow().center = *targetToGuard->getPosition();
		}

		return GXROW_AIGUARDINNERSTATE.attackState->update();
	}
	else if (GXROW_AIGUARDINNERSTATE.enterState)
	{
		return GXROW_AIGUARDINNERSTATE.enterState->update();
	}

	return STATE_SUCCESS;
}

//--------------------------------------------------------------------------------------
void AIGuardInnerState::onExit( StateExitType status )
{
	Object *obj = getMachineOwner();
	if (GXROW_AIGUARDINNERSTATE.attackState)
	{
		GXROW_AIGUARDINNERSTATE.attackState->onExit(status);
		deleteInstance(GXROW_AIGUARDINNERSTATE.attackState);
		GXROW_AIGUARDINNERSTATE.attackState = nullptr;
	}
	else if (GXROW_AIGUARDINNERSTATE.enterState)
	{
		GXROW_AIGUARDINNERSTATE.enterState->onExit(status);
		deleteInstance(GXROW_AIGUARDINNERSTATE.enterState);
		GXROW_AIGUARDINNERSTATE.enterState = nullptr;
	}

	if (obj->getTeam())
	{
		obj->getTeam()->setTeamTargetObject(nullptr); // clear the target.
	}
}

//-- AIGuardOuterState ----------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardOuterState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardOuterState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardOuterState::loadPostProcess()
{						 AIGuardOuterState
	onEnter();
}

// ------------------------------------------------------------------------------------------------
AIGuardOuterState::~AIGuardOuterState()
{
	deleteInstance(GXROW_AIGUARDOUTERSTATE.attackState);
	TheGxAIGuardOuterStates.release( m_gxAIGuardOuterStateRow );	// web port (round 5)
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardOuterState::onEnter()
{
	if (getGuardMachine()->getGuardMode() == GUARDMODE_GUARD_WITHOUT_PURSUIT)
	{
		// "patrol" mode does not follow targets outside the guard area.
		return STATE_SUCCESS;
	}

	Object* targetToGuard = getGuardMachine()->findTargetToGuardByID();
	Coord3D pos = targetToGuard ? *targetToGuard->getPosition() : *getGuardMachine()->getPositionToGuard();

	Object* nemesis = TheGameLogic->findObjectByID(getGuardMachine()->getNemesisID()) ;
	if (nemesis == nullptr)
	{
		DEBUG_LOG(("Unexpected null nemesis in AIGuardInnerState."));
		return STATE_SUCCESS;
	}
	Object *obj = getMachineOwner();

	Real range = TheAI->getAdjustedVisionRangeForObject(obj, AI_VISIONFACTOR_OWNERTYPE | AI_VISIONFACTOR_MOOD);

	const PolygonTrigger *area = getGuardMachine()->getAreaToGuard();
	if (area)
	{
		if (range < area->getRadius())
			range = area->getRadius();
		area->getCenterPoint(&pos);
	}
	GXROW_AIGUARDOUTERSTATE.exitConditions.gxRow().center = pos;
	GXROW_AIGUARDOUTERSTATE.exitConditions.gxRow().radiusSqr = sqr(range);
	GXROW_AIGUARDOUTERSTATE.exitConditions.gxRow().attackGiveUpFrame = TheGameLogic->getFrame() + TheAI->getAiData()->m_guardChaseUnitFrames;
	GXROW_AIGUARDOUTERSTATE.exitConditions.gxRow().conditionsToConsider = (ExitConditions::ATTACK_ExitIfExpiredDuration |
																								ExitConditions::ATTACK_ExitIfOutsideRadius |
																								ExitConditions::ATTACK_ExitIfNoUnitFound);

	GXROW_AIGUARDOUTERSTATE.attackState = newInstance(AIAttackState)(getMachine(), false, true, false, &GXROW_AIGUARDOUTERSTATE.exitConditions);
	GXROW_AIGUARDOUTERSTATE.attackState->getMachine()->setGoalObject(nemesis);

	StateReturnType returnVal = GXROW_AIGUARDOUTERSTATE.attackState->onEnter();
	if (returnVal == STATE_CONTINUE) {
		return STATE_CONTINUE;
	}

	// if we had no one to attack, we were successful, so go to the next state.
	return STATE_SUCCESS;
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardOuterState::update()
{
	if (GXROW_AIGUARDOUTERSTATE.attackState==nullptr) return STATE_SUCCESS;

	// if the position has moved (IE we're guarding an object), move with it.
	Object* targetToGuard = getGuardMachine()->findTargetToGuardByID();
	if (targetToGuard)
	{
		GXROW_AIGUARDOUTERSTATE.exitConditions.gxRow().center = *targetToGuard->getPosition();
	}

	Object* goalObj = GXROW_AIGUARDOUTERSTATE.attackState->getMachineGoalObject();
	if (goalObj)
	{
		Coord3D deltaAggr;
		deltaAggr.x = GXROW_AIGUARDOUTERSTATE.exitConditions.gxRow().center.x - goalObj->getPosition()->x;
		deltaAggr.y = GXROW_AIGUARDOUTERSTATE.exitConditions.gxRow().center.y - goalObj->getPosition()->y;
		deltaAggr.z = GXROW_AIGUARDOUTERSTATE.exitConditions.gxRow().center.z - goalObj->getPosition()->z;
		Real visionSqr = sqr(AIGuardMachine::getStdGuardRange(getMachineOwner()));
		if (deltaAggr.lengthSqr() <= visionSqr)
		{
			// reset the counter
			GXROW_AIGUARDOUTERSTATE.exitConditions.gxRow().attackGiveUpFrame = TheGameLogic->getFrame() + TheAI->getAiData()->m_guardChaseUnitFrames;
		}
	}

	return GXROW_AIGUARDOUTERSTATE.attackState->update();
}

//--------------------------------------------------------------------------------------
void AIGuardOuterState::onExit( StateExitType status )
{
	if (GXROW_AIGUARDOUTERSTATE.attackState)
	{
		GXROW_AIGUARDOUTERSTATE.attackState->onExit(status);
		deleteInstance(GXROW_AIGUARDOUTERSTATE.attackState);
		GXROW_AIGUARDOUTERSTATE.attackState = nullptr;
	}
}

//-- AIGuardReturnState ----------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardReturnState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardReturnState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedInt(&GXROW_AIGUARDRETURNSTATE.nextReturnScanTime);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardReturnState::loadPostProcess()
{
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardReturnState::onEnter()
{
	UnsignedInt now = TheGameLogic->getFrame();
	GXROW_AIGUARDRETURNSTATE.nextReturnScanTime = now + GameLogicRandomValue(0, TheAI->getAiData()->m_guardEnemyReturnScanRate);

// no, no, no, don't do this in onEnter, unless you like really slow maps. (srj)
//	if (getGuardMachine()->lookForInnerTarget())
//		return STATE_FAILURE; // early termination because we found a target.

	Object* targetToGuard = getGuardMachine()->findTargetToGuardByID();
	AIInternalMoveToState::gxRow().goalPosition = targetToGuard ? *targetToGuard->getPosition() : *getGuardMachine()->getPositionToGuard();

	const PolygonTrigger *area = getGuardMachine()->getAreaToGuard();
	if (area)
	{
		area->getCenterPoint(&AIInternalMoveToState::gxRow().goalPosition);
	}
	AIUpdateInterface *ai = getMachineOwner()->getAIUpdateInterface();
	if (ai && ai->isDoingGroundMovement())
	{
		TheAI->pathfinder()->adjustDestination(getMachineOwner(), ai->getLocomotorSet(), &AIInternalMoveToState::gxRow().goalPosition);
	}
	setAdjustsDestination(true);
	return AIInternalMoveToState::onEnter();
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardReturnState::update()
{
	UnsignedInt now = TheGameLogic->getFrame();
	if (now >= GXROW_AIGUARDRETURNSTATE.nextReturnScanTime)
	{
		GXROW_AIGUARDRETURNSTATE.nextReturnScanTime = now + TheAI->getAiData()->m_guardEnemyReturnScanRate;
		if (getGuardMachine()->lookForInnerTarget())
			return STATE_FAILURE; // early termination because we found a target.
	}

	// Just let the return movement finish.
	return AIInternalMoveToState::update();
}

//--------------------------------------------------------------------------------------
void AIGuardReturnState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );
}

//-- AIGuardIdleState ----------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardIdleState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardIdleState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedInt(&GXROW_AIGUARDIDLESTATE.nextEnemyScanTime);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardIdleState::loadPostProcess()
{
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardIdleState::onEnter()
{
	// first time thru, use a random amount so that everyone doesn't scan on the same frame,
	// to avoid "spikes".
	UnsignedInt now = TheGameLogic->getFrame();
	GXROW_AIGUARDIDLESTATE.nextEnemyScanTime = now + GameLogicRandomValue(0, TheAI->getAiData()->m_guardEnemyScanRate);

	return STATE_CONTINUE;
}

//--------------------------------------------------------------------------------------
StateReturnType AIGuardIdleState::update()
{
	//DEBUG_LOG(("AIGuardIdleState frame %d: %08lx",TheGameLogic->getFrame(),getMachineOwner()));

	UnsignedInt now = TheGameLogic->getFrame();
	if (now < GXROW_AIGUARDIDLESTATE.nextEnemyScanTime)
		return STATE_SLEEP(GXROW_AIGUARDIDLESTATE.nextEnemyScanTime - now);

	GXROW_AIGUARDIDLESTATE.nextEnemyScanTime = now + TheAI->getAiData()->m_guardEnemyScanRate;

#ifdef STATE_MACHINE_DEBUG
	//getMachine()->setDebugOutput(true);
#endif
	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAIUpdateInterface();
	// Check to see if we have created a crate we need to pick up.
	if (ai->getCrateID() != INVALID_ID)
	{
		getMachine()->setState(AI_GUARD_GET_CRATE);
		return STATE_SLEEP(GXROW_AIGUARDIDLESTATE.nextEnemyScanTime - now);
	}

	// if anyone is in the inner area, return success.
	if (getGuardMachine()->lookForInnerTarget())
	{
		return STATE_SUCCESS;	// Transitions to AIGuardInnerState.
	}

	// See if the object we are guarding moved.
	Object* targetToGuard = getGuardMachine()->findTargetToGuardByID();
	if (targetToGuard)
	{
		Coord3D pos = *targetToGuard->getPosition();
		Real delta = GXROW_AIGUARDIDLESTATE.guardeePos.x-pos.x;
		if (delta*delta > 4*PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F) {
			GXROW_AIGUARDIDLESTATE.guardeePos = pos;
			return STATE_FAILURE; // goes to AIGuardReturnState.
		}
		delta = GXROW_AIGUARDIDLESTATE.guardeePos.y-pos.y;
		if (delta*delta > 4*PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F) {
			GXROW_AIGUARDIDLESTATE.guardeePos = pos;
			return STATE_FAILURE; // goes to AIGuardReturnState.
		}
	}
	return STATE_SLEEP(GXROW_AIGUARDIDLESTATE.nextEnemyScanTime - now);
}

//--------------------------------------------------------------------------------------
void AIGuardIdleState::onExit( StateExitType status )
{

}

//-- AIGuardPickUpCrateState ----------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AIGuardPickUpCrateState::AIGuardPickUpCrateState( StateMachine *machine )	: AIPickUpCrateState(machine)
{
#ifdef STATE_MACHINE_DEBUG
		setName("AIGuardPickUpCrateState");
#endif
}
//--------------------------------------------------------------------------------------
StateReturnType AIGuardPickUpCrateState::onEnter()
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
StateReturnType AIGuardPickUpCrateState::update()
{
	return AIPickUpCrateState::update();
}

//--------------------------------------------------------------------------------------
void AIGuardPickUpCrateState::onExit( StateExitType status )
{

}

//-- AIGuardAttackAggressorState ------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AIGuardAttackAggressorState::AIGuardAttackAggressorState( StateMachine *machine ) : State( machine, "AIGuardAttackAggressorState" )
{
	m_gxAIGuardAttackAggressorStateRow = TheGxAIGuardAttackAggressorStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

	GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState = nullptr;
}

// ------------------------------------------------------------------------------------------------
AIGuardAttackAggressorState::~AIGuardAttackAggressorState()
{
	deleteInstance(GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState);
	TheGxAIGuardAttackAggressorStates.release( m_gxAIGuardAttackAggressorStateRow );	// web port (round 5)
}

//-------------------------------------------------------------------------------------------------
StateReturnType AIGuardAttackAggressorState::onEnter()
{
	Object *obj = getMachineOwner();
	ObjectID nemID = INVALID_ID;

	if (obj->getBodyModule() && obj->getBodyModule()->getLastDamageInfo()->in.m_sourceID) {
		nemID = obj->getBodyModule()->getLastDamageInfo()->in.m_sourceID;
		getGuardMachine()->setNemesisID(nemID);
	}

	Object *nemesis = TheGameLogic->findObjectByID(getGuardMachine()->getNemesisID());
	if (nemesis == nullptr)
	{
		DEBUG_LOG(("Unexpected null nemesis in AIGuardAttackAggressorState."));
		return STATE_SUCCESS;
	}

	Object* targetToGuard = getGuardMachine()->findTargetToGuardByID();
	Coord3D pos = targetToGuard ? *targetToGuard->getPosition() : *getGuardMachine()->getPositionToGuard();
	//Don't allow guarding units to leave their guard radius!
	GXROW_AIGUARDATTACKAGGRESSORSTATE.exitConditions.gxRow().center = pos;
	GXROW_AIGUARDATTACKAGGRESSORSTATE.exitConditions.gxRow().radiusSqr = sqr(AIGuardMachine::getStdGuardRange(getMachineOwner()));
	GXROW_AIGUARDATTACKAGGRESSORSTATE.exitConditions.gxRow().attackGiveUpFrame = TheGameLogic->getFrame() + TheAI->getAiData()->m_guardChaseUnitFrames;
	GXROW_AIGUARDATTACKAGGRESSORSTATE.exitConditions.gxRow().conditionsToConsider = (ExitConditions::ATTACK_ExitIfExpiredDuration |
																						 ExitConditions::ATTACK_ExitIfNoUnitFound |
																						 ExitConditions::ATTACK_ExitIfOutsideRadius );

	GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState = newInstance(AIAttackState)(getMachine(), true, true, false, &GXROW_AIGUARDATTACKAGGRESSORSTATE.exitConditions);
	GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState->getMachine()->setGoalObject(nemesis);

	StateReturnType returnVal = GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState->onEnter();
	if (returnVal == STATE_CONTINUE) {
		return STATE_CONTINUE;
	}

	// if we had no one to attack, we were successful, so go to the next state.
	return STATE_SUCCESS;
}

//-------------------------------------------------------------------------------------------------
StateReturnType AIGuardAttackAggressorState::update()
{
	if (GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState==nullptr) return STATE_SUCCESS;
	// if the position has moved (IE we're guarding an object), move with it.
	Object* targetToGuard = getGuardMachine()->findTargetToGuardByID();
	if (targetToGuard)
	{
		GXROW_AIGUARDATTACKAGGRESSORSTATE.exitConditions.gxRow().center = *targetToGuard->getPosition();
	}

	return GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState->update();
}

//-------------------------------------------------------------------------------------------------
void AIGuardAttackAggressorState::onExit( StateExitType status )
{
	Object *obj = getMachineOwner();
	if (GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState)
	{
		GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState->onExit(status);
		deleteInstance(GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState);
		GXROW_AIGUARDATTACKAGGRESSORSTATE.attackState = nullptr;
	}

	if (obj->getTeam())
	{
		obj->getTeam()->setTeamTargetObject(nullptr); // clear the target.
	}
}

//-------------------------------------------------------------------------------------------------
void AIGuardAttackAggressorState::crc( Xfer *xfer )
{

}

//-------------------------------------------------------------------------------------------------
void AIGuardAttackAggressorState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

}

//-------------------------------------------------------------------------------------------------
void AIGuardAttackAggressorState::loadPostProcess()
{
	onEnter();
}

AIGuardIdleState::AIGuardIdleState( StateMachine *machine ) : State( machine, "AIGuardIdleState" )
{
	m_gxAIGuardIdleStateRow = TheGxAIGuardIdleStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
 }

AIGuardIdleState::~AIGuardIdleState()
{
	TheGxAIGuardIdleStates.release( m_gxAIGuardIdleStateRow );	// web port (round 5)
}

AIGuardInnerState::AIGuardInnerState( StateMachine *machine ) : State( machine, "AIGuardInner" )
{
	m_gxAIGuardInnerStateRow = TheGxAIGuardInnerStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().attackState = nullptr;
		gxRow().enterState = nullptr;
	}

AIGuardOuterState::AIGuardOuterState( StateMachine *machine ) : State( machine, "AIGuardOuter" )
{
	m_gxAIGuardOuterStateRow = TheGxAIGuardOuterStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().attackState = nullptr;
	}

AIGuardReturnState::AIGuardReturnState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIGuardReturn" )
{
	m_gxAIGuardReturnStateRow = TheGxAIGuardReturnStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().nextReturnScanTime = 0;
	}

AIGuardReturnState::~AIGuardReturnState()
{
	TheGxAIGuardReturnStates.release( m_gxAIGuardReturnStateRow );	// web port (round 5)
}

ExitConditions::ExitConditions()
{
	m_gxExitConditionsRow = TheGxExitConditionss.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_EXITCONDITIONS.attackGiveUpFrame = 0;
	GXROW_EXITCONDITIONS.conditionsToConsider = 0;
	GXROW_EXITCONDITIONS.radiusSqr = 0.0f;

		gxRow().center.zero();
	}

ExitConditions::~ExitConditions()
{
	TheGxExitConditionss.release( m_gxExitConditionsRow );	// web port (round 5)
}

