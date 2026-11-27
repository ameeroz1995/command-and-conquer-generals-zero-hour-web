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

// FILE: BattlePlanUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Author: Kris Morness, September 2002
// Desc:   Update module to handle building states and battle plan execution & changes
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_MAXHEALTHCHANGETYPE_NAMES						// for TheMaxHealthChangeTypeNames[]

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/BitFlagsIO.h"
#include "Common/Radar.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Player.h"
#include "Common/Xfer.h"

#include "GameClient/GameClient.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/FXList.h"
#include "GameClient/ControlBar.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/BattlePlanUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/StealthDetectorUpdate.h"



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
BattlePlanUpdateModuleData::BattlePlanUpdateModuleData()
{
	m_specialPowerTemplate								= nullptr;
	m_bombardmentPlanAnimationFrames			= 0;
	m_holdTheLinePlanAnimationFrames			= 0;
	m_searchAndDestroyPlanAnimationFrames = 0;
	m_battlePlanParalyzeFrames						= 0;


	m_holdTheLineArmorDamageScalar				= 1.0f;
	m_searchAndDestroySightRangeScalar		= 1.0f;
	m_strategyCenterSearchAndDestroySightRangeScalar = 1.0f;
	m_strategyCenterSearchAndDestroyDetectsStealth = true;
	m_strategyCenterHoldTheLineMaxHealthScalar = 1.0f;
	m_strategyCenterHoldTheLineMaxHealthChangeType = PRESERVE_RATIO;

}

//-------------------------------------------------------------------------------------------------
/*static*/ void BattlePlanUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "SpecialPowerTemplate",									INI::parseSpecialPowerTemplate,	nullptr, offsetof( BattlePlanUpdateModuleData, m_specialPowerTemplate ) },

    { "BombardmentPlanAnimationTime",					INI::parseDurationUnsignedInt,  nullptr, offsetof( BattlePlanUpdateModuleData, m_bombardmentPlanAnimationFrames ) },
    { "HoldTheLinePlanAnimationTime",					INI::parseDurationUnsignedInt,  nullptr, offsetof( BattlePlanUpdateModuleData, m_holdTheLinePlanAnimationFrames ) },
    { "SearchAndDestroyPlanAnimationTime",		INI::parseDurationUnsignedInt,  nullptr, offsetof( BattlePlanUpdateModuleData, m_searchAndDestroyPlanAnimationFrames ) },
		{ "TransitionIdleTime",										INI::parseDurationUnsignedInt,  nullptr, offsetof( BattlePlanUpdateModuleData, m_transitionIdleFrames ) },

		{ "BombardmentPlanUnpackSoundName",				INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_bombardmentUnpackName ) },
		{ "BombardmentPlanPackSoundName",					INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_bombardmentPackName ) },
		{ "BombardmentMessageLabel",							INI::parseAsciiString,					nullptr,	offsetof( BattlePlanUpdateModuleData, m_bombardmentMessageLabel ) },
		{ "BombardmentAnnouncementName",					INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_bombardmentAnnouncementName ) },
		{ "SearchAndDestroyPlanUnpackSoundName",	INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_searchAndDestroyUnpackName ) },
		{ "SearchAndDestroyPlanIdleLoopSoundName",INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_searchAndDestroyIdleName ) },
		{ "SearchAndDestroyPlanPackSoundName",		INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_searchAndDestroyPackName ) },
		{ "SearchAndDestroyMessageLabel",					INI::parseAsciiString,					nullptr,	offsetof( BattlePlanUpdateModuleData, m_searchAndDestroyMessageLabel ) },
		{ "SearchAndDestroyAnnouncementName",			INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_searchAndDestroyAnnouncementName ) },
		{ "HoldTheLinePlanUnpackSoundName",				INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_holdTheLineUnpackName ) },
		{ "HoldTheLinePlanPackSoundName",					INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_holdTheLinePackName ) },
		{ "HoldTheLineMessageLabel",							INI::parseAsciiString,					nullptr,	offsetof( BattlePlanUpdateModuleData, m_holdTheLineMessageLabel ) },
		{ "HoldTheLineAnnouncementName",					INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_holdTheLineAnnouncementName ) },

		{ "ValidMemberKindOf",										KindOfMaskType::parseFromINI,								nullptr, offsetof( BattlePlanUpdateModuleData, m_validMemberKindOf ) },
		{ "InvalidMemberKindOf",									KindOfMaskType::parseFromINI,								nullptr, offsetof( BattlePlanUpdateModuleData, m_invalidMemberKindOf ) },
		{ "BattlePlanChangeParalyzeTime",					INI::parseDurationUnsignedInt,  nullptr, offsetof( BattlePlanUpdateModuleData, m_battlePlanParalyzeFrames ) },
		{ "HoldTheLinePlanArmorDamageScalar",			INI::parseReal,									nullptr, offsetof( BattlePlanUpdateModuleData, m_holdTheLineArmorDamageScalar ) },
		{ "SearchAndDestroyPlanSightRangeScalar",	INI::parseReal,									nullptr, offsetof( BattlePlanUpdateModuleData, m_searchAndDestroySightRangeScalar ) },

		{ "StrategyCenterSearchAndDestroySightRangeScalar", INI::parseReal,				nullptr, offsetof( BattlePlanUpdateModuleData, m_strategyCenterSearchAndDestroySightRangeScalar ) },
		{ "StrategyCenterSearchAndDestroyDetectsStealth",   INI::parseBool,				nullptr, offsetof( BattlePlanUpdateModuleData, m_strategyCenterSearchAndDestroyDetectsStealth ) },
		{ "StrategyCenterHoldTheLineMaxHealthScalar",				INI::parseReal,				nullptr, offsetof( BattlePlanUpdateModuleData, m_strategyCenterHoldTheLineMaxHealthScalar ) },
    { "StrategyCenterHoldTheLineMaxHealthChangeType",		INI::parseIndexList,  TheMaxHealthChangeTypeNames, offsetof( BattlePlanUpdateModuleData, m_strategyCenterHoldTheLineMaxHealthChangeType ) },

		{ "VisionObjectName",											INI::parseAsciiString,					nullptr, offsetof( BattlePlanUpdateModuleData, m_visionObjectName ) },

		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
BattlePlanUpdate::BattlePlanUpdate( Thing *thing, const ModuleData* moduleData ) : SpecialPowerUpdateModule( thing, moduleData )
{
	D().m_bonuses = nullptr;

	const BattlePlanUpdateModuleData *data = getBattlePlanUpdateModuleData();

	D().m_status								= TRANSITIONSTATUS_IDLE;
	D().m_currentPlan						= PLANSTATUS_NONE;
	D().m_desiredPlan						= PLANSTATUS_NONE;
	D().m_planAffectingArmy			= PLANSTATUS_NONE;
	D().m_nextReadyFrame				= 0;
	D().m_invalidSettings				= false;
	D().m_centeringTurret				= false;

	D().m_bonuses = newInstance(BattlePlanBonuses);
	D().m_bonuses->m_validKindOf = data->m_validMemberKindOf;
	D().m_bonuses->m_invalidKindOf = data->m_invalidMemberKindOf;

	D().m_visionObjectID = INVALID_ID;

	D().m_specialPowerModule   = nullptr;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
BattlePlanUpdate::~BattlePlanUpdate()
{
	TheAudio->removeAudioEvent( D().m_bombardmentUnpack.getPlayingHandle() );
	TheAudio->removeAudioEvent( D().m_bombardmentPack.getPlayingHandle() );
	TheAudio->removeAudioEvent( D().m_searchAndDestroyUnpack.getPlayingHandle() );
	TheAudio->removeAudioEvent( D().m_searchAndDestroyIdle.getPlayingHandle() );
	TheAudio->removeAudioEvent( D().m_searchAndDestroyPack.getPlayingHandle() );
	TheAudio->removeAudioEvent( D().m_holdTheLineUnpack.getPlayingHandle() );
	TheAudio->removeAudioEvent( D().m_holdTheLinePack.getPlayingHandle() );

}

// ------------------------------------------------------------------------------------------------
/** On delete */
// ------------------------------------------------------------------------------------------------
void BattlePlanUpdate::onDelete()
{

	// extend base class
	UpdateModule::onDelete();

	// delete our vision object, if it exists
	Object *obj;
	if( D().m_visionObjectID != INVALID_ID )
	{
		obj = TheGameLogic->findObjectByID( D().m_visionObjectID );
		if( obj )
			TheGameLogic->destroyObject( obj );
	}

	// If we get destroyed, then make sure we remove our bonus!
	// srj sez: we can't do this in the dtor because our team
	// (and thus controlling player) has already been nulled by then...
	Player* player = getObject()->getControllingPlayer();
	// however, player CAN legitimately be null during game reset cycles
	// (and which point it doesn't really matter if we can remove the bonus or not)
	//DEBUG_ASSERTCRASH(player != nullptr, ("Hmm, controller is null"));
	if( player && D().m_planAffectingArmy != PLANSTATUS_NONE )
	{
		player->changeBattlePlan( D().m_planAffectingArmy, -1, D().m_bonuses );
	}

}

//-------------------------------------------------------------------------------------------------
// Validate that we have the necessary data from the ini file.
//-------------------------------------------------------------------------------------------------
void BattlePlanUpdate::onObjectCreated()
{
	const BattlePlanUpdateModuleData *data = getBattlePlanUpdateModuleData();
	Object *obj = getObject();

	if( !data->m_specialPowerTemplate )
	{
		DEBUG_CRASH( ("%s object's BattlePlanUpdate lacks access to the SpecialPowerTemplate. Needs to be specified in ini.", obj->getTemplate()->getName().str() ) );
		D().m_invalidSettings = true;
		return;
	}

	D().m_specialPowerModule = obj->getSpecialPowerModule( data->m_specialPowerTemplate );

	//Create instances of the sounds required.
	D().m_bombardmentUnpack.setEventName( data->m_bombardmentUnpackName );
	D().m_bombardmentPack.setEventName(	data->m_bombardmentPackName );
	D().m_bombardmentAnnouncement.setEventName( data->m_bombardmentAnnouncementName );
	D().m_searchAndDestroyUnpack.setEventName( data->m_searchAndDestroyUnpackName );
	D().m_searchAndDestroyIdle.setEventName( data->m_searchAndDestroyIdleName );
	D().m_searchAndDestroyPack.setEventName( data->m_searchAndDestroyPackName );
	D().m_searchAndDestroyAnnouncement.setEventName( data->m_searchAndDestroyAnnouncementName );
	D().m_holdTheLineUnpack.setEventName( data->m_holdTheLineUnpackName );
	D().m_holdTheLinePack.setEventName(	data->m_holdTheLinePackName );
	D().m_holdTheLineAnnouncement.setEventName( data->m_holdTheLineAnnouncementName );
	TheAudio->getInfoForAudioEvent( &D().m_bombardmentUnpack );
	TheAudio->getInfoForAudioEvent( &D().m_bombardmentPack );
	TheAudio->getInfoForAudioEvent( &D().m_bombardmentAnnouncement );
	TheAudio->getInfoForAudioEvent( &D().m_searchAndDestroyUnpack );
	TheAudio->getInfoForAudioEvent( &D().m_searchAndDestroyIdle );
	TheAudio->getInfoForAudioEvent( &D().m_searchAndDestroyPack );
	TheAudio->getInfoForAudioEvent( &D().m_searchAndDestroyAnnouncement );
	TheAudio->getInfoForAudioEvent( &D().m_holdTheLineUnpack );
	TheAudio->getInfoForAudioEvent( &D().m_holdTheLinePack );
	TheAudio->getInfoForAudioEvent( &D().m_holdTheLineAnnouncement );

	getObject()->setWeaponSetFlag( WEAPONSET_VETERAN );
	AIUpdateInterface *ai = obj->getAI();
	if( ai )
	{
		// lock it just till the weapon is empty or the attack is "done"
		obj->setWeaponLock( PRIMARY_WEAPON, LOCKED_TEMPORARILY );
	}
	enableTurret( false );
}

//-------------------------------------------------------------------------------------------------
Bool BattlePlanUpdate::initiateIntentToDoSpecialPower(const SpecialPowerTemplate *specialPowerTemplate, const Object *targetObj, const Coord3D *targetPos, const Waypoint *way, UnsignedInt commandOptions )
{
	if( D().m_specialPowerModule->getSpecialPowerTemplate() != specialPowerTemplate )
	{
		//Check to make sure our modules are connected.
		return FALSE;
	}

	//Set the desired status based on the command button option!
	if( BitIsSet( commandOptions, OPTION_ONE ) )
	{
		D().m_desiredPlan = PLANSTATUS_BOMBARDMENT;
	}
	else if( BitIsSet( commandOptions, OPTION_TWO ) )
	{
		D().m_desiredPlan = PLANSTATUS_HOLDTHELINE;
	}
	else if( BitIsSet( commandOptions, OPTION_THREE ) )
	{
		D().m_desiredPlan = PLANSTATUS_SEARCHANDDESTROY;
	}
	else
	{
		DEBUG_CRASH( ("Selected an unsupported strategy for strategy center.") );
		return FALSE;
	}

	getObject()->getControllingPlayer()->getAcademyStats()->recordBattlePlanSelected();
	return TRUE;
}

Bool BattlePlanUpdate::isPowerCurrentlyInUse( const CommandButton *command ) const
{
	//@todo -- perhaps we may need this one day...
	return false;
}

//-------------------------------------------------------------------------------------------------
CommandOption BattlePlanUpdate::getCommandOption() const
{
	switch( D().m_desiredPlan )
	{
		case PLANSTATUS_BOMBARDMENT:
			return OPTION_ONE;
		case PLANSTATUS_HOLDTHELINE:
			return OPTION_TWO;
		case PLANSTATUS_SEARCHANDDESTROY:
			return OPTION_THREE;
	}
	return (CommandOption)0;
}

//-------------------------------------------------------------------------------------------------
/** The update callback. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime BattlePlanUpdate::update()
{

	if( D().m_invalidSettings )
	{
		// can't return UPDATE_SLEEP_FOREVER unless we are sleepy...
		return UPDATE_SLEEP_NONE;
		///return UPDATE_SLEEP_FOREVER;
	}

	//const BattlePlanUpdateModuleData *data = getBattlePlanUpdateModuleData();
	//Object *obj = getObject();

	UnsignedInt now = TheGameLogic->getFrame();

	if( D().m_nextReadyFrame <= now )
	{
		switch( D().m_status )
		{
			case TRANSITIONSTATUS_IDLE:
				//There's only two cases where we are in an idle status -- upon initialization
				//when no plan has yet been selected. The other case is after we've finished
				//packing the previous plan up and are waiting to unpack the new state.
				if( D().m_desiredPlan != PLANSTATUS_NONE )
				{
					D().m_currentPlan = D().m_desiredPlan;
					setStatus( TRANSITIONSTATUS_UNPACKING );
				}
				break;
			case TRANSITIONSTATUS_UNPACKING:
				//If we're unpacking, we are forcing the user to wait until the plan is unpacked
				//before allowing him to select a new plan. The plan doesn't become active until
				//we're finished unpacking.
				setStatus( TRANSITIONSTATUS_ACTIVE );
				if( D().m_currentPlan == PLANSTATUS_BOMBARDMENT )
				{
					enableTurret( true );
				}
				break;
			case TRANSITIONSTATUS_ACTIVE:
				//If we're active and the user has selected a different plan, then we need to
				//pack up.
				if( D().m_currentPlan != D().m_desiredPlan )
				{
					if( D().m_currentPlan == PLANSTATUS_BOMBARDMENT )
					{
						//Special case situation -- in bombardment status, we need to center
						//the turret prior to packing up, so handle it here.
						AIUpdateInterface *ai = getObject()->getAI();
						if( ai )
						{
							if( isTurretInNaturalPosition() )
							{
								//It's centered, so pack
								setStatus( TRANSITIONSTATUS_PACKING );
								D().m_centeringTurret = false;
								enableTurret( false );
							}
							else if( !D().m_centeringTurret )
							{
								//It's not centered, and not trying to center, so order it to center.
								ai->aiIdle( CMD_FROM_AI );
#if !RETAIL_COMPATIBLE_CRC
								// TheSuperHackers @bugfix Stubbjax 19/02/2026 Prevent using the turret while it recenters.
								enableTurret(false);
#endif
								recenterTurret();
								D().m_centeringTurret = true;
							}
						}
					}
					else
					{
						setStatus( TRANSITIONSTATUS_PACKING );
					}
				}
				break;
			case TRANSITIONSTATUS_PACKING:
				//If we finished packing, then go idle until we can switch to our new plan.
				setStatus( TRANSITIONSTATUS_IDLE );
				break;
		}
	}

	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
/** Create vision objects for all players revealing this building to all */
// ------------------------------------------------------------------------------------------------
void BattlePlanUpdate::createVisionObject()
{
	if (D().m_visionObjectID != INVALID_ID) // don't want two.
		return;

	const BattlePlanUpdateModuleData *data = getBattlePlanUpdateModuleData();
	Object *obj = getObject();

	// get template of object to create
	const ThingTemplate *tt = TheThingFactory->findTemplate( data->m_visionObjectName );
	DEBUG_ASSERTCRASH( tt, ("BattlePlanUpdate::setStatus - Invalid vision object name '%s'",
													data->m_visionObjectName.str()) );

	if (!tt)
		return;

	Player *pPlayer = ThePlayerList->getNeutralPlayer();
	// sanity
	if(!pPlayer)
		return;

	Object *visionObject;

	// create object for this player
	visionObject = TheThingFactory->newObject( tt, pPlayer->getDefaultTeam() );
	if( visionObject )
	{

		// record we have an object
		D().m_visionObjectID = visionObject->getID();

		// set position
		visionObject->setPosition( obj->getPosition() );

		// set the shroud clearing range
		visionObject->setShroudClearingRange( obj->getGeometryInfo().getBoundingSphereRadius() );

	}

}

//-------------------------------------------------------------------------------------------------
void BattlePlanUpdate::setStatus( TransitionStatus newStatus )
{
	const BattlePlanUpdateModuleData *data = getBattlePlanUpdateModuleData();
	Object *obj = getObject();

	if( D().m_status == newStatus )
	{
		return;
	}

	TransitionStatus oldStatus = D().m_status;

	//Turn off old defining states and sounds
	switch( oldStatus )
	{
		case TRANSITIONSTATUS_IDLE:
			break;

		case TRANSITIONSTATUS_UNPACKING:
			switch( D().m_currentPlan )
			{
				case PLANSTATUS_BOMBARDMENT:
					obj->clearModelConditionState( MODELCONDITION_DOOR_1_OPENING );
					TheAudio->removeAudioEvent( D().m_bombardmentUnpack.getPlayingHandle() );
					break;
				case PLANSTATUS_HOLDTHELINE:
					obj->clearModelConditionState( MODELCONDITION_DOOR_2_OPENING );
					TheAudio->removeAudioEvent( D().m_holdTheLineUnpack.getPlayingHandle() );
					break;
				case PLANSTATUS_SEARCHANDDESTROY:
					obj->clearModelConditionState( MODELCONDITION_DOOR_3_OPENING );
					TheAudio->removeAudioEvent( D().m_searchAndDestroyUnpack.getPlayingHandle() );
					break;
			}
			break;

		case TRANSITIONSTATUS_ACTIVE:
			switch( D().m_currentPlan )
			{
				case PLANSTATUS_BOMBARDMENT:
					obj->clearModelConditionState( MODELCONDITION_DOOR_1_WAITING_TO_CLOSE );
					break;
				case PLANSTATUS_HOLDTHELINE:
					obj->clearModelConditionState( MODELCONDITION_DOOR_2_WAITING_TO_CLOSE );
					break;
				case PLANSTATUS_SEARCHANDDESTROY:
					obj->clearModelConditionState( MODELCONDITION_DOOR_3_WAITING_TO_CLOSE );
					TheAudio->removeAudioEvent( D().m_searchAndDestroyIdle.getPlayingHandle() );
					break;
			}
			break;

		case TRANSITIONSTATUS_PACKING:
			switch( D().m_currentPlan )
			{
				case PLANSTATUS_BOMBARDMENT:
					obj->clearModelConditionState( MODELCONDITION_DOOR_1_CLOSING );
					TheAudio->removeAudioEvent( D().m_bombardmentPack.getPlayingHandle() );
					break;
				case PLANSTATUS_HOLDTHELINE:
					obj->clearModelConditionState( MODELCONDITION_DOOR_2_CLOSING );
					TheAudio->removeAudioEvent( D().m_holdTheLinePack.getPlayingHandle() );
					break;
				case PLANSTATUS_SEARCHANDDESTROY:
					obj->clearModelConditionState( MODELCONDITION_DOOR_3_CLOSING );
					TheAudio->removeAudioEvent( D().m_searchAndDestroyPack.getPlayingHandle() );
					break;
			}
			break;
	}

	UnsignedInt now = TheGameLogic->getFrame();

	//Handle entering new status
	switch( newStatus )
	{
		case TRANSITIONSTATUS_IDLE:
		{
			D().m_currentPlan = PLANSTATUS_NONE;
			D().m_nextReadyFrame = now + data->m_transitionIdleFrames;
			break;
		}

		case TRANSITIONSTATUS_UNPACKING:
		{

			// play a radar blip showing the battle plan change in the color of the
			TheRadar->createPlayerEvent( obj->getControllingPlayer(),
																	 obj->getPosition(),
																	 RADAR_EVENT_BATTLE_PLAN );

			//this is now handled with ShroudRevealToAllRange in thingTemplate
			//createVisionObject();

			switch( D().m_currentPlan )
			{
				case PLANSTATUS_BOMBARDMENT:
					obj->setModelConditionState( MODELCONDITION_DOOR_1_OPENING );
					obj->getDrawable()->setAnimationLoopDuration( data->m_bombardmentPlanAnimationFrames );
					D().m_nextReadyFrame = now + data->m_bombardmentPlanAnimationFrames;
					if( D().m_bombardmentUnpack.getEventName().isNotEmpty() )
					{
						D().m_bombardmentUnpack.setObjectID( obj->getID() );
						D().m_bombardmentUnpack.setPlayingHandle( TheAudio->addAudioEvent( &D().m_bombardmentUnpack ) );
					}

					// display a message to *all* users
					TheInGameUI->message( TheGameText->fetch( data->m_bombardmentMessageLabel ) );
					if( D().m_bombardmentAnnouncement.getEventName().isEmpty() == FALSE )
					{
						D().m_bombardmentAnnouncement.setPosition( obj->getPosition() );
						TheAudio->addAudioEvent( &D().m_bombardmentAnnouncement );
					}
					break;

				case PLANSTATUS_HOLDTHELINE:
					obj->setModelConditionState( MODELCONDITION_DOOR_2_OPENING );
					obj->getDrawable()->setAnimationLoopDuration( data->m_holdTheLinePlanAnimationFrames );
					D().m_nextReadyFrame = now + data->m_holdTheLinePlanAnimationFrames;
					if( D().m_holdTheLineUnpack.getEventName().isNotEmpty() )
					{
						D().m_holdTheLineUnpack.setObjectID( obj->getID() );
						D().m_holdTheLineUnpack.setPlayingHandle( TheAudio->addAudioEvent( &D().m_holdTheLineUnpack ) );
					}

					// display a message to *all* users
					TheInGameUI->message( TheGameText->fetch( data->m_holdTheLineMessageLabel ) );
					if( D().m_holdTheLineAnnouncement.getEventName().isEmpty() == FALSE )
					{
						D().m_holdTheLineAnnouncement.setPosition( obj->getPosition() );
						TheAudio->addAudioEvent( &D().m_holdTheLineAnnouncement );
					}
					break;

				case PLANSTATUS_SEARCHANDDESTROY:
					obj->setModelConditionState( MODELCONDITION_DOOR_3_OPENING );
					obj->getDrawable()->setAnimationLoopDuration( data->m_searchAndDestroyPlanAnimationFrames );
					D().m_nextReadyFrame = now + data->m_searchAndDestroyPlanAnimationFrames;
					if( D().m_searchAndDestroyUnpack.getEventName().isNotEmpty() )
					{
						D().m_searchAndDestroyUnpack.setObjectID( obj->getID() );
						D().m_searchAndDestroyUnpack.setPlayingHandle( TheAudio->addAudioEvent( &D().m_searchAndDestroyUnpack ) );
					}

					// display a message to *all* users
					TheInGameUI->message( TheGameText->fetch( data->m_searchAndDestroyMessageLabel ) );
					if( D().m_searchAndDestroyAnnouncement.getEventName().isEmpty() == FALSE )
					{
						D().m_searchAndDestroyAnnouncement.setPosition( obj->getPosition() );
						TheAudio->addAudioEvent( &D().m_searchAndDestroyAnnouncement );
					}
					break;

			}

			break;

		}

		case TRANSITIONSTATUS_ACTIVE:

			setBattlePlan( D().m_currentPlan );

			switch( D().m_currentPlan )
			{
				case PLANSTATUS_BOMBARDMENT:
					obj->setModelConditionState( MODELCONDITION_DOOR_1_WAITING_TO_CLOSE );
					break;
				case PLANSTATUS_HOLDTHELINE:
					obj->setModelConditionState( MODELCONDITION_DOOR_2_WAITING_TO_CLOSE );
					break;
				case PLANSTATUS_SEARCHANDDESTROY:
					obj->setModelConditionState( MODELCONDITION_DOOR_3_WAITING_TO_CLOSE );
					if( D().m_searchAndDestroyIdle.getEventName().isNotEmpty() )
					{
						D().m_searchAndDestroyIdle.setObjectID( obj->getID() );
						D().m_searchAndDestroyIdle.setPlayingHandle( TheAudio->addAudioEvent( &D().m_searchAndDestroyIdle ) );
					}
					break;
			}
			break;

		case TRANSITIONSTATUS_PACKING:

			setBattlePlan( PLANSTATUS_NONE );

			switch( D().m_currentPlan )
			{
				case PLANSTATUS_BOMBARDMENT:
					obj->setModelConditionState( MODELCONDITION_DOOR_1_CLOSING );
					obj->getDrawable()->setAnimationLoopDuration( data->m_bombardmentPlanAnimationFrames );
					D().m_nextReadyFrame = now + data->m_bombardmentPlanAnimationFrames;
					if( D().m_bombardmentUnpack.getEventName().isNotEmpty() )
					{
						D().m_bombardmentPack.setObjectID( obj->getID() );
						D().m_bombardmentPack.setPlayingHandle( TheAudio->addAudioEvent( &D().m_bombardmentPack ) );
					}
					break;
				case PLANSTATUS_HOLDTHELINE:
					obj->setModelConditionState( MODELCONDITION_DOOR_2_CLOSING );
					obj->getDrawable()->setAnimationLoopDuration( data->m_holdTheLinePlanAnimationFrames );
					D().m_nextReadyFrame = now + data->m_holdTheLinePlanAnimationFrames;
					if( D().m_holdTheLineUnpack.getEventName().isNotEmpty() )
					{
						D().m_holdTheLinePack.setObjectID( obj->getID() );
						D().m_holdTheLinePack.setPlayingHandle( TheAudio->addAudioEvent( &D().m_holdTheLinePack ) );
					}
					break;
				case PLANSTATUS_SEARCHANDDESTROY:
					obj->setModelConditionState( MODELCONDITION_DOOR_3_CLOSING );
					obj->getDrawable()->setAnimationLoopDuration( data->m_searchAndDestroyPlanAnimationFrames );
					D().m_nextReadyFrame = now + data->m_searchAndDestroyPlanAnimationFrames;
					if( D().m_searchAndDestroyUnpack.getEventName().isNotEmpty() )
					{
						D().m_searchAndDestroyPack.setObjectID( obj->getID() );
						D().m_searchAndDestroyPack.setPlayingHandle( TheAudio->addAudioEvent( &D().m_searchAndDestroyPack ) );
					}
					break;
			}
			break;
	}

	//Set new status.
	D().m_status = newStatus;
}

//------------------------------------------------------------------------------------------------
void BattlePlanUpdate::enableTurret( Bool enable )
{
	AIUpdateInterface *ai = getObject()->getAI();
	if( ai )
	{
		WhichTurretType tur = ai->getWhichTurretForCurWeapon();
		if( tur != TURRET_INVALID )
		{
			ai->setTurretEnabled( tur, enable );
		}
	}
}

//------------------------------------------------------------------------------------------------
void BattlePlanUpdate::recenterTurret()
{
	AIUpdateInterface *ai = getObject()->getAI();
	if( ai )
	{
		WhichTurretType tur = ai->getWhichTurretForCurWeapon();
		if( tur != TURRET_INVALID )
		{
			ai->recenterTurret( tur );
		}
	}
}

//------------------------------------------------------------------------------------------------
Bool BattlePlanUpdate::isTurretInNaturalPosition()
{
	AIUpdateInterface *ai = getObject()->getAI();
	if( ai )
	{
		WhichTurretType tur = ai->getWhichTurretForCurWeapon();
		if( tur != TURRET_INVALID )
		{
			return ai->isTurretInNaturalPosition( tur );
		}
	}
	return false;
}

//------------------------------------------------------------------------------------------------
static void paralyzeTroop( Object *obj, void *userData )
{
	const BattlePlanUpdateModuleData *data = (BattlePlanUpdateModuleData*)userData;
	if( obj->isAnyKindOf( data->m_validMemberKindOf ) )
	{
		if( !obj->isAnyKindOf( data->m_invalidMemberKindOf ) )
		{
			obj->setDisabledUntil( DISABLED_PARALYZED, TheGameLogic->getFrame() + data->m_battlePlanParalyzeFrames );
		}
	}
}

//------------------------------------------------------------------------------------------------
void BattlePlanUpdate::setBattlePlan( BattlePlanStatus plan )
{
	const BattlePlanUpdateModuleData *data = getBattlePlanUpdateModuleData();
	Object *obj = getObject();
	Player *player = obj->getControllingPlayer();
	if( player )
	{
		switch( D().m_planAffectingArmy )
		{
			case PLANSTATUS_BOMBARDMENT:
			{
				//Remove the previous plan!
				player->changeBattlePlan( PLANSTATUS_BOMBARDMENT, -1, D().m_bonuses );

				//The only building bonus is actually the turret, and that's already handled!
				break;
			}
			case PLANSTATUS_HOLDTHELINE:
			{
				//Remove the previous plan!
				player->changeBattlePlan( PLANSTATUS_HOLDTHELINE, -1, D().m_bonuses );

				//Remove building health bonuses
				if( data->m_strategyCenterHoldTheLineMaxHealthScalar != 1.0f )
				{
					BodyModuleInterface *body = obj->getBodyModule();
					body->setMaxHealth( body->getMaxHealth() * 1.0f / data->m_strategyCenterHoldTheLineMaxHealthScalar, data->m_strategyCenterHoldTheLineMaxHealthChangeType );
				}
				break;
			}
			case PLANSTATUS_SEARCHANDDESTROY:
			{
				//Remove the previous plan!
				player->changeBattlePlan( PLANSTATUS_SEARCHANDDESTROY, -1, D().m_bonuses );

				//Remove sight range bonus
				if( data->m_strategyCenterSearchAndDestroySightRangeScalar != 1.0f )
				{
					obj->setVisionRange( obj->getVisionRange() * 1.0f / data->m_strategyCenterSearchAndDestroySightRangeScalar );
					obj->setShroudClearingRange( obj->getShroudClearingRange() * 1.0f / data->m_strategyCenterSearchAndDestroySightRangeScalar );
				}

				//Remove stealth detection
				if( data->m_strategyCenterSearchAndDestroyDetectsStealth )
				{
					static NameKeyType key_StealthDetectorUpdate = NAMEKEY( "StealthDetectorUpdate" );
					StealthDetectorUpdate *update = (StealthDetectorUpdate*)obj->findUpdateModule( key_StealthDetectorUpdate );
					if( update )
					{
						update->setSDEnabled( false );
					}
				}

				break;
			}
		}

		//Revert to default no-bonuses!
		D().m_bonuses->m_armorScalar					= 1.0f;
		D().m_bonuses->m_sightRangeScalar		= 1.0f;
		D().m_bonuses->m_bombardment					= 0;
		D().m_bonuses->m_searchAndDestroy		= 0;
		D().m_bonuses->m_holdTheLine					= 0;

		//Add new bonuses!
		switch( plan )
		{
			case PLANSTATUS_NONE:
				//Paralyze troops!
				player->iterateObjects( paralyzeTroop, (void*)data );
				break;

			case PLANSTATUS_BOMBARDMENT:
				//Set the bombardment bonuses
				D().m_bonuses->m_bombardment	= 1; //for weapon bonuses

				//Add the new plan!
				player->changeBattlePlan( PLANSTATUS_BOMBARDMENT, 1, D().m_bonuses );
				break;

			case PLANSTATUS_HOLDTHELINE:
				//Add building health bonuses
				if( data->m_strategyCenterHoldTheLineMaxHealthScalar )
				{
					BodyModuleInterface *body = obj->getBodyModule();
					body->setMaxHealth( body->getMaxHealth() * data->m_strategyCenterHoldTheLineMaxHealthScalar, data->m_strategyCenterHoldTheLineMaxHealthChangeType );
				}

				//Set the hold-the-line bonuses
				D().m_bonuses->m_armorScalar = data->m_holdTheLineArmorDamageScalar;
				D().m_bonuses->m_holdTheLine	= 1; //for weapon bonuses

				//Add the new plan!
				player->changeBattlePlan( PLANSTATUS_HOLDTHELINE, 1, D().m_bonuses );
				break;
			case PLANSTATUS_SEARCHANDDESTROY:
				//Add sight range bonus
				if( data->m_strategyCenterSearchAndDestroySightRangeScalar != 1.0f )
				{
					obj->setVisionRange( obj->getVisionRange() * data->m_strategyCenterSearchAndDestroySightRangeScalar );
					obj->setShroudClearingRange( obj->getShroudClearingRange() * data->m_strategyCenterSearchAndDestroySightRangeScalar );
				}

				//Enable stealth detection
				if( data->m_strategyCenterSearchAndDestroyDetectsStealth )
				{
					static NameKeyType key_StealthDetectorUpdate = NAMEKEY( "StealthDetectorUpdate" );
					StealthDetectorUpdate *update = (StealthDetectorUpdate*)obj->findUpdateModule( key_StealthDetectorUpdate );
					if( update )
					{
						update->setSDEnabled( true );
					}
				}

				//Set the search-and-destroy bonuses
				D().m_bonuses->m_searchAndDestroy	= 1; //for weapon bonuses
				D().m_bonuses->m_sightRangeScalar	= data->m_searchAndDestroySightRangeScalar;

				//Add the new plan!
				player->changeBattlePlan( PLANSTATUS_SEARCHANDDESTROY, 1, D().m_bonuses );
				break;
		}
		D().m_planAffectingArmy = plan;
	}
}

//------------------------------------------------------------------------------------------------
//Returns the currently active battle plan -- unpacked and ready... returns PLANSTATUS_NONE if in
//transition!
//------------------------------------------------------------------------------------------------
BattlePlanStatus BattlePlanUpdate::getActiveBattlePlan() const
{
	if( D().m_status == TRANSITIONSTATUS_ACTIVE )
	{
		return D().m_planAffectingArmy;
	}
	return PLANSTATUS_NONE;
}

//------------------------------------------------------------------------------------------------
void BattlePlanUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

//------------------------------------------------------------------------------------------------
// Xfer method
//	Version Info:
//	1: Initial version
//------------------------------------------------------------------------------------------------
void BattlePlanUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// current plan
	xfer->xferUser( &D().m_currentPlan, sizeof( BattlePlanStatus ) );

	// desired plan
	xfer->xferUser( &D().m_desiredPlan, sizeof( BattlePlanStatus ) );

	// plan affecting army
	xfer->xferUser( &D().m_planAffectingArmy, sizeof( BattlePlanStatus ) );

	// status
	xfer->xferUser( &D().m_status, sizeof( TransitionStatus ) );

	// next ready frame
	xfer->xferUnsignedInt( &D().m_nextReadyFrame );

	// don't need to save this interface, it's retrieved on object creation
	// SpecialPowerModuleInterface *D().m_specialPowerModule;

	// invalid settings
	xfer->xferBool( &D().m_invalidSettings );

	// centering turret
	xfer->xferBool( &D().m_centeringTurret );

	// bonuses
	xfer->xferReal( &D().m_bonuses->m_armorScalar );
	xfer->xferInt( &D().m_bonuses->m_bombardment );
	xfer->xferInt( &D().m_bonuses->m_searchAndDestroy );
	xfer->xferInt( &D().m_bonuses->m_holdTheLine );
	xfer->xferReal( &D().m_bonuses->m_sightRangeScalar );
	D().m_bonuses->m_validKindOf.xfer( xfer );
	D().m_bonuses->m_invalidKindOf.xfer( xfer );

	// vision object data
	xfer->xferObjectID( &D().m_visionObjectID );

}

//------------------------------------------------------------------------------------------------
void BattlePlanUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
