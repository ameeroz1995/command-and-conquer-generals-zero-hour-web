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

// FILE: SpawnBehavior.cpp ////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, January 2002
// Desc:   Update will create and monitor a group of spawned units and replace as needed
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameState.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Player.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameClient/Drawable.h" //selection logic
#include "GameClient/InGameUI.h" // selection logic
#include "GameLogic/ExperienceTracker.h" //veterancy logic
#include "GameLogic/Module/StealthUpdate.h"

// Web port (round 5, units are pure data): SpawnBehavior's per-unit state is a row of TheGxSpawnBehaviors;
// GXROW_SPAWNBEHAVIOR is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
#define GXROW_SPAWNBEHAVIOR (const_cast<GxSpawnBehaviorRow &>(*m_gxSpawnBehaviorRow))



#define NONE_SPAWNED_YET (0xffffffff)





#define SPAWN_DELAY_MIN_FRAMES (16) // about as rapidly as you'd expect people to successively exit through the same door
//-------------------------------------------------------------------------------------------------
SpawnBehavior::SpawnBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	static Bool gxRegistered = FALSE;
	if (!gxRegistered)
	{
		gxRegistered = TRUE;
		GameLogic::gxRegisterKindKernel(NAMEKEY("SpawnBehavior"), gxKernel, gxApply);
	}

	const SpawnBehaviorModuleData* md = getSpawnBehaviorModuleData();

	// GEE, THIS IS NEW...
	// NOW, WE CAN HAVE A LIST OF TEMPLATE NAMES
	m_templateNameIterator = md->m_spawnTemplateNameData.begin();
	GXROW_SPAWNBEHAVIOR.spawnTemplate = TheThingFactory->findTemplate( *m_templateNameIterator );
	//each time m_spawn template is used, it will increment m_templateNameIterator,
	//thus scanning through the ordered list of template names
	//looping back to the beginning

	GXROW_SPAWNBEHAVIOR.framesToWait = 0;
	GXROW_SPAWNBEHAVIOR.firstBatchCount = 0;
	if( md->m_isOneShotData )
		GXROW_SPAWNBEHAVIOR.oneShotCountdown = md->m_spawnNumberData;
	else
		GXROW_SPAWNBEHAVIOR.oneShotCountdown = -1;

	GXROW_SPAWNBEHAVIOR.active = TRUE;

	GXROW_SPAWNBEHAVIOR.replacementTimes.clear();
	// The initializing of the initial bursters is handled in the first update @todo invent an object::postConstructionProcess() some day
	GXROW_SPAWNBEHAVIOR.initialBurstCountdown = md->m_initialBurst;
	GXROW_SPAWNBEHAVIOR.initialBurstTimesInited = FALSE;



	GXROW_SPAWNBEHAVIOR.aggregateHealth = md->m_aggregateHealth;

	GXROW_SPAWNBEHAVIOR.spawnCount = NONE_SPAWNED_YET;
	GXROW_SPAWNBEHAVIOR.active = TRUE;
	GXROW_SPAWNBEHAVIOR.selfTaskingSpawnCount = 0;
}

//-------------------------------------------------------------------------------------------------
SpawnBehavior::~SpawnBehavior()
{
	GXROW_SPAWNBEHAVIOR.replacementTimes.clear();
}

// ------------------------------------------------------------------------------------------------
void SpawnBehavior::onDelete()
{
	const SpawnBehaviorModuleData *modData = getSpawnBehaviorModuleData();

	// destroy anything that we have spawned that is not already dead
	if( modData->m_spawnedRequireSpawner )
	{
		Object *obj;

		for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); /*empty*/ )
		{

			// get object
			obj = TheGameLogic->findObjectByID( *it );

			// increment iterator incase the id list is alterd because what we're about to do
			++it;

			//
			// destroy this if it's alive, in the usual case this object "dies" and is
			// not "destroyed".  on *death* we *KILL* our spawned things, but this is here
			// and will *DESTROY* our spawned things if we ourselves are destroyed and they
			// are still alive (such a case would be calling destroy object on us directly)
			//
			if( obj && obj->isEffectivelyDead() == FALSE )
				TheGameLogic->destroyObject( obj );

		}

	}

}

// ------------------------------------------------------------------------------------------------
void SpawnBehavior::onDie( const DamageInfo *damageInfo )
{
	const SpawnBehaviorModuleData *modData = getSpawnBehaviorModuleData();

	///@todo isDieApplicable should be called outside of the onDie call
	if( modData->m_dieMuxData.isDieApplicable( getObject(), damageInfo ) == FALSE )
		return;

	for( objectIDListIterator iter = GXROW_SPAWNBEHAVIOR.spawnIDs.begin();
				iter != GXROW_SPAWNBEHAVIOR.spawnIDs.end();
				iter++
					)
	{
		Object *currentSpawn = TheGameLogic->findObjectByID( (*iter) );
		if( currentSpawn )
		{
			// Go through all my spawns and see if they have a SlavedUpdate I can tell I was killed to
			for (BehaviorModule** update = currentSpawn->getBehaviorModules(); *update; ++update)
			{
				SlavedUpdateInterface* sdu = (*update)->getSlavedUpdateInterface();
				if (sdu != nullptr)
				{
					sdu->onSlaverDie( damageInfo );
					break;
				}
			}

			// our spawner has died, we must invalidate the ID now in the spawned object
			currentSpawn->setProducer( nullptr );

		}
	}

	// kill anything that we have spawned if our module data directs us to do so
	if( modData->m_spawnedRequireSpawner )
	{
		Object *obj;

		for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); /*emtpy*/ )
		{

			// get object
			obj = TheGameLogic->findObjectByID( *it );

			// increment iterator incase the id list is alterd because what we're about to do
			++it;

			// kill it if not already dead
			if( obj && obj->isEffectivelyDead() == FALSE )
				obj->kill();

		}

	}

}

//-------------------------------------------------------------------------------------------------
// Round 5 addendum: update() = gxStep (the kernel part: the spawn timer, own row only) + gxApplyStep
// (aggregate health, burst setup, spawning). Under the per-kind hook (match rule updatePasses) the kind's
// kernel runs gxStep for every spawner first and apply runs gxApplyStep in object-ID order; nothing else
// reads or writes the timer, so the result is the same as the units' own turns.

// verify mode (page switch bit 21): at each unit's apply, its row must be as the kernel left it (nobody
// touched it in between), so computing it in the unit's own turn would have given the same result.
static void gxSpawnVerify(const char *what, Bool same)
{
#ifdef __EMSCRIPTEN__
	extern int gx_verify_mismatch, gx_verify_checked;
	++gx_verify_checked;
	if (!same) ++gx_verify_mismatch;
	static int n = 0;
	if (!same || (++n % 20000) == 0)
		printf("GXVERIFY kindKernels %s: %s, checked %d, mismatches %d\n", what, same ? "ok" : "MISMATCH", gx_verify_checked, gx_verify_mismatch);
#endif
}
extern int GLES3_Page_Switches();
Bool SpawnBehavior::gxStep()
{
	if (--GXROW_SPAWNBEHAVIOR.framesToWait > 0)
		return FALSE;
	GXROW_SPAWNBEHAVIOR.framesToWait = SPAWN_UPDATE_RATE;
	return TRUE;
}
void SpawnBehavior::gxKernel( UpdateModulePtr *mods, Int begin, Int end )
{
	const UnsignedInt now = TheGameLogic->getFrame();	// read only: fixed for the frame
	for (Int k = begin; k < end; ++k)
	{
		SpawnBehavior *m = static_cast<SpawnBehavior *>(mods[k]);
		m->gxRow().gxDue = m->gxStep();
		m->gxRow().gxKernelFrame = now;
		m->gxRow().gxSnap = m->gxRow().framesToWait;
	}
}
void SpawnBehavior::gxApply( UpdateModulePtr *mods, Int count )
{
	const Bool gxVerify = (GLES3_Page_Switches() & 2097152) != 0;
	for (Int k = 0; k < count; ++k)
	{
		SpawnBehavior *m = static_cast<SpawnBehavior *>(mods[k]);
		if (gxVerify)
			gxSpawnVerify("spawn", m->gxRow().gxSnap == m->gxRow().framesToWait && m->gxRow().gxKernelFrame == TheGameLogic->getFrame());
		m->gxApplyStep(m->gxRow().gxDue);
	}
}

UpdateSleepTime SpawnBehavior::update()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (GXROW_SPAWNBEHAVIOR.gxKernelFrame == now && now != 0)
		return UPDATE_SLEEP_NONE;	// this frame's turn was taken by the kind kernel + apply
	// the timer is not read by the steps before it, so stepping it first changes nothing
	return gxApplyStep(gxStep());
}

UpdateSleepTime SpawnBehavior::gxApplyStep( Bool due )
{
/// @todo srj use SLEEPY_UPDATE here

	//EVERY FRAME
	if ( GXROW_SPAWNBEHAVIOR.aggregateHealth )
	{
		computeAggregateStates();
	}

	const SpawnBehaviorModuleData* md = getSpawnBehaviorModuleData();
	if ( ! GXROW_SPAWNBEHAVIOR.initialBurstTimesInited )
	{
		GXROW_SPAWNBEHAVIOR.initialBurstTimesInited = TRUE;

		Bool runtimeProduced = getObject()->getProducerID()!=INVALID_ID; //this was produced by a production module, rather than a script or worldbuilder
		Int now = TheGameLogic->getFrame();
		Int burstInitCount = GXROW_SPAWNBEHAVIOR.initialBurstCountdown;
		for( int listIndex = 0; listIndex < md->m_spawnNumberData; listIndex++ )
		{
			if ( md->m_initialBurst > 0 )
			{
				UnsignedInt birthFrame = now;
				if ( runtimeProduced && burstInitCount > 0)
				{
					--burstInitCount;
					birthFrame += (listIndex*SPAWN_DELAY_MIN_FRAMES);
				}
				GXROW_SPAWNBEHAVIOR.replacementTimes.push_back( runtimeProduced  );// Set all spawns to be created in rapid succession
			}
			else
				GXROW_SPAWNBEHAVIOR.replacementTimes.push_back( listIndex );// Set all spawns to be created as soon as possible
		}
	}



	//SPARSELY SOLVED
	if (!due)
	{
		return UPDATE_SLEEP_NONE;
	}

	//Go through the list and make a spawn for each number that is less than now's frame
	if( shouldTryToSpawn() )
	{
		// Due to STL's list vomiting when you erase in a for loop,
		// Start at the beginning, and if you erase one, use the return
		// value to manually step to the next.  Otherwise, step by yourself.
		// begin == end means empty list, so no special check is needed.
		intListIterator iterator = GXROW_SPAWNBEHAVIOR.replacementTimes.begin();
		while( iterator != GXROW_SPAWNBEHAVIOR.replacementTimes.end() )
		{
			Int replacementTime = *iterator;
			UnsignedInt currentTime = TheGameLogic->getFrame();
			if( currentTime > replacementTime )
			{
				//If you create one, you pop the number off the list
				if( createSpawn() )
					iterator = GXROW_SPAWNBEHAVIOR.replacementTimes.erase( iterator );
				else
					iterator++;
			}
			else
				iterator++;
		}

		if( md->m_isOneShotData  &&  (GXROW_SPAWNBEHAVIOR.oneShotCountdown <= 0) )
			stopSpawning(); //I only trigger one batch.  ie on Creation.
	}
	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
Bool SpawnBehavior::maySpawnSelfTaskAI( Real maxSelfTaskersRatio )
{
	if ( GXROW_SPAWNBEHAVIOR.spawnCount == 0)
		return FALSE;
	if ( maxSelfTaskersRatio == 0)
		return FALSE;


	//if my last attack command was from player or script, I need to forbid my spawn from disobeying that command
	//otherwise (since my attack state was autoacquired my ny own ai), let them deviate by the ratio specified.
	Object* obj = getObject();
	if ( ! obj )
		return FALSE;
	AIUpdateInterface *ai = obj->getAI();
	if ( ! ai )
		return FALSE;

	CommandSourceType lastAttackCommandSource = ai->getLastCommandSource();


	if ( lastAttackCommandSource != CMD_FROM_AI )
		return FALSE;


	Real curSelfTaskersRatio = (Real)GXROW_SPAWNBEHAVIOR.selfTaskingSpawnCount / (Real)GXROW_SPAWNBEHAVIOR.spawnCount;

	return ( curSelfTaskersRatio < maxSelfTaskersRatio );
}

// ------------------------------------------------------------------------------------------------
Object* SpawnBehavior::getClosestSlave( const Coord3D *pos )
{
	Object *closest = nullptr;
	Real closestDistance;
	for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
		{
			Real distance = ThePartitionManager->getDistanceSquared( obj, pos, FROM_CENTER_2D );

			if( !closest || closestDistance > distance )
			{
				closest = obj;
				closestDistance = distance;
			}
		}
	}
	return closest; //Could be null!
}

// ------------------------------------------------------------------------------------------------
void SpawnBehavior::orderSlavesToAttackTarget( Object *target, Int maxShotsToFire, CommandSourceType cmdSource )
{
	for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
		{
			AIUpdateInterface *ai = obj->getAI();
			if( ai )
			{
				ai->aiForceAttackObject( target, maxShotsToFire, cmdSource );
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
void SpawnBehavior::orderSlavesToAttackPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
{
	for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
		{
			AIUpdateInterface *ai = obj->getAI();
			if( ai )
			{
				ai->aiAttackPosition( pos, maxShotsToFire, cmdSource );
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
void SpawnBehavior::orderSlavesToGoIdle( CommandSourceType cmdSource )
{
	for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
		{
			AIUpdateInterface *ai = obj->getAI();
			if( ai )
			{
				ai->aiIdle( cmdSource );
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
void SpawnBehavior::orderSlavesDisabledUntil( DisabledType type, UnsignedInt frame )
{
	for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
		{
			AIUpdateInterface *ai = obj->getAI();
			if( ai )
			{
				ai->aiIdle( CMD_FROM_AI );
			}
			obj->setDisabledUntil( type, frame );
		}
	}
}

// ------------------------------------------------------------------------------------------------
void SpawnBehavior::orderSlavesToClearDisabled( DisabledType type )
{
	for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
		{
			obj->clearDisabled( type );
		}
	}
}

// ------------------------------------------------------------------------------------------------
CanAttackResult SpawnBehavior::getCanAnySlavesAttackSpecificTarget( AbleToAttackType attackType, const Object *target, CommandSourceType cmdSource )
{
	Bool invalidShot = FALSE;
	for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
		{
			CanAttackResult result = obj->getAbleToAttackSpecificObject( attackType, target, cmdSource );

			switch( result )
			{
				case ATTACKRESULT_POSSIBLE:
				case ATTACKRESULT_POSSIBLE_AFTER_MOVING:
					return result;

				case ATTACKRESULT_NOT_POSSIBLE:
					break;

				case ATTACKRESULT_INVALID_SHOT:
					invalidShot = TRUE;
					break;

				default:
					DEBUG_CRASH( ("SpawnBehavior::getCanAnySlavesAttackSpecificTarget encountered unhandled CanAttackResult of %d. Treating as not possible...", result) );
					break;
			}
		}
	}
	//Prioritize the reasonings!
	if( invalidShot )
	{
		return ATTACKRESULT_INVALID_SHOT;
	}
	return ATTACKRESULT_NOT_POSSIBLE;
}

// ------------------------------------------------------------------------------------------------
CanAttackResult SpawnBehavior::getCanAnySlavesUseWeaponAgainstTarget( AbleToAttackType attackType, const Object *victim, const Coord3D *pos, CommandSourceType cmdSource )
{
	Bool invalidShot = FALSE;
	for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
		{
			CanAttackResult result = obj->getAbleToUseWeaponAgainstTarget( attackType, victim, pos, cmdSource );

			switch( result )
			{
				case ATTACKRESULT_POSSIBLE:
				case ATTACKRESULT_POSSIBLE_AFTER_MOVING:
					return result;

				case ATTACKRESULT_NOT_POSSIBLE:
					break;

				case ATTACKRESULT_INVALID_SHOT:
					invalidShot = TRUE;
					break;

				default:
					DEBUG_CRASH( ("SpawnBehavior::getCanAnySlavesUseWeaponAgainstTarget encountered unhandled CanAttackResult of %d. Treating as not possible...", result) );
					break;
			}
		}
	}
	//Prioritize the reasonings!
	if( invalidShot )
	{
		return ATTACKRESULT_INVALID_SHOT;
	}
	return ATTACKRESULT_NOT_POSSIBLE;
}

// ------------------------------------------------------------------------------------------------
void SpawnBehavior::giveSlavesStealthUpgrade( Bool grantStealth )
{
	for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
		{
			obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_CAN_STEALTH ), grantStealth );
		}
	}
}

// ------------------------------------------------------------------------------------------------
Bool SpawnBehavior::canAnySlavesAttack()
{
	for( objectIDListIterator it = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); it != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if( obj )
		{
			if( obj->isAbleToAttack() )
			{
				return true;
			}
		}
	}
	return false;
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class OrphanData
{

public:

	OrphanData();

	const ThingTemplate *m_matchTemplate;
	Object *m_source;
	Object *m_closest;
	Real m_closestDistSq;

};

#define BIG_DISTANCE 99999999.9f
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
OrphanData::OrphanData()
{

	m_matchTemplate = nullptr;
	m_source = nullptr;
	m_closest = nullptr;
	m_closestDistSq = BIG_DISTANCE;

}

// ------------------------------------------------------------------------------------------------
static void findClosestOrphan( Object *obj, void *userData )
{
	OrphanData *orphanData = (OrphanData *)userData;

	// if template doesn't match do nothing
	if( obj->getTemplate()->isEquivalentTo( orphanData->m_matchTemplate ) == FALSE )
		return;

	// this object must be orphaned
	if( obj->getProducerID() != INVALID_ID )
		return;

	// is this the closest one so far
	Real distSq = ThePartitionManager->getDistanceSquared( orphanData->m_source, obj, FROM_CENTER_2D );
	if( distSq < orphanData->m_closestDistSq )
	{

		orphanData->m_closest = obj;
		orphanData->m_closestDistSq = distSq;

	}

}

// ------------------------------------------------------------------------------------------------
Object *SpawnBehavior::reclaimOrphanSpawn()
{
	Player *player = getObject()->getControllingPlayer();
	const SpawnBehaviorModuleData *md = getSpawnBehaviorModuleData();

	//
	// iterate all the objects my controlling player has and look for any orphaned things
	// that we would normally spawn, if found we'll just make it our own
	//
	// EVEN MORE NEW AND DIFFERENT
	// This block scans the list for matchTemplates
	//

	OrphanData orphanData;
	AsciiString prevName;
	for (std::vector<AsciiString>::const_iterator tempName = md->m_spawnTemplateNameData.begin();
			tempName != md->m_spawnTemplateNameData.end();
			++tempName)
	{
		if (prevName.compare(*tempName)) // the list may have redundancy, this will skip some of it
			continue;
		orphanData.m_matchTemplate = TheThingFactory->findTemplate( *tempName );
		orphanData.m_source = getObject();
		orphanData.m_closest = nullptr;
		orphanData.m_closestDistSq = BIG_DISTANCE;
		player->iterateObjects( findClosestOrphan, &orphanData );
		prevName = *tempName;
	}

	return orphanData.m_closest;
}

//-------------------------------------------------------------------------------------------------
Bool SpawnBehavior::createSpawn()
{
	Object *parent = getObject();
	const SpawnBehaviorModuleData *md = getSpawnBehaviorModuleData();

	ExitInterface* exitInterface = parent->getObjectExitInterface();
	if( exitInterface == nullptr )
	{
		DEBUG_ASSERTCRASH( exitInterface != nullptr, ("Something cannot have SpawnBehavior without an exit interface") );
		return FALSE;
	}

	ExitDoorType exitDoor = exitInterface->reserveDoorForExit(nullptr, nullptr);
	if (exitDoor == DOOR_NONE_AVAILABLE)
		return FALSE;

	Object *newSpawn = nullptr;

	// try to reclaim orphaned objects if possible
	Bool reclaimedOrphan = FALSE;
	if( md->m_canReclaimOrphans && md->m_isOneShotData == FALSE )
	{
		newSpawn = reclaimOrphanSpawn();
		if( newSpawn )
			reclaimedOrphan = TRUE;
	}

													// This assures that an orphan has not just been reclaimed,
	if( newSpawn == nullptr )	// and that we really want a new spawn, here.
	{
		GXROW_SPAWNBEHAVIOR.spawnTemplate = TheThingFactory->findTemplate( *m_templateNameIterator );

		//newSpawn = TheThingFactory->newObject( GXROW_SPAWNBEHAVIOR.spawnTemplate, parent->getControllingPlayer()->getDefaultTeam() );
		newSpawn = TheThingFactory->newObject( GXROW_SPAWNBEHAVIOR.spawnTemplate, parent->getTeam() ); // just a little worried about this...

		// Count this unit towards our score.
		newSpawn->getControllingPlayer()->onUnitCreated(parent, newSpawn);

		// Gee, this is new...
		// Now, we can have a list of template names.
		// Each time m_spawn template is used, it will increment m_templateNameIterator,
		// thus scanning through the ordered list of template names, and
		// looping back to the beginning

		++m_templateNameIterator;
		if ( m_templateNameIterator == md->m_spawnTemplateNameData.end())
		{
			m_templateNameIterator = md->m_spawnTemplateNameData.begin();
		}
	}


	newSpawn->setProducer(parent);

	// If they have a SlavedUpdate, then I have to tell them who their daddy is from now on.
	for (BehaviorModule** update = newSpawn->getBehaviorModules(); *update; ++update)
	{
		SlavedUpdateInterface* sdu = (*update)->getSlavedUpdateInterface();
		if (sdu != nullptr)
		{
			sdu->onEnslave( parent );
			break;
		}
	}

	GXROW_SPAWNBEHAVIOR.spawnIDs.push_back( newSpawn->getID() );

	if( reclaimedOrphan == FALSE )
	{
		if ( md->m_exitByBudding )
		{

			Bool barracksExitSuccess = FALSE;

			if ( GXROW_SPAWNBEHAVIOR.initialBurstCountdown > 0 )
			{
				Object *barracks = TheGameLogic->findObjectByID( parent->getProducerID() );
				if ( barracks && barracks->isKindOf( KINDOF_STRUCTURE ) )
				{
					ExitInterface* barracksExitInterface = barracks->getObjectExitInterface();
					if ( barracksExitInterface )
					{
						ExitDoorType barracksDoor = barracksExitInterface->reserveDoorForExit(nullptr, nullptr);
						barracksExitInterface->exitObjectViaDoor( newSpawn, barracksDoor );
						newSpawn->setProducer(parent);//let parents producer exit him, but he thinks it was me
						--GXROW_SPAWNBEHAVIOR.initialBurstCountdown;
						barracksExitSuccess = TRUE;
					}

				}

			}

			if ( ! barracksExitSuccess )
			{
				// find the closest spawn to the nexus...
				//there is probably a more elegant way to choose the budHost, but oh well
				Object *budHost = nullptr;
				Object *curSpawn = nullptr;
				Real tapeMeasure = 99999;
				Real closest = 999999.9f; // 1000 * 1000
				objectIDListIterator iter;
				for( iter = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); iter != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); iter++)
				{
					curSpawn = TheGameLogic->findObjectByID( *iter );
					if ( curSpawn )
					{
						if (curSpawn == newSpawn )
							continue;

						tapeMeasure = ThePartitionManager->getDistanceSquared( curSpawn, parent, FROM_CENTER_2D );
						if ( tapeMeasure < closest )
						{
							closest = tapeMeasure;
							budHost = curSpawn;
						}
					}
				}
				exitInterface->exitObjectByBudding( newSpawn, budHost );// also handles the nullptr pointer okay

			}

		}
		else
		{
			exitInterface->exitObjectViaDoor( newSpawn, exitDoor );
		}

	}
	else
		exitInterface->unreserveDoorForExit( exitDoor );

	if( md->m_isOneShotData )
		GXROW_SPAWNBEHAVIOR.oneShotCountdown--;


	if ( GXROW_SPAWNBEHAVIOR.spawnCount == NONE_SPAWNED_YET )
	{
		GXROW_SPAWNBEHAVIOR.spawnCount = 1;
	}
	else
	{
		++ GXROW_SPAWNBEHAVIOR.spawnCount;
	}


	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void SpawnBehavior::onSpawnDeath( ObjectID deadSpawn, DamageInfo *damageInfo )
{
	objectIDListIterator it = std::find(GXROW_SPAWNBEHAVIOR.spawnIDs.begin(), GXROW_SPAWNBEHAVIOR.spawnIDs.end(), deadSpawn);

	// If the iterator is at the end, we didn't find deadSpawn, so bail out.
	// Otherwise, bad crash stuff will happen.
	if (it == GXROW_SPAWNBEHAVIOR.spawnIDs.end())
		return;

	//When one dies, you push (now + delay) as the time a new one should be made
	const SpawnBehaviorModuleData* md = getSpawnBehaviorModuleData();

	Int replacementTime = md->m_spawnReplaceDelayData + TheGameLogic->getFrame();
	GXROW_SPAWNBEHAVIOR.replacementTimes.push_back( replacementTime );

	GXROW_SPAWNBEHAVIOR.spawnIDs.erase( it );

	--GXROW_SPAWNBEHAVIOR.spawnCount;

	if ( (GXROW_SPAWNBEHAVIOR.spawnCount == 0) && GXROW_SPAWNBEHAVIOR.aggregateHealth) // I'm dead without my spawn
	{
		Object *killer = TheGameLogic->findObjectByID(damageInfo->in.m_sourceID);
		if (killer != nullptr) {
			killer->scoreTheKill(getObject());
		}
		TheGameLogic->destroyObject(getObject());
		//getObject()->kill();
		return;
	}
}

//-------------------------------------------------------------------------------------------------
void SpawnBehavior::stopSpawning()
{
	GXROW_SPAWNBEHAVIOR.active = FALSE;
}

//-------------------------------------------------------------------------------------------------
void SpawnBehavior::startSpawning()
{
	GXROW_SPAWNBEHAVIOR.active = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** When I become damaged */
// ------------------------------------------------------------------------------------------------
void SpawnBehavior::onDamage( DamageInfo *info )
{
	for( objectIDListIterator iter = GXROW_SPAWNBEHAVIOR.spawnIDs.begin();
				iter != GXROW_SPAWNBEHAVIOR.spawnIDs.end();
				iter++
					)
	{
		Object *currentSpawn = TheGameLogic->findObjectByID( (*iter) );
		if( currentSpawn )
		{
			// Go through all my spawns and see if they have a SlavedUpdate I can tell I was hurt to
			for (BehaviorModule** update = currentSpawn->getBehaviorModules(); *update; ++update)
			{
				SlavedUpdateInterface* sdu = (*update)->getSlavedUpdateInterface();
				if (sdu != nullptr)
				{
					sdu->onSlaverDamage( info );
					break;
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool SpawnBehavior::shouldTryToSpawn()
{
	const SpawnBehaviorModuleData *modData = getSpawnBehaviorModuleData();

	// Not if we are turned off
	if( !GXROW_SPAWNBEHAVIOR.active )
		return FALSE;
	if( getObject()->getStatusBits().test( OBJECT_STATUS_RECONSTRUCTING ) && modData->m_isOneShotData )
	{
		// If we are a Hole rebuild, not only should we not, but we should never ask again.
		stopSpawning();
		return FALSE;
	}
	// Not if we are under construction or being sold
	if( getObject()->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || getObject()->testStatus(OBJECT_STATUS_SOLD) )
		return FALSE;
	// Not if we are civilian controlled
	if( getObject()->isNeutralControlled() )
		return FALSE;

	return TRUE;
}

//********************************************************************
//* This function allows various object states to be either inherited
//* from the spawner to the spawn, or vice versa
//* Selection is distributed across all spawn and spawner at once
//* Health for the spawner is calc'd by aggregating the sum of all
//*   spawn health, and dividing by optimal health of full population
//*   (that is, the max spawn, SpawnBehaviorModuleData::m_spawnNumberData)
//*   at full health.
//* Veterancy is sucked out of any unit that has any and put into the
//* Spawner, scaled by 1/SpawnBehaviorModuleData::m_spawnNumberData;
//* The HealthBoxPosition (maybe to include moodicon, vet icon) is calc'd
//* as an average position of all the spawn.
//********************************************************************

void SpawnBehavior::computeAggregateStates()
{
	if ( ! GXROW_SPAWNBEHAVIOR.aggregateHealth ) // sanity
		return;

	Object* obj = getObject();
	const SpawnBehaviorModuleData* md = getSpawnBehaviorModuleData();

	Int spawnCount = 0;
	Int spawnCountMax = md->m_spawnNumberData;
	Coord3D avgSpawnPos;

	avgSpawnPos.set(0,0,0);
	Real acrHealth = 0.0f;
	Real avgHealthMax = 0.0f;

	ExperienceTracker *expTracker = obj->getExperienceTracker();
	VeterancyLevel vetLevel = expTracker->getVeterancyLevel();

	Bool SomebodyIsSelected = FALSE;
	Bool SomebodyIsNotSelected = FALSE;

	Drawable *spawnDraw = nullptr;
	Object *currentSpawn = nullptr;

	WeaponBonusConditionFlags spawnWeaponBonus;


	GXROW_SPAWNBEHAVIOR.selfTaskingSpawnCount = 0;

	for( objectIDListIterator iter = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); iter != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); iter++)
	{
		currentSpawn = TheGameLogic->findObjectByID( (*iter) );

		if( currentSpawn )
		{
			//GXROW_SPAWNBEHAVIOR.selfTaskingSpawnCount += ( currentSpawn->isSelf);

			for (BehaviorModule** update = currentSpawn->getBehaviorModules(); *update; ++update)
			{
				SlavedUpdateInterface* sdu = (*update)->getSlavedUpdateInterface();
				if (sdu != nullptr)
				{
					GXROW_SPAWNBEHAVIOR.selfTaskingSpawnCount += ( sdu->isSelfTasking());
					break;
				}
			}


			// VETERANCY LEVEL *************************************
			VeterancyLevel spawnVetLevel = currentSpawn->getVeterancyLevel();
			if ( spawnVetLevel > vetLevel) // whoever has more, set the other guy
			{
				expTracker->setVeterancyLevel(spawnVetLevel);
			}
			else if ( spawnVetLevel < vetLevel) // whoever has more, set the other guy
			{
				currentSpawn->getExperienceTracker()->setVeterancyLevel(vetLevel);
			}

			spawnWeaponBonus = currentSpawn->getWeaponBonusCondition();

			avgSpawnPos.add(currentSpawn->getPosition());

			BodyModuleInterface *body = currentSpawn->getBodyModule();
			acrHealth    += body->getHealth();
			avgHealthMax += body->getMaxHealth();

			spawnDraw = currentSpawn->getDrawable();

			if ( spawnDraw->isSelected() )
			{
				SomebodyIsSelected = TRUE;
			}
			else
			{
				SomebodyIsNotSelected = TRUE;
			}

			spawnCount++;
		}
	}



	// SELECTION STATE *****************************************
	// THIS LOGIC IS SIMPLE
	// IF ANY ONE OF MY SPAWN ARE SELECTED RIGHT NOW,
	// I JUST SET THEM ALL SELECTED
	// THIS WAY WE ARE ALWAYS ONE BIG HAPPY MOB
	// BUT I ONLY PROCEED IF THERE IS ONE AMONG ALL OF US WHO IS NOT SELECTED

	if ( SomebodyIsSelected && ( ! obj->getDrawable()->isSelected() || SomebodyIsNotSelected ) )
	{
		GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
		teamMsg->appendBooleanArgument( FALSE );// not creating new team so pass false

		if ( SomebodyIsNotSelected ) // lets select everybody
		{
			for( objectIDListIterator iter = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); iter != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); iter++)
			{
				currentSpawn = TheGameLogic->findObjectByID( (*iter) );

				if( currentSpawn )
				{
					spawnDraw = currentSpawn->getDrawable();


					if ( ! spawnDraw->isSelected())
					{
						TheInGameUI->selectDrawable( spawnDraw );
						TheInGameUI->setDisplayedMaxWarning( FALSE );
						teamMsg->appendBooleanArgument( FALSE );// not creating new team so pass false
						teamMsg->appendObjectIDArgument( currentSpawn->getID() );
					}
				}
			}
		}
		// if somebody is selected then I sure need to be!
		if ( ! obj->getDrawable()->isSelected())
		{
			TheInGameUI->selectDrawable( obj->getDrawable() );
			TheInGameUI->setDisplayedMaxWarning( FALSE );
			teamMsg->appendBooleanArgument( FALSE );// not creating new team so pass false
			teamMsg->appendObjectIDArgument( obj->getID() );
		}

	}



	// HEALTH BOX POSITION *****************************
	// pick a centered, average spot to draw the health box
	avgSpawnPos.scale(1.0f / spawnCount);
	avgSpawnPos.sub(obj->getPosition());
	obj->setHealthBoxOffset(avgSpawnPos);



	// HEALTH STATE *************************************
	// make my health an aggregate of all my spawns' healths
	if ( spawnCount )
	{
		avgHealthMax /= spawnCount;
		Real perfectTotalHealth = avgHealthMax * spawnCountMax;
		Real actualHealth  = acrHealth / perfectTotalHealth;
		obj->getBodyModule()->setInitialHealth(100.0f * actualHealth);
	}
	else
	{
		obj->getBodyModule()->setInitialHealth(0);// I been sick <
	}


	// HOUSEKEEPING *************************************
	//make sure no enemies are shooting at the nexus, since it doesn't 'exist'
	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_MASKED ) );

}

//-------------------------------------------------------------------------------------------------
Bool SpawnBehavior::areAllSlavesStealthed() const
{
	Object *currentSpawn;

	for( std::list<ObjectID>::const_iterator iter = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); iter != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); iter++)
	{
		currentSpawn = TheGameLogic->findObjectByID( (*iter) );
		if( currentSpawn )
		{
      const StealthUpdate *stealthUpdate = currentSpawn->getStealth();
			if( !stealthUpdate || !stealthUpdate->allowedToStealth( currentSpawn ) )
			{
				return FALSE;
			}
		}
	}

	return TRUE; //0 or more spawns are ALL stealthed... I suppose if you have NO spawns, then they are considered stealthed ;)
}

//-------------------------------------------------------------------------------------------------
void SpawnBehavior::revealSlaves()
{
	Object *currentSpawn;

	for( objectIDListIterator iter = GXROW_SPAWNBEHAVIOR.spawnIDs.begin(); iter != GXROW_SPAWNBEHAVIOR.spawnIDs.end(); iter++)
	{
		currentSpawn = TheGameLogic->findObjectByID( (*iter) );
		if( currentSpawn )
		{
			StealthUpdate *stealthUpdate = currentSpawn->getStealth();
			if( stealthUpdate )
			{
				stealthUpdate->markAsDetected();
			}
		}
	}
}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SpawnBehavior::crc( Xfer *xfer )
{

	// extend base class
	BehaviorModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Added m_initialBurstTimesInited to the save. jba.
	* 3: TheSuperHackers @bugfix Stubbjax 16/02/2026 Added m_initialBurstCountdown.
*/
// ------------------------------------------------------------------------------------------------
void SpawnBehavior::xfer( Xfer *xfer )
{
	AsciiString name;

	// version
#if RETAIL_COMPATIBLE_XFER_SAVE
	XferVersion currentVersion = 2;
#else
	XferVersion currentVersion = 3;
#endif
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	BehaviorModule::xfer( xfer );


	if (version >= 2) {
		xfer->xferBool(&GXROW_SPAWNBEHAVIOR.initialBurstTimesInited);
	}

	if (version >= 3) {
		xfer->xferUnsignedInt(&GXROW_SPAWNBEHAVIOR.initialBurstCountdown);
	}

	// spawn template
	name = GXROW_SPAWNBEHAVIOR.spawnTemplate ? GXROW_SPAWNBEHAVIOR.spawnTemplate->getName() : AsciiString::TheEmptyString;
	xfer->xferAsciiString( &name );
	if( xfer->getXferMode() == XFER_LOAD )
	{

		GXROW_SPAWNBEHAVIOR.spawnTemplate = nullptr;
		if( name.isEmpty() == FALSE )
		{

			GXROW_SPAWNBEHAVIOR.spawnTemplate = TheThingFactory->findTemplate( name );
			if( GXROW_SPAWNBEHAVIOR.spawnTemplate == nullptr )
			{

				DEBUG_CRASH(( "SpawnBehavior::xfer - Unable to find template '%s'", name.str() ));
				throw SC_INVALID_DATA;

			}

		}

	}

	// one shot countdown
	xfer->xferInt( &GXROW_SPAWNBEHAVIOR.oneShotCountdown );

	// frames to wait
	xfer->xferInt( &GXROW_SPAWNBEHAVIOR.framesToWait );

	// first batch count
	xfer->xferInt( &GXROW_SPAWNBEHAVIOR.firstBatchCount );

	// replacement times
	if( xfer->getXferMode() == XFER_LOAD )
		GXROW_SPAWNBEHAVIOR.replacementTimes.clear();
	xfer->xferSTLIntList( &GXROW_SPAWNBEHAVIOR.replacementTimes );

	// spawn ids
	xfer->xferSTLObjectIDList( &GXROW_SPAWNBEHAVIOR.spawnIDs );

	// active
	xfer->xferBool( &GXROW_SPAWNBEHAVIOR.active );

	// aggregate health
	xfer->xferBool( &GXROW_SPAWNBEHAVIOR.aggregateHealth );

	// spawn count
	xfer->xferInt( &GXROW_SPAWNBEHAVIOR.spawnCount );

	// self tasking spawn count
	xfer->xferUnsignedInt( &GXROW_SPAWNBEHAVIOR.selfTaskingSpawnCount );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SpawnBehavior::loadPostProcess()
{

	// extend base class
	BehaviorModule::loadPostProcess();

}
