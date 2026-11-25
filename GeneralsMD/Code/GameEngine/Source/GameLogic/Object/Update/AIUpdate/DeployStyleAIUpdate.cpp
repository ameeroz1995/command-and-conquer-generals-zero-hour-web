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

// DeployStyleAIUpdate.cpp ////////////
// Author: Kris Morness, August 2002
// Desc:   A standard ai update that also handles units that must deploy to attack and pack before moving.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"

#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"

#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"

#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DeployStyleAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
DeployStyleAIUpdate::DeployStyleAIUpdate( Thing *thing, const ModuleData* moduleData ) : AIUpdateInterface( thing, moduleData )
{
	m_gxRow = GxComponentPool<GxState>::the().alloc();
	R().state = READY_TO_MOVE;
	R().frameToWaitForDeploy = 0;
}

//-------------------------------------------------------------------------------------------------
DeployStyleAIUpdate::~DeployStyleAIUpdate()
{
	GxComponentPool<GxState>::the().release(m_gxRow);
}

//-------------------------------------------------------------------------------------------------
Bool DeployStyleAIUpdate::isIdle() const
{
	return AIUpdateInterface::isIdle();
}

//-------------------------------------------------------------------------------------------------
void DeployStyleAIUpdate::aiDoCommand( const AICommandParms* parms )
{
	if (!isAllowedToRespondToAiCommands(parms))
		return;

	/*
	//Hack code to allow follow waypoint scripts to be converted to attack follow waypoint scripts
	//for simple-test-map-purposes.
	switch( parms->m_cmd )
	{
		case AICMD_FOLLOW_WAYPOINT_PATH:
			((AICommandParms*)parms)->m_cmd = AICMD_ATTACKFOLLOW_WAYPOINT_PATH;
			break;
		case AICMD_FOLLOW_WAYPOINT_PATH_AS_TEAM:
			((AICommandParms*)parms)->m_cmd = AICMD_ATTACKFOLLOW_WAYPOINT_PATH_AS_TEAM;
			break;
	}
	*/
	AIUpdateInterface::aiDoCommand( parms );
}

//-------------------------------------------------------------------------------------------------
// web port (round 5 addendum): the deploy kernels. Pure: from the state, the deploy timer and the
// gathered facts (in range or guarding, trying to move, turrets), the state change to make.
// reads their arguments only; write nothing.
struct GxDeployOps { Int n; DeployStateTypes state[1]; Bool reverse[1]; };
#define GX_DEPLOY_OP(s_, r_) do { ops.state[0] = (s_); ops.reverse[0] = (r_); ops.n = 1; } while (0)

// the deploy timer: a finished deploy / undeploy becomes ready to attack / move
static GxDeployOps gxDeployTimerKernel(DeployStateTypes st, UnsignedInt frameToWait, UnsignedInt now)
{
	GxDeployOps ops; ops.n = 0;
	if( frameToWait != 0 && now >= frameToWait )
	{
		if( st == DEPLOY ) GX_DEPLOY_OP( READY_TO_ATTACK, FALSE );
		else if( st == UNDEPLOY ) GX_DEPLOY_OP( READY_TO_MOVE, FALSE );
	}
	return ops;
}

// the decision: deploy to attack or guard, pack up to move
static GxDeployOps gxDeployKernel(DeployStateTypes st, UnsignedInt frameToWait, Bool attackOrGuard,
	Bool tryingToMove, Bool turretMustCenter, Bool turretCentered)
{
	GxDeployOps ops; ops.n = 0;
	if( attackOrGuard )
	{
		switch( st )
		{
			case READY_TO_MOVE: GX_DEPLOY_OP( DEPLOY, FALSE ); break;	// deploy before we can attack
			case UNDEPLOY: if( frameToWait != 0 ) GX_DEPLOY_OP( DEPLOY, TRUE ); break;	// reverse the undeploy
			case ALIGNING_TURRETS: GX_DEPLOY_OP( READY_TO_ATTACK, FALSE ); break;	// aligning turrets can attack now
			default: break;
		}
	}
	else if( tryingToMove )
	{
		switch( st )
		{
			case READY_TO_ATTACK:
				if( turretMustCenter ) GX_DEPLOY_OP( ALIGNING_TURRETS, FALSE );	// center before we can undeploy
				else GX_DEPLOY_OP( UNDEPLOY, FALSE );
				break;
			case DEPLOY: if( frameToWait != 0 ) GX_DEPLOY_OP( UNDEPLOY, TRUE ); break;	// reverse the deploy
			case ALIGNING_TURRETS: if( turretCentered ) GX_DEPLOY_OP( UNDEPLOY, FALSE ); break;
			default: break;
		}
	}
	return ops;
}
#undef GX_DEPLOY_OP

UpdateSleepTime DeployStyleAIUpdate::update()
{
	// have to call our parent's isIdle, because we override it to never return true
	// when we have a pending command...
	Object *self = getObject();
	Weapon *weapon = self->getCurrentWeapon();
	UnsignedInt now = TheGameLogic->getFrame();
	const DeployStyleAIUpdateModuleData *data = getDeployStyleAIUpdateModuleData();

	//Are we attempting to move? If so we can't do it unless we are undeployed.
	Bool isTryingToMove = isWaitingForPath() || getPath();

	//Are we trying to attack something. If so, we need to be in range before we can do so.
	Bool isTryingToAttack = getStateMachine()->isInAttackState();
	Bool isInRange = FALSE;

	//Are we in guard mode? If so, are we idle... idle guarders deploy for fastest response against attackers.
	Bool isInGuardIdleState = getStateMachine()->isInGuardIdleState();

	AIUpdateInterface *ai = self->getAI();

	if( isTryingToAttack && weapon )
	{
		Object *victim = ai->getCurrentVictim();
		if( victim )
		{
			isInRange = weapon->isWithinAttackRange( self, victim );
		}
		else
		{
			const Coord3D *pos = ai->getCurrentVictimPos();
			if( pos )
			{
				isInRange = weapon->isWithinAttackRange( self, pos );
			}
		}
	}

	// web port (round 5 addendum): two kernels, each followed by its apply (setMyState: model
	// conditions, timers, sounds). First the deploy timer, then the decision, with the turret facts
	// gathered after the first change as before.
	GxDeployOps ops = gxDeployTimerKernel(R().state, R().frameToWaitForDeploy, now);
	for( Int k = 0; k < ops.n; ++k )
		setMyState( ops.state[k], ops.reverse[k] );
	Bool turretMustCenter = FALSE, turretCentered = FALSE;
	if( !(isInRange || isInGuardIdleState) && isTryingToMove && (R().state == READY_TO_ATTACK || R().state == ALIGNING_TURRETS) )
	{
		WhichTurretType tur = getWhichTurretForCurWeapon();
		if( tur != TURRET_INVALID )
		{
			turretMustCenter = doTurretsHaveToCenterBeforePacking();
			turretCentered = R().state == ALIGNING_TURRETS && isTurretInNaturalPosition( tur );
		}
	}
	ops = gxDeployKernel(R().state, R().frameToWaitForDeploy, isInRange || isInGuardIdleState, isTryingToMove, turretMustCenter, turretCentered);
	for( Int k = 0; k < ops.n; ++k )
		setMyState( ops.state[k], ops.reverse[k] );

	switch( R().state )
	{
		case READY_TO_MOVE:
			if( isTryingToMove )
			{
				self->setModelConditionState( MODELCONDITION_MOVING );
			}
			break;

		case READY_TO_ATTACK:
			break;

		case DEPLOY:
			if( data->m_manualDeployAnimations )
			{
				UnsignedInt totalFrames = getPackTime();
				UnsignedInt framesLeft = R().frameToWaitForDeploy - now;
				Drawable *draw = self->getDrawable();
				if( draw )
				{
					draw->setAnimationFrame( totalFrames - framesLeft );
				}
			}
			getStateMachine()->setTemporaryState( AI_BUSY, UPDATE_SLEEP_NONE );
			setLocomotorGoalNone();
			break;
		case UNDEPLOY:
			if( data->m_manualDeployAnimations )
			{
				UnsignedInt framesLeft = R().frameToWaitForDeploy - now;
				Drawable *draw = self->getDrawable();
				if( draw )
				{
					draw->setAnimationFrame( framesLeft );
				}
			}
			getStateMachine()->setTemporaryState( AI_BUSY, UPDATE_SLEEP_NONE );
			setLocomotorGoalNone();
			break;

		case ALIGNING_TURRETS:
			getStateMachine()->setTemporaryState( AI_BUSY, UPDATE_SLEEP_NONE );
			setLocomotorGoalNone();
			break;
	}

	AIUpdateInterface::update();
	//We can't sleep the deploy AI because any new commands need to be caught and sent
	//into busy state during the update.
	return UPDATE_SLEEP_NONE;

}

//-------------------------------------------------------------------------------------------------
void DeployStyleAIUpdate::setMyState( DeployStateTypes stateID, Bool reverseDeploy )
{
	R().state = stateID;
	Object *self = getObject();
	UnsignedInt now = TheGameLogic->getFrame();
	const DeployStyleAIUpdateModuleData *data = getDeployStyleAIUpdateModuleData();

	switch( stateID )
	{
		case DEPLOY:
		{
			//Tell our object to deploy (so it can continue the same attack later).
			self->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_PACKING ),
																						 MAKE_MODELCONDITION_MASK( MODELCONDITION_UNPACKING ) );

			if( reverseDeploy )
			{
				//This is a zero to max animation.
				UnsignedInt totalFrames = getUnpackTime();
				UnsignedInt framesLeft = R().frameToWaitForDeploy - now;
				R().frameToWaitForDeploy = now + totalFrames - framesLeft;
				if( data->m_manualDeployAnimations )
				{
					Drawable *draw = self->getDrawable();
					if( draw )
					{
						draw->setAnimationFrame( totalFrames - framesLeft );
					}
				}
			}
			else
			{
				R().frameToWaitForDeploy = getUnpackTime() + now;
			}

			//Play deploy sound
			const ThingTemplate *thing = self->getTemplate();
			const AudioEventRTS* soundToPlayPtr = thing->getPerUnitSound( "Deploy" );
			if( soundToPlayPtr )
			{
				AudioEventRTS soundToPlay = *soundToPlayPtr;
				soundToPlay.setObjectID( self->getID() );
				TheAudio->addAudioEvent( &soundToPlay );
			}

			break;
		}
		case UNDEPLOY:
		{
			//This status will tell the approach AI state to succeed automatically. This prevents
			//twitching on deploy. Make sure we clear it now so when it goes into that state, it'll
			//actually move.
			self->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DEPLOYED ) );

			//Tell our object to pack up (so it can continue the same move later).
			self->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK2( MODELCONDITION_UNPACKING, MODELCONDITION_DEPLOYED ),
																						 MAKE_MODELCONDITION_MASK( MODELCONDITION_PACKING ) );

			if( reverseDeploy )
			{
				//This is a max to zero animation.
				UnsignedInt totalFrames = getUnpackTime();
				UnsignedInt framesLeft = R().frameToWaitForDeploy - now;
				R().frameToWaitForDeploy = now + totalFrames - framesLeft;
				if( data->m_manualDeployAnimations )
				{
					Drawable *draw = self->getDrawable();
					if( draw )
					{
						draw->setAnimationFrame( framesLeft );
					}
				}
			}
			else
			{
				R().frameToWaitForDeploy = getPackTime() + now;
			}

			if( doTurretsFunctionOnlyWhenDeployed() )
			{
				WhichTurretType tur = getWhichTurretForCurWeapon();
				if( tur != TURRET_INVALID )
				{
					setTurretEnabled( tur, false );
				}
			}

			//Play undeploy sound
			const ThingTemplate *thing = self->getTemplate();
			const AudioEventRTS* soundToPlayPtr = thing->getPerUnitSound( "Undeploy" );
			if( soundToPlayPtr )
			{
				AudioEventRTS soundToPlay = *soundToPlayPtr;
				soundToPlay.setObjectID( self->getID() );
				TheAudio->addAudioEvent( &soundToPlay );
			}

			break;
		}
		case READY_TO_MOVE:
		{
			R().frameToWaitForDeploy = 0;

			self->clearModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_PACKING ) );
			break;
		}
		case READY_TO_ATTACK:
		{
			//This status will tell the approach AI state to succeed automatically. This prevents
			//twitching on deploy.
			self->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DEPLOYED ) );

			R().frameToWaitForDeploy = 0;

			self->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_UNPACKING ),
																						 MAKE_MODELCONDITION_MASK( MODELCONDITION_DEPLOYED) );

			if( doTurretsFunctionOnlyWhenDeployed() )
			{
				WhichTurretType tur = getWhichTurretForCurWeapon();
				if( tur != TURRET_INVALID )
				{
					setTurretEnabled( tur, true );
				}
			}

			break;
		}
		case ALIGNING_TURRETS:
		{
			R().frameToWaitForDeploy = 0;
			WhichTurretType tur = getWhichTurretForCurWeapon();
			if( tur != TURRET_INVALID )
			{
				recenterTurret( tur );
			}
			break;
		}
	}

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DeployStyleAIUpdate::crc( Xfer *xfer )
{
	// extend base class
	AIUpdateInterface::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Added support for attack move
	* 3: Added improved support for guard, and support for hunt AI
 **/
// ------------------------------------------------------------------------------------------------
void DeployStyleAIUpdate::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 4;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
	AIUpdateInterface::xfer(xfer);

	if( version >= 4 )
	{
		xfer->xferUser(&R().state, sizeof(R().state));
		xfer->xferUnsignedInt(&R().frameToWaitForDeploy);
	}
	else if( xfer->getXferMode() == XFER_LOAD )
	{
		//Good riddance!!!
		Bool obsoleteBool;
		UnsignedInt obsoleteUnsignedInt;
		ObjectID obsoleteObjectID;
		Coord3D obsoleteCoord3D;
   	AICommandParmsStorage	obsoleteAICommandParmsStorage;

		xfer->xferBool( &obsoleteBool );
		xfer->xferUnsignedInt( &obsoleteUnsignedInt );
		xfer->xferUser(&R().state, sizeof(R().state));

		if( version >= 2 )
		{
			xfer->xferObjectID( &obsoleteObjectID );
			xfer->xferObjectID( &obsoleteObjectID );
			xfer->xferCoord3D( &obsoleteCoord3D );
			xfer->xferBool( &obsoleteBool );
			xfer->xferBool( &obsoleteBool );
			xfer->xferBool( &obsoleteBool );
		}
		if( version >= 3 )
		{
			xfer->xferBool( &obsoleteBool );
			xfer->xferBool( &obsoleteBool );
		}

		obsoleteAICommandParmsStorage.doXfer( xfer );

		if( version < 2 )
		{
			obsoleteAICommandParmsStorage.doXfer(xfer);
		}

		//Initialize unit to able to move.
		R().state = READY_TO_MOVE;
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DeployStyleAIUpdate::loadPostProcess()
{
 // extend base class
	AIUpdateInterface::loadPostProcess();
}

