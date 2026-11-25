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

// AIStates.cpp
// Implementation of AI behavior states
// Author: Michael S. Booth, January 2002
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine


#include "Common/ActionManager.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/CRCDebug.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"

#include "GameClient/ControlBar.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"

#include "GameLogic/AIDock.h"
#include "GameLogic/AIGuard.h"
#include "GameLogic/AIGuardRetaliate.h"
#include "GameLogic/AITNGuard.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Squad.h"
#include "GameLogic/TurretAI.h"
#include "GameLogic/Weapon.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/StealthUpdate.h"

// Web port (round 5, units are pure data): AIWanderState's per-unit state is a row of TheGxAIWanderStates;
// GXROW_AIWANDERSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIWanderStateRow> TheGxAIWanderStates;
#define GXROW_AIWANDERSTATE (TheGxAIWanderStates.at(m_gxAIWanderStateRow))


// Web port (round 5, units are pure data): AIWanderInPlaceState's per-unit state is a row of TheGxAIWanderInPlaceStates;
// GXROW_AIWANDERINPLACESTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIWanderInPlaceStateRow> TheGxAIWanderInPlaceStates;
#define GXROW_AIWANDERINPLACESTATE (TheGxAIWanderInPlaceStates.at(m_gxAIWanderInPlaceStateRow))


// Web port (round 5, units are pure data): AITunnelNetworkGuardState's per-unit state is a row of TheGxAITunnelNetworkGuardStates;
// GXROW_AITUNNELNETWORKGUARDSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAITunnelNetworkGuardStateRow> TheGxAITunnelNetworkGuardStates;
#define GXROW_AITUNNELNETWORKGUARDSTATE (TheGxAITunnelNetworkGuardStates.at(m_gxAITunnelNetworkGuardStateRow))


// Web port (round 5, units are pure data): AIRappelState's per-unit state is a row of TheGxAIRappelStates;
// GXROW_AIRAPPELSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIRappelStateRow> TheGxAIRappelStates;
#define GXROW_AIRAPPELSTATE (TheGxAIRappelStates.at(m_gxAIRappelStateRow))


// Web port (round 5, units are pure data): AIPanicState's per-unit state is a row of TheGxAIPanicStates;
// GXROW_AIPANICSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIPanicStateRow> TheGxAIPanicStates;
#define GXROW_AIPANICSTATE (TheGxAIPanicStates.at(m_gxAIPanicStateRow))


// Web port (round 5, units are pure data): AIMoveToState's per-unit state is a row of TheGxAIMoveToStates;
// GXROW_AIMOVETOSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIMoveToStateRow> TheGxAIMoveToStates;
#define GXROW_AIMOVETOSTATE (TheGxAIMoveToStates.at(m_gxAIMoveToStateRow))


// Web port (round 5, units are pure data): AIMoveAndEvacuateState's per-unit state is a row of TheGxAIMoveAndEvacuateStates;
// GXROW_AIMOVEANDEVACUATESTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIMoveAndEvacuateStateRow> TheGxAIMoveAndEvacuateStates;
#define GXROW_AIMOVEANDEVACUATESTATE (TheGxAIMoveAndEvacuateStates.at(m_gxAIMoveAndEvacuateStateRow))


// Web port (round 5, units are pure data): AIMoveAndDeleteState's per-unit state is a row of TheGxAIMoveAndDeleteStates;
// GXROW_AIMOVEANDDELETESTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIMoveAndDeleteStateRow> TheGxAIMoveAndDeleteStates;
#define GXROW_AIMOVEANDDELETESTATE (TheGxAIMoveAndDeleteStates.at(m_gxAIMoveAndDeleteStateRow))


// Web port (round 5, units are pure data): AIIdleState's per-unit state is a row of TheGxAIIdleStates;
// GXROW_AIIDLESTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIIdleStateRow> TheGxAIIdleStates;
#define GXROW_AIIDLESTATE (TheGxAIIdleStates.at(m_gxAIIdleStateRow))


// Web port (round 5, units are pure data): AIHuntState's per-unit state is a row of TheGxAIHuntStates;
// GXROW_AIHUNTSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIHuntStateRow> TheGxAIHuntStates;
#define GXROW_AIHUNTSTATE (TheGxAIHuntStates.at(m_gxAIHuntStateRow))


// Web port (round 5, units are pure data): AIGuardState's per-unit state is a row of TheGxAIGuardStates;
// GXROW_AIGUARDSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardStateRow> TheGxAIGuardStates;
#define GXROW_AIGUARDSTATE (TheGxAIGuardStates.at(m_gxAIGuardStateRow))


// Web port (round 5, units are pure data): AIGuardRetaliateState's per-unit state is a row of TheGxAIGuardRetaliateStates;
// GXROW_AIGUARDRETALIATESTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIGuardRetaliateStateRow> TheGxAIGuardRetaliateStates;
#define GXROW_AIGUARDRETALIATESTATE (TheGxAIGuardRetaliateStates.at(m_gxAIGuardRetaliateStateRow))


// Web port (round 5, units are pure data): AIFollowWaypointPathExactState's per-unit state is a row of TheGxAIFollowWaypointPathExactStates;
// GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIFollowWaypointPathExactStateRow> TheGxAIFollowWaypointPathExactStates;
#define GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE (TheGxAIFollowWaypointPathExactStates.at(m_gxAIFollowWaypointPathExactStateRow))


// Web port (round 5, units are pure data): AIFollowPathState's per-unit state is a row of TheGxAIFollowPathStates;
// GXROW_AIFOLLOWPATHSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIFollowPathStateRow> TheGxAIFollowPathStates;
#define GXROW_AIFOLLOWPATHSTATE (TheGxAIFollowPathStates.at(m_gxAIFollowPathStateRow))


// Web port (round 5, units are pure data): AIFaceState's per-unit state is a row of TheGxAIFaceStates;
// GXROW_AIFACESTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIFaceStateRow> TheGxAIFaceStates;
#define GXROW_AIFACESTATE (TheGxAIFaceStates.at(m_gxAIFaceStateRow))


// Web port (round 5, units are pure data): AIExitState's per-unit state is a row of TheGxAIExitStates;
// GXROW_AIEXITSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIExitStateRow> TheGxAIExitStates;
#define GXROW_AIEXITSTATE (TheGxAIExitStates.at(m_gxAIExitStateRow))


// Web port (round 5, units are pure data): AIExitInstantlyState's per-unit state is a row of TheGxAIExitInstantlyStates;
// GXROW_AIEXITINSTANTLYSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIExitInstantlyStateRow> TheGxAIExitInstantlyStates;
#define GXROW_AIEXITINSTANTLYSTATE (TheGxAIExitInstantlyStates.at(m_gxAIExitInstantlyStateRow))


// Web port (round 5, units are pure data): AIDockState's per-unit state is a row of TheGxAIDockStates;
// GXROW_AIDOCKSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIDockStateRow> TheGxAIDockStates;
#define GXROW_AIDOCKSTATE (TheGxAIDockStates.at(m_gxAIDockStateRow))


// Web port (round 5, units are pure data): AIAttackState's per-unit state is a row of TheGxAIAttackStates;
// GXROW_AIATTACKSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIAttackStateRow> TheGxAIAttackStates;
#define GXROW_AIATTACKSTATE (TheGxAIAttackStates.at(m_gxAIAttackStateRow))


// Web port (round 5, units are pure data): AIAttackSquadState's per-unit state is a row of TheGxAIAttackSquadStates;
// GXROW_AIATTACKSQUADSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIAttackSquadStateRow> TheGxAIAttackSquadStates;
#define GXROW_AIATTACKSQUADSTATE (TheGxAIAttackSquadStates.at(m_gxAIAttackSquadStateRow))


// Web port (round 5, units are pure data): AIAttackPursueTargetState's per-unit state is a row of TheGxAIAttackPursueTargetStates;
// GXROW_AIATTACKPURSUETARGETSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIAttackPursueTargetStateRow> TheGxAIAttackPursueTargetStates;
#define GXROW_AIATTACKPURSUETARGETSTATE (TheGxAIAttackPursueTargetStates.at(m_gxAIAttackPursueTargetStateRow))


// Web port (round 5, units are pure data): AIAttackFollowWaypointPathState's per-unit state is a row of TheGxAIAttackFollowWaypointPathStates;
// GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIAttackFollowWaypointPathStateRow> TheGxAIAttackFollowWaypointPathStates;
#define GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE (TheGxAIAttackFollowWaypointPathStates.at(m_gxAIAttackFollowWaypointPathStateRow))


// Web port (round 5, units are pure data): AIAttackAreaState's per-unit state is a row of TheGxAIAttackAreaStates;
// GXROW_AIATTACKAREASTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIAttackAreaStateRow> TheGxAIAttackAreaStates;
#define GXROW_AIATTACKAREASTATE (TheGxAIAttackAreaStates.at(m_gxAIAttackAreaStateRow))


// Web port (round 5, units are pure data): AIAttackApproachTargetState's per-unit state is a row of TheGxAIAttackApproachTargetStates;
// GXROW_AIATTACKAPPROACHTARGETSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIAttackApproachTargetStateRow> TheGxAIAttackApproachTargetStates;
#define GXROW_AIATTACKAPPROACHTARGETSTATE (TheGxAIAttackApproachTargetStates.at(m_gxAIAttackApproachTargetStateRow))


// Web port (round 5, units are pure data): AIAttackAimAtTargetState's per-unit state is a row of TheGxAIAttackAimAtTargetStates;
// GXROW_AIATTACKAIMATTARGETSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIAttackAimAtTargetStateRow> TheGxAIAttackAimAtTargetStates;
#define GXROW_AIATTACKAIMATTARGETSTATE (TheGxAIAttackAimAtTargetStates.at(m_gxAIAttackAimAtTargetStateRow))


// Web port (round 5, units are pure data): AIEnterState's per-unit state is a row of TheGxAIEnterStates;
// GXROW_AIENTERSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIEnterStateRow> TheGxAIEnterStates;
#define GXROW_AIENTERSTATE (TheGxAIEnterStates.at(m_gxAIEnterStateRow))


// Web port (round 5, units are pure data): AIPickUpCrateState's per-unit state is a row of TheGxAIPickUpCrateStates;
// GXROW_AIPICKUPCRATESTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIPickUpCrateStateRow> TheGxAIPickUpCrateStates;
#define GXROW_AIPICKUPCRATESTATE (TheGxAIPickUpCrateStates.at(m_gxAIPickUpCrateStateRow))


// Web port (round 5, units are pure data): AIMoveAndTightenState's per-unit state is a row of TheGxAIMoveAndTightenStates;
// GXROW_AIMOVEANDTIGHTENSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIMoveAndTightenStateRow> TheGxAIMoveAndTightenStates;
#define GXROW_AIMOVEANDTIGHTENSTATE (TheGxAIMoveAndTightenStates.at(m_gxAIMoveAndTightenStateRow))


// Web port (round 5, units are pure data): AIFollowWaypointPathState's per-unit state is a row of TheGxAIFollowWaypointPathStates;
// GXROW_AIFOLLOWWAYPOINTPATHSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIFollowWaypointPathStateRow> TheGxAIFollowWaypointPathStates;
#define GXROW_AIFOLLOWWAYPOINTPATHSTATE (TheGxAIFollowWaypointPathStates.at(m_gxAIFollowWaypointPathStateRow))


// Web port (round 5, units are pure data): AIInternalMoveToState's per-unit state is a row of TheGxAIInternalMoveToStates;
// GXROW_AIINTERNALMOVETOSTATE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxAIInternalMoveToStateRow> TheGxAIInternalMoveToStates;
#define GXROW_AIINTERNALMOVETOSTATE (TheGxAIInternalMoveToStates.at(m_gxAIInternalMoveToStateRow))



static Bool cannotPossiblyAttackObject( State *thisState, void* userData );

//----------------------------------------------------------------------------------------------------------
AICommandParms::AICommandParms(AICommandType cmd, CommandSourceType cmdSource) :
	m_cmd(cmd),
	m_cmdSource(cmdSource),
	m_obj(nullptr),
	m_otherObj(nullptr),
	m_team(nullptr),
	m_waypoint(nullptr),
	m_polygon(nullptr),
	m_intValue(0),
	m_commandButton(nullptr),
	m_path(nullptr)
{
		m_pos.zero();
		m_coords.clear();
}

//----------------------------------------------------------------------------------------------------------
void AICommandParmsStorage::store(const AICommandParms& parms)
{
	m_cmd = parms.m_cmd;
  m_cmdSource = parms.m_cmdSource;
  m_pos = parms.m_pos;
  m_obj = parms.m_obj ? parms.m_obj->getID() : INVALID_ID;
  m_otherObj = parms.m_otherObj ? parms.m_otherObj->getID() : INVALID_ID;
  m_teamName = parms.m_team ? parms.m_team->getName() : AsciiString::TheEmptyString;
	m_coords = parms.m_coords;
  m_waypoint = parms.m_waypoint;
  m_polygon = parms.m_polygon;
  m_intValue = parms.m_intValue;       /// misc usage
  m_damage = parms.m_damage;
	m_commandButton = parms.m_commandButton;
	m_path = parms.m_path;	/// @todo srj -- probably need a better way to safely save/restore this
}

//----------------------------------------------------------------------------------------------------------
void AICommandParmsStorage::reconstitute(AICommandParms& parms) const
{
	parms.m_cmd = m_cmd;
  parms.m_cmdSource = m_cmdSource;
  parms.m_pos = m_pos;
  parms.m_obj = TheGameLogic->findObjectByID(m_obj);
  parms.m_otherObj = TheGameLogic->findObjectByID(m_otherObj);
  parms.m_team = TheTeamFactory->findTeam(m_teamName);
	parms.m_coords = m_coords;
  parms.m_waypoint = m_waypoint;
  parms.m_polygon = m_polygon;
  parms.m_intValue = m_intValue;
  parms.m_damage = m_damage;
	parms.m_commandButton = m_commandButton;
	parms.m_path = m_path;	/// @todo srj -- probably need a better way to safely save/restore this
}

//----------------------------------------------------------------------------------------------------------
void AICommandParmsStorage::doXfer(Xfer *xfer)
{
	xfer->xferUser(&m_cmd, sizeof(m_cmd));
	xfer->xferUser(&m_cmd, sizeof(m_cmdSource));
	xfer->xferCoord3D(&m_pos);
	xfer->xferObjectID(&m_obj);
	xfer->xferObjectID(&m_otherObj);
	xfer->xferAsciiString(&m_teamName);
	Int numCoords = m_coords.size();
	xfer->xferInt(&numCoords);
	Int i;
	if (xfer->getXferMode() == XFER_LOAD)
	{
		for (i=0; i<numCoords; i++) {
			Coord3D pos;
			xfer->xferCoord3D(&pos);
			m_coords.push_back(pos);
		}
	} else {
		for (i=0; i<numCoords; i++) {
			Coord3D pos = m_coords[i];
			xfer->xferCoord3D(&pos);
		}
	}

 	UnsignedInt id = INVALID_WAYPOINT_ID;
	if (m_waypoint) {
		id = m_waypoint->getID();
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD && id!= INVALID_WAYPOINT_ID)
	{
		m_waypoint = TheTerrainLogic->getWaypointByID(id);
	}

	AsciiString triggerName;
	if (m_polygon) triggerName = m_polygon->getTriggerName();
	xfer->xferAsciiString(&triggerName);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (triggerName.isNotEmpty()) {
			m_polygon = TheTerrainLogic->getTriggerAreaByName(triggerName);
		}
	}

	xfer->xferInt(&m_intValue);

	xfer->xferSnapshot(&m_damage);

	AsciiString cmdName;
	if (m_commandButton) {
		cmdName = m_commandButton->getName();
	}
	xfer->xferAsciiString(&cmdName);
	if (cmdName.isNotEmpty() && m_commandButton==nullptr) {
		m_commandButton = TheControlBar->findCommandButton(cmdName);
	}

	Bool hasPath = m_path!=nullptr;
	xfer->xferBool(&hasPath);
	if (hasPath && m_path==nullptr) {
		m_path = newInstance(Path);
	}
	if (hasPath) {
		xfer->xferSnapshot(m_path);
	}

}

//----------------------------------------------------------------------------------------------------------
/**
 * Compare two positions to see if they are logically equal
 * @todo Move this somewhere more useful (MSB)
 */
static Bool isSamePosition( const Coord3D *ourPos, const Coord3D *prevTargetPos, const Coord3D *curTargetPos )
{
	Coord3D diff;

	// for pathfinding purposes, only care about 2d pos. (srj)
	diff.x = curTargetPos->x - prevTargetPos->x;
	diff.y = curTargetPos->y - prevTargetPos->y;

	Coord3D toTarget;
 	// for pathfinding purposes, only care about 2d pos. (srj)
	toTarget.x = curTargetPos->x - ourPos->x;
	toTarget.y = curTargetPos->y - ourPos->y;

	// Tolerance is (dist/10)squared.
	const Real TOLERANCE_FACTOR = 1.0f / (10.0f * 10.0f);
	Real toleranceSqr = (toTarget.x*toTarget.x+toTarget.y*toTarget.y) * TOLERANCE_FACTOR;

	if (diff.x * diff.x + diff.y * diff.y > toleranceSqr)
		return false;

	return true;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AttackStateMachine::crc( Xfer *xfer )
{
	StateMachine::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AttackStateMachine::xfer( Xfer *xfer )
{
	XferVersion cv = 1;
	XferVersion v = cv;
	xfer->xferVersion( &v, cv );

	StateMachine::xfer(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AttackStateMachine::loadPostProcess()
{
	StateMachine::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
static Bool inWeaponRangeObject(State *thisState, void* userData);

//----------------------------------------------------------------------------------------------------------
/**
 * Create an AI state machine. Define all of the states the machine
 * can possibly be in, and set the initial (default) state.
 */
AttackStateMachine::AttackStateMachine( Object *obj, AIAttackState* att, AsciiString name, Bool follow, Bool attackingObject, Bool forceAttacking )
	: StateMachine( obj, name )
{
	// we want to use the CONTINUE mode (not NEW) since we already have acquired the target.
	static const StateConditionInfo objectConditionsNormal[] =
	{
		StateConditionInfo(outOfWeaponRangeObject, AttackStateMachine::CHASE_TARGET, nullptr),
		StateConditionInfo(wantToSquishTarget, AttackStateMachine::CHASE_TARGET, nullptr),
		StateConditionInfo(cannotPossiblyAttackObject, EXIT_MACHINE_WITH_FAILURE, (void*)ATTACK_CONTINUED_TARGET),
		StateConditionInfo(nullptr, INVALID_STATE_ID, nullptr)
	};

	// we want to use the CONTINUE mode (not NEW) since we already have acquired the target.
	static const StateConditionInfo objectConditionsForced[] =
	{
		StateConditionInfo(outOfWeaponRangeObject, AttackStateMachine::CHASE_TARGET, nullptr),
		StateConditionInfo(cannotPossiblyAttackObject, EXIT_MACHINE_WITH_FAILURE, (void*)ATTACK_CONTINUED_TARGET_FORCED),
		StateConditionInfo(wantToSquishTarget, AttackStateMachine::CHASE_TARGET, nullptr),
		StateConditionInfo(nullptr, INVALID_STATE_ID, nullptr)
	};

	const StateConditionInfo* objectConditions = forceAttacking ? objectConditionsForced : objectConditionsNormal;

	static const StateConditionInfo positionConditions[] =
	{
		StateConditionInfo(outOfWeaponRangePosition, AttackStateMachine::CHASE_TARGET, nullptr),
		StateConditionInfo(nullptr, INVALID_STATE_ID, nullptr)
	};

#ifdef STATE_MACHINE_DEBUG
		AsciiString fullName = name;
		if (follow) fullName.concat(" follow");
		if (attackingObject) fullName.concat(" object");
		setName(fullName);
		//setDebugOutput(true);
#endif

	// order matters: first state is the default state.
	// The default is Aim rather than Approach so things that cannot move will be able to shoot
	// things that are in range.  Things that cannot move will automatically FAILURE on approach state.
	/*
		This state will succeed when we are aiming a useful weapon at the victim, and fail
		if the victim is dead. (Exception: if the weapon is on a turret, we don't leave this
		state unless we get out of range.)
	*/
	defineState(	AttackStateMachine::AIM_AT_TARGET,
								newInstance(AIAttackAimAtTargetState)( this, attackingObject, forceAttacking ),
								AttackStateMachine::FIRE_WEAPON,
								EXIT_MACHINE_WITH_FAILURE,
								attackingObject ? objectConditions : positionConditions );

	/*
		Note that the fire state succeeds iff it is able to fire... it will "fail"
		if unable to fire. However, it may be unable to fire because the target object
		is already dead.
	*/
	defineState( AttackStateMachine::FIRE_WEAPON,
								newInstance(AIAttackFireWeaponState)( this, att ),
								AttackStateMachine::AIM_AT_TARGET,
								AttackStateMachine::AIM_AT_TARGET,
								attackingObject ? objectConditions : positionConditions );


	if (obj->isKindOf(KINDOF_IMMOBILE) == FALSE)
	{
		if (obj->isKindOf(KINDOF_PORTABLE_STRUCTURE) && obj->isKindOf(KINDOF_CAN_ATTACK))
		{
			static const StateConditionInfo portableStructureChaseConditions[] =
			{
				StateConditionInfo(inWeaponRangeObject, AttackStateMachine::AIM_AT_TARGET, nullptr),
				StateConditionInfo(nullptr, INVALID_STATE_ID, nullptr)
			};

			/* we're a rider on a mobile object, so we can't control our motion.
				just make bogus states that always fall back into "aim".
			*/
			defineState(	AttackStateMachine::CHASE_TARGET,
										newInstance(ContinueState)(this),
										EXIT_MACHINE_WITH_FAILURE,
										EXIT_MACHINE_WITH_FAILURE,
										portableStructureChaseConditions );
		}
		else if (attackingObject)
		{
			/*
				This state will pursue a target that is moving away from it.  If it is not moving away,
				it will drop into the AIAttackApproachTarget state.
			*/
			defineState(	AttackStateMachine::CHASE_TARGET,
										newInstance(AIAttackPursueTargetState)( this, follow, attackingObject, forceAttacking ),
										AttackStateMachine::APPROACH_TARGET,
										AttackStateMachine::APPROACH_TARGET );

			/*
				This state will succeed when we have a useful weapon within range of victim, and fail
				if the victim is dead
			*/
			defineState(	AttackStateMachine::APPROACH_TARGET,
										newInstance(AIAttackApproachTargetState)( this, follow, attackingObject, forceAttacking ),
										AttackStateMachine::AIM_AT_TARGET,
										EXIT_MACHINE_WITH_FAILURE );
		}
		else
		{
			/*
				This state will succeed when we have a useful weapon within range of victim, and fail
				if the victim is dead
			*/
			defineState(	AttackStateMachine::CHASE_TARGET,
										newInstance(AIAttackApproachTargetState)( this, follow, attackingObject, forceAttacking ),
										AttackStateMachine::AIM_AT_TARGET,
										EXIT_MACHINE_WITH_FAILURE );

			/*
				This state will succeed when we have a useful weapon within range of victim, and fail
				if the victim is dead
			*/
			defineState(	AttackStateMachine::APPROACH_TARGET,
										newInstance(AIAttackApproachTargetState)( this, follow, attackingObject, forceAttacking ),
										AttackStateMachine::AIM_AT_TARGET,
										EXIT_MACHINE_WITH_FAILURE );
		}

	}
	else
	{
		/*
			This state always instantly fails, so when immobile things transition here, we bail.
		*/
		defineState(	AttackStateMachine::CHASE_TARGET,
									newInstance(FailureState)(this),
									EXIT_MACHINE_WITH_FAILURE,
									EXIT_MACHINE_WITH_FAILURE );
	}

};

//----------------------------------------------------------------------------------------------------------
AttackStateMachine::~AttackStateMachine()
{
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
static Object* findEnemyInContainer(Object* killer, Object* bldg)
{
	const ContainedItemsList* items = bldg->getContain() ? bldg->getContain()->getContainedItemsList() : nullptr;
	if (items)
	{
		for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
		{
			if ((*it)->isEffectivelyDead())
			{
				DEBUG_CRASH(("why is there a dead thing in this container?"));
				continue;
			}

			// order matters: we want to know if I consider it to be an enemy, not vice versa
			if (killer->getRelationship(*it) == ENEMIES)
			{
				return *it;
			}
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------------------------------------
static Int killEnemiesInContainer(Object* killer, Object* bldg, Int maxToKill)
{
	Int numKilled = 0;
	while (numKilled < maxToKill)
	{
		Object* enemy = findEnemyInContainer(killer, bldg);
		if (enemy)
		{
			Object *containedByObject = enemy->getContainedBy();
			if( containedByObject )
			{
				ContainModuleInterface *contain = containedByObject->getContain();
				if( contain )
				{
					contain->removeFromContain(enemy);
				}
			}
			if (killer)
				killer->scoreTheKill( enemy );
			enemy->kill();
			++numKilled;
		}
		else
		{
			break;
		}
	}
	return numKilled;
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
AIRappelState::AIRappelState( StateMachine *machine ) : State( machine, "AIRappelState" )
{
	m_gxAIRappelStateRow = TheGxAIRappelStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

}

//-----------------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIRappelState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIRappelState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferReal(&GXROW_AIRAPPELSTATE.rappelRate);
	xfer->xferReal(&GXROW_AIRAPPELSTATE.destZ);
	xfer->xferBool(&GXROW_AIRAPPELSTATE.targetIsBldg);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIRappelState::loadPostProcess()
{
}
//-----------------------------------------------------------------------------------------------------------
StateReturnType AIRappelState::onEnter()
{
	Object* obj = getMachineOwner();
	if (!obj->isKindOf(KINDOF_CAN_RAPPEL))
		return STATE_FAILURE;

	//AIUpdateInterface* ai = obj->getAI();

	obj->setModelConditionState(MODELCONDITION_RAPPELLING);
// don't do this, or we'll be unable to be forced out of this state.
// instead, just manipulate physics directly.
//obj->setHeld();
	obj->getPhysics()->resetDynamicPhysics();

	GXROW_AIRAPPELSTATE.targetIsBldg = true;
	Object* bldg = getMachineGoalObject();
	if (bldg == nullptr || bldg->isEffectivelyDead() || !bldg->isKindOf(KINDOF_STRUCTURE))
		GXROW_AIRAPPELSTATE.targetIsBldg = false;

	const Coord3D* pos = obj->getPosition();

	const Bool onlyHealthyBridges = true;	// ignore dead bridges.
	PathfindLayerEnum layerAtDest = TheTerrainLogic->getHighestLayerForDestination(pos, onlyHealthyBridges);
	GXROW_AIRAPPELSTATE.destZ = TheTerrainLogic->getLayerHeight(pos->x, pos->y, layerAtDest);
	if (GXROW_AIRAPPELSTATE.targetIsBldg)
		GXROW_AIRAPPELSTATE.destZ += bldg->getGeometryInfo().getMaxHeightAbovePosition();
	else
		obj->setLayer(layerAtDest);

	AIUpdateInterface *ai = obj->getAI();
	Real MAX_RAPPEL_RATE = fabs(TheGlobalData->m_gravity) * LOGICFRAMES_PER_SECOND * 2.5f;
	GXROW_AIRAPPELSTATE.rappelRate = -min(ai->getDesiredSpeed(), MAX_RAPPEL_RATE);

	return STATE_CONTINUE;
}

//-----------------------------------------------------------------------------------------------------------

StateReturnType AIRappelState::update()
{

	StateReturnType result = STATE_CONTINUE;

	Object* obj = getMachineOwner();
	const Coord3D* pos = obj->getPosition();

	Object* bldg = getMachineGoalObject();
	if (GXROW_AIRAPPELSTATE.targetIsBldg && (bldg == nullptr || bldg->isEffectivelyDead()))
	{
		// if bldg is destroyed, just head for the ground
		// BGC - bldg could be destroyed as they are heading down the rope.
		GXROW_AIRAPPELSTATE.targetIsBldg = false;
		GXROW_AIRAPPELSTATE.destZ = TheTerrainLogic->getGroundHeight(pos->x, pos->y);
	}

	// nuke 2d speed...
	obj->getPhysics()->scrubVelocity2D(0);
	// and clamp z speed to rappel rate (gravity will have accelerated us)
	obj->getPhysics()->scrubVelocityZ(GXROW_AIRAPPELSTATE.rappelRate);

	if (!GXROW_AIRAPPELSTATE.targetIsBldg)
	{
		// if heading for ground, do this every frame... since jitter at the very start
		// can move us slightly, and on uneven ground it might matter.
		GXROW_AIRAPPELSTATE.destZ = TheTerrainLogic->getLayerHeight(pos->x, pos->y, obj->getLayer());
	}
	if (pos->z <= GXROW_AIRAPPELSTATE.destZ)
	{
		Coord3D tmp = *pos;
		tmp.z = GXROW_AIRAPPELSTATE.destZ;
		obj->setPosition(&tmp);

		if (GXROW_AIRAPPELSTATE.targetIsBldg)
		{
			DEBUG_ASSERTCRASH(TheActionManager->canEnterObject(obj, bldg, obj->getAI()->getLastCommandSource(), COMBATDROP_INTO), ("Hmm, this seems unlikely"));
			// if there are enemies... kill up to two. if we kill two, then we die ourselves,
			// otherwise we enter the bldg.
			const Int MAX_TO_KILL = 2;
			Int numKilled = killEnemiesInContainer(obj, bldg, MAX_TO_KILL);

			if (numKilled > 0)
			{
				const FXList* fx = obj->getTemplate()->getPerUnitFX("CombatDropKillFX");
				FXList::doFXObj(fx, bldg, nullptr);
				DEBUG_LOG(("Killing %d enemies in combat drop!",numKilled));
			}

			if (numKilled == MAX_TO_KILL)
			{
				obj->kill();
				DEBUG_LOG(("Killing SELF in combat drop!"));
			}
			else
			{
				if (bldg->getContain() && bldg->getContain()->isValidContainerFor(obj, TRUE ))
				{
					bldg->getContain()->addToContain(obj);
				}
				else
				{
					// this can legitimately happen if you drop into a full (or nearly full) building.
					// let's just place the guy on the ground nearby, since it sucks to fall from the top of a building.
					Real exitAngle = bldg->getOrientation();
					// Garrison doesn't have reserveDoor or exitDelay, so if we do nothing, everyone will appear on top
					// of each other and get stuck inside each others' extent (except for the first guy).  So we'll
					// scatter the start point around a little to make it better.
					Real offset = min(obj->getBoundingCircleRadius(),
														bldg->getBoundingCircleRadius());
					Real angle = GameLogicRandomValueReal( PI, 2*PI );//Downish.
					Coord3D startPosition = *bldg->getPosition();
					startPosition.x += offset * Cos( angle );
					startPosition.y += offset * Sin( angle );
					startPosition.z = TheTerrainLogic->getGroundHeight( startPosition.x, startPosition.y );

					obj->setPosition( &startPosition );
					obj->setOrientation( exitAngle );

					FindPositionOptions options;
					options.startAngle = (Real)(1.5 * PI);//Down.
					options.maxRadius = 200;
					Coord3D endPosition;
					Bool foundPosition = ThePartitionManager->findPositionAround( &startPosition, &options, &endPosition );

					if( foundPosition )
					{
						std::vector<Coord3D> exitPath;
						exitPath.push_back(endPosition);
						AIUpdateInterface* ai = obj->getAI();
						if( ai )
						{
							ai->aiFollowPath( &exitPath, bldg, CMD_FROM_AI );
						}
					}

				}
			}
		}

		result = STATE_SUCCESS;
	}

	return result;
}

//-----------------------------------------------------------------------------------------------------------
void AIRappelState::onExit( StateExitType status )
{
	Object* obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	obj->clearModelConditionState(MODELCONDITION_RAPPELLING);
// don't do this, or we'll be unable to be forced out of this state.
// instead, just manipulate physics directly.
//obj->clearHeld();
	ai->setDesiredSpeed( FAST_AS_POSSIBLE );
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/*

	NOTE NOTE NOTE NOTE NOTE

	Do NOT subclass this unless you want ALL of the states this machine possesses.
	If you only want SOME of the states, please make a new StateMachine, descended
	from StateMachine, NOT AIStateMachine. Thank you. (srj)

	NOTE NOTE NOTE NOTE NOTE

*/
AIStateMachine::AIStateMachine( Object *obj, AsciiString name ) : StateMachine( obj, name )
{
	DEBUG_ASSERTCRASH(getOwner(), ("An AI State Machine '%s' was constructed without an owner, please tell JKMCD", name.str()));
	DEBUG_ASSERTCRASH(getOwner()->getAI(), ("An AI State Machine '%s' was constructed without an AIUpdateInterface, please tell JKMCD", name.str()));

	m_goalPath.clear();
	m_goalWaypoint = nullptr;
	m_goalSquad = nullptr;

	m_temporaryState = nullptr;
	m_temporaryStateFramEnd = 0;

	// order matters: first state is the default state.
	defineState( AI_IDLE,																	newInstance(AIIdleState)( this, AIIdleState::LOOK_FOR_TARGETS), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_TO,															newInstance(AIMoveToState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_OUT_OF_THE_WAY,									newInstance(AIMoveOutOfTheWayState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_AND_TIGHTEN,											newInstance(AIMoveAndTightenState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_AWAY_FROM_REPULSORS,							newInstance(AIMoveAwayFromRepulsorsState)( this ), AI_WANDER_IN_PLACE, AI_WANDER_IN_PLACE );
	defineState( AI_WANDER_IN_PLACE,											newInstance(AIWanderInPlaceState)( this ), AI_MOVE_AWAY_FROM_REPULSORS, AI_MOVE_AWAY_FROM_REPULSORS );
	defineState( AI_ATTACK_MOVE_TO,												newInstance(AIAttackMoveToState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM,					newInstance(AIAttackFollowWaypointPathState)( this, true ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACKFOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS,	newInstance(AIAttackFollowWaypointPathState)( this, false ), AI_IDLE, AI_IDLE );

	defineState( AI_FOLLOW_WAYPOINT_PATH_AS_TEAM,					newInstance(AIFollowWaypointPathState)( this, true ), AI_IDLE, AI_IDLE );
	defineState( AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS,	newInstance(AIFollowWaypointPathState)( this, false ), AI_IDLE, AI_IDLE );
	defineState( AI_FOLLOW_WAYPOINT_PATH_AS_TEAM_EXACT,		newInstance(AIFollowWaypointPathExactState)( this, true ), AI_IDLE, AI_IDLE );
	defineState( AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS_EXACT,newInstance(AIFollowWaypointPathExactState)( this, false ), AI_IDLE, AI_IDLE );
	defineState( AI_FOLLOW_PATH,													newInstance(AIFollowPathState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_FOLLOW_EXITPRODUCTION_PATH,						newInstance(AIFollowPathState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_AND_EVACUATE,					newInstance(AIMoveAndEvacuateState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_MOVE_AND_EVACUATE_AND_EXIT,	newInstance(AIMoveAndEvacuateState)( this ), AI_MOVE_AND_DELETE, AI_MOVE_AND_DELETE );
	defineState( AI_MOVE_AND_DELETE,						newInstance(AIMoveAndDeleteState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_WAIT,												newInstance(AIWaitState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACK_POSITION,						newInstance(AIAttackState)( this, false, false, false,  nullptr ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACK_OBJECT,							newInstance(AIAttackState)( this, false, true, false, nullptr ), AI_IDLE, AI_IDLE );
	defineState( AI_FORCE_ATTACK_OBJECT,				newInstance(AIAttackState)( this, false, true, true, nullptr ), AI_IDLE, AI_IDLE );

	defineState( AI_ATTACK_AND_FOLLOW_OBJECT,		newInstance(AIAttackState)( this, true, true, false, nullptr ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACK_SQUAD,								newInstance(AIAttackSquadState)( this, nullptr ), AI_IDLE, AI_IDLE );
	defineState( AI_WANDER,											newInstance(AIWanderState)( this ), AI_IDLE, AI_MOVE_AWAY_FROM_REPULSORS );
	defineState( AI_PANIC,											newInstance(AIPanicState)( this ), AI_IDLE, AI_MOVE_AWAY_FROM_REPULSORS );
	defineState( AI_DEAD,												newInstance(AIDeadState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_DOCK,												newInstance(AIDockState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_ENTER,											newInstance(AIEnterState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_EXIT,												newInstance(AIExitState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_EXIT_INSTANTLY,							newInstance(AIExitInstantlyState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_GUARD,											newInstance(AIGuardState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_GUARD_TUNNEL_NETWORK,				newInstance(AITunnelNetworkGuardState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_GUARD_RETALIATE,						newInstance(AIGuardRetaliateState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_HUNT,												newInstance(AIHuntState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACK_AREA,								newInstance(AIAttackAreaState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_FACE_OBJECT,								newInstance(AIFaceState)( this, true ), AI_IDLE, AI_IDLE );
	defineState( AI_FACE_POSITION,							newInstance(AIFaceState)( this, false ), AI_IDLE, AI_IDLE );
	defineState( AI_PICK_UP_CRATE,							newInstance(AIPickUpCrateState)( this ), AI_IDLE, AI_IDLE );

	defineState( AI_RAPPEL_INTO,								newInstance(AIRappelState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_BUSY,												newInstance(AIBusyState)( this ), AI_IDLE, AI_IDLE );
}

//----------------------------------------------------------------------------------------------------------
AIStateMachine::~AIStateMachine()
{
	deleteInstance(m_goalSquad);
}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIStateMachine::crc( Xfer *xfer )
{
	StateMachine::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIStateMachine::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
	StateMachine::xfer(xfer);

	Int i;
	Int count = m_goalPath.size();
	xfer->xferInt(&count);
	for (i=0; i<count; i++) {
		Coord3D pos;
		if (xfer->getXferMode() != XFER_LOAD)
		{
			pos = m_goalPath[i];
		}
		xfer->xferCoord3D(&pos);
		if (xfer->getXferMode() == XFER_LOAD)
		{
			m_goalPath.push_back(pos);
		}
	}

	AsciiString waypointName;
	if (m_goalWaypoint) waypointName = m_goalWaypoint->getName();
	xfer->xferAsciiString(&waypointName);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (waypointName.isNotEmpty()) {
			m_goalWaypoint = TheTerrainLogic->getWaypointByName(waypointName);
		}
	}
	Bool hasSquad = (m_goalSquad!=nullptr);
	xfer->xferBool(&hasSquad);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		if (hasSquad && m_goalSquad==nullptr) {
			m_goalSquad = newInstance( Squad );
		}
	}
	if (hasSquad) {
		xfer->xferSnapshot(m_goalSquad);
	}

	StateID id = INVALID_STATE_ID;
	if (m_temporaryState) {
		id = m_temporaryState->getID();
		DEBUG_ASSERTCRASH(id!=INVALID_STATE_ID, ("State has invalid state id, no really. jba."));
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD && id != INVALID_STATE_ID) {
		m_temporaryState = internalGetState( id );
	}
	if (m_temporaryState!=nullptr) {
		xfer->xferSnapshot(m_temporaryState);
	}

	xfer->xferUnsignedInt(&m_temporaryStateFramEnd);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIStateMachine::loadPostProcess()
{
	StateMachine::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
/**
 * Define a simple path
 */
void AIStateMachine::setGoalPath( std::vector<Coord3D>* path )
{
	stl::move_or_swap(m_goalPath, *path);
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
/**
 * Get the current state name.
 */
AsciiString AIStateMachine::getCurrentStateName() const
{
	AsciiString name = StateMachine::getCurrentStateName();

	if (m_temporaryState) {
		name.concat(" /T/");
		name.concat(m_temporaryState->getName());
	}
	return name;
}
#endif

//-----------------------------------------------------------------------------
/**
 * Run one step of the machine
 */
StateReturnType AIStateMachine::updateStateMachine()
{
	//-extraLogging
	#if defined(RTS_DEBUG)
		Bool idle = getOwner()->getAI()->isIdle();
		if( !idle && TheGlobalData->m_extraLogging )
			DEBUG_LOG( ("%d - %s::update() start - %s", TheGameLogic->getFrame(), getCurrentStateName().str(), getOwner()->getTemplate()->getName().str() ) );
	#endif
	//end -extraLogging

	if (m_temporaryState)
	{
		// execute this state
		StateReturnType status = m_temporaryState->update();
		if (m_temporaryStateFramEnd < TheGameLogic->getFrame()) {
			// ran out of time.
			if (status == STATE_CONTINUE) {
				status = STATE_SUCCESS;
			}
		}
		if (status==STATE_CONTINUE)
		{
			//-extraLogging
			#if defined(RTS_DEBUG)
				if( !idle && TheGlobalData->m_extraLogging )
					DEBUG_LOG( (" - RETURN EARLY STATE_CONTINUE") );
			#endif
			//end -extraLogging

			return status;
		}
		m_temporaryState->onExit(EXIT_NORMAL);
		m_temporaryState = nullptr;
	}
	StateReturnType retType = StateMachine::updateStateMachine();

	//-extraLogging
	#if defined(RTS_DEBUG)
		AsciiString result;
		if( TheGlobalData->m_extraLogging )
		{
			switch( retType )
			{
				case STATE_CONTINUE:
					result.format( "CONTINUE" );
					break;
				case STATE_SUCCESS:
					result.format( "SUCCESS" );
					break;
				case STATE_FAILURE:
					result.format( "FAILURE" );
					break;
				default:
					result.format( "UNKNOWN %d", retType );
					break;
			}
			if( !idle )
				DEBUG_LOG( (" - RETURNING %s", result.str() ) );
		}
	#endif
	//end -extraLogging

	return retType;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Change the temporary state of the machine, and number of frames limit.th
 */
StateReturnType AIStateMachine::setTemporaryState( StateID newStateID, Int frameLimitCoount )

{
	// extract the state associated with the given ID
	State *newState = internalGetState( newStateID );
#ifdef STATE_MACHINE_DEBUG
	if (getWantsDebugOutput())
	{
		StateID curState = INVALID_STATE_ID;
		if (m_temporaryState) {
			curState = m_temporaryState->getID();
		}
		DEBUG_LOG_RAW(("%d '%s' -(TEMP)- '%s' %x exit ", TheGameLogic->getFrame(), getOwner()->getTemplate()->getName().str(), getName().str(), this));
		if (m_temporaryState) {
			DEBUG_LOG_RAW((" '%s' ", m_temporaryState->getName().str()));
		} else {
			DEBUG_LOG_RAW((" INVALID_STATE_ID "));
		}
		if (newState) {
			DEBUG_LOG(("enter '%s'", newState->getName().str()));
		} else {
			DEBUG_LOG(("to INVALID_STATE"));
		}
	}
#endif
	if (m_temporaryState) {
		m_temporaryState->onExit(EXIT_RESET);
		m_temporaryState = nullptr;
	}
	if (newState) {
		m_temporaryState = newState;
		StateReturnType ret = m_temporaryState->onEnter();
		if (ret != STATE_CONTINUE) {
			m_temporaryState->onExit(EXIT_NORMAL);
			m_temporaryState = nullptr;
			return ret;
		}
		enum {FRAME_COUNT_MAX = 60*LOGICFRAMES_PER_SECOND};
		// If you need to up this check, ok, but 1 minute seems overly long for a temporary state override.  jba.
		DEBUG_ASSERTCRASH(frameLimitCoount<=FRAME_COUNT_MAX, ("Unusually long time to set temporary state."));
		if (frameLimitCoount>FRAME_COUNT_MAX) {
			frameLimitCoount = FRAME_COUNT_MAX;
		}
		m_temporaryStateFramEnd = TheGameLogic->getFrame()+frameLimitCoount;
		return ret;
	}
	return STATE_FAILURE;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Add a point to a simple path
 */
void AIStateMachine::addToGoalPath( const Coord3D *pathPoint)
{
	if (m_goalPath.empty()) {
		m_goalPath.push_back(*pathPoint);
	}	else {
		Coord3D *finalPoint = &m_goalPath[ m_goalPath.size() - 1 ];
		if( !finalPoint->equals( *pathPoint ) )
		{
			m_goalPath.push_back(*pathPoint);
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Return path position at index "i"
 */
const Coord3D *AIStateMachine::getGoalPathPosition( Int i ) const
{
	if (i < 0 || i >= m_goalPath.size())
		return nullptr;

	return &m_goalPath[i];
}

//----------------------------------------------------------------------------------------------------------
/**
 * Set the current goal waypoint. If we reach this waypoint and there
 * are connections to further points, continue on.
 */
void AIStateMachine::setGoalWaypoint( const Waypoint *way )
{
	m_goalWaypoint = way;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Return the current goal waypoint
 */
const Waypoint *AIStateMachine::getGoalWaypoint()
{
	return m_goalWaypoint;
}

//----------------------------------------------------------------------------------------------------------
void AIStateMachine::clear()
{
	StateMachine::clear();
	m_goalPath.clear();
	m_goalWaypoint = nullptr;
	m_goalSquad = nullptr;

	AIUpdateInterface* ai = getOwner()->getAI();
	if (ai)
		ai->friend_notifyStateMachineChanged();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIStateMachine::resetToDefaultState()
{
	StateReturnType tmp = StateMachine::resetToDefaultState();

	AIUpdateInterface* ai = getOwner()->getAI();
	if (ai)
		ai->friend_notifyStateMachineChanged();

	return tmp;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIStateMachine::setState(StateID newStateID)
{
	StateID oldID = getCurrentStateID();
	StateReturnType tmp = StateMachine::setState(newStateID);

	AIUpdateInterface* ai = getOwner()->getAI();
	if (ai && oldID != newStateID)
		ai->friend_notifyStateMachineChanged();

	return tmp;
}

//----------------------------------------------------------------------------------------------------------
void AIStateMachine::setGoalTeam( const Team *team )
{
	if (m_goalSquad == nullptr) {
		m_goalSquad = newInstance( Squad );
	}

	m_goalSquad->squadFromTeam(team, true);
}

//----------------------------------------------------------------------------------------------------------
void AIStateMachine::setGoalSquad( const Squad *squad )
{
	if (m_goalSquad == nullptr) {
		m_goalSquad = newInstance( Squad );
	}

	(*m_goalSquad) = (*squad);
}

//----------------------------------------------------------------------------------------------------------
void AIStateMachine::setGoalAIGroup( const AIGroup *group )
{
	if (m_goalSquad == nullptr) {
		m_goalSquad = newInstance( Squad );
	}

	m_goalSquad->squadFromAIGroup(group, true);
}

//----------------------------------------------------------------------------------------------------------
Squad *AIStateMachine::getGoalSquad()
{
	return m_goalSquad;
}

// State transition conditions ----------------------------------------------------------------------------
/**
 * Return true if the machine's owner's current weapon's range
 * cannot reach the goalObject.
 */
Bool outOfWeaponRangeObject( State *thisState, void* userData )
{
	Object *obj = thisState->getMachineOwner();
	Object *victim = thisState->getMachineGoalObject();
	Weapon *weapon = obj->getCurrentWeapon();

	CRCDEBUG_LOG(("outOfWeaponRangeObject()"));
	if (victim && weapon)
	{
		Bool viewBlocked = false;
		AIUpdateInterface *ai = obj->getAI();
		Bool onGround = true;
		if (ai) {
			onGround = ai->isDoingGroundMovement();
		}
		if( obj->isKindOf(KINDOF_IMMOBILE) ) {
			onGround = true;
		}
		// brutal special case for stinger soldiers, who
		// generally don't have locomotors, but are still on the ground.
		if (obj->isKindOf(KINDOF_SPAWNS_ARE_THE_WEAPONS))
		{
			onGround = true;
		}
		Object *containedBy = obj->getContainedBy();
		if( containedBy && (containedBy->isKindOf( KINDOF_STRUCTURE ) || !containedBy->isAirborneTarget()) )
		{
			//Contained objects on the ground -- garrisoned buildings for example!
			onGround = true;
		}
		// srj sez: at tiny ranges, isAttackViewBlockedByObstacle() can return false positives,
		// so just skip it for contact weapons
		if (victim && !weapon->isContactWeapon() && onGround && !victim->isSignificantlyAboveTerrain())
		{
			viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(obj, *obj->getPosition(), victim, *victim->getPosition());
		}
		// A weapon with leech range temporarily has unlimited range and is locked onto its target.
		if (!weapon->hasLeechRange() && viewBlocked)
		{
			//CRCDEBUG_LOG(("outOfWeaponRangeObject() - object %d (%s) view is blocked for attacking %d (%s)",
			//	obj->getID(), obj->getTemplate()->getName().str(),
			//	victim->getID(), victim->getTemplate()->getName().str()));
			return true;
		}
		if (!weapon->hasLeechRange() && !weapon->isWithinAttackRange(obj, victim))
		{
			//CRCDEBUG_LOG(("outOfWeaponRangeObject() - object %d (%s) is out of range for attacking %d (%s)",
			//	obj->getID(), obj->getTemplate()->getName().str(),
			//	victim->getID(), victim->getTemplate()->getName().str()));
			return true;
		}
	}

	return false;
}

static Bool inWeaponRangeObject(State *thisState, void* userData)
{
	return !outOfWeaponRangeObject(thisState, userData);
}

Bool wantToSquishTarget( State *thisState, void* userData )
{
	Object *obj = thisState->getMachineOwner();
	Object *victim = thisState->getMachineGoalObject();

	if (obj && victim)
	{
		if (victim->getContainedBy()) {
			return false; // can't crush guys in buildings or vehicles.
		}
		if( obj->getAI() && (obj->getAI()->getWhichTurretForCurWeapon() != TURRET_INVALID) )
		{
			// I can only decide to crush-attack if I am attacking with a turreted weapon.
			if (TheAI->getAiData()->m_aiCrushesInfantry) {
				if (obj && obj->getControllingPlayer() &&
					obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER) {
					if (obj->canCrushOrSquish(victim)) {
						if (!obj->isKindOf(KINDOF_DONT_AUTO_CRUSH_INFANTRY)) {
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

Bool outOfWeaponRangePosition( State *thisState, void* userData )
{
	Object *obj = thisState->getMachineOwner();
	const Coord3D *pos = thisState->getMachineGoalPosition();
	Weapon *weapon = obj->getCurrentWeapon();

	if (weapon && pos)
	{
		AIUpdateInterface *ai = obj->getAI();
		Bool onGround = true;
		if (ai) {
			onGround = ai->isDoingGroundMovement();
		}
		if( obj->isKindOf(KINDOF_IMMOBILE) ) {
			onGround = true;
		}
		// brutal special case for stinger soldiers, who
		// generally don't have locomotors, but are still on the ground.
		if (obj->isKindOf(KINDOF_SPAWNS_ARE_THE_WEAPONS))
		{
			onGround = true;
		}
		Object *containedBy = obj->getContainedBy();
		if( containedBy && (containedBy->isKindOf( KINDOF_STRUCTURE ) || !containedBy->isAirborneTarget()) )
		{
			//Contained objects on the ground -- garrisoned buildings for example!
			onGround = true;
		}

		Bool viewBlocked = false;
		if (onGround)
		{
			viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(obj, *obj->getPosition(), nullptr, *pos);
		}
		if (viewBlocked)
		{
			return true;
		}
		if (!weapon->isWithinAttackRange(obj, pos))
		{
			return true;
		}
	}

	return false;
}

/**
 * Return true if the machine's owner's cannot attack in any way
 */
static Bool cannotPossiblyAttackObject( State *thisState, void* userData )
{
	AbleToAttackType attackType = (AbleToAttackType)(UnsignedInt)userData;
	Object *obj = thisState->getMachineOwner();
	Object *victim = thisState->getMachineGoalObject();

	if (obj && victim)
	{
		if( !obj->isAbleToAttack() )
		{
			return TRUE;
		}
		CanAttackResult result = obj->getAbleToAttackSpecificObject( attackType, victim, obj->getAI()->getLastCommandSource() );
		if( result != ATTACKRESULT_POSSIBLE && result != ATTACKRESULT_POSSIBLE_AFTER_MOVING )
		{
			return TRUE;
		}
	}

	return FALSE;
}


//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

const UnsignedInt IDLE_COUNTDOWN_DELAY = (LOGICFRAMES_PER_SECOND * 2);

//----------------------------------------------------------------------------------------------
AIIdleState::AIIdleState( StateMachine *machine, AIIdleState::AIIdleTargetingType shouldLookForTargets ) : State( machine,"AIIdleState")
{
	m_gxAIIdleStateRow = TheGxAIIdleStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIIDLESTATE.shouldLookForTargets = shouldLookForTargets == AIIdleState::LOOK_FOR_TARGETS;

		GXROW_AIIDLESTATE.inited = FALSE;
		GXROW_AIIDLESTATE.initialSleepOffset = -1;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIIdleState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIIdleState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedShort(&GXROW_AIIDLESTATE.initialSleepOffset);
	xfer->xferBool(&GXROW_AIIDLESTATE.shouldLookForTargets);
	xfer->xferBool(&GXROW_AIIDLESTATE.inited);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIIdleState::loadPostProcess()
{
}
//----------------------------------------------------------------------------------------------
/**
 * Stake out our space.
 */
DECLARE_PERF_TIMER(AIIdleState)
StateReturnType AIIdleState::onEnter()
{
	USE_PERF_TIMER(AIIdleState)
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	// We could possibly not have ai here if we were constructed this frame. Strange but true. :-<
	if (ai)
		ai->resetNextMoodCheckTime();

	GXROW_AIIDLESTATE.inited = true;

	// reset the idle countdown so that we don't do expensive checks too often,
	// randomized so we avoid spikes
	GXROW_AIIDLESTATE.initialSleepOffset = (UnsignedShort)GameLogicRandomValue(0, IDLE_COUNTDOWN_DELAY);

	// never sleep at the start, since we have to do checkGoalPos first time thru
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------
void AIIdleState::doInitIdleState()
{
	// Only do it once, but do it in the update cause onEnter for idle is called during object creation.  jba.

	if (!GXROW_AIIDLESTATE.inited)
		return;

	GXROW_AIIDLESTATE.inited = false;

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	const Locomotor* loco = ai->getCurLocomotor();
	Bool ultraAccurate = (loco != nullptr && loco->isUltraAccurate());
#define NO_STOP_AND_SLIDE
	if (ai->isIdle() && ai->isDoingGroundMovement())
	{
		/*

			You may be asking yourself, "If I'm in an idle state, how can I be doing ground movement?"

			answer from jba:

			If a unit is moving, and you hit stop, it forces it into the idle state.
			Depending where it is, it may be between pathfind grids.
			This is a bad thing.
			So it "cheat moves", to the nearest grid.
			Also, for locos the "close enough" distance is 1 or so.
			So it moves the rest of the way to it's goal location by cheating.

		*/
		// Update the goal.
		Coord3D goalPos = *obj->getPosition();
		// but only if we have a valid position.
		if (goalPos.x || goalPos.y || goalPos.z)
		{
			TheAI->pathfinder()->updateGoal(obj, &goalPos, obj->getLayer());
			if (!ultraAccurate && TheAI->pathfinder()->goalPosition(obj, &goalPos))
			{
				if (TheGameLogic->getFrame()<=1) {
					obj->setPosition(&goalPos);
				} else {
#ifdef STOP_AND_SLIDE
					ai->setFinalPosition(&goalPos);
#endif
				}
				TheAI->pathfinder()->updateGoal(obj, &goalPos, obj->getLayer());
			}
		}
	}

	ai->setLocomotorGoalNone();
	ai->setCurrentVictim(nullptr);
}

//----------------------------------------------------------------------------------------------
/**
 * Just sit there.
 */
StateReturnType AIIdleState::update()
{
	USE_PERF_TIMER(AIIdleState)

	doInitIdleState();

	UnsignedInt timeToSleep = IDLE_COUNTDOWN_DELAY + GXROW_AIIDLESTATE.initialSleepOffset;
	UnsignedInt oldSleepOffset = GXROW_AIIDLESTATE.initialSleepOffset;
	GXROW_AIIDLESTATE.initialSleepOffset = 0;

	// This state is used internally some places, so we don't necessarily want to be looking for targets
	// Places that use AI_IDLE internally should set this to false in the constructor. jkmcd
	if ( GXROW_AIIDLESTATE.shouldLookForTargets && !getMachine()->isLocked() )
	{
		// if we are here, it's time to check again
		Object *obj = getMachineOwner();
		AIUpdateInterface *ai = obj->getAI();

		// do repulsor logic
		if (obj->isKindOf(KINDOF_CAN_BE_REPULSED) && ai->isIdle())
		{
			Object* enemy = TheAI->findClosestRepulsor(obj, obj->getVisionRange());
			if (enemy)
			{
				getMachine()->setState(AI_MOVE_AWAY_FROM_REPULSORS);
				// since we just changed the state, it doesn't really matter what we return here.
				return STATE_CONTINUE;
			}
		}

		// Check to see if we have created a crate we need to pick up.
		Object* crate = ai->checkForCrateToPickup();
		if (crate)
		{
			ai->aiMoveToObject(crate, CMD_FROM_AI);
			// since we just changed the state, it doesn't really matter what we return here.
			return STATE_CONTINUE;
		}


		if (! obj->isDisabledByType( DISABLED_PARALYZED ) &&
				! obj->isDisabledByType( DISABLED_UNMANNED ) &&
				! obj->isDisabledByType( DISABLED_EMP ) &&
				! obj->isDisabledByType( DISABLED_SUBDUED ) &&
				! obj->isDisabledByType( DISABLED_HACKED ) )
		{
			// mood targeting
			UnsignedInt moodAdjust = ai->getMoodMatrixActionAdjustment(MM_Action_Idle);
			if ((moodAdjust & MAA_Affect_Range_IgnoreAll) == 0)
			{
				// If we're supposed to attack based on mood, etc, then we will do so.
				Object* enemy = ai->getNextMoodTarget( true, true );
				if (enemy)
				{
	 				ai->aiAttackObject(enemy, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
					// weird but true. return state_continue, because if we're here, we're actually an attack state
					// since we just changed the state, it doesn't really matter what we return here.
					return STATE_CONTINUE;
				}
			}
		}

		UnsignedInt now = TheGameLogic->getFrame();
		UnsignedInt nextMoodCheckTime = ai->getNextMoodCheckTime();
		if (nextMoodCheckTime > now)
		{
			// if we need to look for targets, might need to sleep less.
			UnsignedInt moodSleep = nextMoodCheckTime - now;
			if (moodSleep < timeToSleep)
			{
				timeToSleep = moodSleep;
				// if we do this, save the random sleep offset for next time.
				GXROW_AIIDLESTATE.initialSleepOffset = oldSleepOffset;
			}
		}
	}

	return STATE_SLEEP(timeToSleep);
}

//----------------------------------------------------------------------------------------------
/**
 * Just sit there, but dead-like.
 */
StateReturnType AIDeadState::onEnter()
{
	Object *obj = getMachineOwner();

	// How can an object be null here? I don't think it actually can, but this check must be
	// here for a reason. - jkmcd
	if (obj)
	{
		ModelConditionFlags nonDyingStuff;
		nonDyingStuff.set(MODELCONDITION_USING_WEAPON_A);
		nonDyingStuff.set(MODELCONDITION_USING_WEAPON_B);
		nonDyingStuff.set(MODELCONDITION_USING_WEAPON_C);
		nonDyingStuff.set(MODELCONDITION_FIRING_A);
		nonDyingStuff.set(MODELCONDITION_FIRING_B);
		nonDyingStuff.set(MODELCONDITION_FIRING_C);
		nonDyingStuff.set(MODELCONDITION_BETWEEN_FIRING_SHOTS_A);
		nonDyingStuff.set(MODELCONDITION_BETWEEN_FIRING_SHOTS_B);
		nonDyingStuff.set(MODELCONDITION_BETWEEN_FIRING_SHOTS_C);
		nonDyingStuff.set(MODELCONDITION_RELOADING_A);
		nonDyingStuff.set(MODELCONDITION_RELOADING_B);
		nonDyingStuff.set(MODELCONDITION_RELOADING_C);
		nonDyingStuff.set(MODELCONDITION_PREATTACK_A);
		nonDyingStuff.set(MODELCONDITION_PREATTACK_B);
		nonDyingStuff.set(MODELCONDITION_PREATTACK_C);
#ifdef ALLOW_SURRENDER
		nonDyingStuff.set(MODELCONDITION_SURRENDER);
#endif
		nonDyingStuff.set(MODELCONDITION_MOVING);

		// dying objects are NEVER firing, surrendered, etc, so clear 'em all here.
		obj->clearAndSetModelConditionFlags(nonDyingStuff, MAKE_MODELCONDITION_MASK(MODELCONDITION_DYING));
		TheScriptEngine->notifyOfObjectCreationOrDestruction();
	}

	return STATE_CONTINUE;
}


StateReturnType AIDeadState::update()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	ai->setLocomotorGoalNone();

	// re-mark this every time, just in case our health miraculously heals from 0 to nonzero...
	obj->setEffectivelyDead(true);

	if (obj->isKindOf(KINDOF_INFANTRY))
	{
		PhysicsBehavior* phys = obj->getPhysics();
		if (phys)
		{
			// we want 'em to stop, but looks wonky if they stop dead in their tracks in onEnter.
			// this slows 'em down quickly
			const Real FACTOR = 0.8f;	// 0.8 ^ 30 == 0.012, so they slow to 4% of speed over 1 sec
			Real vel = phys->getVelocityMagnitude();
			phys->scrubVelocity2D(vel * FACTOR);
		}
	}

	return STATE_CONTINUE;
}

void AIDeadState::onExit( StateExitType status )
{
	Object *obj = getMachineOwner();
	obj->clearModelConditionState(MODELCONDITION_DYING);
}

//----------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIInternalMoveToState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIInternalMoveToState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
	xfer->xferCoord3D(&GXROW_AIINTERNALMOVETOSTATE.goalPosition);
	xfer->xferUser(&GXROW_AIINTERNALMOVETOSTATE.goalLayer, sizeof(GXROW_AIINTERNALMOVETOSTATE.goalLayer));
	xfer->xferBool(&GXROW_AIINTERNALMOVETOSTATE.waitingForPath);
	xfer->xferCoord3D(&GXROW_AIINTERNALMOVETOSTATE.pathGoalPosition);
	xfer->xferUnsignedInt(&GXROW_AIINTERNALMOVETOSTATE.pathTimestamp);
	xfer->xferUnsignedInt(&GXROW_AIINTERNALMOVETOSTATE.blockedRepathTimestamp);
	xfer->xferBool(&GXROW_AIINTERNALMOVETOSTATE.adjustDestinations);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIInternalMoveToState::loadPostProcess()
{
	startMoveSound();
}

Bool AIInternalMoveToState::getAdjustsDestination() const
{
	const Object *obj = getMachineOwner();
	if (obj->testStatus(OBJECT_STATUS_PARACHUTING))
		return false;

	const AIUpdateInterface* ai = obj->getAI();
	if (ai && !ai->isAllowedToAdjustDestination())
		return false;

	return GXROW_AIINTERNALMOVETOSTATE.adjustDestinations;
}

/**
 * (Re)compute a path to the goal position, if we are on our own,
 * or we are the leader of a group.
 */
Bool AIInternalMoveToState::computePath()
{
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	GXROW_AIINTERNALMOVETOSTATE.waitingForPath = true;
	ai->requestPath(&GXROW_AIINTERNALMOVETOSTATE.goalPosition, getAdjustsDestination());
	ai->friend_startingMove();
	return true;
}

/**
 * We are initiating a moveTo action.
 * Pathfind from m_goalPosition to goal.
 */
StateReturnType AIInternalMoveToState::onEnter()
{
	GXROW_AIINTERNALMOVETOSTATE.ambientPlayingHandle = AHSV_Error;
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	GXROW_AIINTERNALMOVETOSTATE.waitingForPath = ai->isWaitingForPath();

	if( obj->testStatus( OBJECT_STATUS_IMMOBILE ) )
	{
		return STATE_FAILURE;
	}

	if (ai->getCurLocomotor()) {
		ai->getCurLocomotor()->startMove();
		if (ai->getCurLocomotor()->isUltraAccurate())
		{
			setAdjustsDestination(false); // if we're being ultra accurate, we can't adjust the destination. jba.
		}
	}
	GXROW_AIINTERNALMOVETOSTATE.tryOneMoreRepath = true;  // We may try one more repath after the first one is finished.
	ai->friend_startingMove();

	// get object physics state
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics && physics->isMotive()) {
		// If we were already moving, go immediately to the animation.
		// This is so if you redirect a moving infantry or other animated model, he doesn't
		// pop into idle for 1 frame.  jba.
		//Determine if we are on a cliff cell... if so, use the climbing model condition
		//instead of moving.
		Int cellX = REAL_TO_INT( obj->getPosition()->x / PATHFIND_CELL_SIZE );
		Int cellY = REAL_TO_INT( obj->getPosition()->y / PATHFIND_CELL_SIZE );

		PathfindCell* cell = TheAI->pathfinder()->getCell( obj->getLayer(), cellX, cellY );
		ModelConditionFlagType modelConditionFlag = (cell && cell->getType() != PathfindCell::CELL_CLIFF) ? MODELCONDITION_MOVING : MODELCONDITION_CLIMBING;
		obj->setModelConditionState( modelConditionFlag );
	}
	// This is a classic January 2 type of hack
	// Since the worker can go into its attack modelcondition in AIAttack::OnEnter(),
	// it needs to get its moving modelcondition set timely enough to trigger a transition
	// from conditionX into ATTACKING|MOVING... Thanks for reading, MLorenzen,  Jan 2, 2003
	else if ( obj->isKindOf( KINDOF_DOZER ) && obj->isKindOf( KINDOF_HARVESTER ) )
		obj->setModelConditionState( MODELCONDITION_MOVING );



	if( getAdjustsDestination() && !obj->testStatus( OBJECT_STATUS_RIDER8 ) )
	{
		if (!TheAI->pathfinder()->adjustDestination(obj, ai->getLocomotorSet(), &GXROW_AIINTERNALMOVETOSTATE.goalPosition))
		{
			TheAI->pathfinder()->snapClosestGoalPosition(obj, &GXROW_AIINTERNALMOVETOSTATE.goalPosition);
		}
		TheAI->pathfinder()->updateGoal(obj, &GXROW_AIINTERNALMOVETOSTATE.goalPosition, TheTerrainLogic->getLayerForDestination(&GXROW_AIINTERNALMOVETOSTATE.goalPosition));
	}

	// request a path to the destination
	if (!computePath())
	{
		ai->friend_endingMove();
		return STATE_FAILURE;
	}

	// Target to stop at the end of this path.
	// This value will be overridden by the FollowWaypoint ai state.
	ai->setPathExtraDistance(0);
	ai->setDesiredSpeed( FAST_AS_POSSIBLE );

	startMoveSound();
	return STATE_CONTINUE;
}

/**
 * Start playing the object's move sound.
 */
void AIInternalMoveToState::startMoveSound()
{
	Object *obj = getMachineOwner();
	const BodyModuleInterface *objBody = obj->getBodyModule();
	if (objBody && IS_CONDITION_WORSE(objBody->getDamageState(), BODY_DAMAGED))
	{
		AudioEventRTS soundEventMoveDamaged = *obj->getTemplate()->getSoundMoveStartDamaged();
		if (!soundEventMoveDamaged.getEventName().isEmpty())
		{
			soundEventMoveDamaged.setObjectID(obj->getID());
			TheAudio->addAudioEvent( &soundEventMoveDamaged );
		}
		else
		{
			soundEventMoveDamaged = *obj->getTemplate()->getSoundMoveLoopDamaged();
			if (!soundEventMoveDamaged.getEventName().isEmpty())
			{
				soundEventMoveDamaged.setObjectID(obj->getID());
				GXROW_AIINTERNALMOVETOSTATE.ambientPlayingHandle = TheAudio->addAudioEvent( &soundEventMoveDamaged );
			}
		}
	}
	else
	{
		AudioEventRTS soundEventMove = *obj->getTemplate()->getSoundMoveStart();
		soundEventMove.setObjectID(obj->getID());

		if (!soundEventMove.getEventName().isEmpty())
		{
			TheAudio->addAudioEvent( &soundEventMove );
		}
		else
		{
			soundEventMove = *obj->getTemplate()->getSoundMoveLoop();
			soundEventMove.setObjectID(obj->getID());
			if (!soundEventMove.getEventName().isEmpty())
			{
				GXROW_AIINTERNALMOVETOSTATE.ambientPlayingHandle = TheAudio->addAudioEvent( &soundEventMove );
			}
		}
	}

}

/**
 * We are leaving the moveTo state.
 */
void AIInternalMoveToState::onExit( StateExitType status )
{
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	// stop ambient sound associated with movement
	TheAudio->removeAudioEvent( GXROW_AIINTERNALMOVETOSTATE.ambientPlayingHandle );

 	// If this onExit is the result of the state machine being deleted, then there is no AI.
	// (This is why destructors should not do game logic)
	if (ai) {
		ai->friend_endingMove();
		DEBUG_ASSERTLOG(obj->getTeam(), ("AIInternalMoveToState::onExit obj has null team."));
		if (obj->getTeam() && ai->isDoingGroundMovement() && ai->getCurLocomotor() &&
								ai->getCurLocomotor()->isUltraAccurate()) {
			Real dx = GXROW_AIINTERNALMOVETOSTATE.goalPosition.x-obj->getPosition()->x;
			Real dy = GXROW_AIINTERNALMOVETOSTATE.goalPosition.y-obj->getPosition()->y;
			if (dx*dx+dy*dy<PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F)
			{
				// We are doing accurate ground movement, so make sure we end exactly at the goal.
				ai->setFinalPosition(&GXROW_AIINTERNALMOVETOSTATE.goalPosition);
			}
		}
	}
}

/**
 * Execute the moveTo behavior towards GoalPosition.
 */

StateReturnType AIInternalMoveToState::update()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	//Kris: 7/01/03 (Temporary debug hook for units not being able to leave maps)
	Bool blah = FALSE;
	if( getMachineOwner()->testStatus( OBJECT_STATUS_RIDER8 ) )
	{
		blah = TRUE;
	}

	//If we're deployed, don't move! But keep the state around until we can packup (edge case).
	//if( obj->testStatus( OBJECT_STATUS_DEPLOYED ) )
	//{
	//	return STATE_CONTINUE;
	//}

	Path *thePath = ai->getPath();
	if (GXROW_AIINTERNALMOVETOSTATE.waitingForPath)
	{
		// bump the timer.
		GXROW_AIINTERNALMOVETOSTATE.pathTimestamp = TheGameLogic->getFrame();
		if (ai->isWaitingForPath()) {
			/// @todo srj -- find a way to sleep for a number of frames here, if possible
			if (!ai->gxOnCoarsePath())	// web port (match rule coarseStart): keep moving on the zone-level route
				return STATE_CONTINUE;
		}
		else
		{
		if (thePath==nullptr)
		{
			//Kris: 7/01/03 (Temporary debug hook for units not being able to leave maps)
			if( blah )
			{
				blah = blah;
			}
			return STATE_FAILURE;
		}
		GXROW_AIINTERNALMOVETOSTATE.waitingForPath = false;
		GXROW_AIINTERNALMOVETOSTATE.pathGoalPosition = GXROW_AIINTERNALMOVETOSTATE.goalPosition;
		if (this->getAdjustsDestination()) {
				TheAI->pathfinder()->updateGoal(obj, thePath->getLastNode()->getPosition(), thePath->getLastNode()->getLayer());
		} else {
				TheAI->pathfinder()->removeGoal(obj);
		}
		if (!ai->getRetryPath()) {
			GXROW_AIINTERNALMOVETOSTATE.tryOneMoreRepath = false;
		}
		}
	}

	Bool forceRecompute = false;
	if (thePath==nullptr) {
		forceRecompute = true;
	}
	Bool blocked=false;
	if (ai->isBlockedAndStuck() || ai->getNumFramesBlocked()>2*LOGICFRAMES_PER_SECOND) {
		forceRecompute = true;
		blocked = true;
		GXROW_AIINTERNALMOVETOSTATE.blockedRepathTimestamp = TheGameLogic->getFrame();
		// Intense debug logging jba.
		//DEBUG_LOG(("Info - Blocked - recomputing."));
	}

	//Determine if we are on a cliff cell... if so, use the climbing model condition
	//instead of moving.
	Int cellX = REAL_TO_INT( obj->getPosition()->x / PATHFIND_CELL_SIZE );
	Int cellY = REAL_TO_INT( obj->getPosition()->y / PATHFIND_CELL_SIZE );

	PathfindCell* cell = TheAI->pathfinder()->getCell( obj->getLayer(), cellX, cellY );
	ModelConditionFlagType setConditionFlag = MODELCONDITION_MOVING;
	// Totally hacky set of conditions to make col. burton's monkey ass not slide down
	// the cliffs backwards.  This could use some improvement at some point.  jba. 31DEC2002
	if (cell && cell->getType() == PathfindCell::CELL_CLIFF && !cell->getPinched()) {

		if (ai->getCurLocomotor() && ai->getCurLocomotor()->isMovingBackwards()) {
			setConditionFlag = MODELCONDITION_RAPPELLING;
			obj->clearModelConditionState( MODELCONDITION_CLIMBING );
		} else {
			setConditionFlag = MODELCONDITION_CLIMBING;
			obj->clearModelConditionState( MODELCONDITION_RAPPELLING );
		}
	}
	if (ai->getNumFramesBlocked()>LOGICFRAMES_PER_SECOND/4)
	{
		obj->clearModelConditionState( MODELCONDITION_MOVING );
	}
	else
	{
		//Clear climbing if modelConditionFlag is not climbing
		if( setConditionFlag == MODELCONDITION_MOVING )
		{
			obj->clearModelConditionState( MODELCONDITION_CLIMBING );
			obj->clearModelConditionState( MODELCONDITION_RAPPELLING );
		}

		obj->setModelConditionState(MODELCONDITION_MOVING);

		if (setConditionFlag!=MODELCONDITION_MOVING) {
			obj->setModelConditionState( setConditionFlag );
		}

	}

	if (ai->canComputeQuickPath()) {
		if (obj->isKindOf(KINDOF_PROJECTILE)) {
			GXROW_AIINTERNALMOVETOSTATE.pathTimestamp = 0; // We can compute paths cheap, so don't delay to recompute.  jba.
			forceRecompute = true;
		}
	}

	if (thePath != nullptr)
		ai->setLocomotorGoalPositionOnPath();

	// web port (match rule coarseStart): on the coarse route only steer along it; the full route is coming
	if (ai->isWaitingForPath())
		return STATE_CONTINUE;

	// web port (match rule routeRefine): near the end of the detailed stretch, plan the next one
	if (!forceRecompute && thePath && thePath->m_gxRefine) {
		const Real dx = obj->getPosition()->x - thePath->m_gxRefineEnd.x, dy = obj->getPosition()->y - thePath->m_gxRefineEnd.y;
		if (dx * dx + dy * dy < sqr(12 * PATHFIND_CELL_SIZE_F)) {
			thePath->m_gxRefine = FALSE;	// ask once per stretch
			forceRecompute = true;
		}
	}

	// if our goal has moved, recompute our path
	if (forceRecompute || TheGameLogic->getFrame() - GXROW_AIINTERNALMOVETOSTATE.pathTimestamp > MIN_REPATH_TIME)
	{
		if (forceRecompute || !isSamePosition(obj->getPosition(), &GXROW_AIINTERNALMOVETOSTATE.pathGoalPosition, &GXROW_AIINTERNALMOVETOSTATE.goalPosition ))
		{
			// goal moved - repath
			if (!computePath())
			{
				//Kris: 7/01/03 (Temporary debug hook for units not being able to leave maps)
				if( blah )
				{
					blah = blah;
				}
				return STATE_FAILURE;
			}

			// srj sez: must re-set setLocoGoal after computePath, since computePath
			// can set the loco goal to NONE...
			if (ai->getPath() != nullptr)
				ai->setLocomotorGoalPositionOnPath();
			else
			{
				return STATE_CONTINUE;
			}
		}
	}

	//
	// Check if we have reached our destination
	//
	Real onPathDistToGoal = ai->getLocomotorDistanceToGoal();
	//DEBUG_LOG(("onPathDistToGoal = %f %s",onPathDistToGoal, obj->getTemplate()->getName().str()));
	if (ai->getCurLocomotor() && (onPathDistToGoal < ai->getCurLocomotor()->getCloseEnoughDist()))
	{
		if (ai->isDoingGroundMovement()) {
			// sanity check
			Coord3D delta;
			Coord3D goalPos = GXROW_AIINTERNALMOVETOSTATE.goalPosition;
			if (ai->getPath()->getLastNode()) {
				goalPos = *ai->getPath()->getLastNode()->getPosition();
			}
			delta.x = obj->getPosition()->x - goalPos.x;
			delta.y = obj->getPosition()->y - goalPos.y;
			delta.z = 0;
			if (delta.length() > 4*PATHFIND_CELL_SIZE_F) {
				//DEBUG_LOG(("AIInternalMoveToState Trying to finish early.  Continuing..."));
				onPathDistToGoal = ai->getLocomotorDistanceToGoal();
				return STATE_CONTINUE;
			}
		}
		// we have reached the end of the path
		if (getAdjustsDestination())
		{
			ai->setLocomotorGoalNone();
		}
		DEBUG_ASSERTLOG(!getMachine()->getWantsDebugOutput(), ("AIInternalMoveToState::update: reached end of path, exiting state with success"));
		//Kris: 7/01/03 (Temporary debug hook for units not being able to leave maps)
		if( blah )
		{
			blah = blah;
		}
		return STATE_SUCCESS;
	}

	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------------------------------
class AIAttackMoveStateMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIAttackMoveStateMachine, "AIAttackMoveStateMachine" );

public:

	AIAttackMoveStateMachine( Object *owner, AsciiString name );

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveStateMachine::crc( Xfer *xfer )
{
	StateMachine::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveStateMachine::xfer( Xfer *xfer )
{
	XferVersion cv = 1;
	XferVersion v = cv;
	xfer->xferVersion( &v, cv );

	StateMachine::xfer(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveStateMachine::loadPostProcess()
{
	StateMachine::loadPostProcess();
}

//-----------------------------------------------------------------------------------------------------------
AIAttackMoveStateMachine::AIAttackMoveStateMachine(Object *owner, AsciiString name) : StateMachine(owner, name)
{
	// order matters: first state is the default state.
	defineState( AI_IDLE, newInstance(AIIdleState)( this, AIIdleState::DO_NOT_LOOK_FOR_TARGETS ), AI_IDLE, AI_IDLE );
	defineState( AI_PICK_UP_CRATE, newInstance(AIPickUpCrateState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_ATTACK_OBJECT,	newInstance(AIAttackState)(this, false, true, false, nullptr ), AI_IDLE, AI_IDLE);
}

//----------------------------------------------------------------------------------------------------------
AIAttackMoveStateMachine::~AIAttackMoveStateMachine()
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//
// note - has no crc/xfer as has no member vars. jba.

//-------------------------------------------------------------------------------------------------
AIMoveToState::AIMoveToState(StateMachine *machine) : AIInternalMoveToState( machine, "AIMoveToState" )
{
	m_gxAIMoveToStateRow = TheGxAIMoveToStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIMOVETOSTATE.isMoveTo = true;

	// GXROW_AIMOVETOSTATE.isMoveTo is a boolean that specifies that this thing is ACTUALLY A MOVE TO.
	// child classes should set it false. (We don't have or want RTTI, so...)
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveToState::onEnter()
{

	//Kris: 7/01/03 (Temporary debug hook for units not being able to leave maps)
	if( getMachineOwner()->testStatus( OBJECT_STATUS_RIDER8 ) )
	{
		int blah = 0;
		blah++;
	}
	setAdjustsDestination(true);

	//If we have a goal object and are trying to ignore it as an obstacle...
	//This is used in the case of units trying to get really close.
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if( getMachineGoalObject() )
	{
		if( ai && getMachineGoalObject()->getID() == ai->getIgnoredObstacleID() )
		{
			setAdjustsDestination( false );
		}
	}

	// if we have a goal object, move to it, otherwise move to goal position
	if (getMachineGoalObject())	{
		AIInternalMoveToState::gxRow().goalPosition = *getMachineGoalObject()->getPosition();
		if (getMachineOwner()->isKindOf(KINDOF_PROJECTILE)) {
			Real halfHeight = getMachineGoalObject()->getGeometryInfo().getMaxHeightAbovePosition()/2.0f;
			AIInternalMoveToState::gxRow().goalPosition.z += halfHeight;
			if (getMachineGoalObject()->getPosition()->z < AIInternalMoveToState::gxRow().goalPosition.z) {
				AIInternalMoveToState::gxRow().goalPosition.z += halfHeight;
			}
		}
	} else
		AIInternalMoveToState::gxRow().goalPosition = *getMachineGoalPosition();

	StateReturnType ret = AIInternalMoveToState::onEnter();
	if (getMachineOwner()->getFormationID() != NO_FORMATION_ID) {
		AIGroup *group = ai->getGroup();
		if (group) {
			Real speed = group->getSpeed();
			ai->setDesiredSpeed(speed);
		}
	}
	return ret;
}

//----------------------------------------------------------------------------------------------------------
void AIMoveToState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveToState::update()
{
	AIUpdateInterface *ai = getMachineOwner()->getAI();

	//Kris: 7/01/03 (Temporary debug hook for units not being able to leave maps)
	if( getMachineOwner()->testStatus( OBJECT_STATUS_RIDER8 ) )
	{
		Int blah = 0;
		blah++;
	}

	UnsignedInt adjustment = ai->getMoodMatrixActionAdjustment(MM_Action_Move);
	if (GXROW_AIMOVETOSTATE.isMoveTo && (adjustment & MAA_Action_To_AttackMove))
		ai->aiAttackMoveToPosition(&AIInternalMoveToState::gxRow().goalPosition, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);

	// if we have a goal object, move to it, as it may have moved
	Object* goalObj = getMachineGoalObject();
	Object *obj = getMachineOwner();
	if (goalObj)
	{
		AIInternalMoveToState::gxRow().goalPosition = *goalObj->getPosition();
		Bool gotPhysics = obj->getPhysics()!=nullptr && goalObj->getPhysics()!=nullptr;
		Bool isMissile = obj->isKindOf(KINDOF_PROJECTILE);
		if (isMissile) {
			Real halfHeight = getMachineGoalObject()->getGeometryInfo().getMaxHeightAbovePosition()/2.0f;
			AIInternalMoveToState::gxRow().goalPosition.z += halfHeight;
			Real zDelta = AIInternalMoveToState::gxRow().goalPosition.z - obj->getPosition()->z;
			if (zDelta>0) {
				AIInternalMoveToState::gxRow().goalPosition.z += zDelta;
			}
		}
		//gotPhysics = false;
		if (gotPhysics && isMissile && !goalObj->isKindOf(KINDOF_IMMOBILE)) {
			Coord3D ourPos = *obj->getPosition();
			Coord3D delta;
			delta.x = AIInternalMoveToState::gxRow().goalPosition.x - ourPos.x;
			delta.y = AIInternalMoveToState::gxRow().goalPosition.y - ourPos.y;
			delta.z = AIInternalMoveToState::gxRow().goalPosition.z - ourPos.z;
			Real mySpeed = obj->getPhysics()->getVelocityMagnitude();
			Real goalSpeed = goalObj->getPhysics()->getVelocityMagnitude();
			if (mySpeed<5.0f) mySpeed = 5.0f; // avoid divide by 0.
			Real leadDistance = (0.5*delta.length()) * goalSpeed / mySpeed;
			Coord3D dir;
			goalObj->getUnitDirectionVector3D(dir);
			AIInternalMoveToState::gxRow().goalPosition.x += dir.x*leadDistance;
			AIInternalMoveToState::gxRow().goalPosition.y += dir.y*leadDistance;
			AIInternalMoveToState::gxRow().goalPosition.z += dir.z*leadDistance;
		}
		//DEBUG_LOG(("update goal pos to %f %f %f",AIInternalMoveToState::gxRow().goalPosition.x,AIInternalMoveToState::gxRow().goalPosition.y,AIInternalMoveToState::gxRow().goalPosition.z));
	} else {
		Bool isMissile = obj->isKindOf(KINDOF_PROJECTILE);
		if (isMissile) {
			// When missiles are moving uphill, they need to start up quickly to clear hills.  jba.
			AIInternalMoveToState::gxRow().goalPosition = *getMachineGoalPosition();
			Real zDelta = AIInternalMoveToState::gxRow().goalPosition.z - obj->getPosition()->z;
			if (zDelta>0) {
				AIInternalMoveToState::gxRow().goalPosition.z += zDelta;
			}
		}

	}

	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//
// note - has no crc/xfer as has no member vars. jba.

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveOutOfTheWayState::onEnter()
{
	setAdjustsDestination(true);
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	if (ai->getPath()==nullptr) {
		// Must have existing path.
		return STATE_FAILURE;
	}
	AIInternalMoveToState::gxRow().goalPosition = *ai->getPath()->getLastNode()->getPosition();
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
Bool AIMoveOutOfTheWayState::computePath()
{
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	AIInternalMoveToState::gxRow().waitingForPath = true;
	if (ai->isBlockedAndStuck()) {
		ai->setCanPathThroughUnits(true);
		return true; // don't repath, just stop.
	}
	return true; // just use the existing path.  See above.
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveOutOfTheWayState::update()
{
	if (getMachineOwner()->isEffectivelyDead()) {
		return STATE_SUCCESS;
	}
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------------------
void AIMoveOutOfTheWayState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit(status);
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai) {
		ai->destroyPath();
		ai->setCanPathThroughUnits(false);
		ai->clearMoveOutOfWay();
	}
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIMoveAndTightenState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIMoveAndTightenState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
	AIInternalMoveToState::xfer(xfer);
	xfer->xferInt(&GXROW_AIMOVEANDTIGHTENSTATE.okToRepathTimes);
	xfer->xferBool(&GXROW_AIMOVEANDTIGHTENSTATE.checkForPath);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIMoveAndTightenState::loadPostProcess()
{
	AIInternalMoveToState::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndTightenState::onEnter()
{
	setAdjustsDestination(false);
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	GXROW_AIMOVEANDTIGHTENSTATE.okToRepathTimes = 1;
	GXROW_AIMOVEANDTIGHTENSTATE.checkForPath = true;
	TheAI->pathfinder()->removeGoal(obj);
	AIInternalMoveToState::gxRow().goalPosition = *getMachineGoalPosition();
	ai->requestApproachPath(&AIInternalMoveToState::gxRow().goalPosition);
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndTightenState::update()
{
	if (GXROW_AIMOVEANDTIGHTENSTATE.checkForPath) {
		Object *obj = getMachineOwner();
		AIUpdateInterface *ai = obj->getAI();
		Path *thePath = ai->getPath();
		if (thePath && !ai->isWaitingForPath()) {
			setAdjustsDestination(true);
			GXROW_AIMOVEANDTIGHTENSTATE.checkForPath = false;
		}
	}
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------------------
Bool AIMoveAndTightenState::computePath()
{
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	if (ai->isBlockedAndStuck()) {
		if (GXROW_AIMOVEANDTIGHTENSTATE.okToRepathTimes>0) {
			GXROW_AIMOVEANDTIGHTENSTATE.okToRepathTimes--;
			AIInternalMoveToState::gxRow().waitingForPath = true;
			ai->requestPath(&AIInternalMoveToState::gxRow().goalPosition, true);
			return true;
		}
		//DEBUG_LOG(("AIMoveAndTightenState::computePath - stuck, failing."));
		return false;		 // don't repath for now.  jba.
	}
	return true; // just use the existing path.  See above.
}



//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

StateReturnType AIMoveAwayFromRepulsorsState::onEnter()
{
	setAdjustsDestination(false);
	Object *obj = getMachineOwner();
	Object* enemy = TheAI->findClosestRepulsor(getMachineOwner(), obj->getVisionRange());
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (!enemy || !ai) {
		return STATE_FAILURE;
	}
	ai->chooseLocomotorSet(LOCOMOTORSET_PANIC);

	if (obj)
	{
		obj->setModelConditionState(MODELCONDITION_PANICKING);
	}
	AIMoveAndTightenState::gxRow().okToRepathTimes = 1;
	AIMoveAndTightenState::gxRow().checkForPath = true;
	TheAI->pathfinder()->removeGoal(obj);
	ai->requestSafePath(enemy->getID());
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAwayFromRepulsorsState::update()
{
	if (AIMoveAndTightenState::gxRow().checkForPath) {
		Object *obj = getMachineOwner();
		AIUpdateInterface *ai = obj->getAI();
		Path *thePath = ai->getPath();
		if (thePath && !ai->isWaitingForPath()) {
			AIInternalMoveToState::gxRow().goalPosition = *thePath->getLastNode()->getPosition();
			setAdjustsDestination(false);
			AIMoveAndTightenState::gxRow().checkForPath = false;
		}
	}
	return AIInternalMoveToState::update();
}

//----------------------------------------------------------------------------------------------------------
Bool AIMoveAwayFromRepulsorsState::computePath()
{
	if (AIMoveAndTightenState::gxRow().okToRepathTimes>0) {
		AIMoveAndTightenState::gxRow().okToRepathTimes--;
		return true;
	}
	return false;  // don't recompute path, just stop moving.
}

//----------------------------------------------------------------------------------------------------------
void AIMoveAwayFromRepulsorsState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );
	Object *obj = getMachineOwner();
	if (obj)
	{
		obj->clearModelConditionState(MODELCONDITION_PANICKING);
	}
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

/**
 * Returns true if we can pursue the unit.  Requires that :
 * 1. We are faster than the other unit.
 * 2. The other unit is moving.
 * 3. The other unit is moving away from us.
 */
static Bool canPursue(Object *source, Weapon *weapon, Object *victim)
{
	/* This state is only used if the target is moving away from us, and has physics. */
	if (!victim->getPhysics()) {
		return false;
	}
	AIUpdateInterface *ai = source->getAI();
	if (!ai) {
		return false;
	}

	// Have to have a turret to pursue.
	WhichTurretType tur = ai->getWhichTurretForCurWeapon();
	if (tur == TURRET_INVALID) {
		return false;
	}

	if (TheAI->getAiData()->m_aiCrushesInfantry) {
		if ( source->getControllingPlayer() &&
			(source->getControllingPlayer()->getPlayerType() == PLAYER_COMPUTER) &&
			source->canCrushOrSquish(victim) ) {
			return true;	// Always pursue if we can squish.
		}
	}

	if (weapon->isTooClose(source, victim)) {
		return false;		// Don't chase it if we are already too close.
	}

	Real ourMaxSpeed = source->getAI()->getCurLocomotorSpeed();

	Real victimSpeed = victim->getPhysics()->getForwardSpeed2D();
	if (victimSpeed >= ourMaxSpeed) {
		return false; // we can't catch them.
	}
	if (victimSpeed < ourMaxSpeed/10) {
		return false; // They aren't moving very fast, so don't chase.
	}
	Real dx = victim->getPosition()->x - source->getPosition()->x;
	Real dy = victim->getPosition()->y - source->getPosition()->y;
	Coord3D victimVector = *victim->getUnitDirectionVector2D();
	if (dx*victimVector.x + dy*victimVector.y < 0 ) {
		return false; // they are moving towards us.
	}
	return true;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Compute a valid spot to fire our weapon from.
 * Result in m_goalPosition.
 * Return false if can't find a good spot.
 */
Bool AIAttackApproachTargetState::computePath()
{
	Bool forceRepath = false;

	// if we're immobile we can't possibly approach the target
	if( getMachineOwner()->isMobile() == false )
		return false;

	//CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - begin for object %d", getMachineOwner()->getID()));

	AIUpdateInterface *ai = getMachineOwner()->getAI();

	if (ai->isBlockedAndStuck())
	{
		forceRepath = true;
		// Intense logging. jba
		//CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - stuck, recomputing for object %d", getMachineOwner()->getID()));
	}
	if (AIInternalMoveToState::gxRow().waitingForPath) return true;

	if (!forceRepath && ai->getPath()==nullptr && !ai->isWaitingForPath())
	{
		forceRepath = true;
	}

	// force minimum time between recomputation
	/// @todo Unify recomputation conditions & account for obj ID so everyone doesn't compute on the same frame (MSB)
	if (!forceRepath && TheGameLogic->getFrame() - GXROW_AIATTACKAPPROACHTARGETSTATE.approachTimestamp < MIN_RECOMPUTE_TIME)
	{
		//CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing because of min time for object %d", getMachineOwner()->getID()));
		return true;
	}

	GXROW_AIATTACKAPPROACHTARGETSTATE.approachTimestamp = TheGameLogic->getFrame();

	// if we have a goal object, move to it, otherwise move to goal position
	if (getMachineGoalObject())
	{

		Object* source = getMachineOwner();
		// if our victim's position hasn't changed, don't re-path
		if (!forceRepath && isSamePosition(source->getPosition(), &GXROW_AIATTACKAPPROACHTARGETSTATE.prevVictimPos, getMachineGoalObject()->getPosition() ))
		{
			CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing because victim in same place for object %d", getMachineOwner()->getID()));
			return true;
		}

		Weapon* weapon = source->getCurrentWeapon();
		if (!weapon)
		{
			CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing because of no weapon for object %d", getMachineOwner()->getID()));
			return false;
		}

		// remember where we think our victim is, so if it moves, we can re-path
		Object *victim = getMachineGoalObject();
		GXROW_AIATTACKAPPROACHTARGETSTATE.prevVictimPos = *victim->getPosition();
		if (canPursue(source, weapon, victim))
		{
			return false;  // break out, and do the pursuit state.
		}
		setAdjustsDestination(true);
		if (weapon->isContactWeapon())
		{
			// Weapon is basically a contact weapon, so let the attacker pathfind into the target.
			ai->ignoreObstacle(victim);
			setAdjustsDestination(false); // We want to run into the target.
			ai->setPathExtraDistance(10*PATHFIND_CELL_SIZE_F); // We don't want it to slow down.
		}

		AIInternalMoveToState::gxRow().goalPosition = GXROW_AIATTACKAPPROACHTARGETSTATE.prevVictimPos;
		AIInternalMoveToState::gxRow().waitingForPath = true;

		Coord3D pos;
		victim->getGeometryInfo().getCenterPosition( *victim->getPosition(), pos );

		CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - requestAttackPath() for object %d", getMachineOwner()->getID()));
		ai->requestAttackPath(victim->getID(), &pos );
		GXROW_AIATTACKAPPROACHTARGETSTATE.stopIfInRange = false; // we have calculated a position to shoot from, so go there.

		CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing after repathing for object %d", getMachineOwner()->getID()));
		return true;
	}
	else
	{
		// goal position.
		setAdjustsDestination(true);
		GXROW_AIATTACKAPPROACHTARGETSTATE.stopIfInRange = false; // Attack position is used by missiles, and they hit the position.
		AIInternalMoveToState::gxRow().goalPosition = *getMachineGoalPosition();
		if (!forceRepath)
		{
			CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing because we're aiming for a fixed position for object %d", getMachineOwner()->getID()));
			return true; // fixed positions don't move.
		}
		// must use computeAttackPath so that min ranges are considered.
		AIInternalMoveToState::gxRow().waitingForPath = true;
		ai->requestAttackPath(INVALID_ID, &AIInternalMoveToState::gxRow().goalPosition);
		CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing after repathing at a fixed position for object %d", getMachineOwner()->getID()));
		return true;
	}


	CRCDEBUG_LOG(("AIAttackApproachTargetState::computePath - bailing at end of function for object %d", getMachineOwner()->getID()));
	return true;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackApproachTargetState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackApproachTargetState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
  AIInternalMoveToState::xfer( xfer );

	xfer->xferCoord3D(&GXROW_AIATTACKAPPROACHTARGETSTATE.prevVictimPos);
	xfer->xferUnsignedInt(&GXROW_AIATTACKAPPROACHTARGETSTATE.approachTimestamp);
	xfer->xferBool(&GXROW_AIATTACKAPPROACHTARGETSTATE.follow);
	xfer->xferBool(&GXROW_AIATTACKAPPROACHTARGETSTATE.isAttackingObject);
	xfer->xferBool(&GXROW_AIATTACKAPPROACHTARGETSTATE.stopIfInRange);
	xfer->xferBool(&GXROW_AIATTACKAPPROACHTARGETSTATE.isInitialApproach);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackApproachTargetState::loadPostProcess()
{
 // extend base class
  AIInternalMoveToState::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackApproachTargetState::onEnter()
{
	// contained by AIAttackState, so no separate timer
	// urg. hacky. if we are a projectile, turn on precise z-pos.
	//CRCDEBUG_LOG(("AIAttackApproachTargetState::onEnter() - object %d", getMachineOwner()->getID()));
	Object* source = getMachineOwner();
	AIUpdateInterface* ai = source->getAI();
	if (source->isKindOf(KINDOF_PROJECTILE))
	{
		if (ai->getCurLocomotor())
			ai->getCurLocomotor()->setUsePreciseZPos(true);
	}

	if (getMachine()->isGoalObjectDestroyed())
	{
		return STATE_SUCCESS; // Already killed victim.
	}

	//If our object is deployed, can't tell him to move!
	//if( source && source->testStatus( OBJECT_STATUS_DEPLOYED ) )
//	{
//		return STATE_SUCCESS;
//	}

	GXROW_AIATTACKAPPROACHTARGETSTATE.prevVictimPos.x = 0.0f;
	GXROW_AIATTACKAPPROACHTARGETSTATE.prevVictimPos.y = 0.0f;
	GXROW_AIATTACKAPPROACHTARGETSTATE.prevVictimPos.z = 0.0f;

	GXROW_AIATTACKAPPROACHTARGETSTATE.approachTimestamp = -MIN_RECOMPUTE_TIME;

	// See if we're close enough.
	Object *victim = getMachineGoalObject();
	if (victim)
	{
		Weapon* weapon = source->getCurrentWeapon();
		if (!weapon)
		{
			return STATE_FAILURE;
		}
		if (weapon->isWithinAttackRange(source, victim))
		{
			Bool viewBlocked = false;
			if (source && victim && ai->isDoingGroundMovement() && !victim->isSignificantlyAboveTerrain())
			{
				viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, *source->getPosition(), victim, *victim->getPosition());
			}
			if (!viewBlocked)
			{
				return STATE_SUCCESS;
			}
		}

		// Check here:  If we are a player, and we got to this state via an ai command (ie we auto-acquired),
		// we don't want to chase the unit. isAllowedToChase is set when we are in a deploy and attack state (troop crawler).
		// Kris (July 2003): If we are retaliating... don't fail out!
		if( ai->getCurrentStateID() != AI_GUARD_RETALIATE )
		{
			if (source->getControllingPlayer()->getPlayerType() == PLAYER_HUMAN)
			{
				if (ai->getLastCommandSource() == CMD_FROM_AI && !ai->isAllowedToChase() )
				{
					if (!weapon->isContactWeapon())
					{
						return STATE_FAILURE;
					}
				}
			} else {
				// Computer player.  Don't chase aircraft, unless we're hunting. jba [8/27/2003]
				Bool hunt = ai->getCurrentStateID() == AI_HUNT;
				if (!hunt && victim->isKindOf(KINDOF_AIRCRAFT) && victim->isAirborneTarget())
				{
					return STATE_FAILURE;
				}
			}
		}
		if (canPursue(source, weapon, victim))
		{
			return STATE_SUCCESS;  // break out, and do the pursuit state.
		}
	} else {
		// Attacking a position.  For a variety of reasons, we need to destroy any existing path or we spin. jba. [8/25/2003]
		ai->destroyPath();
	}
	// If we have a turret, start aiming.
	WhichTurretType tur = ai->getWhichTurretForCurWeapon();
	if (tur != TURRET_INVALID)
	{
		if (GXROW_AIATTACKAPPROACHTARGETSTATE.isAttackingObject)
		{
			ai->setTurretTargetObject(tur, victim, GXROW_AIATTACKAPPROACHTARGETSTATE.isForceAttacking);
		}
		else
		{
			ai->setTurretTargetPosition(tur, getMachineGoalPosition());
		}
	}

	// find a good spot to shoot from
	//CRCDEBUG_LOG(("AIAttackApproachTargetState::onEnter() - calling computePath() for object %d", getMachineOwner()->getID()));
	if (computePath() == false)
		return STATE_FAILURE;

	setAdjustsDestination(false);
	StateReturnType ret =  AIInternalMoveToState::onEnter();
	setAdjustsDestination(true);
	return ret;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackApproachTargetState::updateInternal()
{
	AIUpdateInterface* ai = getMachineOwner()->getAI();
	//CRCDEBUG_LOG(("AIAttackApproachTargetState::updateInternal() - object %d", getMachineOwner()->getID()));
	if (getMachine()->isGoalObjectDestroyed())
	{
		ai->notifyVictimIsDead();
		ai->setCurrentVictim(nullptr);
		return STATE_FAILURE;
	}
	GXROW_AIATTACKAPPROACHTARGETSTATE.stopIfInRange = !ai->isAttackPath();

	StateReturnType code = STATE_FAILURE;
 	Object* source = getMachineOwner();
	Weapon* weapon = source->getCurrentWeapon();
	Object *victim = getMachineGoalObject();
	if (victim)
	{
 		if (source->getControllingPlayer()->getPlayerType() == PLAYER_COMPUTER)
		{
			Bool hunt = ai->getCurrentStateID() == AI_HUNT;
			// Computer player.  Don't chase aircraft unless hunting. jba [8/27/2003]
			if (!hunt && victim->isKindOf(KINDOF_AIRCRAFT) && victim->isAirborneTarget())
			{
				return STATE_FAILURE;
			}
		}
		if( victim->testStatus( OBJECT_STATUS_STEALTHED ) && !victim->testStatus( OBJECT_STATUS_DETECTED ) && !victim->testStatus( OBJECT_STATUS_DISGUISED ) )
		{
			// If obj is stealthed, can no longer approach.
			// TheSuperHackers @bugfix Stubbjax 19/11/2025 Except when disarming stealthed mines or traps.
#if RETAIL_COMPATIBLE_CRC
			return STATE_FAILURE;
#else
			const Bool isTargetingMine = weapon && weapon->getDamageType() == DAMAGE_DISARM &&
				(victim->isKindOf(KINDOF_MINE) || victim->isKindOf(KINDOF_BOOBY_TRAP) || victim->isKindOf(KINDOF_DEMOTRAP));

			if (!isTargetingMine)
				return STATE_FAILURE;
#endif
		}
		ai->setCurrentVictim(victim);
		// Attacking an object.
		if (weapon && weapon->isContactWeapon() && weapon->isWithinAttackRange(source, victim))
		{
			return STATE_SUCCESS;
		}
		if (GXROW_AIATTACKAPPROACHTARGETSTATE.stopIfInRange && weapon && weapon->isWithinAttackRange(source, victim))
		{
			Bool viewBlocked = false;
			if (victim && ai->isDoingGroundMovement() && !victim->isSignificantlyAboveTerrain())
			{
				viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, *source->getPosition(), victim, *victim->getPosition());
			}
			if (!viewBlocked)
			{
				return STATE_SUCCESS;
			}
		}
		// find a good spot to shoot from
		//CRCDEBUG_LOG(("AIAttackApproachTargetState::updateInternal() - calling computePath() to victim for object %d", getMachineOwner()->getID()));
		if (computePath() == false)
			return STATE_SUCCESS;
		code = AIInternalMoveToState::update();


		if (code != STATE_CONTINUE)
		{
			return STATE_SUCCESS;	// Always return state success, as state failure exits the attack.
			// we may need to aim & do another approach if the target moved.  jba.
		}
	}
	else
	{
		// Attacking a position.
		// find a good spot to shoot from
		//CRCDEBUG_LOG(("AIAttackApproachTargetState::updateInternal() - calling computePath() to position for object %d", getMachineOwner()->getID()));
		if (GXROW_AIATTACKAPPROACHTARGETSTATE.stopIfInRange && weapon && weapon->isWithinAttackRange(source, &AIInternalMoveToState::gxRow().goalPosition))
		{
			Bool viewBlocked = false;
			if ( ai->isDoingGroundMovement() )
			{
				viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, *source->getPosition(), nullptr, AIInternalMoveToState::gxRow().goalPosition);
			}
			if (!viewBlocked)
			{
				return STATE_SUCCESS;
			}
		}
		if (computePath() == false)
			return STATE_FAILURE;
		code = AIInternalMoveToState::update();
	}
	return code;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackApproachTargetState::update()
{
	// contained by AIAttackState, so no separate timer

	StateReturnType code = updateInternal();
	Object* source = getMachineOwner();
	AIUpdateInterface *ai = source->getAI();

	if (GXROW_AIATTACKAPPROACHTARGETSTATE.follow && GXROW_AIATTACKAPPROACHTARGETSTATE.isAttackingObject)
	{
		// Basically, if the object is alive, we continue, in case the target moves.
		Object* victim = getMachineGoalObject();
		if (source && victim && source->isMobile() && !victim->getTemplate()->isKindOf(KINDOF_IMMOBILE))
		{
			if (code != STATE_CONTINUE)
			{
				GXROW_AIATTACKAPPROACHTARGETSTATE.isInitialApproach = false;
			}
			// Object is still alive (and so are we)
			// It could move (and so can we), so just continue & keep checking.
			code = STATE_CONTINUE;
		}
	}

	if (GXROW_AIATTACKAPPROACHTARGETSTATE.isInitialApproach)
	{
		WhichTurretType tur = ai->getWhichTurretForCurWeapon();
		if (tur != TURRET_INVALID)
		{
			Object *temporaryTarget = ai->getNextMoodTarget( true, false );
			if (temporaryTarget)
			{
				ai->setTurretTargetObject(tur, temporaryTarget, GXROW_AIATTACKAPPROACHTARGETSTATE.isForceAttacking);
			}
		}
	}

	return code;
}

//----------------------------------------------------------------------------------------------------------
void AIAttackApproachTargetState::onExit( StateExitType status )
{
	// contained by AIAttackState, so no separate timer
	AIInternalMoveToState::onExit( status );

	AIUpdateInterface *ai = getMachineOwner()->getAI();
	Object *obj = getMachineOwner();
	if (ai) {
		ai->ignoreObstacle(nullptr);

		// Per JohnA, this state should not be calling ai->destroyPath, because we can have spastic users
		// that click the target repeatedly. This will prevent the unit from stuttering for said spastic
		// users.
		// ai->destroyPath();
		// urg. hacky. if we are a projectile, reset precise z-pos.
		if (getMachineOwner()->isKindOf(KINDOF_PROJECTILE))
		{
			if (ai && ai->getCurLocomotor())
				ai->getCurLocomotor()->setUsePreciseZPos(false);
		}
		if (ai->isDoingGroundMovement()) {
			Real dx = AIInternalMoveToState::gxRow().goalPosition.x-obj->getPosition()->x;
			Real dy = AIInternalMoveToState::gxRow().goalPosition.y-obj->getPosition()->y;
			if (dx*dx+dy*dy<PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F*0.125)
			{
				// We are doing accurate ground movement, so make sure we end exactly at the goal.
				obj->setPosition(&AIInternalMoveToState::gxRow().goalPosition);
			}
		}
	}

	GXROW_AIATTACKAPPROACHTARGETSTATE.isInitialApproach = false;	// We only want to allow turreted things to fire at enemies during their
																// first approach
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------



//----------------------------------------------------------------------------------------------------------
/**
 * Compute a valid spot to fire our weapon from.
 * Result in m_goalPosition.
 * Return false if can't find a good spot.
 */
Bool AIAttackPursueTargetState::computePath()
{
	Bool forceRepath = false;

	// if we're immobile we can't possibly approach the target
	if( getMachineOwner()->isMobile() == false )
		return false;

	AIUpdateInterface *ai = getMachineOwner()->getAI();

	if (ai->isBlockedAndStuck())
	{
		return false;
	}
	if (AIInternalMoveToState::gxRow().waitingForPath) return true;

	if (!forceRepath && ai->getPath()==nullptr && !ai->isWaitingForPath())
	{
		forceRepath = true;
	}

	// force minimum time between recomputation
	/// @todo Unify recomputation conditions & account for obj ID so everyone doesn't compute on the same frame (MSB)
	if (!forceRepath && TheGameLogic->getFrame() - GXROW_AIATTACKPURSUETARGETSTATE.approachTimestamp < MIN_RECOMPUTE_TIME)
	{
		return true;
	}

	GXROW_AIATTACKPURSUETARGETSTATE.approachTimestamp = TheGameLogic->getFrame();

	DEBUG_ASSERTLOG(getMachineGoalObject(), ("***************************Should only be pursuing objects.  jba"));
	// if we have a goal object, move to it, otherwise fail & continue to AIAttackApproachTargetState
	if (getMachineGoalObject())
	{

		Object* source = getMachineOwner();
		// if our victim's position hasn't changed, don't re-path
		if (!forceRepath && isSamePosition(source->getPosition(), &GXROW_AIATTACKPURSUETARGETSTATE.prevVictimPos, getMachineGoalObject()->getPosition() ))
			return true;

		Weapon* weapon = source->getCurrentWeapon();
		if (!weapon)
		{
			return false;
		}
		if (!canPursue(source, weapon, getMachineGoalObject())) {
			return false;
		}

		// remember where we think our victim is, so if it moves, we can re-path
		Object *victim = getMachineGoalObject();
		GXROW_AIATTACKPURSUETARGETSTATE.prevVictimPos = *victim->getPosition();

		setAdjustsDestination(true);

		AIInternalMoveToState::gxRow().goalPosition = GXROW_AIATTACKPURSUETARGETSTATE.prevVictimPos;
		AIInternalMoveToState::gxRow().waitingForPath = true;
		ai->requestPath(&AIInternalMoveToState::gxRow().goalPosition, false);
		GXROW_AIATTACKPURSUETARGETSTATE.stopIfInRange = false; // we have calculated a position to shoot from, so go there.
		return true;
	}

	return false;
}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackPursueTargetState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackPursueTargetState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
  AIInternalMoveToState::xfer( xfer );

	xfer->xferCoord3D(&GXROW_AIATTACKPURSUETARGETSTATE.prevVictimPos);
	xfer->xferUnsignedInt(&GXROW_AIATTACKPURSUETARGETSTATE.approachTimestamp);
	xfer->xferBool(&GXROW_AIATTACKPURSUETARGETSTATE.follow);
	xfer->xferBool(&GXROW_AIATTACKPURSUETARGETSTATE.isAttackingObject);
	xfer->xferBool(&GXROW_AIATTACKPURSUETARGETSTATE.stopIfInRange);
	xfer->xferBool(&GXROW_AIATTACKPURSUETARGETSTATE.isInitialApproach);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackPursueTargetState::loadPostProcess()
{
 // extend base class
  AIInternalMoveToState::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackPursueTargetState::onEnter()
{
	// contained by AIAttackState, so no separate timer
	// If we return STATE_SUCCESS or STATE_FAILURE, we proceed to AIAttackApproachTargetState.
	Object* source = getMachineOwner();
	AIUpdateInterface* ai = source->getAI();

	if (source->isKindOf(KINDOF_PROJECTILE))
	{
		//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - is a projectile for object %d (%s)", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
		return STATE_SUCCESS;	 // Projectiles go directly to AIAttackApproachTargetState.
	}

	if (getMachine()->isGoalObjectDestroyed())
	{
		//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - goal object is destroyed for object %d (%s)", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
		return STATE_SUCCESS; // Already killed victim.
	}
	if (!GXROW_AIATTACKPURSUETARGETSTATE.isAttackingObject)	{
		//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - not attacking for object %d (%s)", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
		return STATE_SUCCESS; // only pursue objects - positions don't move.
	}

	setAdjustsDestination(false);

	// Check here:  If we are a player, and we got to this state via an ai command (ie we auto-acquired),
	// we don't want to chase the unit.
	// Kris (July 2003): If we are retaliating... don't succeed out!
	if( ai->getCurrentStateID() != AI_GUARD_RETALIATE )
	{
		if (source->getControllingPlayer()->getPlayerType() == PLAYER_HUMAN)
		{
			if (ai->getLastCommandSource() == CMD_FROM_AI)
			{
				return STATE_SUCCESS;

			}
		}
	}

	GXROW_AIATTACKPURSUETARGETSTATE.prevVictimPos.x = 0.0f;
	GXROW_AIATTACKPURSUETARGETSTATE.prevVictimPos.y = 0.0f;
	GXROW_AIATTACKPURSUETARGETSTATE.prevVictimPos.z = 0.0f;

	GXROW_AIATTACKPURSUETARGETSTATE.approachTimestamp = -MIN_RECOMPUTE_TIME;

	// See if we're close enough.
	Object *victim = getMachineGoalObject();
	if (victim) {
		Weapon* weapon = source->getCurrentWeapon();
		if (!weapon)
		{
			return STATE_FAILURE;
		}
		if (!canPursue(source, weapon, victim) )
		{
			//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - can't pursue for object %d (%s)", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
			return STATE_SUCCESS;
		}
	}	else {
		//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - no victim for object %d (%s)", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
		return STATE_SUCCESS; // gotta have a victim.
	}
	// If we have a turret, start aiming.
	WhichTurretType tur = ai->getWhichTurretForCurWeapon();
	if (tur != TURRET_INVALID)
	{
		ai->setTurretTargetObject(tur, victim, GXROW_AIATTACKPURSUETARGETSTATE.isForceAttacking);
	} else {
		//CRCDEBUG_LOG(("AIAttackPursueTargetState::onEnter() - no turret for object %d (%s)", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
		return STATE_SUCCESS; // we only pursue with turrets, as non-turreted weapons can't fire on the run.
	}

	// find a good spot to shoot from
	if (computePath() == false)
		return STATE_SUCCESS;

	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackPursueTargetState::updateInternal()
{
	AIUpdateInterface* ai = getMachineOwner()->getAI();
	if (getMachine()->isGoalObjectDestroyed())
	{
		ai->notifyVictimIsDead();
		ai->setCurrentVictim(nullptr);
		return STATE_FAILURE;
	}
	GXROW_AIATTACKPURSUETARGETSTATE.stopIfInRange = false;

	Object* source = getMachineOwner();
	StateReturnType code = STATE_FAILURE;
 	Object *victim = getMachineGoalObject();
	if (victim)
	{
		if( victim->testStatus( OBJECT_STATUS_STEALTHED ) && !victim->testStatus( OBJECT_STATUS_DETECTED ) && !victim->testStatus( OBJECT_STATUS_DISGUISED ) )
		{
			return STATE_FAILURE;	// If obj is stealthed, can no longer pursue.
		}
		ai->setCurrentVictim(victim);
		// Attacking an object.
		// find a good spot to shoot from
		if (computePath() == false)
			return STATE_FAILURE;
		code = AIInternalMoveToState::update();
		if (code != STATE_CONTINUE)
		{
			//CRCDEBUG_LOG(("AIAttackPursueTargetState::updateInternal() - failed internal update() for object %d (%s)", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
			return STATE_SUCCESS;	// Always return state success, as state failure exits the attack.
			// we may need to aim & do another approach if the target moved.  jba.
		}
		Weapon* weapon = source->getCurrentWeapon();
		if (!weapon)
			return STATE_FAILURE;
		// If we have a turret, start aiming.
		WhichTurretType tur = ai->getWhichTurretForCurWeapon();
		if (tur == TURRET_INVALID)
		{
			//CRCDEBUG_LOG(("AIAttackPursueTargetState::updateInternal() - no turret for object %d (%s)", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
			return STATE_SUCCESS; // We currently only pursue with a turret weapon.
		}

		Bool viewBlocked = false;
		if (ai->isDoingGroundMovement() && !victim->isSignificantlyAboveTerrain())
		{
			viewBlocked = TheAI->pathfinder()->isAttackViewBlockedByObstacle(source, *source->getPosition(), victim, *victim->getPosition());
		}
		if (!viewBlocked && victim->getPhysics() && weapon->isWithinAttackRange(source, victim)) {
			// If we have a turret, start aiming.
			ai->setTurretTargetObject(tur, victim, GXROW_AIATTACKPURSUETARGETSTATE.isForceAttacking);
			//  match speeds;
			GXROW_AIATTACKPURSUETARGETSTATE.isInitialApproach = false;
			Real victimSpeed = victim->getPhysics()->getForwardSpeed2D();
			if (weapon->isGoalPosWithinAttackRange(source, source->getPosition(), victim, victim->getPosition())){
				victimSpeed *= 0.95f;
			}
			if (source->canCrushOrSquish(victim)) {
				victimSpeed = FAST_AS_POSSIBLE;
			}
			ai->setDesiredSpeed(victimSpeed);
			// Really intense debug info.  jba.
			// DEBUG_LOG(("VS %f, OS %f, goal %f", victim->getPhysics()->getForwardSpeed2D(), source->getPhysics()->getForwardSpeed2D(), victimSpeed));
		}	else {
			ai->setDesiredSpeed(FAST_AS_POSSIBLE);
		}
	}
	return code;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackPursueTargetState::update()
{
	// contained by AIAttackState, so no separate timer

	StateReturnType code = updateInternal();
	Object* source = getMachineOwner();
	AIUpdateInterface *ai = source->getAI();

	if (GXROW_AIATTACKPURSUETARGETSTATE.isInitialApproach)
	{
		WhichTurretType tur = ai->getWhichTurretForCurWeapon();
		if (tur != TURRET_INVALID)
		{
			Object *temporaryTarget = ai->getNextMoodTarget( true, false );
			if (temporaryTarget)
			{
				ai->setTurretTargetObject(tur, temporaryTarget, GXROW_AIATTACKPURSUETARGETSTATE.isForceAttacking);
			}
		}
	}

	return code;
}

//----------------------------------------------------------------------------------------------------------
void AIAttackPursueTargetState::onExit( StateExitType status )
{
	// contained by AIAttackState, so no separate timer
	//CRCDEBUG_LOG(("AIAttackPursueTargetState::onExit() for object %d (%s)", getMachineOwner()->getID(), getMachineOwner()->getTemplate()->getName().str()));
	AIInternalMoveToState::onExit( status );

	GXROW_AIATTACKPURSUETARGETSTATE.isInitialApproach = false;	// We only want to allow turreted things to fire at enemies during their
																// first approach
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
Bool AIPickUpCrateState::computePath()
{
	return AIInternalMoveToState::computePath();
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIPickUpCrateState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIPickUpCrateState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
  AIInternalMoveToState::xfer( xfer );

	xfer->xferInt(&GXROW_AIPICKUPCRATESTATE.delayCounter);
	xfer->xferCoord3D(&AIInternalMoveToState::gxRow().goalPosition);

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIPickUpCrateState::loadPostProcess()
{
 // extend base class
  AIInternalMoveToState::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIPickUpCrateState::onEnter()
{
	Object* goalObj = getMachineGoalObject();
	if (!goalObj) {
		return STATE_FAILURE;
	}
	setAdjustsDestination(true);
	AIInternalMoveToState::gxRow().goalPosition = *goalObj->getPosition();
	GXROW_AIPICKUPCRATESTATE.delayCounter = 3;
	return STATE_CONTINUE;

}

//----------------------------------------------------------------------------------------------------------
void AIPickUpCrateState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );

}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIPickUpCrateState::update()
{
			/// @todo srj -- find a way to sleep for a number of frames here, if possible

	if (GXROW_AIPICKUPCRATESTATE.delayCounter) {
		GXROW_AIPICKUPCRATESTATE.delayCounter--;
		if (GXROW_AIPICKUPCRATESTATE.delayCounter == 0) {
			return AIInternalMoveToState::onEnter();
		}
		return STATE_CONTINUE;
	}
	// do movement
	StateReturnType status = AIInternalMoveToState::update();

	return status;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIFollowPathState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIFollowPathState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferInt(&GXROW_AIFOLLOWPATHSTATE.index);
	xfer->xferBool(&GXROW_AIFOLLOWPATHSTATE.adjustFinal);
	xfer->xferBool(&GXROW_AIFOLLOWPATHSTATE.adjustFinalOverride);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIFollowPathState::loadPostProcess()
{
	AIInternalMoveToState::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowPathState::onEnter()
{
	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	GXROW_AIFOLLOWPATHSTATE.index = 0;
	const Coord3D *pos = ai->friend_getGoalPathPosition( GXROW_AIFOLLOWPATHSTATE.index );

	if (pos == nullptr)
		return STATE_FAILURE;

	// set initial movement goal
	AIInternalMoveToState::gxRow().goalPosition = *pos;
 	const Coord3D *nextPos = ai->friend_getGoalPathPosition( GXROW_AIFOLLOWPATHSTATE.index+1 );
	GXROW_AIFOLLOWPATHSTATE.adjustFinal = true;

	//Assign this value to the AIUpdateInterface so object's can access this value while
	//determine which waypoints to plot in the waypoint renderer.
	ai->friend_setCurrentGoalPathIndex( GXROW_AIFOLLOWPATHSTATE.index );


 	if (getID() == AI_FOLLOW_EXITPRODUCTION_PATH) {
		ai->setCanPathThroughUnits(true);
		setAdjustsDestination(false);
		GXROW_AIFOLLOWPATHSTATE.adjustFinal = true;
	}
	StateReturnType ret = AIInternalMoveToState::onEnter();
	if (obj->getFormationID() != NO_FORMATION_ID) {
		AIGroup *group = ai->getGroup();
		if (group) {
			Real speed = group->getSpeed();
			ai->setDesiredSpeed(speed);
		}
	}
 	if (nextPos)
	{
		Coord2D delta;
		delta.x = nextPos->x - pos->x;
		delta.y = nextPos->y - pos->y;
		Real offset = delta.length();
 		const Coord3D *followingPos = ai->friend_getGoalPathPosition( GXROW_AIFOLLOWPATHSTATE.index+2 );
		if (followingPos) offset += 4*PATHFIND_CELL_SIZE_F;
		ai->setPathExtraDistance(offset);
		// We are in the middle of a path, so don't set the final goal location yet.
		setAdjustsDestination(false);
	}
	else
	{
		setAdjustsDestination(GXROW_AIFOLLOWPATHSTATE.adjustFinal);
		ai->setPathExtraDistance(0);

		// urg. hacky. if we are a projectile on the last segment, turn on precise z-pos.
		if (obj->isKindOf(KINDOF_PROJECTILE))
		{
			if (ai && ai->getCurLocomotor())
				ai->getCurLocomotor()->setUsePreciseZPos(true);
		}
	}
	return ret;
}

//----------------------------------------------------------------------------------------------------------
void AIFollowPathState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );

	// turn off precision-z-pos when we exit, just in case.
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (!ai) return;
	ai->setCanPathThroughUnits(false);
	if (ai->getCurLocomotor())
		ai->getCurLocomotor()->setUsePreciseZPos(false);

	//Assign this value to the AIUpdateInterface so object's can access this value while
	//determine which waypoints to plot in the waypoint renderer.
	ai->friend_setCurrentGoalPathIndex( -1 );
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowPathState::update()
{
	getMachine()->setGoalPosition(&AIInternalMoveToState::gxRow().goalPosition);
	// do movement
	StateReturnType status = AIInternalMoveToState::update();
	// if move to has finished, move to next point on path
	if (status == STATE_SUCCESS || status == STATE_FAILURE)
	{
		Object *obj = getMachineOwner();
		AIUpdateInterface *ai = obj->getAI();
		if (status == STATE_FAILURE && GXROW_AIFOLLOWPATHSTATE.retryCount>0) {
			// If we failed, & haven't reached retry limit, try again.  jba.
			GXROW_AIFOLLOWPATHSTATE.retryCount--;
		}	else {
			++GXROW_AIFOLLOWPATHSTATE.index;
		}
		const Coord3D *pos = ai->friend_getGoalPathPosition( GXROW_AIFOLLOWPATHSTATE.index );

		Bool tooClose=true;
		while (pos && tooClose) {
			Real dx = pos->x - obj->getPosition()->x;
			Real dy = pos->y - obj->getPosition()->y;
			tooClose = false;
			if (sqr(dx) + sqr(dy) < sqr(PATHFIND_CELL_SIZE_F)) {
				tooClose = true;
			}
			if (tooClose) {
				GXROW_AIFOLLOWPATHSTATE.index++;
				pos = ai->friend_getGoalPathPosition(GXROW_AIFOLLOWPATHSTATE.index);
			}
		}


		//Assign this value to the AIUpdateInterface so object's can access this value while
		//determine which waypoints to plot in the waypoint renderer.
		ai->friend_setCurrentGoalPathIndex( GXROW_AIFOLLOWPATHSTATE.index );
		ai->ignoreObstacleID(INVALID_ID); // we have exited whatever object we are leaving, if any.  jba.
		if (pos == nullptr)
		{
			// reached the end of the path
			return STATE_SUCCESS;
		}

		ai->friend_startingMove();
		// set next movement goal
		AIInternalMoveToState::gxRow().goalPosition = *pos;
 		const Coord3D *nextPos = ai->friend_getGoalPathPosition( GXROW_AIFOLLOWPATHSTATE.index+1 );

 		if (nextPos)
		{
			Coord2D delta;
			delta.x = nextPos->x - pos->x;
			delta.y = nextPos->y - pos->y;
			Real offset = delta.length();
 			const Coord3D *followingPos = ai->friend_getGoalPathPosition( GXROW_AIFOLLOWPATHSTATE.index+2 );
			if (followingPos) offset += 4*PATHFIND_CELL_SIZE_F;
			ai->setPathExtraDistance(offset);
			// We are in the middle of a path, so don't set the final goal location yet.
			setAdjustsDestination(false);
		}
		else
		{
			setAdjustsDestination(GXROW_AIFOLLOWPATHSTATE.adjustFinal && (GXROW_AIFOLLOWPATHSTATE.adjustFinalOverride || ai->isDoingGroundMovement()));
			if (getAdjustsDestination())
			{
				if (!TheAI->pathfinder()->adjustDestination(getMachineOwner(), ai->getLocomotorSet(), &AIInternalMoveToState::gxRow().goalPosition)) {
					return STATE_FAILURE;
				}
				TheAI->pathfinder()->updateGoal(getMachineOwner(), &AIInternalMoveToState::gxRow().goalPosition, TheTerrainLogic->getLayerForDestination(&AIInternalMoveToState::gxRow().goalPosition));
			}

			// urg. hacky. if we are a projectile on the last segment, turn on precise z-pos.
			if (obj->isKindOf(KINDOF_PROJECTILE))
			{
				if (ai && ai->getCurLocomotor())
					ai->getCurLocomotor()->setUsePreciseZPos(true);
			}
		}
		computePath();
		return STATE_CONTINUE;
	}

	return status;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIMoveAndEvacuateState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIMoveAndEvacuateState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferCoord3D(&GXROW_AIMOVEANDEVACUATESTATE.origin);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIMoveAndEvacuateState::loadPostProcess()
{
	AIInternalMoveToState::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndEvacuateState::onEnter()
{
	Object *obj = getMachineOwner();

	getMachine()->lock("AIMoveAndEvacuateState::onEnter");		// This state is not user interruptable.

	GXROW_AIMOVEANDEVACUATESTATE.origin = *obj->getPosition();
	setAdjustsDestination(true);

	// if we have a goal object, move to it, otherwise move to goal position
	if (getMachine()->getGoalObject())
		AIInternalMoveToState::gxRow().goalPosition = *getMachine()->getGoalObject()->getPosition();
	else
		AIInternalMoveToState::gxRow().goalPosition = *getMachine()->getGoalPosition();
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndEvacuateState::update()
{
	Object *obj = getMachine()->getOwner();
	if (obj->isEffectivelyDead())
	{
		return STATE_FAILURE;
	}

	// do movement
	StateReturnType status = AIInternalMoveToState::update();
	if (status != STATE_CONTINUE)
	{
		Object *obj = getMachineOwner();
		if (obj->isEffectivelyDead())
		{
			return STATE_FAILURE;
		}

		AIUpdateInterface *ai = obj->getAI();
		ai->aiEvacuate(FALSE, CMD_FROM_AI);
		obj->getTeam()->setActive();
	}

	return status;
}

//----------------------------------------------------------------------------------------------------------
void AIMoveAndEvacuateState::onExit( StateExitType status )
{
	getMachine()->unlock();
	getMachine()->setGoalPosition(&GXROW_AIMOVEANDEVACUATESTATE.origin); // In case we follow with a AIMoveAndDeleteState.
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
AIAttackMoveToState::AIAttackMoveToState( StateMachine *machine ) : AIMoveToState(machine)
{
#ifdef STATE_MACHINE_DEBUG
	setName("AIAttackMoveToState");
#endif	// set up the state
	AIMoveToState::gxRow().isMoveTo = false;
	m_frameToSleepUntil = 0;
	m_retryCount = ATTACK_RETRY_COUNT;
	m_attackMoveMachine = newInstance(AIAttackMoveStateMachine)(getMachineOwner(), "AIAttackMoveMachine");
	m_attackMoveMachine->initDefaultState();
}

//----------------------------------------------------------------------------------------------------------
AIAttackMoveToState::~AIAttackMoveToState()
{
	deleteInstance(m_attackMoveMachine);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveToState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveToState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 2;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
  AIMoveToState::xfer( xfer );

	if (version>=2) {
		xfer->xferUnsignedInt(&m_frameToSleepUntil);
		xfer->xferInt(&m_retryCount);
	}
	xfer->xferSnapshot(m_attackMoveMachine);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackMoveToState::loadPostProcess()
{
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIAttackMoveToState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (m_attackMoveMachine) name.concat(m_attackMoveMachine->getCurrentStateName());
	else name.concat("*null m_deployMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackMoveToState::onEnter()
{
	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAI();
	m_attackMoveMachine->clear();
	m_attackMoveMachine->setState( AI_IDLE );
	m_commandSrc = ai->getLastCommandSource();
	m_retryCount = ATTACK_RETRY_COUNT;
	m_frameToSleepUntil = 0;

	return AIMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
void AIAttackMoveToState::onExit( StateExitType status )
{
	m_attackMoveMachine->setState(AI_IDLE);
	AIMoveToState::onExit(status);
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackMoveToState::update()
{

	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAI();

	Bool forceRetargetThisFrame = false;
	Bool shouldRepathThisFrame = false;

	JetAIUpdate *jetAI = ai->getJetAIUpdate();
	if( jetAI && jetAI->isOutOfSpecialReloadAmmo() )
	{
		//We need to return to base to reload!
		return STATE_SUCCESS;
	}

	if (!m_attackMoveMachine->isInIdleState())
	{
		ai->setLocomotorGoalNone();
		owner->clearModelConditionState(MODELCONDITION_MOVING);
		m_attackMoveMachine->updateStateMachine();

		// if the machine is now idling, then we need to attempt to get a new target
		if (m_attackMoveMachine->isInIdleState()) {
			forceRetargetThisFrame = true;
			shouldRepathThisFrame = true;
			ai->friend_setLastCommandSource(m_commandSrc);
		} else {
			return STATE_CONTINUE;
		}
	}

	if (m_attackMoveMachine->isInIdleState())
	{

		// Check to see if we have created a crate we need to pick up.
		Object* crate = ai->checkForCrateToPickup();
		if (crate)
		{
			m_attackMoveMachine->setGoalObject(crate);
			m_attackMoveMachine->setState( AI_PICK_UP_CRATE );
			return STATE_CONTINUE;
		}

		Object* nextObjectToAttack;
		nextObjectToAttack = ai->getNextMoodTarget( !forceRetargetThisFrame, false );
		if (nextObjectToAttack != nullptr)
		{
			ai->friend_endingMove();
			m_attackMoveMachine->setGoalObject(nextObjectToAttack);
			m_attackMoveMachine->setState( AI_ATTACK_OBJECT );
			shouldRepathThisFrame = false;	// we're about to drop out of this function, but this is semantic emphasis.
			// Note that we picked up this command from the ai.
			ai->friend_setLastCommandSource(CMD_FROM_AI);
			// we don't want an update to take place 'till next frame.
			return STATE_CONTINUE;
		}
	}

	if (m_frameToSleepUntil>TheGameLogic->getFrame()) {
		return STATE_CONTINUE;
	} else if (m_frameToSleepUntil == TheGameLogic->getFrame()) {
		shouldRepathThisFrame = true;
	}

	if (shouldRepathThisFrame)
	{
		AIMoveToState::onEnter();
		forceRepath();
	}

	StateReturnType ret = AIMoveToState::update();
	if (ret != STATE_CONTINUE) {
		if (m_retryCount<1) return ret;
		/* check for close enough. */
		Real distSqr = sqr(owner->getPosition()->x - AIInternalMoveToState::gxRow().pathGoalPosition.x) + sqr(owner->getPosition()->y-AIInternalMoveToState::gxRow().pathGoalPosition.y);
		if (distSqr < sqr(ATTACK_CLOSE_ENOUGH_CELLS*PATHFIND_CELL_SIZE_F)) {
			return ret;
		}
		DEBUG_LOG(("AIAttackMoveToState::update Distance from goal %f, retrying.", sqrt(distSqr)));

		ret = STATE_CONTINUE;
		m_retryCount--;
		// Sleep 3 seconds.  We can attack during these frames, just not move.
		m_frameToSleepUntil = TheGameLogic->getFrame() + 3*LOGICFRAMES_PER_SECOND;
	}
	return ret;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIMoveAndDeleteState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIMoveAndDeleteState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferBool(&GXROW_AIMOVEANDDELETESTATE.appendGoalPosition);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIMoveAndDeleteState::loadPostProcess()
{
	AIInternalMoveToState::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndDeleteState::onEnter()
{
	setAdjustsDestination(false);
	getMachine()->lock("AIMoveAndDeleteState::onEnter");
	// if we have a goal object, move to it, otherwise move to goal position
	if (getMachine()->getGoalObject())
		AIInternalMoveToState::gxRow().goalPosition = *getMachine()->getGoalObject()->getPosition();
	else
		AIInternalMoveToState::gxRow().goalPosition = *getMachine()->getGoalPosition();
	GXROW_AIMOVEANDDELETESTATE.appendGoalPosition = true; // We may be moving off the map.
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIMoveAndDeleteState::update()
{
	Object *obj = getMachine()->getOwner();
	if (obj->isEffectivelyDead())
	{
		return STATE_FAILURE;
	}
	// do movement
	AIUpdateInterface *ai = obj->getAI();
	if (ai->getCurLocomotor())
	{
		ai->getCurLocomotor()->setAllowInvalidPosition(true);
	}
	if (GXROW_AIMOVEANDDELETESTATE.appendGoalPosition)
	{
		Path *thePath = ai->getPath();
		if (!ai->isWaitingForPath() && ai->getPath())
		{
			AIInternalMoveToState::gxRow().goalPosition.z = TheTerrainLogic->getGroundHeight(AIInternalMoveToState::gxRow().goalPosition.x, AIInternalMoveToState::gxRow().goalPosition.y);
			thePath->appendNode( &AIInternalMoveToState::gxRow().goalPosition, LAYER_GROUND);
			GXROW_AIMOVEANDDELETESTATE.appendGoalPosition = false; // just did it.
		}
	}
	StateReturnType status = AIInternalMoveToState::update();
	if (status != STATE_CONTINUE)
	{
		Object *obj = getMachineOwner();
		TheGameLogic->destroyObject(obj);
	}
	return status;
}

//----------------------------------------------------------------------------------------------------------
void AIMoveAndDeleteState::onExit( StateExitType status )
{
	getMachine()->unlock();
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

#define ALLOW_BACKTRACK
//----------------------------------------------------------------------------------------------------------
const Waypoint * AIFollowWaypointPathState::getNextWaypoint()
{
#ifdef ALLOW_BACKTRACK
	Int linkCount = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getNumLinks();
	Int which = GameLogicRandomValue( 0, linkCount-1 );
	const Waypoint *nextWay = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getLink( which );
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint;

	getMachine()->setGoalPosition(GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getLocation());// THANKS, JOHN
	return nextWay;
#else
	if (!hasNextWaypoint()) {
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint;
		return nullptr;
	}
	Int skip = -1;
	Int i;
	Int linkCount = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getNumLinks();
	for (i=0; i<linkCount; i++) {
		if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint == GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getLink(i)) {
			skip = i;
			break;
		}
	}
	Int which = 0;
	if (skip >= 0) {
		which = GameLogicRandomValue( 0, linkCount-2 );
		if (which == skip) which = linkCount-1;
	}	else {
		// pick a random link
		which = GameLogicRandomValue( 0, linkCount-1 );
	}
	const Waypoint *nextWay = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getLink( which );
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint;
	return nextWay;
#endif
}

//----------------------------------------------------------------------------------------------------------
Bool AIFollowWaypointPathState::hasNextWaypoint()
{
#ifdef ALLOW_BACKTRACK
	return GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getNumLinks()>0;
#else
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getNumLinks()==0) {
		return false; // no links, no next.
	}
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint==nullptr) {
		return GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getNumLinks()>0;
	}
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getNumLinks()>1) {
		// Two links, always works.
		return true;
	}
	// We have a prior waypoint, and 1 link.
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint == GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getLink(0)) {
		return false; // don't go back to same waypoint.
	}
	return true;
#endif
}

//----------------------------------------------------------------------------------------------------------
Real AIFollowWaypointPathState::calcExtraPathDistance()
{
	Real extra = PATHFIND_CELL_SIZE_F/10.0f;
	const Waypoint *curWay = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint;
	Int limit = 5; // just look ahead 5, in case of circular paths.  jba
	while (curWay && limit>0) {
		limit--;
		Int linkCount = curWay->getNumLinks();
		if (linkCount == 0) return extra;
		Int which = 0;
		const Waypoint *nextWay = curWay->getLink( which );
		Coord2D delta;
		delta.x = nextWay->getLocation()->x - curWay->getLocation()->x;
		delta.y = nextWay->getLocation()->y - curWay->getLocation()->y;
		extra += delta.length();
		curWay = nextWay;
	}
	return extra;
}

//----------------------------------------------------------------------------------------------------------
void AIFollowWaypointPathState::computeGoal(Bool useGroupOffsets)
{
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint == nullptr)
		return;

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	Coord3D dest = *GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getLocation();

	AIInternalMoveToState::gxRow().goalLayer = LAYER_GROUND; // waypoints are always on the ground.

	if (TheAI->pathfinder()->isPointOnWall(&dest)) {
		// except when they're on the wall.  jba.
		dest.z = TheAI->pathfinder()->getWallHeight();
		AIInternalMoveToState::gxRow().goalLayer = LAYER_WALL;
	}

	ai->setPathExtraDistance(calcExtraPathDistance());
	if (hasNextWaypoint()) {
		// We are in the middle of a path, so don't set the final goal location yet.
		setAdjustsDestination(false);
	} else {
		setAdjustsDestination(true);
		// urg. hacky. if we are a projectile on the last segment, turn on precise z-pos.
		if (obj->isKindOf(KINDOF_PROJECTILE))
		{
			if (ai && ai->getCurLocomotor())
				ai->getCurLocomotor()->setUsePreciseZPos(true);
		}
	}

#define NO_ROTATE_OFFSETS
#ifdef ROTATE_OFFSETS
	Real dx = (dest.x) - (obj->getPosition()->x - GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.x);
	Real dy = (dest.y) - (obj->getPosition()->y - GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.y);
	Real angle;
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint) {
		dx = dest.x - GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint->getLocation()->x;
		dy = dest.y - GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint->getLocation()->y;
		angle = atan2(dy, dx);
		Real deltaAngle = angle - GXROW_AIFOLLOWWAYPOINTPATHSTATE.angle;
		Real s = sin(deltaAngle);
		Real c = cos(deltaAngle);
		Real x = GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.x * c - GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.y * s;
		Real y = GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.y * c + GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.x * s;
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.x = x;
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.y = y;
	}	else {
		angle = atan2(dy, dx);
	}
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.angle = angle;
#endif


	AIInternalMoveToState::gxRow().goalPosition = dest;
	AIInternalMoveToState::gxRow().goalPosition.x += GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.x;
	AIInternalMoveToState::gxRow().goalPosition.y += GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.y;
	if (AIInternalMoveToState::gxRow().goalLayer == LAYER_WALL) {
		if (!TheAI->pathfinder()->isPointOnWall(&AIInternalMoveToState::gxRow().goalPosition)) {
			AIInternalMoveToState::gxRow().goalPosition = dest;
		}
	} else {
		AIInternalMoveToState::gxRow().goalPosition.z = TheTerrainLogic->getGroundHeight(AIInternalMoveToState::gxRow().goalPosition.x, AIInternalMoveToState::gxRow().goalPosition.y);
	}
	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent(&extent);

	if (extent.isInRegionNoZ(&dest)) {
		// The waypoint is on the map.  Check & see if the adjusted position is off map [8/28/2003]
		if (!extent.isInRegionNoZ(&AIInternalMoveToState::gxRow().goalPosition)) {
			// clamp to in region. [8/28/2003]
			if (AIInternalMoveToState::gxRow().goalPosition.x < extent.lo.x+PATHFIND_CELL_SIZE_F) {
				AIInternalMoveToState::gxRow().goalPosition.x = extent.lo.x+PATHFIND_CELL_SIZE_F;
			}
			if (AIInternalMoveToState::gxRow().goalPosition.y < extent.lo.y+PATHFIND_CELL_SIZE_F) {
				AIInternalMoveToState::gxRow().goalPosition.y = extent.lo.y+PATHFIND_CELL_SIZE_F;
			}
			if (AIInternalMoveToState::gxRow().goalPosition.x > extent.hi.x-PATHFIND_CELL_SIZE_F) {
				AIInternalMoveToState::gxRow().goalPosition.x = extent.hi.x-PATHFIND_CELL_SIZE_F;
			}
			if (AIInternalMoveToState::gxRow().goalPosition.y > extent.hi.y-PATHFIND_CELL_SIZE_F) {
				AIInternalMoveToState::gxRow().goalPosition.y = extent.hi.y-PATHFIND_CELL_SIZE_F;
			}
		}
	}

	if (!extent.isInRegionNoZ(&AIInternalMoveToState::gxRow().goalPosition)) {
		setAdjustsDestination(false); // moving off the map.
		ai->getCurLocomotor()->setAllowInvalidPosition(true); // allow it to move off the map.
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.appendGoalPosition = true; // Moving off the map.
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferCoord2D(&GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset);
	xfer->xferReal(&GXROW_AIFOLLOWWAYPOINTPATHSTATE.angle);
	xfer->xferInt(&GXROW_AIFOLLOWWAYPOINTPATHSTATE.framesSleeping);

	UnsignedInt id = INVALID_WAYPOINT_ID;
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint) {
		id = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getID();
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint = TheTerrainLogic->getWaypointByID(id);
	}
	id = INVALID_WAYPOINT_ID;
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint) {
		id = GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint->getID();
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint = TheTerrainLogic->getWaypointByID(id);
	}

	xfer->xferBool(&GXROW_AIFOLLOWWAYPOINTPATHSTATE.appendGoalPosition);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathState::loadPostProcess()
{
	AIInternalMoveToState::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowWaypointPathState::onEnter()
{
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.appendGoalPosition = false; // not moving off the map at this point.
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint = nullptr;
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint = ((AIStateMachine *)getMachine())->getGoalWaypoint();
	AIUpdateInterface *ai = getMachineOwner()->getAI();

	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint == nullptr && !GXROW_AIFOLLOWWAYPOINTPATHSTATE.moveAsGroup)		return STATE_FAILURE;

	getMachine()->setGoalPosition(GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getLocation());

	GXROW_AIFOLLOWWAYPOINTPATHSTATE.framesSleeping = 0;
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.x = GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.y = 0;

	Object *obj = getMachineOwner();
/*	Interesting thought experiment.  Didn't work well. jba
	Real distSqrLimit = 9*obj->getGeometryInfo().getMajorRadius()*obj->getGeometryInfo().getMajorRadius();
	const Waypoint *way = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint;
	Bool doPrecise = false;
	while (way) {
		if (way->getNext()) {
			Real dx = way->getLocation()->x - way->getNext()->getLocation()->x;
			Real dy = way->getLocation()->y - way->getNext()->getLocation()->y;
			Real distSqr = dx*dx + dy*dy;
			if (distSqr < distSqrLimit) {
				doPrecise = true;
			}
		}
		way = way->getNext();
	}
	if (doPrecise && ai->getCurLocomotor()) {
		//ai->getCurLocomotor()->setUltraAccurate(true);
	}
*/


	Real speed = FAST_AS_POSSIBLE;
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.moveAsGroup && GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint) {
		obj->getTeam()->setCurrentWaypoint(GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint);
		AIGroup *group = ai->getGroup();
		if (group) {
			speed = group->getSpeed();
			Coord3D center;

			group->getCenter( &center );
			GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.x = obj->getPosition()->x - center.x;
			GXROW_AIFOLLOWWAYPOINTPATHSTATE.groupOffset.y = obj->getPosition()->y - center.y;
		}
	}
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint==nullptr && GXROW_AIFOLLOWWAYPOINTPATHSTATE.moveAsGroup) {
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint = obj->getTeam()->getCurrentWaypoint();
	}
	// set initial movement goal
	computeGoal(GXROW_AIFOLLOWWAYPOINTPATHSTATE.moveAsGroup);
	StateReturnType ret = AIInternalMoveToState::onEnter();

	ai->setDesiredSpeed(speed);

	// Update the extra path distance.   AIInternalMoveToState::onEnter resets it.
	ai->setPathExtraDistance(calcExtraPathDistance());
	if (hasNextWaypoint()) {
		// We are in the middle of a path, so don't set the final goal location yet.
		setAdjustsDestination(false);
	} else {
		setAdjustsDestination(ai->isDoingGroundMovement());
		if (getAdjustsDestination()) {
			if (!TheAI->pathfinder()->adjustDestination(getMachineOwner(), ai->getLocomotorSet(), &AIInternalMoveToState::gxRow().goalPosition)) {
				DEBUG_LOG(("Breaking out of follow waypoint path"));
				return STATE_FAILURE;
			}
			TheAI->pathfinder()->updateGoal(getMachineOwner(), &AIInternalMoveToState::gxRow().goalPosition, AIInternalMoveToState::gxRow().goalLayer);
		}
		// urg. hacky. if we are a projectile on the last segment, turn on precise z-pos.
		if (obj->isKindOf(KINDOF_PROJECTILE))
		{
			if (ai && ai->getCurLocomotor())
				ai->getCurLocomotor()->setUsePreciseZPos(true);
		}
	}
	if (ret != STATE_CONTINUE) {
		DEBUG_LOG(("Breaking out of follow waypoint path"));
	}
	return ret;
}

//----------------------------------------------------------------------------------------------------------
void AIFollowWaypointPathState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );

	// turn off precision-z-pos when we exit, just in case.
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai && ai->getCurLocomotor()) {
		ai->getCurLocomotor()->setUsePreciseZPos(false);
		ai->getCurLocomotor()->setUltraAccurate(false);
	}
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowWaypointPathState::update()
{
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.framesSleeping>0) {
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.framesSleeping--;
		return STATE_CONTINUE;
	}
	Object *obj = getMachineOwner();
 	AIUpdateInterface *ai = obj->getAI();


	getMachine()->setGoalPosition(GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getLocation());

	UnsignedInt adjustment = ai->getMoodMatrixActionAdjustment(MM_Action_Move);
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.isFollowWaypointPathState && (adjustment & MAA_Action_To_AttackMove))	{
		if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.moveAsGroup) {
			ai->aiAttackFollowWaypointPathAsTeam(GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		}	else {
			ai->aiAttackFollowWaypointPath(GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		}
	}

	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.appendGoalPosition) {
		Path *thePath = ai->getPath();
		if (!ai->isWaitingForPath() && ai->getPath()) {
			//Coord3D pathEnd = *thePath->getLastNode()->getPosition();
			thePath->appendNode(&AIInternalMoveToState::gxRow().goalPosition, LAYER_GROUND);	// waypoints are always on the ground.
			GXROW_AIFOLLOWWAYPOINTPATHSTATE.appendGoalPosition = false; // just did it.
		}
	}
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.moveAsGroup && GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint != obj->getTeam()->getCurrentWaypoint()) {
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint = GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint;
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint = obj->getTeam()->getCurrentWaypoint();
		if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint == nullptr) {
			return STATE_SUCCESS;
		}
		computeGoal(false);
		if (getAdjustsDestination() && ai->isDoingGroundMovement()) {
			if (!TheAI->pathfinder()->adjustDestination(obj, ai->getLocomotorSet(), &AIInternalMoveToState::gxRow().goalPosition)) {
				if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint) {
// TheSuperHackers @info helmutbuhler 05/05/2025 This debug mutates the code to become CRC incompatible
#if defined(RTS_DEBUG) || !RETAIL_COMPATIBLE_CRC
					DEBUG_LOG(("Breaking out of follow waypoint path %s of %s",
					GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getName().str(), GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getPathLabel1().str()));
#endif
				}
				return STATE_FAILURE;
			}
		}
		ai->friend_startingMove();
		computePath();
		if (getAdjustsDestination()) {
			TheAI->pathfinder()->updateGoal(obj, &AIInternalMoveToState::gxRow().goalPosition, AIInternalMoveToState::gxRow().goalLayer);
		}
	}

	// do movement
	StateReturnType status = AIInternalMoveToState::update();

	// We may want to allow ourselves to bail out of this one early. In order to do this, we check and
	// see if we're moving as a group, and then if our team is owned by an AI Skirmish player.
	// If it is, then we compute the group centroid, and see if it is within some distance of the
	if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.moveAsGroup) {
		if (obj->getControllingPlayer()->isSkirmishAIPlayer()) {
			Team *team = obj->getTeam();
			AIGroupPtr group = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
			team->getTeamAsAIGroup(group);
#else
			team->getTeamAsAIGroup(group.Peek());
#endif

			Coord3D pos;
			group->getCenter(&pos);

			pos.x -= AIInternalMoveToState::gxRow().goalPosition.x;
			pos.y -= AIInternalMoveToState::gxRow().goalPosition.y;
			pos.z = 0;

			Int numInGroup = group->getCount();
			if (pos.length() <= (numInGroup * TheAI->getAiData()->m_skirmishGroupFudgeValue)) {
				// Consider ourselves close enough.
				status = STATE_SUCCESS;
			}
		}
	}

	// if move to has finished, move to next point on waypoint path
	if (status != STATE_CONTINUE)
	{
		GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint = getNextWaypoint();

		//LORENZEN ADDED LORENZEN ADDED LORENZEN ADDED
		Object *obj = getMachineOwner();
		AIUpdateInterface *ai = obj->getAI();
		if ( GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint )
			ai->setPriorWaypointID( GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint->getID() );
		if ( GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint )
			ai->setCurrentWaypointID( GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getID() );
		//LORENZEN ADDED LORENZEN ADDED LORENZEN ADDED



		// if there are no links from this waypoint, we're done
		if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint==nullptr)	{
			/// Trigger "end of waypoint path" scripts (jba)
			ai->setCompletedWaypoint(GXROW_AIFOLLOWWAYPOINTPATHSTATE.priorWaypoint);
			return STATE_SUCCESS;
		}
		if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.moveAsGroup) {
			obj->getTeam()->setCurrentWaypoint(GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint);
		}

		computeGoal(false);
		if (getAdjustsDestination() && ai->isDoingGroundMovement()) {
			if (!TheAI->pathfinder()->adjustDestination(obj, ai->getLocomotorSet(), &AIInternalMoveToState::gxRow().goalPosition)) {
				if (GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint) {
// TheSuperHackers @info helmutbuhler 05/05/2025 This debug mutates the code to become CRC incompatible
#if defined(RTS_DEBUG) || !RETAIL_COMPATIBLE_CRC
					DEBUG_LOG(("Breaking out of follow waypoint path %s of %s",
					GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getName().str(), GXROW_AIFOLLOWWAYPOINTPATHSTATE.currentWaypoint->getPathLabel1().str()));
#endif
				}
				return STATE_FAILURE;
			}
		}
		ai->friend_startingMove();
		computePath();
		if (getAdjustsDestination()) {
			TheAI->pathfinder()->updateGoal(obj, &AIInternalMoveToState::gxRow().goalPosition, AIInternalMoveToState::gxRow().goalLayer);
		}

		return STATE_CONTINUE;
	}
	if (status != STATE_CONTINUE) {
		DEBUG_LOG(("Breaking out of follow waypoint path"));
	}
	return status;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathExactState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathExactState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);
	UnsignedInt id = INVALID_WAYPOINT_ID;
	if (GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.lastWaypoint) {
		id = GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.lastWaypoint->getID();
	}
	xfer->xferUnsignedInt(&id);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.lastWaypoint = TheTerrainLogic->getWaypointByID(id);
	}
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIFollowWaypointPathExactState::loadPostProcess()
{
	AIInternalMoveToState::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowWaypointPathExactState::onEnter()
{
	const Waypoint *currentWaypoint = ((AIStateMachine *)getMachine())->getGoalWaypoint();
	AIUpdateInterface *ai = getMachineOwner()->getAI();

	if (currentWaypoint == nullptr) return STATE_FAILURE;

	getMachine()->setGoalPosition(currentWaypoint->getLocation());

	Coord2D groupOffset;
	groupOffset.x = groupOffset.y = 0;

	Object *obj = getMachineOwner();

	Real speed = FAST_AS_POSSIBLE;
	if (GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.moveAsGroup) {
		AIGroup *group = ai->getGroup();
		if (group) {
			speed = group->getSpeed();
			Coord3D center;

			group->getCenter( &center );
			groupOffset.x = obj->getPosition()->x - center.x;
			groupOffset.y = obj->getPosition()->y - center.y;
		}
	}
	ai->setCanPathThroughUnits(true);
	setAdjustsDestination(false);
	// set initial movement goal
	StateReturnType ret = AIInternalMoveToState::onEnter();
	ai->setPathFromWaypoint(currentWaypoint, &groupOffset);
	GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.lastWaypoint = currentWaypoint;
	ai->getCurLocomotor()->setAllowInvalidPosition(true); // allow it to move off the map.

	//Kris: October 4, 2002 -- Commented out by guidance of John A.
	//			Artist couldn't load his map, and turned out that it was because
	//			there was a waypoint path that pointed to itself (2 point path).
	//			John said that this code only needs "a" point, not the "last" point.
	//while (GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.lastWaypoint && GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.lastWaypoint->getLink(0)) {
	//	GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.lastWaypoint = GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.lastWaypoint->getLink(0);
	//}
	ai->setDesiredSpeed(speed);

	return ret;
}

//----------------------------------------------------------------------------------------------------------
void AIFollowWaypointPathExactState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );

	// turn off precision-z-pos when we exit, just in case.
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai) {
		ai->setCompletedWaypoint(GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.lastWaypoint);
		ai->setCanPathThroughUnits(false);
		ai->getCurLocomotor()->setAllowInvalidPosition(false); // turn off allow it to move off the map.
	}
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFollowWaypointPathExactState::update()
{

	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai) ai->setCanPathThroughUnits(true);
	// do movement
	StateReturnType status = AIInternalMoveToState::update();

	return status;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
AIAttackFollowWaypointPathState::AIAttackFollowWaypointPathState( StateMachine *machine, Bool asGroup ) : AIFollowWaypointPathState ( machine, asGroup, false )
{
	m_gxAIAttackFollowWaypointPathStateRow = TheGxAIAttackFollowWaypointPathStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

#ifdef STATE_MACHINE_DEBUG
	setName("AIAttackFollowWaypointPathState");
#endif
	GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine = newInstance(AIAttackMoveStateMachine)(getMachineOwner(), "AIAttackFollowMachine");
	GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->initDefaultState();
}

//-------------------------------------------------------------------------------------------------
AIAttackFollowWaypointPathState::~AIAttackFollowWaypointPathState()
{
	deleteInstance(GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine);
	TheGxAIAttackFollowWaypointPathStates.release( m_gxAIAttackFollowWaypointPathStateRow );	// web port (round 5)
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackFollowWaypointPathState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackFollowWaypointPathState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
  AIFollowWaypointPathState::xfer( xfer );

	xfer->xferSnapshot(GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackFollowWaypointPathState::loadPostProcess()
{
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIAttackFollowWaypointPathState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine) name.concat(GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->getCurrentStateName());
	else name.concat("*null GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine");
	return name;
}
#endif

//-------------------------------------------------------------------------------------------------
StateReturnType AIAttackFollowWaypointPathState ::onEnter()
{
	GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->clear();
	GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->setState( AI_IDLE );

	return AIFollowWaypointPathState::onEnter();
}

//-------------------------------------------------------------------------------------------------
StateReturnType AIAttackFollowWaypointPathState::update()
{

	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAI();

	Bool forceRetargetThisFrame = false;
	Bool shouldRepathThisFrame = false;
	if (!GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->isInIdleState())
	{
		ai->setLocomotorGoalNone();
		owner->clearModelConditionState(MODELCONDITION_MOVING);
		GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->updateStateMachine();

		// if the machine is now idling, then we need to attempt to get a new target
		if (GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->isInIdleState())
		{
			forceRetargetThisFrame = true;
			shouldRepathThisFrame = true;
		}
		else
		{
			return STATE_CONTINUE;
		}
	}

	if (GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->isInIdleState())
	{
		// Check to see if we have created a crate we need to pick up.
		Object* crate = ai->checkForCrateToPickup();
		if (crate)
		{
			GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->setGoalObject(crate);
			GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->setState( AI_PICK_UP_CRATE );
			return STATE_CONTINUE;
		}

		Object* nextObjectToAttack;
		{

			nextObjectToAttack = ai->getNextMoodTarget( !forceRetargetThisFrame, false );
		}
		if (nextObjectToAttack != nullptr)
		{
			GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->setGoalObject(nextObjectToAttack);
			GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->setState( AI_ATTACK_OBJECT );
			shouldRepathThisFrame = false;	// we're about to drop out of this function, but this is semantic emphasis.

			// we don't want an update to take place 'till next frame.
			return STATE_CONTINUE;
		}
	}

	if (shouldRepathThisFrame)
	{
		// Update the goal waypoint if we've moved along the path.
		computeGoal(AIFollowWaypointPathState::gxRow().moveAsGroup);
		computePath();
	}

	return AIFollowWaypointPathState::update();
}

//-------------------------------------------------------------------------------------------------
void AIAttackFollowWaypointPathState ::onExit( StateExitType status )
{
	GXROW_AIATTACKFOLLOWWAYPOINTPATHSTATE.attackFollowMachine->setState(AI_IDLE);
	AIFollowWaypointPathState::onExit(status);
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Wander along a waypoint path.
 */

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIWanderState::crc( Xfer *xfer )
{
	AIFollowWaypointPathState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIWanderState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIFollowWaypointPathState::xfer(xfer);

	xfer->xferInt(&GXROW_AIWANDERSTATE.waitFrames);
	xfer->xferInt(&GXROW_AIWANDERSTATE.timer);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIWanderState::loadPostProcess()
{
	AIFollowWaypointPathState::loadPostProcess();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
StateReturnType AIWanderState::onEnter()
{
	AIFollowWaypointPathState::gxRow().currentWaypoint = ((AIStateMachine *)getMachine())->getGoalWaypoint();

	AIUpdateInterface *ai = getMachineOwner()->getAI();
	AIFollowWaypointPathState::gxRow().priorWaypoint = nullptr;
	if (AIFollowWaypointPathState::gxRow().currentWaypoint == nullptr || ai==nullptr)
		return STATE_FAILURE;
	AIFollowWaypointPathState::gxRow().groupOffset.x = AIFollowWaypointPathState::gxRow().groupOffset.y = 0;
	Locomotor* curLoco = ai->getCurLocomotor();
	if (curLoco && curLoco->getWanderWidthFactor() > 0.0f) {
		Int delta = REAL_TO_INT_FLOOR(curLoco->getWanderWidthFactor()+0.5f);
		if (delta<1) delta = 1;
		AIFollowWaypointPathState::gxRow().groupOffset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		AIFollowWaypointPathState::gxRow().groupOffset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
	}
	GXROW_AIWANDERSTATE.timer = 0;
	GXROW_AIWANDERSTATE.waitFrames = 10 + (getMachineOwner()->getID() & 0x7);
	// set initial movement goal
	computeGoal(false);
	StateReturnType ret = AIInternalMoveToState::onEnter();
	ai->setPathExtraDistance(calcExtraPathDistance());
	return ret;
}


//----------------------------------------------------------------------------------------------------------
StateReturnType AIWanderState::update()
{
	// do movement
	Object *obj = getMachineOwner();
	StateReturnType status = AIInternalMoveToState::update();
	if (obj->isKindOf(KINDOF_CAN_BE_REPULSED)) {
		GXROW_AIWANDERSTATE.timer--;
		if (GXROW_AIWANDERSTATE.timer<0) {
			GXROW_AIWANDERSTATE.timer = GXROW_AIWANDERSTATE.waitFrames;
			Object* enemy = TheAI->findClosestRepulsor(getMachineOwner(), obj->getVisionRange());
			if (enemy) {
				return STATE_FAILURE;
			}
		}
	}
	// if move to has finished, move to next point on waypoint path
	if (status != STATE_CONTINUE)
	{
		AIUpdateInterface *ai = obj->getAI();

		AIFollowWaypointPathState::gxRow().currentWaypoint = getNextWaypoint();
		// if there are no links from this waypoint, we're done
		if (AIFollowWaypointPathState::gxRow().currentWaypoint == nullptr)	{
			/// Trigger "end of waypoint path" scripts (jba)
			ai->setCompletedWaypoint(AIFollowWaypointPathState::gxRow().priorWaypoint);

			return STATE_SUCCESS;
		}

		Locomotor* curLoco = ai->getCurLocomotor();
		if (curLoco && curLoco->getWanderWidthFactor() > 0.0f) {
			Int delta = REAL_TO_INT_FLOOR(curLoco->getWanderWidthFactor()+0.5f);
			if (delta<1) delta = 1;
			AIFollowWaypointPathState::gxRow().groupOffset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
			AIFollowWaypointPathState::gxRow().groupOffset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		}
		computeGoal(false);
		computePath();
		return STATE_CONTINUE;
	}
	// Never leave this state until told to.
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIWanderState::onExit( StateExitType status )
{
	AIFollowWaypointPathState::onExit( status );
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Wander around a point.
 */

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIWanderInPlaceState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIWanderInPlaceState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferCoord3D(&GXROW_AIWANDERINPLACESTATE.origin);
	xfer->xferInt(&GXROW_AIWANDERINPLACESTATE.waitFrames);
	xfer->xferInt(&GXROW_AIWANDERINPLACESTATE.timer);
}

// ------------------------------------------------------------------------------------------------
/** LoadPostProcess */
// ------------------------------------------------------------------------------------------------
void AIWanderInPlaceState::loadPostProcess()
{
	AIInternalMoveToState::loadPostProcess();
}

// ------------------------------------------------------------------------------------------------
StateReturnType AIWanderInPlaceState::onEnter()
{
	GXROW_AIWANDERINPLACESTATE.origin = *getMachineOwner()->getPosition();

	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai) {
		ai->chooseLocomotorSet(LOCOMOTORSET_WANDER);
	}

	Int delta = 3;
	if (ai->getCurLocomotor()) {
		delta = REAL_TO_INT_FLOOR( (ai->getCurLocomotor()->getWanderAboutPointRadius()/PATHFIND_CELL_SIZE_F) + 0.5f);
	}
	Coord3D offset;
	offset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
	offset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
	AIInternalMoveToState::gxRow().goalPosition = GXROW_AIWANDERINPLACESTATE.origin;
	AIInternalMoveToState::gxRow().goalPosition.x += offset.x;
	AIInternalMoveToState::gxRow().goalPosition.y += offset.y;
	GXROW_AIWANDERINPLACESTATE.timer = 0;
	GXROW_AIWANDERINPLACESTATE.waitFrames = 10 + (getMachineOwner()->getID() & 0x7);
	StateReturnType ret = AIInternalMoveToState::onEnter();
	return ret;
}


//----------------------------------------------------------------------------------------------------------
StateReturnType AIWanderInPlaceState::update()
{
	// do movement
	StateReturnType status = AIInternalMoveToState::update();

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (!ai) return STATE_FAILURE;
	if (obj->isKindOf(KINDOF_CAN_BE_REPULSED)) {
		GXROW_AIWANDERINPLACESTATE.timer--;
		if (GXROW_AIWANDERINPLACESTATE.timer<0) {
			GXROW_AIWANDERINPLACESTATE.timer = GXROW_AIWANDERINPLACESTATE.waitFrames;
			Object* enemy = TheAI->findClosestRepulsor(getMachineOwner(), obj->getVisionRange());
			if (enemy) {
				return STATE_FAILURE;
			}
		}
	}
	// if move to has finished, move to next point on waypoint path
	if (status != STATE_CONTINUE)
	{
		Int delta = 3;
		if (ai->getCurLocomotor()) {
			delta = REAL_TO_INT_FLOOR( (ai->getCurLocomotor()->getWanderAboutPointRadius()/PATHFIND_CELL_SIZE_F) + 0.5f);
		}
		Coord3D offset;
		offset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		offset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		AIInternalMoveToState::gxRow().goalPosition = GXROW_AIWANDERINPLACESTATE.origin;
		AIInternalMoveToState::gxRow().goalPosition.x += offset.x;
		AIInternalMoveToState::gxRow().goalPosition.y += offset.y;
		AIInternalMoveToState::onEnter();
		return STATE_CONTINUE;
	}
	// Never leave this state until told to.
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIWanderInPlaceState::onExit( StateExitType status )
{
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIPanicState::crc( Xfer *xfer )
{
	AIFollowWaypointPathState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIPanicState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIFollowWaypointPathState::xfer(xfer);

	xfer->xferInt(&GXROW_AIPANICSTATE.waitFrames);
	xfer->xferInt(&GXROW_AIPANICSTATE.timer);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIPanicState::loadPostProcess()
{
	AIFollowWaypointPathState::loadPostProcess();
}

/**
 * Panic and run screaming along a waypoint path.
 */
StateReturnType AIPanicState::onEnter()
{
	AIFollowWaypointPathState::gxRow().currentWaypoint = ((AIStateMachine *)getMachine())->getGoalWaypoint();

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();
	if (AIFollowWaypointPathState::gxRow().currentWaypoint == nullptr)
		return STATE_FAILURE;
	// set initial movement goal
	Locomotor* curLoco = ai->getCurLocomotor();
	if (curLoco && curLoco->getWanderWidthFactor() > 0.0f) {
		Int delta = REAL_TO_INT_FLOOR(curLoco->getWanderWidthFactor()+0.5f);
		if (delta<1) delta = 1;
		AIFollowWaypointPathState::gxRow().groupOffset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		AIFollowWaypointPathState::gxRow().groupOffset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
	}
	computeGoal(false);
	StateReturnType ret = AIInternalMoveToState::onEnter();

	GXROW_AIPANICSTATE.timer = 0;
	GXROW_AIPANICSTATE.waitFrames = 10 + (getMachineOwner()->getID() & 0x7);
	// Update the extra path distance.   AIInternalMoveToState::onEnter resets it.
	ai->setPathExtraDistance(calcExtraPathDistance());
	if (obj)
	{
		obj->setModelConditionState(MODELCONDITION_PANICKING);
	}

	return ret;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIPanicState::update()
{
	// do movement
	StateReturnType status = AIInternalMoveToState::update();

	Object *obj = getMachineOwner();
	if (obj->isKindOf(KINDOF_CAN_BE_REPULSED)) {
		GXROW_AIPANICSTATE.timer--;
		if (GXROW_AIPANICSTATE.timer<0) {
			GXROW_AIPANICSTATE.timer = GXROW_AIPANICSTATE.waitFrames;
			Object* enemy = TheAI->findClosestRepulsor(getMachineOwner(), obj->getVisionRange());
			if (enemy) {
				return STATE_FAILURE;
			}
		}
	}

	// if move to has finished, move to next point on waypoint path
	if (status == STATE_SUCCESS)
	{
		Object *obj = getMachineOwner();
		AIUpdateInterface *ai = obj->getAI();

		AIFollowWaypointPathState::gxRow().currentWaypoint = getNextWaypoint();
		// if there are no links from this waypoint, we're done
		if (AIFollowWaypointPathState::gxRow().currentWaypoint == nullptr)	{
			/// Trigger "end of waypoint path" scripts (jba)
			ai->setCompletedWaypoint(AIFollowWaypointPathState::gxRow().priorWaypoint);

			return STATE_SUCCESS;
		}
		Locomotor* curLoco = ai->getCurLocomotor();
		if (curLoco && curLoco->getWanderWidthFactor() > 0.0f) {
			Int delta = REAL_TO_INT_FLOOR(curLoco->getWanderWidthFactor()+0.5f);
			if (delta<1) delta = 1;
			AIFollowWaypointPathState::gxRow().groupOffset.x = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
			AIFollowWaypointPathState::gxRow().groupOffset.y = GameLogicRandomValue(-delta, delta)*PATHFIND_CELL_SIZE_F;
		}
		computeGoal(false);
		computePath();
		return STATE_CONTINUE;
	}
	// Never leave this state until told to.
	return STATE_CONTINUE;
}


//----------------------------------------------------------------------------------------------------------
void AIPanicState::onExit( StateExitType status )
{
	Object *obj = getMachineOwner();
	obj->clearModelConditionState(MODELCONDITION_PANICKING);
	AIInternalMoveToState::onExit( status );
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackAimAtTargetState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackAimAtTargetState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferBool(&GXROW_AIATTACKAIMATTARGETSTATE.canTurnInPlace);
	xfer->xferBool(&GXROW_AIATTACKAIMATTARGETSTATE.setLocomotor);

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackAimAtTargetState::loadPostProcess()
{
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackAimAtTargetState::onEnter()
{
	// contained by AIAttackState, so no separate timer
	Object* source = getMachineOwner();
	Weapon* weapon = source->getCurrentWeapon();
	Object* victim = getMachineGoalObject();
	const Coord3D* targetPos = getMachineGoalPosition();
	AIUpdateInterface* sourceAI = source->getAI();
	AIUpdateInterface* victimAI = victim ? victim->getAI() : nullptr;

	Locomotor* curLoco = sourceAI->getCurLocomotor();
	GXROW_AIATTACKAIMATTARGETSTATE.canTurnInPlace = curLoco ? curLoco->getMinSpeed() == 0.0f : false;


//	if (!victim)
//		return STATE_CONTINUE; // Just continue till we get a victim.
// Ick.  This was originally a safety to a single line that required victim, and was never meant
// as an early return to all cases.  We now want to use preattack frames on ground position targets

	Bool inFiringRange = FALSE;

	//If the object is garrisoning a building, then we want to force the object
	//to move to the best fire point, check if it's in firing range, and if not
	//move it back so another unit with longer range can!
	Object *containedBy = source->getContainedBy();
	ContainModuleInterface *contain = containedBy ? containedBy->getContain() : nullptr;

	if( containedBy && weapon && contain && contain->isEnclosingContainerFor( source ) )
	{                                          // non enclosing garrison containers do not use firepoints. Lorenzen, 6/11/03
		if (victim)
		{
			inFiringRange = contain->attemptBestFirePointPosition( source, weapon, victim );
		}
		else
		{
			inFiringRange = contain->attemptBestFirePointPosition( source, weapon, targetPos );
		}
	}
	else if( victim && weapon )
		inFiringRange = weapon->isWithinAttackRange( source, victim );
	else if( weapon )
		inFiringRange = weapon->isWithinAttackRange( source, targetPos );//See, I can be attacking the ground.  Perfectly valid.
	else
		return STATE_FAILURE; // can't happen.

	// add ourself as a targeter BEFORE calling isTemporarilyPreventingAimSuccess().
	if (victimAI)
		victimAI->addTargeter(source->getID(), true);

	if( sourceAI->areTurretsLinked() )
	{
		//Order all turrets to attack.
		for( Int i = 0; i < MAX_TURRETS; i++ )
		{
			if( GXROW_AIATTACKAIMATTARGETSTATE.isAttackingObject )
			{
				sourceAI->setTurretTargetObject( (WhichTurretType)i, victim, GXROW_AIATTACKAIMATTARGETSTATE.isForceAttacking );
			}
			else
			{
				sourceAI->setTurretTargetPosition( (WhichTurretType)i, getMachineGoalPosition() );
			}
		}
	}
	else
	{
		WhichTurretType tur = sourceAI->getWhichTurretForCurWeapon();
		if (tur != TURRET_INVALID)
		{
			//Order specific turret to attack.
			if (GXROW_AIATTACKAIMATTARGETSTATE.isAttackingObject)
			{
				sourceAI->setTurretTargetObject(tur, victim, GXROW_AIATTACKAIMATTARGETSTATE.isForceAttacking);
			}
			else
			{
				sourceAI->setTurretTargetPosition(tur, getMachineGoalPosition());
			}
		}
		else
		{
			// GS moved contact weapon check in here, because Success can never be given to a unit in this state
			// using a turret to attack.  Check out ::update and you will see.

			Bool preventing = victimAI && victimAI->isTemporarilyPreventingAimSuccess();

			// Contact weapons don't aim.  They just go boom.  jba.
			if( weapon && weapon->isContactWeapon() && inFiringRange && !preventing )
				return STATE_SUCCESS;
		}
	}
	GXROW_AIATTACKAIMATTARGETSTATE.setLocomotor = false;

	source->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IS_AIMING_WEAPON ) );
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Orient the machine's owner to face towards the given target
 */

StateReturnType AIAttackAimAtTargetState::update()
{
	// contained by AIAttackState, so no separate timer
	Object* source = getMachineOwner();
	AIUpdateInterface* sourceAI = source->getAI();

	if (!source->hasAnyWeapon())
		return STATE_FAILURE;

	Object* victim = getMachineGoalObject();
	if (GXROW_AIATTACKAIMATTARGETSTATE.isAttackingObject)
	{
		if (!victim || victim->isEffectivelyDead())
			return STATE_FAILURE;	// can't aim at dead things
	}

	WhichTurretType tur = sourceAI->getWhichTurretForCurWeapon();
	if (tur != TURRET_INVALID)
	{
		if (GXROW_AIATTACKAIMATTARGETSTATE.isAttackingObject)
		{
			sourceAI->setTurretTargetObject(tur, victim, GXROW_AIATTACKAIMATTARGETSTATE.isForceAttacking);
		}
		else
		{
			sourceAI->setTurretTargetPosition(tur, getMachineGoalPosition());
		}
		// if we have a turret, but it is incapable of turning, turn ourself.
		// (gotta do this for units like the Comanche, which have fake "turrets"
		// solely to allow for attacking-on-the-move...)
		if (sourceAI->getTurretTurnRate(tur) != 0.0f)
		{
			// The Body can never return Success if the weapon is on the turret, or else we end
			// up shooting the current weapon (which is on the turret) in the wrong direction.
			// We always say Continue, so the Turret can do its own Aiming state.
//			if (GXROW_AIATTACKAIMATTARGETSTATE.isAttackingObject && source->canCrushOrSquish(victim)) {
//				return STATE_SUCCESS;
//			}
			return STATE_CONTINUE;
		}

		// else fall thru!
	}

	// no else here!
	{
		Real relAngle = GXROW_AIATTACKAIMATTARGETSTATE.isAttackingObject ?
											ThePartitionManager->getRelativeAngle2D( source, victim ) :
											ThePartitionManager->getRelativeAngle2D( source, getMachineGoalPosition() );

		const Real REL_THRESH = 0.035f;	// about 2 degrees. (getRelativeAngle2D is current only accurate to about 1.25 degrees)

		Weapon* weapon = source->getCurrentWeapon();
		Real aimDelta = weapon ? weapon->getAimDelta() : 0.0f;

		if (aimDelta < REL_THRESH)
		{
			aimDelta = REL_THRESH;
		}

		//DEBUG_LOG(("AIM: desired %f, actual %f, delta %f, aimDelta %f, goalpos %f %f",rad2deg(obj->getOrientation() + relAngle),rad2deg(obj->getOrientation()),rad2deg(relAngle),rad2deg(aimDelta),victim->getPosition()->x,victim->getPosition()->y));
		if (GXROW_AIATTACKAIMATTARGETSTATE.canTurnInPlace)
		{
			if (fabs(relAngle) > aimDelta)
			{
				Real desiredAngle = source->getOrientation() + relAngle;
				sourceAI->setLocomotorGoalOrientation(desiredAngle);
				GXROW_AIATTACKAIMATTARGETSTATE.setLocomotor = true;
			}
		}
		else
		{
			sourceAI->setLocomotorGoalPositionExplicit(GXROW_AIATTACKAIMATTARGETSTATE.isAttackingObject ? *victim->getPosition() : *getMachineGoalPosition());
		}

		if (fabs(relAngle) < aimDelta /*&& !m_preAttackFrames*/ )
		{
			AIUpdateInterface* victimAI = victim ? victim->getAI() : nullptr;
			// add ourself as a targeter BEFORE calling isTemporarilyPreventingAimSuccess().
			// we do this every time thru, just in case we get into a squabble with our turret-ai
			// over whether or not we are a targeter... (srj)
			if (victimAI)
				victimAI->addTargeter(source->getID(), true);
			Bool preventing = victimAI && victimAI->isTemporarilyPreventingAimSuccess();
			if (preventing)
			{
				return STATE_CONTINUE;
			}
			else
			{
				return STATE_SUCCESS;
			}
		}
	}

	if (source->isDisabledByType(DISABLED_HELD))
	{
		// We are contained by something (transport, building). This means we can't
		// actually move to the location and if we are firing on a specific target
		// and that target is no longer in range, then we need to abort the state
		// so it can fire on other targets.

		// Are we still in range?
		Weapon *weapon = source->getCurrentWeapon();

		// NO BAD WRONG!!! How can this be the one spot to convert an Object to a center pos?  We have
		// an attack object check for a reason!  The center and the edge can be very far apart.
//		const Coord3D *pos = GXROW_AIATTACKAIMATTARGETSTATE.isAttackingObject ? victim->getPosition() : getMachineGoalPosition();

		Bool inRange = FALSE;
		if( GXROW_AIATTACKAIMATTARGETSTATE.isAttackingObject )
			inRange = weapon ? weapon->isWithinAttackRange(source, victim) : FALSE;
		else
			inRange = weapon ? weapon->isWithinAttackRange(source, getMachineGoalPosition()) : FALSE;

		if( !weapon || !inRange )
		{
			// We're no longer in range, so exit with failure so we can automatically
			// reacquire a closer target if possible.
			return STATE_FAILURE;
		}
	}

	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIAttackAimAtTargetState::onExit( StateExitType status )
{
	// contained by AIAttackState, so no separate timer
	if (GXROW_AIATTACKAIMATTARGETSTATE.canTurnInPlace)
	{
		AIUpdateInterface* sourceAI = getMachineOwner()->getAI();
		// Tell the ai we are done moving, if we set the locomotor goal.
		if (sourceAI && GXROW_AIATTACKAIMATTARGETSTATE.setLocomotor)
			sourceAI->setLocomotorGoalNone();
	}
	else
	{
		// don't do the loco call, or else we will "wiggle"... we already have an appropriate goal
	}

	getMachineOwner()->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IS_AIMING_WEAPON ) );

	//getMachineOwner()->clearModelConditionState( MODELCONDITION_PREATTACK );
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Start firing.
 */
StateReturnType AIAttackFireWeaponState::onEnter()
{
	// contained by AIAttackState, so no separate timer
	DEBUG_ASSERTCRASH(m_att != nullptr, ("m_att may not be null"));

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	// Passive stuff will approach but not attack, so we check here (after approach is complete)
	UnsignedInt adjust = ai->getMoodMatrixActionAdjustment(MM_Action_Attack);
	if ((adjust & MAA_Action_Ok) == 0)
	{
		return STATE_FAILURE;
	}
	Object *victim = getMachineGoalObject();

	if (victim && obj->getTeam()->getPrototype()->getTemplateInfo()->m_attackCommonTarget) {
		if (obj->getTeam()->getTeamTargetObject()==nullptr) {
			obj->getTeam()->setTeamTargetObject(victim);
		}
	}

	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IS_FIRING_WEAPON ) );
	obj->preFireCurrentWeapon( getMachineGoalObject() );
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Fire the owner's weapon once and exit.
 */

StateReturnType AIAttackFireWeaponState::update()
{
	// contained by AIAttackState, so no separate timer

	Object *obj = getMachineOwner();
	Object* victim = getMachineGoalObject();

	if (m_att->isAttackingObject())
	{
		// if our target is dead, go ahead and stop.
		if (!victim || victim->isEffectivelyDead())
			return STATE_FAILURE;
	}
	WeaponSlotType wslot;
	Weapon* weapon = obj->getCurrentWeapon(&wslot);
	if (!weapon)
	{
		return STATE_FAILURE;
	}

	WeaponStatus status = weapon->getStatus();
	if (status == PRE_ATTACK)
	{
		return STATE_CONTINUE;
	}
	else if (status != READY_TO_FIRE)
	{
		return STATE_FAILURE;
	}

	/**
		this is the weird case where we have multi turrets, and turret 'a' wants
		to fire, but someone has changed the current weapon to be one not on him.
		rather than addressing the situation, we just punt and wait for it to clear
		up on its own.
	*/
	if (m_att && !m_att->isWeaponSlotOkToFire(wslot))
	{
		return STATE_FAILURE;
	}

	// must adjust the state BEFORE calling fireWeapon, for FX to work correctly...
	obj->setFiringConditionForCurrentWeapon();

	if (m_att->isAttackingObject())
	{
    // Since it is very late in the project, and there is no call for such code...
    // there is currently no support here for linked turrets, as regards Attacking Objects (victims)
    // If the concept of linked turrets is further developed then God help you, and put more code right here
    // that lookl like the //LINKED TURRETS// block, below


		obj->fireCurrentWeapon(victim);

		//Kris: October 21, 2003 - Patch 1.01
		//Fixes cases where some units couldn't transfer their attack to a different object. One example was Colonel Burton attacking
		//any GLA structure. When the structure was destroyed becoming a hole, Burton would stop attacking. Even though there is code
		//to transfer attackers (AIUpdateInterface::transferAttack), it is unable to modify our current victim in our attack state
		//machine. When we move immediately to the aim state in the same frame as the transfer (after this call in fact), the victim
		//was still pointing to the building and not the hole we transferred to. This code fixes that.
		if( victim != obj->getAI()->getCurrentVictim() )
		{
			getMachine()->setGoalObject( obj->getAI()->getCurrentVictim() );
		}

		// clear this, just in case.
		obj->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IGNORING_STEALTH ) );
		Real continueRange = weapon->getContinueAttackRange();
		if (
			continueRange > 0.0f &&
			victim &&
			(victim->isDestroyed() || victim->isEffectivelyDead() || (victim->isKindOf(KINDOF_MINE) && victim->testStatus(OBJECT_STATUS_MASKED)))
		)
		{
			const Coord3D* originalVictimPos = m_att ? m_att->getOriginalVictimPos() : nullptr;
			if (originalVictimPos)
			{
				// note that it is important to use getLastCommandSource here; this allows
				// dozers that were ordered to clear mines by the human to continue to autoacquire,
				// but not if they were ordered by ai.
				AIUpdateInterface* ai = obj->getAI();
				CommandSourceType lastCmdSource = ai ? ai->getLastCommandSource() : CMD_FROM_AI;
				PartitionFilterSamePlayer filterPlayer( victim->getControllingPlayer() );
				PartitionFilterSameMapStatus filterMapStatus(obj);
				PartitionFilterPossibleToAttack filterAttack(ATTACK_NEW_TARGET, obj, lastCmdSource);
				PartitionFilter *filters[] = { &filterAttack, &filterPlayer, &filterMapStatus, nullptr };
				// note that we look around originalVictimPos, *not* the current victim's pos.
				victim = ThePartitionManager->getClosestObject( originalVictimPos, continueRange, FROM_CENTER_2D, filters );// could be null. this is ok.
				if (victim)
				{
					getMachine()->setGoalObject(victim);
					m_att->notifyNewVictimChosen(victim);
				}
			}
		}
	}
	else
	{

    if( getMachineOwner()->getAI()->areTurretsLinked() ) //LINKED TURRETS
    {// it doesn;t matter which weapon slot is locked, current or whatever
      for ( Int slot = PRIMARY_WEAPON; slot < WEAPONSLOT_COUNT ; slot++ )
      {// were firing with all barrels
        Weapon *weapon = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
        if ( weapon )
        {
          if ( weapon->fireWeapon(obj, getMachineGoalPosition()) ) //fire() returns 'reloaded'
            obj->releaseWeaponLock(LOCKED_TEMPORARILY);// unlock, 'cause we're loaded

	  	    obj->notifyFiringTrackerShotFired(weapon, INVALID_ID);
        }
      }
    }
    else
		obj->fireCurrentWeapon(getMachineGoalPosition());
		// clear this, just in case.
		obj->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IGNORING_STEALTH ) );
	}

	m_att->notifyFired();

	return STATE_SUCCESS;
}

//----------------------------------------------------------------------------------------------------------
/**
	Stop firing.
	*/
void AIAttackFireWeaponState::onExit( StateExitType status )
{
	// contained by AIAttackState, so no separate timer
	Object *obj = getMachineOwner();
	obj->clearStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_IS_FIRING_WEAPON, OBJECT_STATUS_IGNORING_STEALTH ) );

	// this can occur if we start a preattack (eg, bayonet)
	// and the target moves out range before we can actually "fire"...
	// leaving us thinking we're still "pre attacking". cancel this state
	// to avoid confusion. (srj)
	Weapon* weapon = obj->getCurrentWeapon();
	if (weapon && weapon->getStatus() == PRE_ATTACK)
	{
		weapon->setPreAttackFinishedFrame(0);
	}
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
/**
 * Do nothing for a period of time.
 */

StateReturnType AIWaitState::update()
{
			/// @todo srj -- find a way to sleep for a number of frames here, if possible
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
AIAttackState::AIAttackState( StateMachine *machine, Bool follow, Bool attackingObject, Bool forceAttacking, AttackExitConditionsInterface* attackParameters) : State( machine , "AIAttackState")
{
	m_gxAIAttackStateRow = TheGxAIAttackStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIATTACKSTATE.attackMachine = nullptr;
	GXROW_AIATTACKSTATE.attackParameters = attackParameters;
	GXROW_AIATTACKSTATE.lockedWeaponOnEnter = nullptr;
	GXROW_AIATTACKSTATE.follow = follow;
	GXROW_AIATTACKSTATE.isAttackingObject = attackingObject;
	GXROW_AIATTACKSTATE.isForceAttacking = forceAttacking;
	GXROW_AIATTACKSTATE.victimTeam = nullptr;

	GXROW_AIATTACKSTATE.originalVictimPos.zero();
#ifdef STATE_MACHINE_DEBUG
	if (machine->getWantsDebugOutput()) {
		DEBUG_LOG(("Creating attack state follow %d, attacking object %d, force attacking %d",
			follow, attackingObject, forceAttacking));
	}
#endif
}

//----------------------------------------------------------------------------------------------------------
AIAttackState::~AIAttackState()
{
	// nope, don't do this, since we may well still have it targeted
	// even though we're leaving this state.
	// turn it off when we do setCurrentVictim(nullptr).
	//addSelfAsTargeter(false);

	if (GXROW_AIATTACKSTATE.attackMachine)
	{
		GXROW_AIATTACKSTATE.attackMachine->halt();
		deleteInstance(GXROW_AIATTACKSTATE.attackMachine);
	}
	TheGxAIAttackStates.release( m_gxAIAttackStateRow );	// web port (round 5)
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = GXROW_AIATTACKSTATE.attackMachine!=nullptr;

	xfer->xferBool(&hasMachine);
	xfer->xferCoord3D(&GXROW_AIATTACKSTATE.originalVictimPos);

	if (hasMachine && GXROW_AIATTACKSTATE.attackMachine==nullptr)	{
		// create new state machine for attack behavior
		GXROW_AIATTACKSTATE.attackMachine = newInstance(AttackStateMachine)(getMachineOwner(), this, "AIAttackMachine", GXROW_AIATTACKSTATE.follow, GXROW_AIATTACKSTATE.isAttackingObject, GXROW_AIATTACKSTATE.isForceAttacking  );
	}
	if (hasMachine) {
		xfer->xferSnapshot(GXROW_AIATTACKSTATE.attackMachine);						///< state sub-machine for attack behavior
	}
	/* Not saved or loaded - passed in on creation.
	Bool										GXROW_AIATTACKSTATE.follow;
	Bool										GXROW_AIATTACKSTATE.isAttackingObject;								// if false, attacking position
	AttackExitConditionsInterface*	GXROW_AIATTACKSTATE.attackParameters;					///< these are not owned by this, and will not be deleted on destruction
	Bool										GXROW_AIATTACKSTATE.isForceAttacking
	*/
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackState::loadPostProcess()
{
	Object* victim = getMachineGoalObject();
	if (victim)
	{
		GXROW_AIATTACKSTATE.victimTeam = victim->getTeam();
	}
	Object* source = getMachineOwner();
	GXROW_AIATTACKSTATE.lockedWeaponOnEnter = source->isCurWeaponLocked() ? source->getCurrentWeapon() : nullptr;
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIAttackState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (GXROW_AIATTACKSTATE.attackMachine) name.concat(GXROW_AIATTACKSTATE.attackMachine->getCurrentStateName());
	else name.concat("*null GXROW_AIATTACKSTATE.attackMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------------------
Bool AIAttackState::chooseWeapon()
{
	Object* victim = getMachineGoalObject();
	if (GXROW_AIATTACKSTATE.isAttackingObject && !victim)
		return FALSE;

	Object* source = getMachineOwner();
	AIUpdateInterface *ai = source->getAI();

	Bool found = FALSE;
//	if (victim) // Pardon?  We still need to pick a weapon if we are attacking the ground.
//	{
		found = source->chooseBestWeaponForTarget(victim, PREFER_MOST_DAMAGE, ai->getLastCommandSource());
		//DEBUG_ASSERTLOG(found, ("unable to autochoose any weapon for %s",source->getTemplate()->getName().str()));
//	}

	// Check if we need to update because of the weapon choice switch.
	source->adjustModelConditionForWeaponStatus();

	return found;
}

//----------------------------------------------------------------------------------------------------------
void AIAttackState::notifyNewVictimChosen(Object* victim)
{
	// do NOT update GXROW_AIATTACKSTATE.originalVictimPos here. It's a new victim, not the original!
	getMachine()->setGoalObject(victim);
	if (GXROW_AIATTACKSTATE.attackMachine)
		GXROW_AIATTACKSTATE.attackMachine->setGoalObject(victim);
}

//----------------------------------------------------------------------------------------------------------
/**
 * Begin an attack on the machine's goal object.
 * To do this complex behavior, instantiate another state machine as a "sub-machine" of
 * the attack state.
 */
DECLARE_PERF_TIMER(AIAttackState)
StateReturnType AIAttackState::onEnter()
{
	USE_PERF_TIMER(AIAttackState)
	//CRCDEBUG_LOG(("AIAttackState::onEnter() - start for object %d", getMachineOwner()->getID()));
	Object* source = getMachineOwner();
	AIUpdateInterface *ai = source->getAI();
	// if we are in sleep mode, we will not attack
	if ((ai->getMoodMatrixActionAdjustment(MM_Action_Attack) & MAA_Action_Ok) == 0)
		return STATE_SUCCESS;

	// if we've met the conditions specified by GXROW_AIATTACKSTATE.attackParameters, we consider ourselves "successful."
	if (GXROW_AIATTACKSTATE.attackParameters && GXROW_AIATTACKSTATE.attackParameters->shouldExit(getMachine()))
		return STATE_SUCCESS;

	//Kris: Jan 12, 2005
	//Don't allow units under construction to attack! The selection/action manager system was responsible for preventing this
	//from ever happening, but failed in two cases which I fixed. This is an extra check to mitigate cheats.
	if( source->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
	{
		return STATE_FAILURE;
	}

	// if all of our weapons are out of ammo, can't attack.
	// (this can happen for units which never auto-reload, like the Raptor)
	if (source->isOutOfAmmo() && !source->isKindOf(KINDOF_PROJECTILE))
		return STATE_FAILURE;

	// create new state machine for attack behavior
	//CRCDEBUG_LOG(("AIAttackState::onEnter() - constructing state machine for object %d", getMachineOwner()->getID()));
	GXROW_AIATTACKSTATE.attackMachine = newInstance(AttackStateMachine)(source, this, "AIAttackMachine", GXROW_AIATTACKSTATE.follow, GXROW_AIATTACKSTATE.isAttackingObject, GXROW_AIATTACKSTATE.isForceAttacking  );

#ifdef STATE_MACHINE_DEBUG
	GXROW_AIATTACKSTATE.attackMachine->setDebugOutput(getMachine()->getWantsDebugOutput());
#endif
	// tell the attack machine who the victim of the attack is
	if (GXROW_AIATTACKSTATE.isAttackingObject)
	{
		Object* victim = getMachineGoalObject();
		if (victim == nullptr || victim->isEffectivelyDead())
		{
			ai->notifyVictimIsDead();
			return STATE_FAILURE;	// we have nothing to attack!
		}
		GXROW_AIATTACKSTATE.victimTeam = victim->getTeam();
		GXROW_AIATTACKSTATE.attackMachine->setGoalObject( victim );
		GXROW_AIATTACKSTATE.originalVictimPos = *victim->getPosition();
	}
	else
	{
		GXROW_AIATTACKSTATE.attackMachine->setGoalPosition(getMachineGoalPosition());
		GXROW_AIATTACKSTATE.originalVictimPos = *getMachineGoalPosition();
	}

	// Something can happen to make none of our weapons work.  Return failure, or we will start shooting
	// our Primary (default pick) regardless of legality.
	Bool weaponPicked = chooseWeapon();
	if( !weaponPicked )
		return STATE_FAILURE;

	Weapon* curWeapon = source->getCurrentWeapon();
	if (curWeapon)
	{
		curWeapon->setMaxShotCount(NO_MAX_SHOTS_LIMIT);
		// icky special case for ignoring stealth units we might be targeting, that are currently stealthed. (srj)
		if (curWeapon->getContinueAttackRange() > 0.0f)
			source->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IGNORING_STEALTH ) );
	}

	GXROW_AIATTACKSTATE.lockedWeaponOnEnter = source->isCurWeaponLocked() ? curWeapon : nullptr;

	StateReturnType retType = GXROW_AIATTACKSTATE.attackMachine->initDefaultState();
	if( retType == STATE_CONTINUE )
	{
		source->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_IS_ATTACKING ) );
		source->setModelConditionState( MODELCONDITION_ATTACKING );
	}
	return retType;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Execute one frame of "attack enemy" behavior.
 */

StateReturnType AIAttackState::update()
{
	USE_PERF_TIMER(AIAttackState)
	// if we've met the conditions specified by GXROW_AIATTACKSTATE.attackParameters, we consider ourselves "successful."
	if (GXROW_AIATTACKSTATE.attackParameters && GXROW_AIATTACKSTATE.attackParameters->shouldExit(getMachine()))
	{
		return STATE_SUCCESS;
	}

	Object* source = getMachineOwner();

	// if all of our weapons are out of ammo, can't attack.
	// (this can happen for units which never auto-reload, like the Raptor)
	if (source->isOutOfAmmo() && !source->isKindOf(KINDOF_PROJECTILE))
	{
		return STATE_FAILURE;
	}

	if (GXROW_AIATTACKSTATE.isAttackingObject)
	{
		Object* victim = getMachineGoalObject();

		if (victim == nullptr || victim->isEffectivelyDead())
		{
			source->getAI()->notifyVictimIsDead();
			return STATE_SUCCESS;	// my, that was easy
		}

		if (victim)
		{
			source->getAI()->setCurrentVictim(victim);
		}

		if( victim->getTeam() != GXROW_AIATTACKSTATE.victimTeam )
		{
			// If, while I have been attacking my victim, it has lost its ability to attack
			//(a recently de-garrisoned building) I should bail here...
			// We are not sure whether the problem occurs here or sometime before, but this is an edge case failsafe for it
			// Steven calls this hack 'greasy,' and I agreesy.-Lorenzen
			AIUpdateInterface *ai = source->getAI();
			if (ai)
			{
				if( !victim->getStatusBits().test( OBJECT_STATUS_CAN_ATTACK ) )
				{
					if ( victim->getContain() != nullptr )
					{
						if (victim->getContain()->isGarrisonable() && (victim->getContain()->getContainCount() == 0) )
						{
							if ( source->getRelationship( victim ) == NEUTRAL )
							{
								ai->friend_setGoalObject(nullptr);
								if (victim==source->getTeam()->getTeamTargetObject()) {
									source->getTeam()->setTeamTargetObject(nullptr);
								}
								ai->notifyVictimIsDead();	// well, not "dead", but longer attackable
								return STATE_FAILURE;
							}
						}
					}
				}
			}
			//The team of the victim has changed since we began attacking it. Evaluate
			//whether or not we should keep attacking it.
			// order matters: we want to know if I consider it to be an enemy, not vice versa
			if( source->getRelationship( victim ) != ENEMIES)
			{
				ai->friend_setGoalObject(nullptr);
				if (victim==source->getTeam()->getTeamTargetObject()) {
					source->getTeam()->setTeamTargetObject(nullptr);
				}
				ai->notifyVictimIsDead();	// well, not "dead", but longer attackable
				return STATE_FAILURE;
			}
		}

		if (victim != GXROW_AIATTACKSTATE.attackMachine->getGoalObject())
		{
			// Our parent machine has changed the goal.  We need to reset to the new goal.  jba.
			GXROW_AIATTACKSTATE.attackMachine->setGoalObject( victim );
		}
	}

	// re-evaluate our weapon choice every frame, so the sub-states don't have to.

	// Something can happen to make none of our weapons work.  Return failure, or we will start shooting
	// our Primary (default pick) regardless of legality.
	Bool weaponPicked = chooseWeapon();
	if( !weaponPicked )
		return STATE_FAILURE;

	Weapon* curWeapon = source->getCurrentWeapon();

	// if we entered with a locked weapon (ie, a special weapon), then we will
	// only keep attacking as long as that weapon remains the cur weapon...
	// if anything ever changes that weapon, we exit attack mode immediately.
	if (GXROW_AIATTACKSTATE.lockedWeaponOnEnter != nullptr && GXROW_AIATTACKSTATE.lockedWeaponOnEnter != curWeapon)
		return STATE_FAILURE;

	// we've shot as many times as we are allowed to
	if (curWeapon == nullptr || curWeapon->getMaxShotCount() <= 0)
		return STATE_FAILURE;

	/**
	 * Run the attack state sub-machine.
	 * If the attack state machine returns anything other than CONTINUE,
	 * it has finished. Propagating the return code will cause
	 * the containing state machine to do the right thing.
	 * Note the use of CONVERT_SLEEP_TO_CONTINUE; even if the sub-machine
	 * sleeps, we still need to be called every frame.
	 */
	return CONVERT_SLEEP_TO_CONTINUE(GXROW_AIATTACKSTATE.attackMachine->updateStateMachine());
}

//----------------------------------------------------------------------------------------------------------
void AIAttackState::onExit( StateExitType status )
{
	USE_PERF_TIMER(AIAttackState)
	// nope, don't do this, since we may well still have it targeted
	// even though we're leaving this state. turn it off when we
	// turn it off when we do setCurrentVictim(nullptr).
	//addSelfAsTargeter(false);

	// destroy the attack machine
	deleteInstance(GXROW_AIATTACKSTATE.attackMachine);
	GXROW_AIATTACKSTATE.attackMachine = nullptr;

	Object *obj = getMachineOwner();
	obj->clearStatus( MAKE_OBJECT_STATUS_MASK4( OBJECT_STATUS_IS_FIRING_WEAPON,
																							OBJECT_STATUS_IS_AIMING_WEAPON,
																							OBJECT_STATUS_IS_ATTACKING,
																							OBJECT_STATUS_IGNORING_STEALTH ) );
	obj->clearModelConditionState( MODELCONDITION_ATTACKING );

	obj->clearLeechRangeModeForAllWeapons();

	AIUpdateInterface *ai = obj->getAI();
	if (ai)
	{
		//ai->notifyVictimIsDead();	no, do NOT do this here.
		ai->setCurrentVictim(nullptr);
		for (int i = 0; i < MAX_TURRETS; ++i)
			ai->setTurretTargetObject((WhichTurretType)i, nullptr, FALSE);
		ai->friend_setGoalObject(nullptr);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------------------------------
class AIAttackThenIdleStateMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIAttackThenIdleStateMachine, "AIAttackThenIdleStateMachine" );

public:

	AIAttackThenIdleStateMachine( Object *owner, AsciiString name );
protected:
	// snapshot interface .
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackThenIdleStateMachine::crc( Xfer *xfer )
{
	StateMachine::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackThenIdleStateMachine::xfer( Xfer *xfer )
{
	XferVersion cv = 1;
	XferVersion v = cv;
	xfer->xferVersion( &v, cv );

	StateMachine::xfer(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackThenIdleStateMachine::loadPostProcess()
{
	StateMachine::loadPostProcess();
}

//-----------------------------------------------------------------------------------------------------------
AIAttackThenIdleStateMachine::AIAttackThenIdleStateMachine(Object *owner, AsciiString name) : StateMachine(owner, name)
{
	// order matters: first state is the default state.
	defineState( AI_ATTACK_OBJECT,	newInstance(AIAttackState)(this, false, true, false, nullptr ), AI_IDLE, AI_IDLE );
	defineState( AI_PICK_UP_CRATE, newInstance(AIPickUpCrateState)( this ), AI_IDLE, AI_IDLE );
	defineState( AI_IDLE, newInstance(AIIdleState)( this, AIIdleState::DO_NOT_LOOK_FOR_TARGETS ), AI_IDLE, AI_IDLE );
}

//----------------------------------------------------------------------------------------------------------
AIAttackThenIdleStateMachine::~AIAttackThenIdleStateMachine()
{
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
AIAttackSquadState::~AIAttackSquadState()
{
	if (GXROW_AIATTACKSQUADSTATE.attackSquadMachine)	{
		GXROW_AIATTACKSQUADSTATE.attackSquadMachine->halt();
		deleteInstance(GXROW_AIATTACKSQUADSTATE.attackSquadMachine);
	}
	TheGxAIAttackSquadStates.release( m_gxAIAttackSquadStateRow );	// web port (round 5)
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackSquadState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackSquadState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = GXROW_AIATTACKSQUADSTATE.attackSquadMachine!=nullptr;

	xfer->xferBool(&hasMachine);

	if (hasMachine && GXROW_AIATTACKSQUADSTATE.attackSquadMachine==nullptr)	{
		// create new state machine for attack behavior
		GXROW_AIATTACKSQUADSTATE.attackSquadMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackMachine"  );
	}

	if (hasMachine) {
		xfer->xferSnapshot(GXROW_AIATTACKSQUADSTATE.attackSquadMachine);
	}
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackSquadState::loadPostProcess()
{
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIAttackSquadState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (GXROW_AIATTACKSQUADSTATE.attackSquadMachine) name.concat(GXROW_AIATTACKSQUADSTATE.attackSquadMachine->getCurrentStateName());
	else name.concat("*null GXROW_AIATTACKSQUADSTATE.attackSquadMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------------------
/**
 * Begin an attack on the machine's goal team.
 * To do this, we use a sub attack machine.
 */
StateReturnType AIAttackSquadState::onEnter()
{
	// create new state machine for attack behavior
	GXROW_AIATTACKSQUADSTATE.attackSquadMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackMachine"  );

	Object *victim = chooseVictim();
	// tell the attack machine who the victim of the attack is
	GXROW_AIATTACKSQUADSTATE.attackSquadMachine->setGoalObject( victim );

	// initial state of attack state machine
	return GXROW_AIATTACKSQUADSTATE.attackSquadMachine->initDefaultState();
}

//----------------------------------------------------------------------------------------------------------
/**
 * Execute one frame of "attack enemy" behavior.
 */

StateReturnType AIAttackSquadState::update()
{

	if( !GXROW_AIATTACKSQUADSTATE.attackSquadMachine )
	{
		return STATE_FAILURE;
	}

	/*
		Note the use of CONVERT_SLEEP_TO_CONTINUE; even if the sub-machine
		sleeps, we still need to be called every frame.
	*/
	StateReturnType attackStatus = CONVERT_SLEEP_TO_CONTINUE(GXROW_AIATTACKSQUADSTATE.attackSquadMachine->updateStateMachine());

	// if we're in attack state,
	if (GXROW_AIATTACKSQUADSTATE.attackSquadMachine->getCurrentStateID() != AI_IDLE)
	{
		return attackStatus;
	}

	// Check to see if we have created a crate we need to pick up.
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	Object* crate = ai->checkForCrateToPickup();
	if (crate)
	{
		GXROW_AIATTACKSQUADSTATE.attackSquadMachine->setGoalObject(crate);
		GXROW_AIATTACKSQUADSTATE.attackSquadMachine->setState( AI_PICK_UP_CRATE );
		return STATE_CONTINUE;
	}

	// choose a new target and start over.
	Object *victim = chooseVictim();
	if (!victim)
	{
		return STATE_SUCCESS;
	}

	GXROW_AIATTACKSQUADSTATE.attackSquadMachine->setGoalObject( victim );
	GXROW_AIATTACKSQUADSTATE.attackSquadMachine->setState(AI_ATTACK_OBJECT);
	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIAttackSquadState::onExit( StateExitType status )
{
	// destroy the attack machine
	deleteInstance(GXROW_AIATTACKSQUADSTATE.attackSquadMachine);
	GXROW_AIATTACKSQUADSTATE.attackSquadMachine = nullptr;
}

//----------------------------------------------------------------------------------------------------------
Object *AIAttackSquadState::chooseVictim()
{
	Squad *victimSquad = ((AIStateMachine*)getMachine())->getGoalSquad();
	if (!victimSquad)
	{
		return nullptr;
	}

	Object *owner = getMachineOwner();
	AIUpdateInterface *ai = owner->getAI();
	UnsignedInt moodVal = ai->getMoodMatrixValue();

	if (moodVal & MM_Controller_AI)
	{
		if (moodVal & MM_Mood_Sleep)
		{
			return nullptr;
		}

		if (moodVal & MM_Mood_Passive)
		{
			BodyModuleInterface *bmi = owner->getBodyModule();
			if (!bmi) {
				return nullptr;
			}

			const DamageInfo *di = bmi->getLastDamageInfo();
			if (!di)
			{
				return nullptr;
			}

			return TheGameLogic->findObjectByID(di->in.m_sourceID);
		}
	}

	GameDifficulty difficulty = owner->getControllingPlayer()->getPlayerDifficulty();
	const CommandSourceType cmdSource = ai->getLastCommandSource();

	if (cmdSource == CMD_FROM_PLAYER)
	{
		// if a player did this, we want to always give them the Seek and obliterate method.
		difficulty = DIFFICULTY_HARD;
	}

	if (TheScriptEngine->getChooseVictimAlwaysUsesNormal())
	{
		difficulty = DIFFICULTY_NORMAL;
	}

	switch(difficulty)
	{
		case DIFFICULTY_EASY:
		{
			// pick a random unit
			VecObjectPtr objects = victimSquad->getLiveObjects();
			Int numUnits = objects.size();
			if (numUnits == 0)
			{
				return nullptr;
			}

			Int unitToAttack = GameLogicRandomValue(0, numUnits - 1);
			return objects[unitToAttack];
			break;
		}

		case DIFFICULTY_NORMAL:
		{
			// pick the closest unit

			PartitionFilterAcceptOnSquad f1(victimSquad);
			PartitionFilterSameMapStatus filterMapStatus(getMachineOwner());
			PartitionFilter *filters[] = { &f1, &filterMapStatus, nullptr };

			Object *victim = ThePartitionManager->getClosestObject(getMachineOwner(), HUGE_DIST, FROM_CENTER_2D, filters, nullptr, nullptr);
			return victim;
			break;
		}

		case DIFFICULTY_HARD:
		{
			// everyone picks the same unit
			VecObjectPtr objects = victimSquad->getLiveObjects();
			if (!objects.empty())
			{
				return objects[0];
			}

			return nullptr;
			break;
		}
	};

	return nullptr;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
AIDockState::~AIDockState()
{
	if (GXROW_AIDOCKSTATE.dockMachine) {
		GXROW_AIDOCKSTATE.dockMachine->halt();
		deleteInstance(GXROW_AIDOCKSTATE.dockMachine);
	}
	TheGxAIDockStates.release( m_gxAIDockStateRow );	// web port (round 5)
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIDockState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIDockState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = GXROW_AIDOCKSTATE.dockMachine!=nullptr;

	xfer->xferBool(&hasMachine);

	if (hasMachine && GXROW_AIDOCKSTATE.dockMachine==nullptr)	{
		// create new state machine for attack behavior
		GXROW_AIDOCKSTATE.dockMachine = newInstance(AIDockMachine)( getMachineOwner());
	}
	if (hasMachine) {
		xfer->xferSnapshot(GXROW_AIDOCKSTATE.dockMachine);
	}
	xfer->xferBool(&GXROW_AIDOCKSTATE.usingPrecisionMovement);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIDockState::loadPostProcess()
{
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIDockState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (GXROW_AIDOCKSTATE.dockMachine) name.concat(GXROW_AIDOCKSTATE.dockMachine->getCurrentStateName());
	else name.concat("*null GXROW_AIDOCKSTATE.dockMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------
/**
 * Dock with the GoalObject.
 * When we enter the AI_DOCK state, create a docking state machine
 * that implements the details of the docking behavior.
 */
StateReturnType AIDockState::onEnter()
{
	// who are we docking with?
	Object *dockWithMe = getMachineGoalObject();
	if (dockWithMe == nullptr)
	{
		// we have nothing to dock with!
		DEBUG_LOG(("No goal in AIDockState::onEnter - exiting."));
		return STATE_FAILURE;
	}
  DockUpdateInterface *dock = nullptr;
	dock = dockWithMe->getDockUpdateInterface();

	// if we have nothing to dock with, fail
	if (dock == nullptr)	{
		DEBUG_LOG(("Goal is not a dock in AIDockState::onEnter - exiting."));
		return STATE_FAILURE;
	}

	// tell the pathfinder to ignore the object we are docking with, so it doesn't block us
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if( ai )
	{
		ai->ignoreObstacle( dockWithMe );
	}

	// create new state machine for attack behavior
	GXROW_AIDOCKSTATE.dockMachine = newInstance(AIDockMachine)( getMachineOwner());

	// tell the docking machine what it is docking with
	GXROW_AIDOCKSTATE.dockMachine->setGoalObject( dockWithMe );
	// now that essential parameters are set, set the machine's initial state
	return GXROW_AIDOCKSTATE.dockMachine->initDefaultState();
}

/**
 * For whatever reason, we are leaving the AI_DOCK state.
 * Destroy the docking sub-machine.
 */
void AIDockState::onExit( StateExitType status )
{
	// destroy the dock machine
	if (GXROW_AIDOCKSTATE.dockMachine) {
		GXROW_AIDOCKSTATE.dockMachine->halt();// GS, you have to halt before you delete to do cleanup.
		deleteInstance(GXROW_AIDOCKSTATE.dockMachine);
		GXROW_AIDOCKSTATE.dockMachine = nullptr;
	}	else {
		DEBUG_LOG(("Dock exited immediately"));
	}

	// stop ignoring our goal object
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai)
	{
		ai->setCanPathThroughUnits(false);
		ai->ignoreObstacle( nullptr );
	}

}

/**
 * We are in the AI_DOCK state, execute the docking behavior.
 */

StateReturnType AIDockState::update()
{

	/**
	 * Run the docking state sub-machine.
	 * If the dock state machine returns anything other than CONTINUE,
	 * it has finished. propagating the return code will cause
	 * the containing state machine to do the right thing.
	 */
	AIUpdateInterface *ai = getMachineOwner()->getAI();
	if (ai)
	{
		ai->setCanPathThroughUnits(true);
		//if (ai->isBlockedAndStuck()) {
			//DEBUG_LOG(("Blocked and stuck."));
		//}
		//if (ai->getNumFramesBlocked()>5) {
			//DEBUG_LOG(("Blocked %d frames", ai->getNumFramesBlocked()));
		//}
	}
	/*
		Note the use of CONVERT_SLEEP_TO_CONTINUE; even if the sub-machine
		sleeps, we still need to be called every frame.
	*/
	return CONVERT_SLEEP_TO_CONTINUE(GXROW_AIDOCKSTATE.dockMachine->updateStateMachine());
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIEnterState::crc( Xfer *xfer )
{
	AIInternalMoveToState::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIEnterState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	AIInternalMoveToState::xfer(xfer);

	xfer->xferObjectID(&GXROW_AIENTERSTATE.entryToClear);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIEnterState::loadPostProcess()
{
	AIInternalMoveToState::loadPostProcess();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIEnterState::onEnter()
{
	GXROW_AIENTERSTATE.entryToClear = INVALID_ID;

	Object* obj = getMachineOwner();
	Object* goal = getMachineGoalObject();
	if (goal)
	{
		if( !TheActionManager->canEnterObject( obj, goal, obj->getAI()->getLastCommandSource(), CHECK_CAPACITY ) )
			return STATE_FAILURE;

		AIInternalMoveToState::gxRow().goalPosition = *goal->getPosition();

		ContainModuleInterface* contain = goal->getContain();
		if (contain)
		{
			contain->onObjectWantsToEnterOrExit(obj, WANTS_TO_ENTER);
			GXROW_AIENTERSTATE.entryToClear = goal->getID();
		}
	}
	else
	{
		return STATE_FAILURE;
	}

	// tell the pathfinder to ignore the enterable object
	AIUpdateInterface *ai = obj->getAI();
	ai->ignoreObstacle( getMachineGoalObject() );
	if (ai->getCurLocomotor())
	{
		ai->getCurLocomotor()->setAllowInvalidPosition(true);
	}
	setAdjustsDestination(false);
	return AIInternalMoveToState::onEnter();
}

//----------------------------------------------------------------------------------------------------------
void AIEnterState::onExit( StateExitType status )
{
	Object* obj = getMachineOwner();
	AIInternalMoveToState::onExit( status );

	// tell the pathfinder to stop ignoring the object
	AIUpdateInterface *ai = obj->getAI();
	if (ai)
	{

		ai->ignoreObstacle( nullptr );
		if (ai->getCurLocomotor())
		{
			ai->getCurLocomotor()->setAllowInvalidPosition(false);
		}
	}

	// use this, rather than getMachineGoalObject, in case the goal
	// is killed while we were waiting...
	if (GXROW_AIENTERSTATE.entryToClear != INVALID_ID)
	{
		Object* goal = TheGameLogic->findObjectByID(GXROW_AIENTERSTATE.entryToClear);
		if (goal)
		{
			ContainModuleInterface* contain = goal->getContain();
			if (contain)
			{
				contain->onObjectWantsToEnterOrExit(obj, WANTS_NEITHER);
			}
		}
	}
}

static bool hasVerticalOverlap(const Object* a, const Object* b)
{
	const float aLower = a->getPosition()->z;
	const float aUpper = aLower + a->getGeometryInfo().getMaxHeightAbovePosition();
	const float bLower = b->getPosition()->z;
	const float bUpper = bLower + b->getGeometryInfo().getMaxHeightAbovePosition();
	return aUpper >= bLower && aLower <= bUpper;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIEnterState::update()
{

	// update the goal position to coincide with the GoalObject
	Object* obj = getMachineOwner();
	Object* goal = getMachineGoalObject();
	if (goal)
	{
		// if our goal is contained by something else, give up. this is for the following bug:
		// -- tell some rangers to enter a humvee
		// -- tell the humvee to enter a chinook
		// -- fly the chinook around; the rangers follow the chinook like dopes
		// this just bails in this case. (srj)
		if (goal->getContainedBy() != nullptr && goal->isAboveTerrain() && !obj->isAboveTerrain())
		{
			return STATE_FAILURE;
		}

		AIInternalMoveToState::gxRow().goalPosition = *goal->getPosition();
		obj->getAI()->friend_setGoalObject(goal);
		if (!TheActionManager->canEnterObject(obj, goal, obj->getAI()->getLastCommandSource(), CHECK_CAPACITY))
		{
			/*
				special-case: if it's an enemy, try attacking it instead. this is to address this bug: (srj)

				Bug: Units stop instead of attacking if building they were trying to garrison is taken by enemy units first.

				1. any game/any faction
				2. build infantry units that can garrison neutral buildings
				3. have infantry  units garrison a neutral building, before they garrison the building have some enemy units garrison it first.

				Expected result: Based on test plan: Instead of just stopping when enemy units garrison building first, they should continue and attack the building.
			*/
			if( obj->getRelationship(goal) == ENEMIES && obj->getAI() )
			{
				CanAttackResult result = TheActionManager->getCanAttackObject(obj, goal, obj->getAI()->getLastCommandSource(), ATTACK_NEW_TARGET);
				if( result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
				{
	 				obj->getAI()->aiAttackObject(goal, NO_MAX_SHOTS_LIMIT, obj->getAI()->getLastCommandSource());
					// weird but true. return state_continue, because if we're here, we're actually an attack state
					// since we just changed the state, it doesn't really matter what we return here.
					return STATE_CONTINUE;
				}
				return STATE_FAILURE;
			}
			return STATE_FAILURE;
		}

		// If we are held, then we must have entered the goal.
		if( getMachineOwner()->isDisabledByType( DISABLED_HELD ) )
		{
			return STATE_SUCCESS;
		}
	}
	else
	{
		return STATE_FAILURE;
	}

	StateReturnType code = AIInternalMoveToState::update();

	// if it's airborne, wait for it to land
#if RETAIL_COMPATIBLE_CRC
	if (code == STATE_SUCCESS && goal->isAboveTerrain() && !obj->isAboveTerrain())
#else
	// TheSuperHackers @bugfix Stubbjax 05/11/2025 Check for vertical overlap when entering containers.
	// This prevents levitating or airborne units from entering containers they are not actually touching.
	if (code == STATE_SUCCESS && !hasVerticalOverlap(goal, obj))
#endif
	{
		code = STATE_CONTINUE;
	}

	if (code == STATE_SUCCESS)
	{
		// Make sure we entered the container.
		// srj sez: I don't think we want to restrict this to HELD items. See the intro of GLA02.map
		// for an example of guys-off-the-border-but-not-held who need this check.
		//if( obj->isDisabledByType( DISABLED_HELD ) )
		{
			if (goal)
			{
				// we didn't enter.  See if we're close.
				Real dx = (obj->getPosition()->x - goal->getPosition()->x);
				Real dy = (obj->getPosition()->y - goal->getPosition()->y);
				Real radius = goal->getGeometryInfo().getMinorRadius();
				if (goal->getGeometryInfo().getGeomType()!=GEOMETRY_BOX) {
					radius = goal->getGeometryInfo().getMajorRadius();
				}
				Bool closeEnough = dx*dx+dy*dy < sqr(radius);
				if (closeEnough) {
					// Grab the container and force ourselves into it.
					// This case is primarily to handle transports on the map border for scripted setup.
					// The partition manager doesn't generate collisions in the border area, so we have to
					// add ourselves.  jba.
					ContainModuleInterface* contain = goal->getContain();
					if (contain)
					{
						contain->addToContain(obj);
					}
				}
			}
		}
	}

	return code;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIExitState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIExitState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferObjectID(&GXROW_AIEXITSTATE.entryToClear);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIExitState::loadPostProcess()
{
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIExitState::onEnter()
{
	GXROW_AIEXITSTATE.entryToClear = INVALID_ID;

	Object* obj = getMachineOwner();
	Object* goal = getMachineGoalObject();
	if (goal)
	{
		ContainModuleInterface* contain = goal->getContain();
		if (contain)
		{
			contain->onObjectWantsToEnterOrExit(obj, WANTS_TO_EXIT);
			GXROW_AIEXITSTATE.entryToClear = goal->getID();
		}
		return STATE_CONTINUE;
	}
	else
	{
		return STATE_FAILURE;
	}
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIExitState::update()
{

	// update the goal position to coincide with the GoalObject
	Object* obj = getMachineOwner();
	Object* goal = getMachineGoalObject();
	if (goal)
	{
		AIUpdateInterface* goalAI = goal->getAI();
		if (goalAI && goalAI->getAiFreeToExit(obj) == WAIT_TO_EXIT)
			return STATE_CONTINUE;

		DEBUG_ASSERTCRASH(obj, ("obj must not be null here"));

		//GS.  The goal of unified ExitInterfaces dies a horrible death.  I can't ask Object for the exit,
		// as removeFromContain is only in the Contain type.  I'm splitting the names in shame.
		ExitInterface* goalExitInterface = goal->getContain() ? goal->getContain()->getContainExitInterface() : nullptr;
		if( goalExitInterface == nullptr )
			return STATE_FAILURE;

		if( goalExitInterface->isExitBusy() )
			return STATE_CONTINUE;// Just wait a sec.

		ExitDoorType exitDoor = goalExitInterface ? goalExitInterface->reserveDoorForExit(obj->getTemplate(), obj) : DOOR_NONE_NEEDED;
		if (exitDoor == DOOR_NONE_AVAILABLE)
			return STATE_FAILURE;

		goalExitInterface->exitObjectViaDoor(obj, exitDoor);
		if( getMachine()->getCurrentStateID() != getID() )
			return STATE_CONTINUE;// Not success, because exitViaDoor has changed us to FollowPath, and if we say Success, our machine will think FollowPath succeeded
		else
			return STATE_SUCCESS;
	}
	else
	{
		return STATE_FAILURE;
	}
}

//----------------------------------------------------------------------------------------------------------
void AIExitState::onExit( StateExitType status )
{
	Object* obj = getMachineOwner();

	// use this, rather than getMachineGoalObject, in case the goal
	// is killed while we were waiting...
	if (GXROW_AIEXITSTATE.entryToClear != INVALID_ID)
	{
		Object* goal = TheGameLogic->findObjectByID(GXROW_AIEXITSTATE.entryToClear);
		if (goal)
		{
			ContainModuleInterface* contain = goal->getContain();
			if (contain)
			{
				contain->onObjectWantsToEnterOrExit(obj, WANTS_NEITHER);
			}
		}
	}
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIExitInstantlyState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIExitInstantlyState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferObjectID(&GXROW_AIEXITINSTANTLYSTATE.entryToClear);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIExitInstantlyState::loadPostProcess()
{
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIExitInstantlyState::onEnter()
{
	GXROW_AIEXITINSTANTLYSTATE.entryToClear = INVALID_ID;

	Object* obj = getMachineOwner();
	Object* goal = getMachineGoalObject();
	if (goal)
	{
		ContainModuleInterface* contain = goal->getContain();
		if (contain)
		{
			contain->onObjectWantsToEnterOrExit(obj, WANTS_TO_EXIT);
			GXROW_AIEXITINSTANTLYSTATE.entryToClear = goal->getID();
		}

		DEBUG_ASSERTCRASH(obj, ("obj must not be null here"));

		//GS.  The goal of unified ExitInterfaces dies a horrible death.  I can't ask Object for the exit,
		// as removeFromContain is only in the Contain type.  I'm splitting the names in shame.
		ExitInterface* goalExitInterface = goal->getContain() ? goal->getContain()->getContainExitInterface() : nullptr;
		if( goalExitInterface == nullptr )
			return STATE_FAILURE;

		goalExitInterface->exitObjectViaDoor( obj, DOOR_1 );

		return STATE_CONTINUE;// Not success, because exitViaDoor has changed us to FollowPath, and if we say Success, our machine will think FollowPath succeeded
	}
	else
	{
		return STATE_FAILURE;
	}
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIExitInstantlyState::update()
{
	if( getMachine()->getCurrentStateID() != getID() )
	{
		return STATE_CONTINUE;// Not success, because exitViaDoor has changed us to FollowPath, and if we say Success, our machine will think FollowPath succeeded
	}
	return STATE_SUCCESS;
}

//----------------------------------------------------------------------------------------------------------
void AIExitInstantlyState::onExit( StateExitType status )
{
	Object* obj = getMachineOwner();

	// use this, rather than getMachineGoalObject, in case the goal
	// is killed while we were waiting...
	if (GXROW_AIEXITINSTANTLYSTATE.entryToClear != INVALID_ID)
	{
		Object* goal = TheGameLogic->findObjectByID(GXROW_AIEXITINSTANTLYSTATE.entryToClear);
		if (goal)
		{
			ContainModuleInterface* contain = goal->getContain();
			if (contain)
			{
				contain->onObjectWantsToEnterOrExit(obj, WANTS_NEITHER);
			}
		}
	}
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
AIGuardState::~AIGuardState()
{
	if (GXROW_AIGUARDSTATE.guardMachine)	{
		GXROW_AIGUARDSTATE.guardMachine->halt();
		deleteInstance(GXROW_AIGUARDSTATE.guardMachine);
	}
	TheGxAIGuardStates.release( m_gxAIGuardStateRow );	// web port (round 5)
}


#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIGuardState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (GXROW_AIGUARDSTATE.guardMachine) name.concat(GXROW_AIGUARDSTATE.guardMachine->getCurrentStateName());
	else name.concat("*null guardMachine");
	return name;
}
#endif
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = GXROW_AIGUARDSTATE.guardMachine!=nullptr;

	xfer->xferBool(&hasMachine);

	if (hasMachine && GXROW_AIGUARDSTATE.guardMachine==nullptr)	{
		// create new state machine for guard behavior
		GXROW_AIGUARDSTATE.guardMachine = newInstance(AIGuardMachine)( getMachineOwner());
	}
	if (hasMachine) {
		xfer->xferSnapshot(GXROW_AIGUARDSTATE.guardMachine);
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardState::loadPostProcess()
{
}


//----------------------------------------------------------------------------------------------------------
//Is our guard state in an attack sub-state?
Bool AIGuardState::isAttack() const
{
	if( GXROW_AIGUARDSTATE.guardMachine )
	{
		return GXROW_AIGUARDSTATE.guardMachine->isInAttackState();
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
//Is our guard state in guard-idle?
Bool AIGuardState::isGuardIdle() const
{
	if( GXROW_AIGUARDSTATE.guardMachine )
	{
		return GXROW_AIGUARDSTATE.guardMachine->isInGuardIdleState();
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Guard location.
 */

StateReturnType AIGuardState::onEnter()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	GXROW_AIGUARDSTATE.guardMachine = newInstance(AIGuardMachine)( getMachineOwner());

	// tell the guarding machine what it is guarding with
	switch(ai->getGuardTargetType())
	{
		case GUARDTARGET_LOCATION: GXROW_AIGUARDSTATE.guardMachine->setTargetPositionToGuard( ai->getGuardLocation() ); break;
		case GUARDTARGET_OBJECT: GXROW_AIGUARDSTATE.guardMachine->setTargetToGuard( TheGameLogic->findObjectByID(ai->getGuardObject()) ); break;
		case GUARDTARGET_AREA: GXROW_AIGUARDSTATE.guardMachine->setAreaToGuard( ai->getAreaToGuard() ); break;
	}
	GXROW_AIGUARDSTATE.guardMachine->setGuardMode(ai->getGuardMode());

	// now that essential parameters are set, set the machine's initial state
	if (GXROW_AIGUARDSTATE.guardMachine->initDefaultState() == STATE_FAILURE)
		return STATE_FAILURE;
	return GXROW_AIGUARDSTATE.guardMachine->setState(AI_GUARD_RETURN);

	obj->getControllingPlayer()->getAcademyStats()->recordGuardAbilityUsed();
}

//----------------------------------------------------------------------------------------------------------
void AIGuardState::onExit( StateExitType status )
{
	deleteInstance(GXROW_AIGUARDSTATE.guardMachine);
	GXROW_AIGUARDSTATE.guardMachine = nullptr;

	Object *obj = getMachineOwner();
	obj->getAI()->clearGuardTargetType();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIGuardState::update()
{
	//DEBUG_LOG(("AIGuardState frame %d: %08lx",TheGameLogic->getFrame(),getMachineOwner()));

	if (GXROW_AIGUARDSTATE.guardMachine == nullptr)
	{
		return STATE_FAILURE; // We actually already exited.
	}

	// if all of our weapons are out of ammo, can't attack.
	// (this can happen for units which never auto-reload, like the Raptor)
	Object* owner = getMachineOwner();
	if( owner->getAI()->getJetAIUpdate() && owner->isOutOfAmmo() && !owner->isKindOf(KINDOF_PROJECTILE) && !owner->getTemplate()->isEnterGuard())
	{
		DEBUG_CRASH(("Hmm, this should probably never happen, since this case should be intercepted by JetAIUpdate"));
		return STATE_FAILURE;
	}

	getMachine()->lock("AIGuardState::update");	// We don't want to switch out of guard during the update.
	StateReturnType ret = GXROW_AIGUARDSTATE.guardMachine->updateStateMachine();
	getMachine()->unlock();
	return ret;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
AIGuardRetaliateState::~AIGuardRetaliateState()
{
	if (GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine)	{
		GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine->halt();
		deleteInstance(GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine);
	}
	TheGxAIGuardRetaliateStates.release( m_gxAIGuardRetaliateStateRow );	// web port (round 5)
}


#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIGuardRetaliateState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if( GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine )
	{
		name.concat(GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine->getCurrentStateName());
	}
	else
	{
		name.concat("*null guardRetaliateMachine");
	}
	return name;
}
#endif
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine!=nullptr;

	xfer->xferBool(&hasMachine);

	if (hasMachine && GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine==nullptr)
	{
		// create new state machine for guard behavior
		GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine = newInstance(AIGuardRetaliateMachine)( getMachineOwner());
	}
	if (hasMachine)
	{
		xfer->xferSnapshot(GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine);
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIGuardRetaliateState::loadPostProcess()
{
}


//----------------------------------------------------------------------------------------------------------
//Is our retaliate state in an attack sub-state?
Bool AIGuardRetaliateState::isAttack() const
{
	if( GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine )
	{
		return GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine->isInAttackState();
	}
	return FALSE;
}


//----------------------------------------------------------------------------------------------------------
/**
 * Guard location.
 */

StateReturnType AIGuardRetaliateState::onEnter()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine = newInstance(AIGuardRetaliateMachine)( getMachineOwner());
#ifdef STATE_MACHINE_DEBUG
	GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine->setDebugOutput(getMachine()->getWantsDebugOutput());
#endif
	// tell the guarding machine what it is guarding with
	GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine->setTargetPositionToGuard( ai->getGoalPosition() );

	Object *goalObject = ai->getGoalObject();
	if( goalObject )
	{
		GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine->setNemesisID( goalObject->getID() );
	}

	// now that essential parameters are set, set the machine's initial state
	return GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine->initDefaultState();
}

//----------------------------------------------------------------------------------------------------------
void AIGuardRetaliateState::onExit( StateExitType status )
{
	deleteInstance(GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine);
	GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine = nullptr;

	Object *obj = getMachineOwner();
	obj->getAI()->clearGuardTargetType();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIGuardRetaliateState::update()
{
	//DEBUG_LOG(("AIGuardRetaliateState frame %d: %08lx",TheGameLogic->getFrame(),getMachineOwner()));

	if (GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine == nullptr)
	{
		return STATE_FAILURE; // We actually already exited.
	}

	// if all of our weapons are out of ammo, can't attack.
	// (this can happen for units which never auto-reload, like the Raptor)
	Object* owner = getMachineOwner();
	if( owner->getAI()->getJetAIUpdate() && owner->isOutOfAmmo() && !owner->isKindOf(KINDOF_PROJECTILE) && !owner->getTemplate()->isEnterGuard())
	{
		DEBUG_CRASH(("Hmm, this should probably never happen, since this case should be intercepted by JetAIUpdate"));
		return STATE_FAILURE;
	}

	StateReturnType ret = GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine->updateStateMachine();
	return ret;
}

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
AITunnelNetworkGuardState::~AITunnelNetworkGuardState()
{
	if (GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine)	{
		GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine->halt();
		deleteInstance(GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine);
	}
	TheGxAITunnelNetworkGuardStates.release( m_gxAITunnelNetworkGuardStateRow );	// web port (round 5)
}


#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AITunnelNetworkGuardState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine) name.concat(GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine->getCurrentStateName());
	else name.concat("*null guardMachine");
	return name;
}
#endif
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AITunnelNetworkGuardState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AITunnelNetworkGuardState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine!=nullptr;

	xfer->xferBool(&hasMachine);

	if (hasMachine && GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine==nullptr)	{
		// create new state machine for guard behavior
		GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine = newInstance(AITNGuardMachine)( getMachineOwner());
	}
	if (hasMachine) {
		xfer->xferSnapshot(GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine);
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AITunnelNetworkGuardState::loadPostProcess()
{
}


//----------------------------------------------------------------------------------------------------------
//Is our guard tunnel network state in an attack sub-state?
Bool AITunnelNetworkGuardState::isAttack() const
{
	if( GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine )
	{
		return GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine->isInAttackState();
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Guard location.
 */

StateReturnType AITunnelNetworkGuardState::onEnter()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine = newInstance(AITNGuardMachine)( getMachineOwner());

	// tell the guarding machine what it is guarding with
	GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine->setTargetPositionToGuard( ai->getGuardLocation() );
	GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine->setGuardMode(ai->getGuardMode());

	// now that essential parameters are set, set the machine's initial state
	if (GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine->initDefaultState() == STATE_FAILURE)
		return STATE_FAILURE;
	return GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine->setState(AI_GUARD_RETURN);
}

//----------------------------------------------------------------------------------------------------------
void AITunnelNetworkGuardState::onExit( StateExitType status )
{
	deleteInstance(GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine);
	GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine = nullptr;

	Object *obj = getMachineOwner();
	obj->getAI()->clearGuardTargetType();
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AITunnelNetworkGuardState::update()
{
	//DEBUG_LOG(("AITunnelNetworkGuardState frame %d: %08lx",TheGameLogic->getFrame(),getMachineOwner()));

	if (GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine == nullptr)
	{
		return STATE_FAILURE; // We actually already exited.
	}

	// if all of our weapons are out of ammo, can't attack.
	// (this can happen for units which never auto-reload, like the Raptor)
	Object* owner = getMachineOwner();
	if (owner->isOutOfAmmo() && !owner->isKindOf(KINDOF_PROJECTILE))
	{
		DEBUG_CRASH(("Hmm, this should probably never happen, since this case should be intercepted by JetAIUpdate"));
		return STATE_FAILURE;
	}

	getMachine()->lock("AITunnelNetworkGuardState::update");	// We don't want to switch out of guard during the update.
	StateReturnType ret = GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine->updateStateMachine();
	getMachine()->unlock();
	return ret;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
AIHuntState::~AIHuntState()
{
	if (GXROW_AIHUNTSTATE.huntMachine)
	{
		GXROW_AIHUNTSTATE.huntMachine->halt();
		deleteInstance(GXROW_AIHUNTSTATE.huntMachine);
	}
	TheGxAIHuntStates.release( m_gxAIHuntStateRow );	// web port (round 5)
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIHuntState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIHuntState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = GXROW_AIHUNTSTATE.huntMachine!=nullptr;

	xfer->xferBool(&hasMachine);

	if (hasMachine && GXROW_AIHUNTSTATE.huntMachine==nullptr)	{
		// create new state machine for hunt behavior
		GXROW_AIHUNTSTATE.huntMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackThenIdleStateMachine");
	}
	if (hasMachine) {
		xfer->xferSnapshot(GXROW_AIHUNTSTATE.huntMachine);
	}
	xfer->xferUnsignedInt(&GXROW_AIHUNTSTATE.nextEnemyScanTime);

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIHuntState::loadPostProcess()
{
}

//----------------------------------------------------------------------------------------------------------
//Is our hunt state in an attack sub-state?
Bool AIHuntState::isAttack() const
{
	if( GXROW_AIHUNTSTATE.huntMachine )
	{
		return GXROW_AIHUNTSTATE.huntMachine->isInAttackState();
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Hunt (seek and destroy).
 */

StateReturnType AIHuntState::onEnter()
{
	// create new state machine for hunt behavior
	GXROW_AIHUNTSTATE.huntMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackThenIdleStateMachine");

	// first time thru, use a random amount so that everyone doesn't scan on the same frame,
	// to avoid "spikes".
	UnsignedInt sleepTime = GameLogicRandomValue(0, ENEMY_SCAN_RATE);
	UnsignedInt now = TheGameLogic->getFrame();
	GXROW_AIHUNTSTATE.nextEnemyScanTime = now + sleepTime;

	// initial state of hunt state machine
	return GXROW_AIHUNTSTATE.huntMachine->initDefaultState();
}

//----------------------------------------------------------------------------------------------------------
void AIHuntState::onExit( StateExitType status )
{
	// destroy the hunt machine
	deleteInstance(GXROW_AIHUNTSTATE.huntMachine);
	GXROW_AIHUNTSTATE.huntMachine = nullptr;

	Object *obj = getMachineOwner();
	if (obj)
	{
		obj->releaseWeaponLock(LOCKED_TEMPORARILY);	// release any temporary locks.
	}
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIHuntState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (GXROW_AIHUNTSTATE.huntMachine) name.concat(GXROW_AIHUNTSTATE.huntMachine->getCurrentStateName());
	else name.concat("*null huntMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------------------
StateReturnType AIHuntState::update()
{

	// look around for better victims every so often
	UnsignedInt now = TheGameLogic->getFrame();
	if (now >= GXROW_AIHUNTSTATE.nextEnemyScanTime)
	{
		Object* owner = getMachineOwner();

		// if all of our weapons are out of ammo, can't hunt.
		// (this can happen for units which never auto-reload, like the Raptor)
		if (owner->isOutOfAmmo() && !owner->isKindOf(KINDOF_PROJECTILE))
			return STATE_FAILURE;

		// Check to see if we have created a crate we need to pick up.
		AIUpdateInterface *ai = owner->getAI();
		Object* crate = ai->checkForCrateToPickup();
		if (crate)
		{
			GXROW_AIHUNTSTATE.huntMachine->setGoalObject(crate);
			GXROW_AIHUNTSTATE.huntMachine->setState( AI_PICK_UP_CRATE );
			return STATE_CONTINUE;
		}

		GXROW_AIHUNTSTATE.nextEnemyScanTime = now + ENEMY_SCAN_RATE;

		const AttackPriorityInfo *info = nullptr;
		info = ai->getAttackInfo();

		// Check if team auto targets same victim.
		Object* teamVictim = nullptr;
		if (owner->getTeam()->getPrototype()->getTemplateInfo()->m_attackCommonTarget)
		{
			teamVictim = owner->getTeam()->getTeamTargetObject();
		}
		Object* victim = nullptr;
		if (teamVictim && info==nullptr)
		{
			victim = teamVictim;
		}
		else
		{
			// do NOT do line of sight check - we want to find everything
			victim = TheAI->findClosestEnemy( owner, 9999.9f, AI::CAN_ATTACK, info );
			if (victim==nullptr && owner->getControllingPlayer() && owner->getControllingPlayer()->getUnitsShouldHunt()) {
				// If we are doing an all hunt, try hunting without the attack priority info. jba.
				victim = TheAI->findClosestEnemy(owner, 9999.9f, AI::CAN_ATTACK, nullptr);
			}
			if (owner->getTeam()->getPrototype()->getTemplateInfo()->m_attackCommonTarget)
			{
				// Check priorities.
				if (teamVictim && info) {
					if (victim==nullptr) {
						DEBUG_LOG(("Couldnt' find victim. hmm."));
						victim = teamVictim;
					}
					Int teamVictimPriority = info->getPriority(teamVictim->getTemplate());
					Int victimPriority;
					if( victim )
						victimPriority = info->getPriority(victim->getTemplate());
					else
						victimPriority = 0;

					if (teamVictimPriority>=victimPriority) {
						victim = teamVictim;
					}
				}
				owner->getTeam()->setTeamTargetObject(victim);
			}
		}
		GXROW_AIHUNTSTATE.huntMachine->setGoalObject( victim );

		if (GXROW_AIHUNTSTATE.huntMachine->getCurrentStateID() == AI_IDLE && victim)
		{
			GXROW_AIHUNTSTATE.huntMachine->setState( AI_ATTACK_OBJECT );
		}
		if (owner->getControllingPlayer() && owner->getControllingPlayer()->getUnitsShouldHunt()==FALSE) {
			// If we are not doing an all hunt, then exit hunt state - no more victims.
			if (GXROW_AIHUNTSTATE.huntMachine->getCurrentStateID() == AI_IDLE && victim==nullptr) {
				return STATE_SUCCESS; // we killed everything :) jba.
			}
		}
	}

	getMachine()->lock("AIHuntState::update");	// The idle state in the sub machine can sometimes acquire targets.
												// It is important to not switch out of this state via a sub machine call. jba.
	/*
		Note the use of CONVERT_SLEEP_TO_CONTINUE; even if the sub-machine
		sleeps, we still need to be called every frame.
	*/
			/// @todo srj -- find a way to sleep for a number of frames here, if possible
	StateReturnType ret = CONVERT_SLEEP_TO_CONTINUE(GXROW_AIHUNTSTATE.huntMachine->updateStateMachine());
	getMachine()->unlock();
	return ret;
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
AIAttackAreaState::~AIAttackAreaState()
{
	if (GXROW_AIATTACKAREASTATE.attackMachine) {
		GXROW_AIATTACKAREASTATE.attackMachine->halt();
		deleteInstance(GXROW_AIATTACKAREASTATE.attackMachine);
	}
	TheGxAIAttackAreaStates.release( m_gxAIAttackAreaStateRow );	// web port (round 5)
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIAttackAreaState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIAttackAreaState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	Bool hasMachine = GXROW_AIATTACKAREASTATE.attackMachine!=nullptr;

	xfer->xferBool(&hasMachine);

	if (hasMachine && GXROW_AIATTACKAREASTATE.attackMachine==nullptr)	{
		// create new state machine for hunt behavior
		GXROW_AIATTACKAREASTATE.attackMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackThenIdleStateMachine");
	}
	if (hasMachine) {
		xfer->xferSnapshot(GXROW_AIATTACKAREASTATE.attackMachine);
	}
	xfer->xferUnsignedInt(&GXROW_AIATTACKAREASTATE.nextEnemyScanTime);

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIAttackAreaState::loadPostProcess()
{
}

#ifdef STATE_MACHINE_DEBUG
//----------------------------------------------------------------------------------------------------------
AsciiString AIAttackAreaState::getName(  ) const
{
	AsciiString name = m_name;
	name.concat("/");
	if (GXROW_AIATTACKAREASTATE.attackMachine) name.concat(GXROW_AIATTACKAREASTATE.attackMachine->getCurrentStateName());
	else name.concat("*null GXROW_AIATTACKAREASTATE.attackMachine");
	return name;
}
#endif

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackAreaState::onEnter()
{

	// create new state machine for hunt behavior
	GXROW_AIATTACKAREASTATE.attackMachine = newInstance(AIAttackThenIdleStateMachine)( getMachineOwner(), "AIAttackThenIdleStateMachine");

	// first time thru, use a random amount so that everyone doesn't scan on the same frame,
	// to avoid "spikes".
	UnsignedInt now = TheGameLogic->getFrame();
	GXROW_AIATTACKAREASTATE.nextEnemyScanTime = now + GameLogicRandomValue(0, ENEMY_SCAN_RATE);

	// initial state of hunt state machine
	return GXROW_AIATTACKAREASTATE.attackMachine->initDefaultState();
}

//----------------------------------------------------------------------------------------------------------
void AIAttackAreaState::onExit( StateExitType status )
{
	// destroy the hunt machine
	deleteInstance(GXROW_AIATTACKAREASTATE.attackMachine);
	GXROW_AIATTACKAREASTATE.attackMachine = nullptr;
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIAttackAreaState::update()
{
	// look around for better victims every so often
	UnsignedInt now = TheGameLogic->getFrame();
	if (now >= GXROW_AIATTACKAREASTATE.nextEnemyScanTime)
	{
		Object* owner = getMachineOwner();

		// if all of our weapons are out of ammo, can't hunt.
		// (this can happen for units which never auto-reload, like the Raptor)
		if (owner->isOutOfAmmo() && !owner->isKindOf(KINDOF_PROJECTILE))
			return STATE_FAILURE;

		// first time thru, add a random amount so that everyone doesn't scan on the same frame,
		// to avoid "spikes". Note that this implementation ensures that this unit checks immediately
		// upon entering this state, then wait a possibly-longer-than-usual time (due to randomness),
		// then settle into a regular schedule.
		GXROW_AIATTACKAREASTATE.nextEnemyScanTime = now + ENEMY_SCAN_RATE;

		AIUpdateInterface *ai = owner->getAI();
		if (ai->getAreaToGuard() == nullptr)
			return STATE_FAILURE;

		const AttackPriorityInfo *info = nullptr;
		info = ai->getAttackInfo();
		PartitionFilterPolygonTrigger polyFilter(ai->getAreaToGuard());

		// do NOT do line of sight check - we want to find everything
		Object *victim = TheAI->findClosestEnemy( owner, 9999.9f, AI::CAN_ATTACK, info, &polyFilter );
		GXROW_AIATTACKAREASTATE.attackMachine->setGoalObject( victim );

		if (GXROW_AIATTACKAREASTATE.attackMachine->getCurrentStateID() == AI_IDLE && victim)
		{
			GXROW_AIATTACKAREASTATE.attackMachine->setState( AI_ATTACK_OBJECT );
		}
		if (victim==nullptr) {
			return STATE_SUCCESS;
		}
	}

	getMachine()->lock("AIAttackAreaState::update");	// The idle state in the sub machine can sometimes acquire targets.
												// It is important to not switch out of this state via a sub machine call. jba.
	/*
		Note the use of CONVERT_SLEEP_TO_CONTINUE; even if the sub-machine
		sleeps, we still need to be called every frame.
	*/
			/// @todo srj -- find a way to sleep for a number of frames here, if possible
	StateReturnType ret = CONVERT_SLEEP_TO_CONTINUE(GXROW_AIATTACKAREASTATE.attackMachine->updateStateMachine());
	getMachine()->unlock();
	return ret;
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AIFaceState::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void AIFaceState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferBool(&GXROW_AIFACESTATE.canTurnInPlace);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AIFaceState::loadPostProcess()
{
	// empty.  jba.
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFaceState::onEnter()
{
	Object* source = getMachineOwner();

	AIUpdateInterface* ai = source->getAI();
	Locomotor* curLoco = ai->getCurLocomotor();
	GXROW_AIFACESTATE.canTurnInPlace = curLoco ? curLoco->getMinSpeed() == 0.0f : false;

	Object* target = getMachineGoalObject();
	if (GXROW_AIFACESTATE.obj && target == nullptr )
	{
		// Nothing to face...
		return STATE_FAILURE;
	}

	return STATE_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------
void AIFaceState::onExit( StateExitType status )
{
}

//----------------------------------------------------------------------------------------------------------
StateReturnType AIFaceState::update()
{

	Object *obj = getMachineOwner();
	AIUpdateInterface *ai = obj->getAI();

	const Coord3D* pos = getMachineGoalPosition();
	if (GXROW_AIFACESTATE.obj)
	{
		Object *target = getMachineGoalObject();
		if (!target)
		{
			// Nothing to face.
			return STATE_FAILURE;
		}
		pos = target->getPosition();
	}
	Real relAngle = ThePartitionManager->getRelativeAngle2D( obj, pos );

	const Real REL_THRESH = 0.035f;	// about 2 degrees. (getRelativeAngle2D is current only accurate to about 1.25 degrees)
	if( fabs( relAngle ) < REL_THRESH )
	{
		return STATE_SUCCESS;
	}

	// turnDelta = yawRate()	NO, do not get this, it is not useful. (srj)

	if (GXROW_AIFACESTATE.canTurnInPlace)
	{
		Real desiredAngle = obj->getOrientation() + relAngle;
		ai->setLocomotorGoalOrientation( desiredAngle );
	}
	else
	{
		ai->setLocomotorGoalPositionExplicit(*pos);
	}

	return STATE_CONTINUE;
}

AIInternalMoveToState::AIInternalMoveToState( StateMachine *machine, AsciiString name ) : State( machine, name ),
	m_gxAIInternalMoveToStateRow( TheGxAIInternalMoveToStates.alloc( nullptr ) )	// web port (round 5): this unit's row (zeroed, like the module was)
{

		gxRow().goalPosition.zero();
		gxRow().pathGoalPosition.zero();
		gxRow().pathTimestamp = 0;
		gxRow().blockedRepathTimestamp = 0;
		gxRow().adjustDestinations = true;
		gxRow().goalLayer = LAYER_INVALID;
	}

AIInternalMoveToState::~AIInternalMoveToState()
{
	TheGxAIInternalMoveToStates.release( m_gxAIInternalMoveToStateRow );	// web port (round 5)
}

AIFollowWaypointPathState::AIFollowWaypointPathState( StateMachine *machine, Bool asGroup ) : AIInternalMoveToState( machine, "AIFollowWaypointPathState" )
{
	m_gxAIFollowWaypointPathStateRow = TheGxAIFollowWaypointPathStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.isFollowWaypointPathState = true;
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.moveAsGroup = asGroup;

		gxRow().angle = 0.0f;
	}

AIFollowWaypointPathState::AIFollowWaypointPathState( StateMachine *machine, Bool asGroup, Bool isFollow) : AIInternalMoveToState( machine,
																"AIFollowWaypointPathState" )
{
	m_gxAIFollowWaypointPathStateRow = TheGxAIFollowWaypointPathStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.isFollowWaypointPathState = isFollow;
	GXROW_AIFOLLOWWAYPOINTPATHSTATE.moveAsGroup = asGroup;

		gxRow().angle = 0.0f;
	}

AIFollowWaypointPathState::~AIFollowWaypointPathState()
{
	TheGxAIFollowWaypointPathStates.release( m_gxAIFollowWaypointPathStateRow );	// web port (round 5)
}

AIMoveAndTightenState::AIMoveAndTightenState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIMoveAndTightenState" )
{
	m_gxAIMoveAndTightenStateRow = TheGxAIMoveAndTightenStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().checkForPath = false;
		gxRow().okToRepathTimes = 0;
	}

AIMoveAndTightenState::AIMoveAndTightenState( StateMachine *machine, const char *name ) : AIInternalMoveToState( machine, name )
{
	m_gxAIMoveAndTightenStateRow = TheGxAIMoveAndTightenStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().checkForPath = false;
		gxRow().okToRepathTimes = 0;
	}

AIMoveAndTightenState::~AIMoveAndTightenState()
{
	TheGxAIMoveAndTightenStates.release( m_gxAIMoveAndTightenStateRow );	// web port (round 5)
}

AIPickUpCrateState::AIPickUpCrateState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIAttackPickUpCrateState" )
{
	m_gxAIPickUpCrateStateRow = TheGxAIPickUpCrateStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		// we're setting m_isInitialApproach to true in the constructor because we want the first pass
		// through this state to allow a unit to attack incidental targets (if it is turreted)
	}

AIPickUpCrateState::~AIPickUpCrateState()
{
	TheGxAIPickUpCrateStates.release( m_gxAIPickUpCrateStateRow );	// web port (round 5)
}

AIEnterState::AIEnterState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIEnterState" )
{
	m_gxAIEnterStateRow = TheGxAIEnterStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
 }

AIEnterState::~AIEnterState()
{
	TheGxAIEnterStates.release( m_gxAIEnterStateRow );	// web port (round 5)
}

AIAttackAimAtTargetState::AIAttackAimAtTargetState( StateMachine *machine, Bool attackingObject, Bool forceAttacking ) : State( machine, "AIAttackAimAtTargetState" )
{
	m_gxAIAttackAimAtTargetStateRow = TheGxAIAttackAimAtTargetStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIATTACKAIMATTARGETSTATE.isAttackingObject = attackingObject;
	GXROW_AIATTACKAIMATTARGETSTATE.canTurnInPlace = false;
	GXROW_AIATTACKAIMATTARGETSTATE.isForceAttacking = forceAttacking;
	GXROW_AIATTACKAIMATTARGETSTATE.setLocomotor = false;

	}

AIAttackAimAtTargetState::~AIAttackAimAtTargetState()
{
	TheGxAIAttackAimAtTargetStates.release( m_gxAIAttackAimAtTargetStateRow );	// web port (round 5)
}

AIAttackApproachTargetState::AIAttackApproachTargetState( StateMachine *machine, Bool follow, Bool attackingObject, Bool forceAttacking ) : AIInternalMoveToState( machine, "AIAttackApproachTargetState" )
{
	m_gxAIAttackApproachTargetStateRow = TheGxAIAttackApproachTargetStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIATTACKAPPROACHTARGETSTATE.follow = follow;
	GXROW_AIATTACKAPPROACHTARGETSTATE.isAttackingObject = attackingObject;
	GXROW_AIATTACKAPPROACHTARGETSTATE.isInitialApproach = true;
	GXROW_AIATTACKAPPROACHTARGETSTATE.isForceAttacking = forceAttacking;

		// we're setting gxRow().isInitialApproach to true in the constructor because we want the first pass
		// through this state to allow a unit to attack incidental targets (if it is turreted)
	}

AIAttackApproachTargetState::~AIAttackApproachTargetState()
{
	TheGxAIAttackApproachTargetStates.release( m_gxAIAttackApproachTargetStateRow );	// web port (round 5)
}

AIAttackAreaState::AIAttackAreaState( StateMachine *machine ) : State( machine, "AIAttackAreaState" )
{
	m_gxAIAttackAreaStateRow = TheGxAIAttackAreaStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIATTACKAREASTATE.attackMachine = nullptr;
	GXROW_AIATTACKAREASTATE.nextEnemyScanTime = 0;
 }

AIAttackPursueTargetState::AIAttackPursueTargetState( StateMachine *machine, Bool follow, Bool attackingObject, Bool forceAttacking ) : AIInternalMoveToState( machine, "AIAttackPursueTargetState" )
{
	m_gxAIAttackPursueTargetStateRow = TheGxAIAttackPursueTargetStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIATTACKPURSUETARGETSTATE.follow = follow;
	GXROW_AIATTACKPURSUETARGETSTATE.isAttackingObject = attackingObject;
	GXROW_AIATTACKPURSUETARGETSTATE.isInitialApproach = true;
	GXROW_AIATTACKPURSUETARGETSTATE.isForceAttacking = forceAttacking;
	GXROW_AIATTACKPURSUETARGETSTATE.approachTimestamp = 0;
	GXROW_AIATTACKPURSUETARGETSTATE.stopIfInRange = false;

		// we're setting gxRow().isInitialApproach to true in the constructor because we want the first pass
		// through this state to allow a unit to attack incidental targets (if it is turreted)
	}

AIAttackPursueTargetState::~AIAttackPursueTargetState()
{
	TheGxAIAttackPursueTargetStates.release( m_gxAIAttackPursueTargetStateRow );	// web port (round 5)
}

AIAttackSquadState::AIAttackSquadState( StateMachine *machine, AttackExitConditionsInterface *attackParameters) : State( machine , "AIAttackSquadState")
{
	m_gxAIAttackSquadStateRow = TheGxAIAttackSquadStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	}

AIDockState::AIDockState( StateMachine *machine ) : State( machine, "AIDockState" )
{
	m_gxAIDockStateRow = TheGxAIDockStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIDOCKSTATE.dockMachine = nullptr;
	GXROW_AIDOCKSTATE.usingPrecisionMovement = FALSE;
 }

AIExitInstantlyState::AIExitInstantlyState( StateMachine *machine ) : State( machine, "AIExitInstantlyState" )
{
	m_gxAIExitInstantlyStateRow = TheGxAIExitInstantlyStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
 }

AIExitInstantlyState::~AIExitInstantlyState()
{
	TheGxAIExitInstantlyStates.release( m_gxAIExitInstantlyStateRow );	// web port (round 5)
}

AIExitState::AIExitState( StateMachine *machine ) : State( machine, "AIExitState" )
{
	m_gxAIExitStateRow = TheGxAIExitStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
 }

AIExitState::~AIExitState()
{
	TheGxAIExitStates.release( m_gxAIExitStateRow );	// web port (round 5)
}

AIFaceState::AIFaceState( StateMachine *machine, Bool obj ) : State( machine, "AIFaceState" )
{
	m_gxAIFaceStateRow = TheGxAIFaceStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIFACESTATE.canTurnInPlace = false;
	GXROW_AIFACESTATE.obj = obj;
 }

AIFaceState::~AIFaceState()
{
	TheGxAIFaceStates.release( m_gxAIFaceStateRow );	// web port (round 5)
}

AIFollowPathState::AIFollowPathState( StateMachine *machine, AsciiString name) : AIInternalMoveToState( machine, name )
{
	m_gxAIFollowPathStateRow = TheGxAIFollowPathStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIFOLLOWPATHSTATE.adjustFinal = true;
	GXROW_AIFOLLOWPATHSTATE.adjustFinalOverride = false;
	GXROW_AIFOLLOWPATHSTATE.index = 0;
	GXROW_AIFOLLOWPATHSTATE.retryCount = 10;
 }

AIFollowPathState::~AIFollowPathState()
{
	TheGxAIFollowPathStates.release( m_gxAIFollowPathStateRow );	// web port (round 5)
}

AIFollowWaypointPathExactState::AIFollowWaypointPathExactState( StateMachine *machine, Bool asGroup ) : AIInternalMoveToState( machine, "AIFollowWaypointPathExactState" )
{
	m_gxAIFollowWaypointPathExactStateRow = TheGxAIFollowWaypointPathExactStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.moveAsGroup = asGroup;
	GXROW_AIFOLLOWWAYPOINTPATHEXACTSTATE.lastWaypoint = nullptr;
 }

AIFollowWaypointPathExactState::~AIFollowWaypointPathExactState()
{
	TheGxAIFollowWaypointPathExactStates.release( m_gxAIFollowWaypointPathExactStateRow );	// web port (round 5)
}

AIGuardRetaliateState::AIGuardRetaliateState( StateMachine *machine ) : State( machine, "AIGuardRetaliateState" )
{
	m_gxAIGuardRetaliateStateRow = TheGxAIGuardRetaliateStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIGUARDRETALIATESTATE.guardRetaliateMachine = nullptr;
}

AIGuardState::AIGuardState( StateMachine *machine ) : State( machine, "AIGuardState" )
{
	m_gxAIGuardStateRow = TheGxAIGuardStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIGUARDSTATE.guardMachine = nullptr;

		gxRow().guardMachine = nullptr;
	}

AIHuntState::AIHuntState( StateMachine *machine ) : State( machine, "AIHuntState" )
{
	m_gxAIHuntStateRow = TheGxAIHuntStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AIHUNTSTATE.huntMachine = nullptr;

		gxRow().nextEnemyScanTime = 0;
	}

AIIdleState::~AIIdleState()
{
	TheGxAIIdleStates.release( m_gxAIIdleStateRow );	// web port (round 5)
}

AIMoveAndDeleteState::AIMoveAndDeleteState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIMoveAndDeleteState" )
{
	m_gxAIMoveAndDeleteStateRow = TheGxAIMoveAndDeleteStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().appendGoalPosition = FALSE;
	}

AIMoveAndDeleteState::~AIMoveAndDeleteState()
{
	TheGxAIMoveAndDeleteStates.release( m_gxAIMoveAndDeleteStateRow );	// web port (round 5)
}

AIMoveAndEvacuateState::AIMoveAndEvacuateState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIMoveAndEvacuateState" )
{
	m_gxAIMoveAndEvacuateStateRow = TheGxAIMoveAndEvacuateStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().origin.zero();
	}

AIMoveAndEvacuateState::~AIMoveAndEvacuateState()
{
	TheGxAIMoveAndEvacuateStates.release( m_gxAIMoveAndEvacuateStateRow );	// web port (round 5)
}

AIMoveToState::~AIMoveToState()
{
	TheGxAIMoveToStates.release( m_gxAIMoveToStateRow );	// web port (round 5)
}

AIPanicState::AIPanicState( StateMachine *machine ) : AIFollowWaypointPathState( machine, false)
{
	m_gxAIPanicStateRow = TheGxAIPanicStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

#ifdef STATE_MACHINE_DEBUG
		setName("AIPanicState");
#endif
	}

AIPanicState::~AIPanicState()
{
	TheGxAIPanicStates.release( m_gxAIPanicStateRow );	// web port (round 5)
}

AIRappelState::~AIRappelState()
{
	TheGxAIRappelStates.release( m_gxAIRappelStateRow );	// web port (round 5)
}

AITunnelNetworkGuardState::AITunnelNetworkGuardState( StateMachine *machine ) : State( machine, "AITunnelNetworkGuardState" )
{
	m_gxAITunnelNetworkGuardStateRow = TheGxAITunnelNetworkGuardStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_AITUNNELNETWORKGUARDSTATE.guardMachine = nullptr;

		gxRow().guardMachine = nullptr;
	}

AIWanderInPlaceState::AIWanderInPlaceState( StateMachine *machine ) : AIInternalMoveToState( machine, "AIWanderInPlaceState" )
{
	m_gxAIWanderInPlaceStateRow = TheGxAIWanderInPlaceStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

		gxRow().origin.zero();
		gxRow().waitFrames = 0;
		gxRow().timer = 0;
	}

AIWanderInPlaceState::~AIWanderInPlaceState()
{
	TheGxAIWanderInPlaceStates.release( m_gxAIWanderInPlaceStateRow );	// web port (round 5)
}

AIWanderState::AIWanderState( StateMachine *machine ) : AIFollowWaypointPathState( machine, false )
{
	m_gxAIWanderStateRow = TheGxAIWanderStates.alloc( nullptr );	// web port (round 5): this unit's row (zeroed, like the module was)

#ifdef STATE_MACHINE_DEBUG
		setName("AIWanderState");
#endif
		gxRow().timer = 0;
		gxRow().waitFrames = 0;
	}

AIWanderState::~AIWanderState()
{
	TheGxAIWanderStates.release( m_gxAIWanderStateRow );	// web port (round 5)
}
