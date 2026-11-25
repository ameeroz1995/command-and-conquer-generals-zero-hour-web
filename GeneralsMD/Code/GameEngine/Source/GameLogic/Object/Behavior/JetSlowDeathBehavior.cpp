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

// FILE: JetSlowDeathBehavior.cpp /////////////////////////////////////////////////////////////////
// Author: Colin Day
// Desc:   Death sequence for jets
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GlobalData.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/JetSlowDeathBehavior.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"

// Web port (round 5, units are pure data): JetSlowDeathBehavior's per-unit state is a row of TheGxJetSlowDeathBehaviors;
// GXROW_JETSLOWDEATHBEHAVIOR is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxJetSlowDeathBehaviorRow> TheGxJetSlowDeathBehaviors;
#define GXROW_JETSLOWDEATHBEHAVIOR (TheGxJetSlowDeathBehaviors.at(m_gxJetSlowDeathBehaviorRow))



///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
JetSlowDeathBehaviorModuleData::JetSlowDeathBehaviorModuleData()
{

	m_fxOnGroundDeath = nullptr;
	m_oclOnGroundDeath = nullptr;

	m_fxInitialDeath = nullptr;
	m_oclInitialDeath = nullptr;

	m_delaySecondaryFromInitialDeath = 0;
	m_fxSecondary = nullptr;
	m_oclSecondary = nullptr;

	m_fxHitGround = nullptr;
	m_oclHitGround = nullptr;

	m_delayFinalBlowUpFromHitGround = 0;
	m_fxFinalBlowUp = nullptr;
	m_oclFinalBlowUp = nullptr;

	m_rollRate = 0.0f;
	m_rollRateDelta = 1.0f;
	m_pitchRate = 0.0f;
	m_fallHowFast = 0.0f;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/*static*/ void JetSlowDeathBehaviorModuleData::buildFieldParse( MultiIniFieldParse &p )
{
  SlowDeathBehaviorModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{

		{ "FXOnGroundDeath",	INI::parseFXList,	nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_fxOnGroundDeath ) },
		{ "OCLOnGroundDeath", INI::parseObjectCreationList, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_oclOnGroundDeath ) },

		{ "FXInitialDeath",	INI::parseFXList, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_fxInitialDeath ) },
		{ "OCLInitialDeath", INI::parseObjectCreationList, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_oclInitialDeath ) },

		{ "DelaySecondaryFromInitialDeath",	INI::parseDurationUnsignedInt, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_delaySecondaryFromInitialDeath ) },
		{ "FXSecondary",	INI::parseFXList, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_fxSecondary ) },
		{ "OCLSecondary", INI::parseObjectCreationList, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_oclSecondary ) },

		{ "FXHitGround", INI::parseFXList, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_fxHitGround ) },
		{ "OCLHitGround", INI::parseObjectCreationList, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_oclHitGround ) },

		{ "DelayFinalBlowUpFromHitGround", INI::parseDurationUnsignedInt, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_delayFinalBlowUpFromHitGround ) },
		{ "FXFinalBlowUp", INI::parseFXList, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_fxFinalBlowUp ) },
		{ "OCLFinalBlowUp", INI::parseObjectCreationList, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_oclFinalBlowUp ) },

		{ "DeathLoopSound", INI::parseAudioEventRTS, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_deathLoopSound ) },

// @todo srj -- RollRate and RollRateDelta and PitchRate should use parseAngularVelocityReal
		{ "RollRate",	INI::parseReal, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_rollRate ) },
		{ "RollRateDelta", INI::parsePercentToReal, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_rollRateDelta ) },
		{ "PitchRate", INI::parseReal, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_pitchRate ) },
		{ "FallHowFast", INI::parsePercentToReal, nullptr, offsetof( JetSlowDeathBehaviorModuleData, m_fallHowFast ) },

		{ nullptr, nullptr, nullptr, 0 }

	};

  p.add( dataFieldParse );

}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
JetSlowDeathBehavior::JetSlowDeathBehavior( Thing *thing, const ModuleData *moduleData ) : SlowDeathBehavior( thing, moduleData )
{
	m_gxJetSlowDeathBehaviorRow = TheGxJetSlowDeathBehaviors.alloc( getObject() );	// web port (round 5): this unit's row (zeroed, like the module was)


	GXROW_JETSLOWDEATHBEHAVIOR.timerDeathFrame = 0;
	GXROW_JETSLOWDEATHBEHAVIOR.timerOnGroundFrame = 0;
	GXROW_JETSLOWDEATHBEHAVIOR.rollRate = 0.0f;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
JetSlowDeathBehavior::~JetSlowDeathBehavior()
{

	TheGxJetSlowDeathBehaviors.release( m_gxJetSlowDeathBehaviorRow );	// web port (round 5)
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void JetSlowDeathBehavior::onDie( const DamageInfo *damageInfo )
{
	Object *us = getObject();

	// if the jet is on the ground we do just our ground fx death
	if( us->isSignificantlyAboveTerrain() == FALSE || us->getStatusBits().test( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
	{
		const JetSlowDeathBehaviorModuleData *modData = getJetSlowDeathBehaviorModuleData();

		// execute fx
		FXList::doFXObj( modData->m_fxOnGroundDeath, us );

		// execute ocl
		ObjectCreationList::create( modData->m_oclOnGroundDeath, us, nullptr );

		// destroy object
		TheGameLogic->destroyObject( us );

	}
	else
	{

		// extend base class for slow death and begin the slow death behavior
		SlowDeathBehavior::onDie( damageInfo );

	}

	getObject()->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) );

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void JetSlowDeathBehavior::beginSlowDeath( const DamageInfo *damageInfo )
{

	// extend functionality
	SlowDeathBehavior::beginSlowDeath( damageInfo );

	// get our info
	Object *us = getObject();
	const JetSlowDeathBehaviorModuleData *modData = getJetSlowDeathBehaviorModuleData();

	// record the frame we died on
	GXROW_JETSLOWDEATHBEHAVIOR.timerDeathFrame = TheGameLogic->getFrame();

	// do some effects
	FXList::doFXObj( modData->m_fxInitialDeath, us );
	ObjectCreationList::create( modData->m_oclInitialDeath, us, nullptr );

	// start audio loop playing
	GXROW_JETSLOWDEATHBEHAVIOR.deathLoopSound = modData->m_deathLoopSound;
	if( GXROW_JETSLOWDEATHBEHAVIOR.deathLoopSound.getEventName().isEmpty() == FALSE )
	{

		GXROW_JETSLOWDEATHBEHAVIOR.deathLoopSound.setObjectID( us->getID() );
		GXROW_JETSLOWDEATHBEHAVIOR.deathLoopSound.setPlayingHandle( TheAudio->addAudioEvent( &GXROW_JETSLOWDEATHBEHAVIOR.deathLoopSound ) );

	}

	// initialize our roll rate to that defined as the initial value in the module data
	GXROW_JETSLOWDEATHBEHAVIOR.rollRate = modData->m_rollRate;

	// set the locomotor so that the plane starts falling
	Locomotor *locomotor = us->getAIUpdateInterface()->getCurLocomotor();
	locomotor->setMaxLift( -TheGlobalData->m_gravity * (1.0f - modData->m_fallHowFast) );

	// do not allow the jet to turn anymore
	locomotor->setMaxTurnRate( 0.0f );

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
UpdateSleepTime JetSlowDeathBehavior::update()
{

	// extend functionality of base class
	SlowDeathBehavior::update();

	// if the death is not activated, do nothing else
	if( isSlowDeathActivated() == FALSE )
		return UPDATE_SLEEP_NONE;

	// get object info
	Object *us = getObject();
	const JetSlowDeathBehaviorModuleData *modData = getJetSlowDeathBehaviorModuleData();

	// roll us around in the air
	PhysicsBehavior *physics = us->getPhysics();
	DEBUG_ASSERTCRASH( physics, ("JetSlowDeathBehavior::beginSlowDeath - '%s' has no physics",
															us->getTemplate()->getName().str()) );
	if( physics )
		physics->setRollRate( GXROW_JETSLOWDEATHBEHAVIOR.rollRate );

	// adjust the roll rate over time
	GXROW_JETSLOWDEATHBEHAVIOR.rollRate *= modData->m_rollRateDelta;

	// do effects for death while in the air
	if( GXROW_JETSLOWDEATHBEHAVIOR.timerOnGroundFrame == 0 )
	{

		PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(us->getPosition());
		us->setLayer(layer);
		Real height;
		if (layer == LAYER_GROUND)
		{
			// (this is more efficient than getGroundHeight because the info is cached)
			height = us->getHeightAboveTerrain();
		}
		else
		{
			Real layerHeight = TheTerrainLogic->getLayerHeight( us->getPosition()->x, us->getPosition()->y, layer );
			height = us->getPosition()->z - layerHeight;
			// slop a little bit for bridges, since we tend to end up fractionally
			// above 'em, and it's easier to just slop it here
			if (height >= 0.0f && height <= 1.0f)
				height = 0.0f;
		}



		Bool hitATree = FALSE;
		// Here we want to make sure we crash if we collide with a tree on the way down
		PhysicsBehavior *phys = us->getPhysics();
		if ( GXROW_JETSLOWDEATHBEHAVIOR.timerOnGroundFrame == 0 && phys )
		{
			ObjectID treeID = phys->getLastCollidee();
			Object *tree = TheGameLogic->findObjectByID( treeID );
			if ( tree )
			{
				if (tree->isKindOf( KINDOF_SHRUBBERY ) )
				hitATree = TRUE;
			}
		}



		// when we've hit the ground, we're totally done
		if( height <= 0.0f || hitATree )
		{

			// stop the death looping sound at the right time
			TheAudio->removeAudioEvent( GXROW_JETSLOWDEATHBEHAVIOR.deathLoopSound.getPlayingHandle() );

			// do some effects
			FXList::doFXObj( modData->m_fxHitGround, us );
			ObjectCreationList::create( modData->m_oclHitGround, us, nullptr );

			// we are now on the ground
			GXROW_JETSLOWDEATHBEHAVIOR.timerOnGroundFrame = TheGameLogic->getFrame();

			// start us rolling on another axis too
			if( physics )
				physics->setPitchRate( modData->m_pitchRate );

		}

		// timers for the secondary effect
		if( GXROW_JETSLOWDEATHBEHAVIOR.timerDeathFrame != 0 &&
				TheGameLogic->getFrame() - GXROW_JETSLOWDEATHBEHAVIOR.timerDeathFrame >= modData->m_delaySecondaryFromInitialDeath )
		{

			// do some effects
			FXList::doFXObj( modData->m_fxSecondary, us );
			ObjectCreationList::create( modData->m_oclSecondary, us, nullptr );

			// clear the death frame timer since we've already executed the event now
			GXROW_JETSLOWDEATHBEHAVIOR.timerDeathFrame = 0;

		}

	}
	else
	{
		// we are on the ground, pay attention to the final explosion timers
		if( TheGameLogic->getFrame() - GXROW_JETSLOWDEATHBEHAVIOR.timerOnGroundFrame >= modData->m_delayFinalBlowUpFromHitGround )
		{

			// do some effects
			FXList::doFXObj( modData->m_fxFinalBlowUp, us );
			ObjectCreationList::create( modData->m_oclFinalBlowUp, us, nullptr );

			// we're all done now
			TheGameLogic->destroyObject( us );

		}

	}

	return UPDATE_SLEEP_NONE;

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void JetSlowDeathBehavior::crc( Xfer *xfer )
{

	// extend base class
	SlowDeathBehavior::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void JetSlowDeathBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	SlowDeathBehavior::xfer( xfer );

	// timer death frame
	xfer->xferUnsignedInt( &GXROW_JETSLOWDEATHBEHAVIOR.timerDeathFrame );

	// on ground frame
	xfer->xferUnsignedInt( &GXROW_JETSLOWDEATHBEHAVIOR.timerOnGroundFrame );

	// roll rate
	xfer->xferReal( &GXROW_JETSLOWDEATHBEHAVIOR.rollRate );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void JetSlowDeathBehavior::loadPostProcess()
{

	// extend base class
	SlowDeathBehavior::loadPostProcess();

}
