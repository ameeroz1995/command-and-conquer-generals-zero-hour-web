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

// FILE: RailroadBehavior.cpp //////////////////////////////////////////////////////////////
// Author: Mark Lorenzen, Sept 2002
// Desc: The railroad track following railroad carriage/locomotive logic module
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"

#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/RailroadGuideAIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DemoTrapUpdate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"

#include "GameClient/Drawable.h"
#include "GameClient/Statistics.h"


///#define RAILROAD_DESYNC_TEST

#ifdef RAILROAD_DESYNC_TEST
#include "winbase.h"
#endif

// TYPES //////////////////////////////////////////////////////////////////////////////////////////
static const Int INVALID_PATH = -1;
#define NORMAL_VEL_Z	0.25f
#define NORMAL_MASS		50.0f






#define FRAMES_UNPULLED_LONG_ENOUGH_TO_UNHITCH (2)


void TrainTrack::incReference()
{
	++m_refCount;
}

Bool TrainTrack::releaseReference()
{
	return ( --m_refCount == 0 );
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
RailroadBehaviorModuleData::RailroadBehaviorModuleData()
{
	m_carriageTemplateNameData.clear();
	m_pathPrefixName.clear();
	m_CrashFXTemplateName.clear();

	m_isLocomotive = FALSE;
	m_runningGarrisonSpeedMax = 1.0f;
	m_killSpeedMin = 1.0f;
	m_speedMax = 4;
	m_acceleration = 1.01f;
	m_braking = 0.99f;
	m_friction = 0.97f;
	m_waitAtStationTime = 150;
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
RailroadBehavior::RailroadBehavior( Thing *thing, const ModuleData *moduleData )
											 : PhysicsBehavior( thing, moduleData )
{
	m_gxRow = GxComponentPool<GxState>::the().alloc();
	const RailroadBehaviorModuleData *modData = getRailroadBehaviorModuleData();

	R().nextStationTask = DO_NOTHING;
	R().trailerID = INVALID_ID;

	conductorPullInfo.speed = 0;
	conductorPullInfo.m_direction = 1.0f;
	conductorPullInfo.m_mostRecentSpecialPointHandle = 0xfacade;
	conductorPullInfo.towHitchPosition.set(0,0,0);
	conductorPullInfo.trackDistance = 0;
	conductorPullInfo.currentWaypoint = 0xfacade;
	conductorPullInfo.previousWaypoint = 0xfacade;

	R().pullInfo.speed = 0.0f;
	R().pullInfo.m_direction = 1.0f;
	R().pullInfo.m_mostRecentSpecialPointHandle = 0xfacade;
	R().pullInfo.towHitchPosition.set(0,0,0);
	R().pullInfo.trackDistance = 0.0f;
	R().pullInfo.currentWaypoint = 0xfacade;
	R().pullInfo.previousWaypoint = 0xfacade;


#ifdef RAILROAD_DESYNC_TEST
	_LARGE_INTEGER pc;
	QueryPerformanceCounter( &pc ); // absolutely, positively random every call!
	Real random = 100000.0f / (Real)pc.LowPart;
	conductorPullInfo.m_direction = random;
	R().pullInfo.m_direction = random;
#endif

	R().runningSound = modData->m_runningSound;
	R().clicketyClackSound = modData->m_clicketyClackSound;
	R().whistleSound = modData->m_whistleSound;
	R().runningSound.setObjectID( getObject()->getID() );
	R().whistleSound.setObjectID( getObject()->getID() ) ;
	R().clicketyClackSound.setObjectID( getObject()->getID() ) ;

	R().track = nullptr;

	R().currentPointHandle = 0xfacade;
	R().waitAtStationTimer = 0;



	R().anchorWaypointID = INVALID_WAYPOINT_ID;

	R().carriagesCreated = FALSE;
	R().hasEverBeenHitched = FALSE;
	R().trackDataLoaded = FALSE;
	R().waitingInWings = TRUE;
	R().endOfLine = FALSE;
	R().isLocomotive = modData->m_isLocomotive;
	R().isLeadCarriage = R().isLocomotive;  // for now, I am the lead, only if I am the locomotive
	R().wantsToBeLeadCarriage = FALSE;
	R().disembark = FALSE;
	R().inTunnel = FALSE;
  R().held = FALSE;


	R().conductorState = R().isLocomotive ? ACCELERATE : COAST;


}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
RailroadBehavior::~RailroadBehavior()
{
	GxComponentPool<GxState>::the().release(m_gxRow);

	TheAudio->removeAudioEvent( R().runningSound.getPlayingHandle() );// no more chugchug when I'm dead

	if( R().track != nullptr )
	{
		if (R().track->releaseReference())
			delete R().track;

		R().track = nullptr;
	}


}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

Bool RailroadBehavior::isRailroad() const
{
	if ( ! R().track )
		return FALSE;// I haven't even started yet!

	if (R().waitingInWings)
		return FALSE;
	if (R().endOfLine)
		return FALSE;
	if (R().isLeadCarriage)
		return TRUE;
	if (R().trailerID==INVALID_ID)
		return TRUE;

	//Explanation: I return that I am a railroad only if I am at one of the ends, otherwise I refuse to cooperate

	return FALSE;
}


// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void RailroadBehavior::onCollide( Object *other, const Coord3D *loc, const Coord3D *normal )
{
	Object *obj = getObject();
	if ( ! obj)
		return;  //sanity
	if ( ! other)
		return;  //sanity
	if ( ! loc)
		return;  //sanity
	if ( ! normal)
		return;  //sanity

	if (R().waitingInWings)
		return ;
	if (R().endOfLine)
		return ;

	for (BehaviorModule** m = other->getBehaviorModules(); *m; ++m)
	{
		CollideModuleInterface* collide = (*m)->getCollide();
		if (!collide)
			continue;

		if( collide->isRailroad())
		{

			if ( R().isLocomotive )//yikes! I just rear ended a stranded train! Oh well...
			{
				other->kill();
			}
			else if ( R().isLeadCarriage ) //yikes! I just coasted into some other carriage
			{
				other->kill();
				obj->kill();//and I am dead, too
			}

			return ;//// its a train, folks
		}

	}

 	if (other->isKindOf( KINDOF_STRUCTURE ) )// now be careful here, we ignore buildings, except for hostile, nasty ones that can blow us up
	{
		//If it is a civilian building like a tunnel or a train station, let it be
		//but if it is a faction building, kill it!
		if (other->isKindOf( KINDOF_FS_POWER ) ||
				other->isKindOf( KINDOF_FS_FACTORY ) ||
				other->isKindOf( KINDOF_FS_BASE_DEFENSE ) ||
				other->isKindOf( KINDOF_FS_TECHNOLOGY ) ||
				other->isKindOf( KINDOF_REBUILD_HOLE ) )
		{
			playImpactSound(other, other->getPosition());
			other->kill();
			return;
		}

		static NameKeyType key_DemoTrapUpdate = NAMEKEY("DemoTrapUpdate");
		DemoTrapUpdate *dtu = (DemoTrapUpdate*)other->findUpdateModule(key_DemoTrapUpdate);
		if( dtu )
		{
			if( !other->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
				obj->kill(); // it can only detonate on me if it is ready

			playImpactSound(other, other->getPosition());
			other->kill();
			return;
		}
	}


	PhysicsBehavior *theirPhys = other->getPhysics();
	if( ! theirPhys )
		return;

	// maybe we don't want to trample this unit?
	const RailroadBehaviorModuleData *modData = getRailroadBehaviorModuleData();
	if ( R().conductorState == WAIT_AT_STATION && (R().pullInfo.speed < modData->m_runningGarrisonSpeedMax) ) // they can grab on safely
	{
		AIUpdateInterface *ai = other->getAI();
		if (ai && ai->getEnterTarget() == obj) // other intends to garrison me.
		{
			return;
		}
	}

	if (other->getContainedBy() == getObject() )//I dont mash my own sweet passengers
		return;


	//if ( other->getModelConditionFlags().test( MODELCONDITION_RUBBLE ) == TRUE )
	//	return; // I don't mess with rubble

	Bool victimIsInfantry = other->isKindOf( KINDOF_INFANTRY );

	const Coord3D *myDir = obj->getUnitDirectionVector2D();
	const Coord3D *myLoc = obj->getPosition();
	const Coord3D *theirLoc = other->getPosition();


	//---------------------------
	// if we made it this far, it is something we dont want to share space with


  Coord3D dlt;
	dlt.x = theirLoc->x - myLoc->x;
	dlt.y = theirLoc->y - myLoc->y;
	dlt.z = theirLoc->z - myLoc->z;

	//Alert all the players of recent disaster
	if ( ! R().whistleSound.isCurrentlyPlaying())
		R().whistleSound.setPlayingHandle(TheAudio->addAudioEvent( &R().whistleSound ));


	Real dist = (Real)sqrtf( dlt.x*dlt.x + dlt.y*dlt.y + dlt.z*dlt.z);
	Real usRadius = obj->getGeometryInfo().getMajorRadius();
	Real themRadius = other->getGeometryInfo().getMajorRadius();
	Real overlap = ((usRadius + themRadius) - dist) + 1;// the plus 1 makes them go just outside of me.
	dlt.normalize();
	if ( ! victimIsInfantry )
	{
		overlap/= 4;

	  dlt.scale( overlap);
	  Coord3D newPos;
	  newPos.x = theirLoc->x + dlt.x;
	  newPos.y = theirLoc->y + dlt.y;
	  newPos.z = theirLoc->z + dlt.z;
	  other->setPosition( &newPos );
	}

  if ( R().conductorState == WAIT_AT_STATION || (R().conductorState == COAST && R().pullInfo.speed < modData->m_runningGarrisonSpeedMax) || !R().isLocomotive )
	{
//  AIUpdateInterface *ai = other->getAI();
//	  if ( ai )
//		  ai->aiIdle( CMD_FROM_AI );// this eliminates yadda by telling them to stop driving into me

    return;//let those trying to board pass through unhindered
	}





	//figure out the relative slope between them and me
	Coord3D delta = *theirLoc;
	delta.sub( myLoc );
	delta.normalize();
	Real dot = delta.x * myDir->x + delta.y * myDir->y + delta.z * myDir->z;

	if (other->isEffectivelyDead())
	{// we just run over debris, instead of shoving it around
		delta.scale( MIN(0.3f,R().pullInfo.speed * 0.66f) );
	}
	else
	{
		delta.scale( MIN(1.4f,R().pullInfo.speed  * 0.66f) );// the faster I go, the harder I slam!

		//Absolute death to be hit by a train, no survival
		if ( R().pullInfo.speed >= modData->m_killSpeedMin ) // they can grab on safely
		{
			other->kill();
			theirPhys->setPitchRate(GameLogicRandomValueReal(-0.03f, 0.03f));
			theirPhys->setRollRate(GameLogicRandomValueReal(-0.03f, 0.03f));
		}
		else
		{
			playImpactSound(other, loc);			//Play a bounce sound

			DamageInfo damageInfo;
			damageInfo.in.m_damageType = DAMAGE_CRUSH;
			damageInfo.in.m_deathType = DEATH_CRUSHED;
			damageInfo.in.m_sourceID = getObject()->getID();
			damageInfo.in.m_amount = R().pullInfo.speed * 10;			// hurt!
			other->attemptDamage( &damageInfo );
		}
	}

	Coord3D heft = *theirLoc;
	heft.z = MAX(heft.z, TheTerrainLogic->getGroundHeight(heft.x, heft.y) + 2); // lift them off the ground
	other->setPosition(&heft);

	delta.z = GameLogicRandomValueReal(0.05f, R().pullInfo.speed/10);	// for some fake heft
	delta.scale(dot);


	// This is a special check so that it won't hurl infantry clear across the map!
	if( ! ( victimIsInfantry && theirPhys->getVelocityMagnitude() > 5.0f ) )
		theirPhys->addVelocityTo( &delta );



	theirPhys->setAllowToFall( TRUE );
	theirPhys->setAllowBouncing( TRUE );
	theirPhys->setAllowAirborneFriction( TRUE );


	const Coord3D up = {0,0,1};
	Coord3D cross;
	myDir->crossProduct( myDir, &up, &cross );

	delta.normalize();
	Real deviationCOG = cross.x * delta.x + cross.y * delta.y + cross.z * delta.z;

	if (dot > 0)
		theirPhys->setYawRate( deviationCOG * -0.06f * R().pullInfo.speed);




}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------


void RailroadBehavior::playImpactSound(Object *victim, const Coord3D *impactPosition)
{
	AudioEventRTS impact;
	//AudioEventRTS *pImpact = &impact;
	PhysicsBehavior *theirPhys = victim->getPhysics();

	Bool hasBounceSound = FALSE;

	if ( theirPhys )
	{
		const AudioEventRTS* bsound = theirPhys->getBounceSound();
		if (bsound)
		{
			impact = *bsound;//copy theirs
			hasBounceSound = TRUE;
		}
	}

	if ( ! hasBounceSound )
	{
		const RailroadBehaviorModuleData *modData = getRailroadBehaviorModuleData();
		if (victim->isKindOf( KINDOF_INFANTRY ))
		{
			impact = modData->m_meatyImpactDefaultSound;//copy
		}
		else if (victim->isKindOf( KINDOF_HUGE_VEHICLE ) || victim->isKindOf( KINDOF_STRUCTURE ))
		{
			impact = modData->m_bigMetalImpactDefaultSound;//copy
		}
		else if (victim->isKindOf( KINDOF_VEHICLE ) )
		{
			impact = modData->m_smallMetalImpactDefaultSound;//copy
		}
	}


	if (impact.getEventName().isEmpty())
		return;

	Real vel = NORMAL_VEL_Z; //the max
	Real mass	= NORMAL_MASS; //the max

	impact.setPosition(impactPosition);
	if ( theirPhys )
	{
		vel += fabs(theirPhys->getVelocity()->length());
		mass += fabs(theirPhys->getMass());

		vel /= 2;
		mass /= 2;//average of him and me
		impact.setPosition(victim->getPosition());
	}

	vel = MIN(NORMAL_VEL_Z, MAX(0, vel));
	mass = MIN(NORMAL_MASS, MAX(0, mass));

	Real volAdjust = NormalizeToRange(MuLaw(vel, NORMAL_VEL_Z, 500), -1, 1, 0.25, 1.0);
	volAdjust *= NormalizeToRange(MuLaw(mass, NORMAL_MASS, 500), -1, 1, 0.25, 1.0);
	impact.setVolume( volAdjust );

	// This sound really should only be played at the position where the collision occurs.
	// Therefore, set the player index of the sound to be the player index of the victim object.
	impact.setPlayerIndex( victim->gxPlayerIndex() );

	TheAudio->addAudioEvent( &impact );
}




// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RailroadBehavior::loadTrackData()
{

	if ( R().track != nullptr )
		return;// lets do this only once!


	//First we scan the map for the nearest waypoint
	Object *obj = getObject();
	const Coord3D *myPos =  obj->getPosition();
	Coord3D delta;
	Real closestDistance = 99999.9f;



	// R().anchorWaypointID COULD HAVE BEEN RECORDED IN XFER, WHICH MEANS I LOADED MY TRACK DATA IN A PRIOR LIFE,
	// SO LETS JUST RE_INIT THE TRACK BASED ON THAT POINT, AUTOMAGICALLY
	Waypoint *anchorWaypoint = nullptr;
	if ( R().anchorWaypointID == INVALID_WAYPOINT_ID )
	{
		Waypoint *anyWaypoint = TheTerrainLogic->getFirstWaypoint();

		while ( anyWaypoint )
		{
			//measure the distance from me to the waypoint
			delta.x = myPos->x - anyWaypoint->getLocation()->x;
			delta.y = myPos->y - anyWaypoint->getLocation()->y;
			delta.z = myPos->z - anyWaypoint->getLocation()->z;
			if (closestDistance > delta.length() )
			{
				closestDistance = delta.length();
				anchorWaypoint = anyWaypoint;
				R().anchorWaypointID = anchorWaypoint->getID();// save for XFER, later
			}

			anyWaypoint = anyWaypoint->getNext();
		}
	}
	else
	{
		anchorWaypoint = TheTerrainLogic->getWaypointByID( R().anchorWaypointID );
	}



	DEBUG_ASSERTCRASH( anchorWaypoint, ( "Railroad... couldn't find a waypoint close enough to be the anchor.\n You should put your train engine right on a waypoint,\n and make sure the path forms a valid track.") );
	if ( ! anchorWaypoint )
		return;






	R().track = NEW( TrainTrack );// this constructor inc's the refcount to 1

	// From now until the next carriage is added, this track is writable using getWritablePointList();
	// This method will return nullptr when refcount is 2 or more
	// getPointList returns the list as const to any caller
	// each carriage must increment the reference when this pointer is passed to it,
	// any subsequent carriage (destructor) will delete this memory here if it releases refcount to zero

	Coord3D fromPos, toPos, fromToDelta;
	R().track->m_length = 0.0f;
	Waypoint *scanner = anchorWaypoint;
	Real distFromTo = 0.0f;


	//Let's start building our own track data from the waypoint data we find
	TrackPointList* track = R().track->getWritablePointList();
	TrackPoint trackPoint; // local workspace

	if ( scanner && track)
	{
		trackPoint.m_distanceFromPrev = 0;
		trackPoint.m_distanceFromFirst = 0;
		trackPoint.m_isFirstPoint = TRUE;
		trackPoint.m_isLastPoint = FALSE;
		trackPoint.m_isTunnelOrBridge = scanner->getName().endsWith("Tunnel");
		trackPoint.m_isStation = scanner->getName().endsWith("Station");
		trackPoint.m_isDisembark = scanner->getName().endsWith("Disembark");
		trackPoint.m_isPingPong = FALSE;
		trackPoint.m_position.set( scanner->getLocation() );
		trackPoint.m_handle = scanner->getID();
		track->push_back( trackPoint );
	}

	while ( scanner )
	{
		trackPoint.clear();// clean up the local workspace to make debugging more fun

		if ( scanner->getNumLinks() )
		{
			Waypoint *anotherWaypoint = scanner->getLink( 0 );

			// if scanner's link is valid, we'll add it to the track. now
			if ( anotherWaypoint != nullptr )
			{

				//measure the track while we are at it
				fromPos = *scanner->getLocation();
				toPos   = *anotherWaypoint->getLocation();
				fromToDelta.x = fromPos.x - toPos.x;
				fromToDelta.y = fromPos.y - toPos.y;
				fromToDelta.z = fromPos.z - toPos.z;
				distFromTo = fromToDelta.length();
				R().track->m_length += distFromTo;

				trackPoint.m_distanceFromPrev = distFromTo;
				trackPoint.m_distanceFromFirst = R().track->m_length;
				trackPoint.m_isFirstPoint = FALSE;
				trackPoint.m_isLastPoint = anotherWaypoint->getLink( 0 ) == nullptr;
				trackPoint.m_isTunnelOrBridge = anotherWaypoint->getName().endsWith("Tunnel");
				trackPoint.m_isStation = anotherWaypoint->getName().endsWith("Station");
				trackPoint.m_isPingPong = scanner->getName().endsWith("PingPong");
				trackPoint.m_isDisembark = scanner->getName().endsWith("Disembark");
				trackPoint.m_position.set( anotherWaypoint->getLocation() );
				trackPoint.m_handle = scanner->getID();
				track->push_back( trackPoint );

			}

			scanner = anotherWaypoint;

		}
		else
			break; // since we have reached a non-linked waypoint


		if ( scanner == anchorWaypoint )
		{
			R().track->m_isLooping = TRUE;
			break; // it must be a looping track. Cool.
		}
	}

}





void RailroadBehavior::makeAWallOutOfThisTrain( Bool on )
{
  if ( on == TRUE )
  	TheAI->pathfinder()->createAWallFromMyFootprint( getObject() ); // Temporarily treat this object as an obstacle.
  else
  	TheAI->pathfinder()->removeWallFromMyFootprint( getObject() );  // Undo createAWallFromMyFootprint.


	if ( R().trailerID != INVALID_ID )
	{
		Object *trailer = TheGameLogic->findObjectByID( R().trailerID );
    if ( trailer )
    {
			static NameKeyType key_RGUpdate = NAMEKEY("RailroadBehavior");
			RailroadBehavior *RGUpdate = (RailroadBehavior*)trailer->findUpdateModule(key_RGUpdate);
			if( RGUpdate )
			{
				RGUpdate->makeAWallOutOfThisTrain( on ); // recursive down the train
			}
    }
	}


}





// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
UpdateSleepTime RailroadBehavior::update()
{


	// load the waypoint data if not loaded
	if( R().trackDataLoaded == FALSE && R().isLocomotive )
	{

		loadTrackData();

		if ( R().track )
		  createCarriages();

		R().trackDataLoaded = TRUE;
	}




	if ( ! R().track )
	{
		return UPDATE_SLEEP_NONE;
	}


	//Object *us = getObject();
	const RailroadBehaviorModuleData *modData = getRailroadBehaviorModuleData();

	if ( R().isLocomotive )
	{

		if ( R().conductorState == APPLY_BRAKES )
		{
			conductorPullInfo.speed *= modData->m_braking;
			if (fabs(conductorPullInfo.speed) < 0.1f)
			{
				conductorPullInfo.speed = 0;
				///////////////////////////////////////( &m_hissySteamSound );

				R().waitAtStationTimer = modData->m_waitAtStationTime;
				R().conductorState = WAIT_AT_STATION;


         makeAWallOutOfThisTrain( TRUE );


				if ( R().disembark )
				{
					disembark();
					R().disembark = FALSE;
				}
      }
		}
		else if ( R().conductorState == WAIT_AT_STATION)
		{
			--R().waitAtStationTimer;
			if ( R().waitAtStationTimer <= 0 && (!R().held) )
			{
				R().conductorState = ACCELERATE;
				conductorPullInfo.speed = 0.05f * conductorPullInfo.m_direction;

				R().runningSound.setPlayingHandle(TheAudio->addAudioEvent( &R().runningSound ));

        makeAWallOutOfThisTrain( FALSE );


			}
			else if ( R().waitAtStationTimer == (modData->m_waitAtStationTime/4) )
			{
				R().whistleSound.setPlayingHandle(TheAudio->addAudioEvent( &R().whistleSound ));
			}

		}
		else if ( R().conductorState == ACCELERATE )
		{
			conductorPullInfo.speed += 0.02f * conductorPullInfo.m_direction; // push start multiplier
			conductorPullInfo.speed *= modData->m_acceleration;
			if ( conductorPullInfo.speed > modData->m_speedMax)
			{
				conductorPullInfo.speed = modData->m_speedMax;
			}
			else if ( conductorPullInfo.speed < -modData->m_speedMax)
			{
				conductorPullInfo.speed = -modData->m_speedMax;
			}


			if ( ! R().runningSound.isCurrentlyPlaying() )
				R().runningSound.setPlayingHandle(TheAudio->addAudioEvent( &R().runningSound ));


		}

	}









	if ( R().wantsToBeLeadCarriage > FRAMES_UNPULLED_LONG_ENOUGH_TO_UNHITCH )//if this flag survived until now, I have lost my puller
	{
		R().isLeadCarriage = TRUE;
	}

	if ( R().isLeadCarriage )
	{

		if ( R().conductorState == COAST )
		{
			conductorPullInfo.speed *= modData->m_friction;
			TheAudio->removeAudioEvent( R().runningSound.getPlayingHandle() );
		}

		conductorPullInfo.trackDistance += conductorPullInfo.speed ;
		// only normalize track position for a looping track, otherwise, let train exit by exceeding tracklength
		while ( (conductorPullInfo.trackDistance > R().track->m_length) && R().track->m_isLooping)
			conductorPullInfo.trackDistance -= R().track->m_length;
		while ( (conductorPullInfo.trackDistance < 0.0f ) && R().track->m_isLooping)
			conductorPullInfo.trackDistance += R().track->m_length;

		FindPosByPathDistance( &conductorPullInfo.towHitchPosition,
														conductorPullInfo.trackDistance,
														R().track->m_length);

		//let the conductor pull "me" while resetting my info, then...
		updatePositionTrackDistance( &conductorPullInfo, &R().pullInfo);


		//get pointer to my trailer
		Object *trailer = TheGameLogic->findObjectByID( R().trailerID );

		if ( trailer )
		{
			//call his pull trailer with my info;
			static NameKeyType key_RGUpdate = NAMEKEY("RailroadBehavior");
			RailroadBehavior *RGUpdate = (RailroadBehavior*)trailer->findUpdateModule(key_RGUpdate);
			if( RGUpdate )
			{
				RGUpdate->getPulled( &R().pullInfo );
			}
		}
		else
		{
			R().trailerID = INVALID_ID;// so I will forget about my trailer and designate myself as the caboose

			if ( R().endOfLine )
				TheGameLogic->destroyObject( getObject() );
		}
	}
	else if ( R().wantsToBeLeadCarriage <= FRAMES_UNPULLED_LONG_ENOUGH_TO_UNHITCH )// if I am not the lead carriage
	{
		R().wantsToBeLeadCarriage ++; // like every young carriage, I aspire to be the lead carriage some day
															// unless getpulled() set this false, I will be on the next update! Joy!
	}


	Drawable *draw = getObject()->getDrawable();
	if (draw)
	{
		if ( ! R().track->m_isLooping )
		{
			draw->setDrawableHidden( R().waitingInWings || R().endOfLine );
		}

			// TURN OFF SMOKE EFFECTS IF WE ARE IN A TUNNEL
		const Coord3D *drawPos = draw->getPosition();
		if (drawPos->z < TheTerrainLogic->getGroundHeight(drawPos->x, drawPos->y) - 3.0f )
			draw->setModelConditionState(MODELCONDITION_OVER_WATER);
		else
			draw->clearModelConditionState(MODELCONDITION_OVER_WATER);

	}


	return UPDATE_SLEEP_NONE;

}

// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------




void RailroadBehavior::disembark()
{
	ContainModuleInterface *contain = getObject()->getContain();
	if (contain)
	{
		contain->orderAllPassengersToExit( CMD_FROM_AI, FALSE );
	}



	Object *trailer = TheGameLogic->findObjectByID( R().trailerID );
	if ( trailer )
	{
		static NameKeyType key_RGUpdate = NAMEKEY("RailroadBehavior");
		RailroadBehavior *RGUpdate = (RailroadBehavior*)trailer->findUpdateModule(key_RGUpdate);
		if( RGUpdate )
		{
			RGUpdate->disembark();
		}
	}

}











///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE ////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////


class PartitionFilterIsValidCarriage : public PartitionFilter
{
private:
	Object* m_obj;
	const RailroadBehaviorModuleData* m_data;

public:

	PartitionFilterIsValidCarriage(Object* obj, const RailroadBehaviorModuleData* data) : m_obj(obj), m_data(data) { }

#if defined(RTS_DEBUG)
	virtual const char* debugGetName() { return "PartitionFilterIsValidCarriage"; }
#endif

	virtual Bool allow(Object *objOther)
	{

		// must exist!
		if ( m_obj == nullptr || objOther == nullptr)
			return FALSE;

		//must not be me!
		if (m_obj == objOther)
			return FALSE;
		// must have railroady goodness
		static NameKeyType key_rb = NAMEKEY("RailroadBehavior");
		RailroadBehavior *rb = (RailroadBehavior*)objOther->findUpdateModule(key_rb);
		if( ! rb )
			return FALSE;

		if ( rb->hasEverBeenHitched() )// two of us can't pull you
			return FALSE;

		// must be our ally (well, maybe)
		if (m_obj->getRelationship(objOther) != ALLIES)
			return FALSE;

		// guess it's carriage-worthy!
		return TRUE;
	}
};


void RailroadBehavior::createCarriages()
{


	if ( ! R().isLocomotive )
	{
		DEBUG_ASSERTCRASH(R().isLocomotive, ("a not locomotive attempted to create carriages"));
		return;
	}

	const RailroadBehaviorModuleData* md = getRailroadBehaviorModuleData();
	Object *self = getObject();



	//First we'll see if the map artist put some cars down on the track for us to find
	Real maxRadius = self->getGeometryInfo().getMajorRadius() * 2.0f;
	Coord3D myHitchLoc  = *self->getPosition();
	Coord3D hitchOffset = *self->getUnitDirectionVector2D();//copy that
	hitchOffset.scale ( - maxRadius );// negative, since I want the back, not the front
	myHitchLoc.add( & hitchOffset );


	PartitionFilterIsValidCarriage pfivc(self, md);
	PartitionFilter *filters[] = { &pfivc, nullptr };


	Object* xferCarriage = nullptr;
	Object *closeCarriage = nullptr;
	Object *firstCarriage = nullptr;

	if ( R().trailerID != INVALID_ID )
	{
		xferCarriage = TheGameLogic->findObjectByID( R().trailerID );
	}

	if (xferCarriage != nullptr)
		closeCarriage = xferCarriage;
	else
		closeCarriage = ThePartitionManager->getClosestObject( &myHitchLoc, maxRadius, FROM_CENTER_2D, filters);

		TemplateNameList list = md->m_carriageTemplateNameData;
		TemplateNameIterator iter = list.begin();
		if ( iter != list.end() )
		{
			const ThingTemplate* temp = TheThingFactory->findTemplate( *iter );




			if (temp)
			{
				if ( closeCarriage )
					firstCarriage = closeCarriage;
				else // or else let's use the defualt template list prvided in the INI
				{
					firstCarriage = TheThingFactory->newObject( temp, self->getTeam() );
					DEBUG_LOG(("%s Added a carriage, %s ", self->getTemplate()->getName().str(),firstCarriage->getTemplate()->getName().str()));
				}

				if ( firstCarriage )
				{
					firstCarriage->setProducer(self);
					R().trailerID = firstCarriage->getID();

					static NameKeyType key_rb = NAMEKEY("RailroadBehavior");
					RailroadBehavior *rb = (RailroadBehavior*)firstCarriage->findUpdateModule(key_rb);
					if( rb )
					{
						if ( closeCarriage )
							rb->hitchNewCarriagebyProximity( self->getID(), R().track );
						else
							rb->hitchNewCarriagebyTemplate( self->getID(), list, ++iter, R().track );// ! increment iter here!
					}
					else
					{
						DEBUG_ASSERTCRASH( rb,
							("%s is attempting to hitch carriage, %s without a RailroadBehavior... \nwhat kind of nutty conductor are you? ",
							self->getTemplate()->getName().str(),
							firstCarriage->getTemplate()->getName().str() ) );
					}
				}

			}
		}

	R().carriagesCreated = TRUE;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RailroadBehavior::hitchNewCarriagebyTemplate( ObjectID locoID, const TemplateNameVector& list, TemplateNameIterator& iter, TrainTrack *track)
{

	if ( R().isLocomotive )
	{
		DEBUG_ASSERTCRASH(R().isLocomotive, ("You can not hitch a locomotive in mid train, dude."));
		return;
	}


	Object *locomotive = TheGameLogic->findObjectByID( locoID );
	if ( !locomotive )// sanity
		return;


	R().track = track;				//ALWAYS INCREFERENCE WHENEVER YOU INHERIT THIS POINTER
	R().track->incReference();//ALWAYS INCREFERENCE WHENEVER YOU INHERIT THIS POINTER

	R().hasEverBeenHitched = TRUE;// so no other trains try to hitch me onto them


	//Okay that's me, now for the next guy
	//---------------------------------------
	Object *newCarriage = nullptr;

	if ( iter != list.end() )// this test is bogus
	{
		const ThingTemplate* temp = TheThingFactory->findTemplate( *iter );

		if (temp)
		{
			newCarriage = TheThingFactory->newObject( temp, locomotive->getTeam() ); // just a little worried about this...
			newCarriage->setProducer(locomotive);
			R().trailerID = newCarriage->getID();

			static NameKeyType key_rb = NAMEKEY("RailroadBehavior");
			RailroadBehavior *rb = (RailroadBehavior*)newCarriage->findUpdateModule(key_rb);
			if( rb )
			{
				rb->hitchNewCarriagebyTemplate( locoID, list, ++iter, R().track );
			}
			else
			{
				DEBUG_ASSERTCRASH( rb,
					("%s could not hitch a %s without a RailroadBehavior... \nwhat kind of nutty conductor are you? \nThe next carriage would have been a %s.",
					locomotive->getTemplate()->getName().str(),
					newCarriage->getTemplate()->getName().str(),
					iter->str()
					) );
			}

		}
	}


}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RailroadBehavior::hitchNewCarriagebyProximity( ObjectID locoID, TrainTrack *track)
{
	if ( R().isLocomotive )
	{
		DEBUG_ASSERTCRASH(R().isLocomotive, ("You can not hitch a locomotive in mid train, dude."));
		return;
	}


	Object *locomotive = TheGameLogic->findObjectByID( locoID );
	if ( !locomotive )// sanity
		return;


	R().track = track;				//ALWAYS INCREFERENCE WHENEVER YOU INHERIT THIS POINTER
	R().track->incReference();//ALWAYS INCREFERENCE WHENEVER YOU INHERIT THIS POINTER

	R().hasEverBeenHitched = TRUE;// so no other trains try to hitch me onto them


	//Okay that's me, now for the next guy
	//---------------------------------------

	//First we'll see if the map artist put some cars down on the track for us to find
	const RailroadBehaviorModuleData* md = getRailroadBehaviorModuleData();
	Object *self = getObject();
	Real maxRadius = self->getGeometryInfo().getMajorRadius() * 2.0f;
	Coord3D myHitchLoc  = *self->getPosition();
	Coord3D hitchOffset = *self->getUnitDirectionVector2D();//copy that
	hitchOffset.scale ( - maxRadius );// negative, since I want the back, not the front
	myHitchLoc.add( & hitchOffset );

	PartitionFilterIsValidCarriage pfivc(self, md);
	PartitionFilter *filters[] = { &pfivc, nullptr };

	Object* xferCarriage = nullptr;
	Object *closeCarriage = nullptr;

	if ( R().trailerID != INVALID_ID )
	{
		xferCarriage = TheGameLogic->findObjectByID( R().trailerID );
	}
	if (xferCarriage != nullptr)
		closeCarriage = xferCarriage;
	else
		closeCarriage = ThePartitionManager->getClosestObject( &myHitchLoc, maxRadius, FROM_CENTER_2D, filters);


	if ( closeCarriage )
	{
		closeCarriage->setProducer(self);
		R().trailerID = closeCarriage->getID();

		static NameKeyType key_rb = NAMEKEY("RailroadBehavior");
		RailroadBehavior *rb = (RailroadBehavior*)closeCarriage->findUpdateModule(key_rb);
		if( rb )
			rb->hitchNewCarriagebyProximity( self->getID(), R().track );
		else
		{
			DEBUG_ASSERTCRASH( rb,
				("%s is attempting to hitch carriage, %s without a RailroadBehavior... \nwhat kind of nutty conductor are you? ",
				self->getTemplate()->getName().str(),
				closeCarriage->getTemplate()->getName().str() ) );
		}
	}





}



// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RailroadBehavior::getPulled( PullInfo *info )
{
	//ENFORCE MY STATUS AS A PULLEE, NOT A PULLER, and update my position, speed etc.
	R().wantsToBeLeadCarriage = 0;

	if ( ! R().track )
	{
		return;
	}





	conductorPullInfo = *info;//copy my puller to my conductor, so that if I ever get detached
	//( meaning that if this function stops getting called ), I will take over as the lead carriage!
	//Wheeeee!!



	info->previousWaypoint = info->currentWaypoint;

	updatePositionTrackDistance( info, &R().pullInfo );


	//get pointer to my trailer
	Object *trailer = TheGameLogic->findObjectByID( R().trailerID );

	if ( trailer )
	{
		//call his pull trailer with MY info;
		static NameKeyType key_RGUpdate = NAMEKEY("RailroadBehavior");
		RailroadBehavior *RGUpdate = (RailroadBehavior*)trailer->findUpdateModule(key_RGUpdate);
		if( RGUpdate )
		{
			RGUpdate->getPulled( &R().pullInfo ); //< this is MY info, not the one passed to me
		}
	}
	else
	{
		R().trailerID = INVALID_ID;// so I will forget about my trailer and designate myself as the caboose
		if ( R().endOfLine )
			TheGameLogic->destroyObject( getObject() );

	}



}





// ------------------------------------------------------------------------------------------------

void alignToTerrain( Real angle, const Coord3D& pos, const Coord3D& normal, Matrix3D& mtx)
{
	Coord3D x, y, z;

	z = normal;

	x.x = Cos( angle );
	x.y = Sin( angle );
	x.z = 0.0f;
	if (z.z != 0.0f)
	{
		x.z = -(x.x*z.x + x.y*z.y) / z.z;
		x.normalize();
	}

	DEBUG_ASSERTCRASH(fabs(x.x*z.x + x.y*z.y + x.z*z.z)<0.0001,("dot is not zero"));

	// now computing the y vector is trivial.
	y.crossProduct( &z, &x, &y );
	y.normalize();

	mtx.Set(  x.x, y.x, z.x, pos.x,
							x.y, y.y, z.y, pos.y,
							x.z, y.z, z.z, pos.z );
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RailroadBehavior::updatePositionTrackDistance( PullInfo *pullerInfo, PullInfo *myInfo )
{

	if ( ! R().track )
	{
		return;
	}



	Object *obj = BehaviorModule::getObject();

	if ( ! obj )
		return; //sanity

	// IF THIS HAS BEEN CALLED, AM AM GETTING PULLED BY SOMETHING,
	// my conductor, or another car

	Real hitchRadius = obj->getGeometryInfo().getMajorRadius();
	myInfo->trackDistance = pullerInfo->trackDistance - (hitchRadius * 2);
	myInfo->speed = pullerInfo->speed;// YES, if I am getting pulled, I must obey puller
	myInfo->m_direction = pullerInfo->m_direction;// YES, if I am getting pulled, I must obey puller


//	pullerInfo->previousWaypoint = pullerInfo->currentWaypoint;// to feed edge test in update


// I THINK THE TURN_TO POINT SHOULD BE A BONE ON THE PREVIOUS CAR!!!!!!
	FindPosByPathDistance( &myInfo->towHitchPosition,
													myInfo->trackDistance, // the look ahead
													R().track->m_length);

	Coord3D carPosition;
	FindPosByPathDistance( &carPosition,
													myInfo->trackDistance,
													R().track->m_length, TRUE);


	Coord3D turnPos = *obj->getPosition();

	if (!R().inTunnel)
		turnPos.z = TheTerrainLogic->getGroundHeight( turnPos.x, turnPos.y );

	const Coord3D* dir = obj->getUnitDirectionVector2D();
	turnPos.x += dir->x * -hitchRadius;
	turnPos.y += dir->y * -hitchRadius;
	Coord3D trackPosDelta;
	trackPosDelta.x = carPosition.x - turnPos.x;
	trackPosDelta.y = carPosition.y - turnPos.y;
	trackPosDelta.z = 0;
	Real dx = pullerInfo->towHitchPosition.x - turnPos.x;
	Real dy = pullerInfo->towHitchPosition.y - turnPos.y;
	Real desiredAngle = atan2(dy, dx);


	Real relAngle = stdAngleDiff(desiredAngle, obj->getTransformMatrix()->Get_Z_Rotation());


	Matrix3D mtx;
	Matrix3D tmp(1);
	tmp.Translate(turnPos.x, turnPos.y, 0);
	tmp.Translate(trackPosDelta.x, trackPosDelta.y, 0);
	tmp.In_Place_Pre_Rotate_Z(relAngle );

	tmp.Translate(-turnPos.x, -turnPos.y, 0);


	mtx.mul(tmp, *obj->getTransformMatrix());


	//enforce ground elevation
	Coord3D normal ;
	Real enforceElevation = TheTerrainLogic->getGroundHeight( turnPos.x, turnPos.y, &normal );




	obj->setTransformMatrix(&mtx);

	if (!R().inTunnel)
		obj->setPositionZ( enforceElevation );



	obj->handlePartitionCellMaintenance();

}


//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
void RailroadBehavior::destroyTheWholeTrainNow()
{
	TheGameLogic->destroyObject( getObject());

	Object *trailer = TheGameLogic->findObjectByID( R().trailerID );
	if ( trailer )
	{
		static NameKeyType key_RGUpdate = NAMEKEY("RailroadBehavior");
		RailroadBehavior *RGUpdate = (RailroadBehavior*)trailer->findUpdateModule(key_RGUpdate);
		if( RGUpdate )
		{
			RGUpdate->destroyTheWholeTrainNow(); //< this is MY info, not the one passed to me
		}
	}


}












// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RailroadBehavior::FindPosByPathDistance( Coord3D *pos, const Real dist, const Real length, Bool setState )
{

	if ( ! R().track )
		return;

	R().waitingInWings = FALSE;

	Real actualDistance = dist;// WRAP TO NORMALIZE

	if ( R().track->m_isLooping )
	{
		while ( actualDistance < 0.0f )
		{
			actualDistance += length;
		}
		while ( actualDistance > length )
		{
			actualDistance -= length;
		}
	}
	else
	{

		if ( dist < 0 )
		{
			actualDistance = 0;
			R().waitingInWings = TRUE;
		}
		else if ( dist >= length )
		{
			actualDistance = length;
			R().endOfLine = TRUE;
		}

		actualDistance = MAX(MIN(length,dist),0); // CLAMP TO NORMALIZE
	}

	pos->set( 0.0f, 0.0f, 0.0f );

	const TrackPointList *pointList = R().track->getPointList();//read-only
	if ( ! pointList )
		return;

	std::list<TrackPoint>::const_iterator pointIter = pointList->begin();


	while ( pointIter != pointList->end() )
	{
		const TrackPoint *thisPoint = &(*pointIter);
		++pointIter;// next pointIter in this list, so then...


		if (thisPoint && thisPoint->m_distanceFromFirst < actualDistance)// I am after this point, and
		{
			Coord3D thisPointPos = thisPoint->m_position;
			const TrackPoint *nextPoint = nullptr;

			// TheSuperHackers Mauller 02/04/2025 Prevent dereferencing of endpoint pointer which throws asserts during Debug
			if (pointIter != pointList->end()) {
				 nextPoint = &(*pointIter);
			}

			if (nextPoint && nextPoint->m_distanceFromFirst > actualDistance)
			{
				//I am between this point and the next
				const Int handleFound = ((TrackPoint*)thisPoint)->getHandle();
				Bool edge = (R().currentPointHandle != handleFound);
				if ( setState  )
				{
					R().inTunnel = thisPoint->m_isTunnelOrBridge;

					if ( R().isLocomotive )// since only locomotives care about such things
					{
						// THE LOCOMOTIVE SNIFFS THE TRACK TO SMELL FOR THE NEXT STATION AND TUNNEL
						if ( edge )
						{
							R().currentPointHandle = handleFound;
							if ( thisPoint->m_isStation )
							{
								R().conductorState = APPLY_BRAKES;
								R().disembark = FALSE;
								TheAudio->removeAudioEvent(R().runningSound.getPlayingHandle());
							}
							else if ( thisPoint->m_isDisembark )
							{
								R().conductorState = APPLY_BRAKES;
								R().disembark = TRUE;
								TheAudio->removeAudioEvent(R().runningSound.getPlayingHandle());
							}
							else if ( thisPoint->m_isPingPong && conductorPullInfo.m_mostRecentSpecialPointHandle != handleFound )
							{
								conductorPullInfo.m_mostRecentSpecialPointHandle = handleFound;
								R().conductorState = APPLY_BRAKES;
								R().disembark = FALSE;
								TheAudio->removeAudioEvent(R().runningSound.getPlayingHandle());
								conductorPullInfo.m_direction = -conductorPullInfo.m_direction;
							}
						}
					}

					if ( edge && ! R().inTunnel )
					{//play my clickety clack sound, `cause I just rode over a join IN the tracks
						TheAudio->addAudioEvent( &R().clicketyClackSound );
						R().clicketyClackSound.setPosition( getObject()->getPosition() );
						R().clicketyClackSound.setVolume( (Real)conductorPullInfo.speed / 10.0f );//assumed max speed
					}
				}



				Real difference = actualDistance - thisPoint->m_distanceFromFirst;

				Coord3D delta        = nextPoint->m_position;

				delta.sub( &thisPointPos );
				delta.normalize();
				delta.scale( difference );
				thisPointPos.add( &delta );

				*pos = thisPointPos;//copy out
				return;
			}
			else
				*pos = thisPointPos;//copy out

		}

	}

	//DEBUG_CRASH(("Railroad could not find a position on the path!"));

}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void RailroadBehavior::crc( Xfer *xfer )
{
	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Added... like, everything.
	* 3: m_held script driven flag for hanging out
	**/
// ------------------------------------------------------------------------------------------------
void RailroadBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// PLEASE NOTE:
	// R().track and trackDataLoaded are indeed NOT xferred,
	// since these are inited afresh within the update module,


	if ( version >= 2 )
	{
		// extend base class
		PhysicsBehavior::xfer( xfer );

		//StationTask R().nextStationTask;
		xfer->xferUser( &R().nextStationTask, sizeof( StationTask ) );

		//ObjectID R().trailerID; ///< the ID of the object I am directly pulling
		xfer->xferObjectID( &R().trailerID );

		//Int R().currentPointHandle;
		xfer->xferInt( &R().currentPointHandle );

		//Int R().waitAtStationTimer;
		xfer->xferInt( &R().waitAtStationTimer );

		//Bool R().carriagesCreated; ///< TRUE once we have made all the cars in the train
		xfer->xferBool( &R().carriagesCreated );

		//Bool R().hasEverBeenHitched; /// has somebody ever hitched me? Remains true, even after puller dies.
		xfer->xferBool( &R().hasEverBeenHitched );

		//Bool R().waitingInWings; /// I have not entered the real track yet, so leave me alone
		xfer->xferBool( &R().waitingInWings );

		//Bool R().endOfLine;				/// I have reached the end of a non looping track
		xfer->xferBool( &R().endOfLine );

		//Bool R().isLocomotive; ///< Am I a locomotive,
		xfer->xferBool( &R().isLocomotive );

		//Bool R().isLeadCarriage; ///< Am the carriage in front,
		xfer->xferBool( &R().isLeadCarriage );

		//Int R().wantsToBeLeadCarriage; ///< Am the carriage in front,
		xfer->xferInt( &R().wantsToBeLeadCarriage );

		//Bool R().disembark; ///< If I wait at a station, I should also evacuate everybody when I get there
		xfer->xferBool( &R().disembark );

		//Bool R().inTunnel; ///< Am I in a tunnel, so I wil not snap to ground height, until the next waypoint,
		xfer->xferBool( &R().inTunnel );

		//ConductorState R().conductorState;
		xfer->xferUser( &R().conductorState, sizeof( ConductorState ) );

		xfer->xferUser( &R().anchorWaypointID, sizeof( WaypointID ) );

		//PullInfo R().pullInfo;
		R().pullInfo.xferPullInfo( xfer );

		//PullInfo conductorPullInfo;
		conductorPullInfo.xferPullInfo( xfer );

	}

	if( version >= 3 )
	{
		xfer->xferBool( &R().held );
	}


}



void RailroadBehavior::PullInfo::xferPullInfo( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	xfer->xferReal( &m_direction);
	xfer->xferReal( &speed);
	xfer->xferReal( &trackDistance);
	xfer->xferCoord3D( &towHitchPosition );
	xfer->xferInt( &m_mostRecentSpecialPointHandle );
	xfer->xferUnsignedInt( &previousWaypoint );
	xfer->xferUnsignedInt( &currentWaypoint );
}


// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void RailroadBehavior::loadPostProcess()
{
	// extend base class
	PhysicsBehavior::loadPostProcess();

	//Bool R().trackDataLoaded; ///< have I TRIED to load track data, yet? I only try once!
	R().trackDataLoaded = FALSE;




	const RailroadBehaviorModuleData *modData = getRailroadBehaviorModuleData();
	R().runningSound = modData->m_runningSound;
	R().clicketyClackSound = modData->m_clicketyClackSound;
	R().whistleSound = modData->m_whistleSound;

	R().runningSound.setObjectID( getObject()->getID() );
	R().whistleSound.setObjectID( getObject()->getID() ) ;
	R().clicketyClackSound.setObjectID( getObject()->getID() ) ;

}

