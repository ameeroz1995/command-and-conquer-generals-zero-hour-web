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

// JetAIUpdate.cpp //////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "Common/GxComponentPool.h"

#define DEFINE_LOCOMOTORSET_NAMES

#include "Common/ActionManager.h"
#include "Common/GlobalData.h"
#include "Common/MiscAudio.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CountermeasuresBehavior.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Module/ParkingPlaceBehavior.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"

const Real BIGNUM = 99999.0f;


//-------------------------------------------------------------------------------------------------
enum TaxiType CPP_11(: Int)
{
	FROM_HANGAR,
	FROM_PARKING,
	TO_PARKING
};

//-------------------------------------------------------------------------------------------------
enum JetAIStateType CPP_11(: Int)
{
	// note that these must be distinct (numerically) from AIStateType. ick.
	JETAISTATETYPE_FIRST = 1000,

	TAXI_FROM_HANGAR,
	TAKING_OFF_AWAIT_CLEARANCE,
	TAXI_TO_TAKEOFF,
	PAUSE_BEFORE_TAKEOFF,
	TAKING_OFF,
	LANDING_AWAIT_CLEARANCE,
	LANDING,
	TAXI_FROM_LANDING,
	ORIENT_FOR_PARKING_PLACE,
	RELOAD_AMMO,
	RETURNING_FOR_LANDING,
	RETURN_TO_DEAD_AIRFIELD,
	CIRCLING_DEAD_AIRFIELD,

	JETAISTATETYPE_LAST
};


//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::getFlag( FlagType f ) const
{
	return (R().flags & (1<<f)) != 0;
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::setFlag( FlagType f, Bool v)
{
	if (v)
		R().flags |= (1<<f);
	else
		R().flags &= ~(1<<f);
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isOutOfSpecialReloadAmmo() const
{
	const Object* jet = getObject();
	// if we have at least one special reload weapon,
	// AND all such weapons are out of ammo,
	// return true.
	Int specials = 0;
	Int out = 0;
	for( Int i = 0; i < WEAPONSLOT_COUNT;	i++ )
	{
		const Weapon* weapon = jet->getWeaponInWeaponSlot((WeaponSlotType)i);
		if (weapon == nullptr || weapon->getReloadType() != RETURN_TO_BASE_TO_RELOAD)
			continue;
		++specials;
		if (weapon->getStatus() == OUT_OF_AMMO)
			++out;
	}
	return specials > 0 && out == specials;
}

//-------------------------------------------------------------------------------------------------
static ParkingPlaceBehaviorInterface* getPP(ObjectID id, Object** airfieldPP = nullptr)
{
	if (airfieldPP)
		*airfieldPP = nullptr;

	Object* airfield = TheGameLogic->findObjectByID( id );
	if (airfield == nullptr || airfield->isEffectivelyDead() || !airfield->isKindOf(KINDOF_FS_AIRFIELD) || airfield->testStatus(OBJECT_STATUS_SOLD))
		return nullptr;

	if (airfieldPP)
		*airfieldPP = airfield;

	ParkingPlaceBehaviorInterface* pp = nullptr;
	for (BehaviorModule** i = airfield->getBehaviorModules(); *i; ++i)
	{
		if ((pp = (*i)->getParkingPlaceBehaviorInterface()) != nullptr)
			break;
	}

	return pp;
}

//-------------------------------------------------------------------------------------------------
class PartitionFilterHasParkingPlace : public PartitionFilter
{
private:
	ObjectID m_id;
public:
	PartitionFilterHasParkingPlace(ObjectID id) : m_id(id) { }
protected:
#if defined(RTS_DEBUG)
	virtual const char* debugGetName() { return "PartitionFilterHasParkingPlace"; }
#endif
	virtual Bool allow(Object *objOther)
	{
		ParkingPlaceBehaviorInterface* pp = getPP(objOther->getID());
		if (pp != nullptr && pp->reserveSpace(m_id, 0.0f, nullptr))
			return true;
		return false;
	}
};

//-------------------------------------------------------------------------------------------------
static Object* findSuitableAirfield(Object* jet)
{
	PartitionFilterAcceptByKindOf					filterKind(MAKE_KINDOF_MASK(KINDOF_FS_AIRFIELD), KINDOFMASK_NONE);
	PartitionFilterRejectByObjectStatus		filterStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNDER_CONSTRUCTION ), OBJECT_STATUS_MASK_NONE );
	PartitionFilterRejectByObjectStatus		filterStatusTwo( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_SOLD ), OBJECT_STATUS_MASK_NONE ); // Independent to make it an OR
	PartitionFilterRelationship						filterTeam(jet, PartitionFilterRelationship::ALLOW_ALLIES);
	PartitionFilterAlive									filterAlive;
	PartitionFilterSameMapStatus					filterMapStatus(jet);
	PartitionFilterHasParkingPlace				filterPP(jet->getID());

	PartitionFilter *filters[16];
	Int numFilters = 0;
	filters[numFilters++] = &filterKind;
	filters[numFilters++] = &filterStatus;
	filters[numFilters++] = &filterStatusTwo;
	filters[numFilters++] = &filterTeam;
	filters[numFilters++] = &filterAlive;
	filters[numFilters++] = &filterPP;
	filters[numFilters++] = &filterMapStatus;
	filters[numFilters] = nullptr;

	return ThePartitionManager->getClosestObject( jet, HUGE_DIST, FROM_CENTER_2D, filters );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/*
	Success: we have runway clearance
	Failure: no runway clearance
*/
class JetAwaitingRunwayState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetAwaitingRunwayState, "JetAwaitingRunwayState")
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
private:

public:
	JetAwaitingRunwayState( StateMachine *machine, Bool landing ) :  State( machine, "JetAwaitingRunwayState") {
		m_gxRow = GxComponentPool<GxState>::the().alloc();
		R().landing = landing; }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		jetAI->friend_setTakeoffInProgress(!R().landing);
		jetAI->friend_setLandingInProgress(R().landing);
		jetAI->friend_setAllowCircling(true);
		return STATE_CONTINUE;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == nullptr)
		{
			// no producer? just skip this step.
			return STATE_SUCCESS;
		}

		// gotta reserve a space in order to reserve a runway
		if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), nullptr))
		{
			DEBUG_ASSERTCRASH(R().landing, ("hmm, this should never happen for taking-off things"));
			return STATE_FAILURE;
		}

		if (pp->reserveRunway(jet->getID(), R().landing))
		{
			return STATE_SUCCESS;
		}
		else if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) && !R().landing )
		{
			//If we're trying to take off an aircraft carrier and fail to reserve a
			//runway, it's because we need to be at the front of the carrier queue.
			//Therefore, we need to move forward whenever possible until we are in
			//the front.
			Coord3D bestPos;
			if( pp->calcBestParkingAssignment( jet->getID(), &bestPos ) )
			{
				jetAI->friend_setTaxiInProgress(true);
				jetAI->friend_setAllowAirLoco(false);
				jetAI->chooseLocomotorSet(LOCOMOTORSET_TAXIING);

				jetAI->destroyPath();
				Path *movePath;
				movePath = newInstance(Path);
				Coord3D pos = *jet->getPosition();
				movePath->prependNode( &pos, LAYER_GROUND );
				movePath->markOptimized();
				movePath->appendNode( &bestPos, LAYER_GROUND );

				TheAI->pathfinder()->setDebugPath(movePath);

				jetAI->friend_setPath( movePath );
				DEBUG_ASSERTCRASH(jetAI->getCurLocomotor(), ("no loco"));
				jetAI->getCurLocomotor()->setUsePreciseZPos(true);
				jetAI->getCurLocomotor()->setUltraAccurate(true);
				jetAI->getCurLocomotor()->setAllowInvalidPosition(true);
				jetAI->ignoreObstacleID(jet->getProducerID());
			}
		}

		// can't get a runway? gotta wait.
		jetAI->setLocomotorGoalNone();
		return STATE_CONTINUE;
	}

	virtual void onExit(StateExitType status)
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if (jetAI)
		{
			jetAI->friend_setTakeoffInProgress(false);
			jetAI->friend_setLandingInProgress(false);
			jetAI->friend_setAllowCircling(false);
		}
	}

public:
	// web port (round 5, units are pure data): the per-unit state is a row of GxComponentPool<GxState>;
	// this module object is the facade (INI, xfer, crc) holding its row handle.
	struct GxState
	{
		Bool landing;
	};
	GxState &R() const;
private:
	int m_gxRow;
};
inline JetAwaitingRunwayState::GxState &JetAwaitingRunwayState::R() const { return GxComponentPool<GxState>::the()[m_gxRow]; }
JetAwaitingRunwayState::~JetAwaitingRunwayState()
{
	GxComponentPool<GxState>::the().release(m_gxRow);
}

//-------------------------------------------------------------------------------------------------
/*
	Success: a new suitable airfield has appeared
	Failure: shouldn't normally happen
*/
class JetOrHeliCirclingDeadAirfieldState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliCirclingDeadAirfieldState, "JetOrHeliCirclingDeadAirfieldState")
protected:
	// snapshot interface	 STUBBED.
	// The state will check immediately after a load game, but I think that's ok.  jba.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};

private:

	enum
	{
		// only recheck for new airfields every second or so
		HOW_OFTEN_TO_CHECK = LOGICFRAMES_PER_SECOND
	};

public:
	JetOrHeliCirclingDeadAirfieldState( StateMachine *machine ) :
		State( machine, "JetOrHeliCirclingDeadAirfieldState") {
		m_gxRow = GxComponentPool<GxState>::the().alloc();
		R().checkAirfield = 0; }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
		{
			return STATE_FAILURE;
		}

		// obscure case: if the jet wasn't spawned, but just placed directly on the map,
		// it might not have an owning airfield, and it might be trying to return
		// simply due to being idle, not out of ammo. so check and don't die in that
		// case, but just punt back out to idle.
		if (!jetAI->isOutOfSpecialReloadAmmo() && jet->getProducerID() == INVALID_ID)
		{
			return STATE_FAILURE;
		}

		// just stay where we are.
		jetAI->setLocomotorGoalNone();

		R().checkAirfield = HOW_OFTEN_TO_CHECK;

		//Play the "low fuel" voice whenever the craft is circling above the airfield.
		AudioEventRTS soundToPlay = *jet->getTemplate()->getPerUnitSound( "VoiceLowFuel" );
		soundToPlay.setObjectID( jet->getID() );
		TheAudio->addAudioEvent( &soundToPlay );

		return STATE_CONTINUE;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
		{
			return STATE_FAILURE;
		}

		// just stay where we are.
		jetAI->setLocomotorGoalNone();

		Real damageRate = jetAI->friend_getOutOfAmmoDamagePerSecond();
		if (damageRate > 0)
		{
			// convert to damage/sec to damage/frame
			damageRate *= SECONDS_PER_LOGICFRAME_REAL;
			// since it's a percentage, multiply times the max health
			damageRate *= jet->getBodyModule()->getMaxHealth();

			DamageInfo damageInfo;
			damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
			damageInfo.in.m_deathType = DEATH_NORMAL;
			damageInfo.in.m_sourceID = INVALID_ID;
			damageInfo.in.m_amount = damageRate;
			jet->attemptDamage( &damageInfo );
		}

		if (--R().checkAirfield <= 0)
		{
			R().checkAirfield = HOW_OFTEN_TO_CHECK;
			Object* airfield = findSuitableAirfield( jet );
			if (airfield)
			{
				jet->setProducer(airfield);
				return STATE_SUCCESS;
			}
		}

		return STATE_CONTINUE;
	}

public:
	// web port (round 5, units are pure data): the per-unit state is a row of GxComponentPool<GxState>;
	// this module object is the facade (INI, xfer, crc) holding its row handle.
	struct GxState
	{
		Int checkAirfield;
	};
	GxState &R() const;
private:
	int m_gxRow;
};
inline JetOrHeliCirclingDeadAirfieldState::GxState &JetOrHeliCirclingDeadAirfieldState::R() const { return GxComponentPool<GxState>::the()[m_gxRow]; }
JetOrHeliCirclingDeadAirfieldState::~JetOrHeliCirclingDeadAirfieldState()
{
	GxComponentPool<GxState>::the().release(m_gxRow);
}

//-------------------------------------------------------------------------------------------------
/*
	Success: we returned to the dead-airfield location
	Failure: shouldn't normally happen
*/
class JetOrHeliReturningToDeadAirfieldState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliReturningToDeadAirfieldState, "JetOrHeliReturningToDeadAirfieldState")
public:
	JetOrHeliReturningToDeadAirfieldState( StateMachine *machine ) : AIInternalMoveToState( machine, "JetOrHeliReturningToDeadAirfieldState") { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
		{
			return STATE_FAILURE;
		}

		setAdjustsDestination(true);
		AIInternalMoveToState::gxRow().goalPosition = *jetAI->friend_getProducerLocation();

		return AIInternalMoveToState::onEnter();
	}

};
EMPTY_DTOR(JetOrHeliReturningToDeadAirfieldState)

//-------------------------------------------------------------------------------------------------
// This solution uses the
// http://www.faqs.org/faqs/graphics/algorithms-faq/
// Subject 1.03
static Bool intersectInfiniteLine2D
(
	Real ax, Real ay, Real ao,
	Real cx, Real cy, Real co,
	Real& ix, Real& iy
)
{
	Real bx = ax + Cos(ao);
	Real by = ay + Sin(ao);
	Real dx = cx + Cos(co);
	Real dy = cy + Sin(co);

	Real denom = ((bx - ax) * (dy - cy) - (by - ay) * (dx - cx));
	if (denom == 0.0f)
	{
		// the lines are parallel.
		return false;
	}

	// The lines intersect.
	Real r = ((ay - cy) * (dx - cx) - (ax - cx) * (dy - cy) ) / denom;
	ix = ax + r * (bx - ax);
	iy = ay + r * (by - ay);
	return true;
}

//-------------------------------------------------------------------------------------------------
/*
	Success: we are on the ground at the runway start
	Failure: we are unable to get on the ground
*/
class JetOrHeliTaxiState : public AIMoveOutOfTheWayState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliTaxiState, "JetOrHeliTaxiState")
private:
public:
	JetOrHeliTaxiState( StateMachine *machine, TaxiType m ) :  AIMoveOutOfTheWayState( machine ) {
		m_gxRow = GxComponentPool<GxState>::the().alloc();
		R().taxiMode = m; }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		jetAI->setCanPathThroughUnits(true);
		jetAI->friend_setTakeoffInProgress(R().taxiMode != TO_PARKING);
		jetAI->friend_setLandingInProgress(R().taxiMode == TO_PARKING);
		jetAI->friend_setTaxiInProgress(true);

		if( R().taxiMode == TO_PARKING )
		{
			//Instantly reload flares.
			CountermeasuresBehaviorInterface *cbi = jet->getCountermeasuresBehaviorInterface();
			if( cbi )
			{
				cbi->reloadCountermeasures();
			}
		}

		jetAI->friend_setAllowAirLoco(false);
		jetAI->chooseLocomotorSet(LOCOMOTORSET_TAXIING);
		DEBUG_ASSERTCRASH(jetAI->getCurLocomotor(), ("no loco"));

		Object* airfield;
		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID(), &airfield);
		if (pp == nullptr)
			return STATE_SUCCESS;	// no airfield? just skip this step.

		ParkingPlaceBehaviorInterface::PPInfo ppinfo;
		if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
			return STATE_FAILURE;	// full?

		Coord3D intermedPt;
		Bool intermed = false;
		Real orient = atan2(ppinfo.runwayPrep.y - ppinfo.parkingSpace.y, ppinfo.runwayPrep.x - ppinfo.parkingSpace.x);
		if (fabs(stdAngleDiff(orient, ppinfo.parkingOrientation)) > PI/128)
		{
			intermedPt.z = (ppinfo.parkingSpace.z + ppinfo.runwayPrep.z) * 0.5f;
			intermed = intersectInfiniteLine2D(
				ppinfo.parkingSpace.x, ppinfo.parkingSpace.y, ppinfo.parkingOrientation,
				ppinfo.runwayPrep.x, ppinfo.runwayPrep.y, ppinfo.parkingOrientation + PI/2,
				intermedPt.x, intermedPt.y);
		}

		jetAI->destroyPath();
		Path *movePath;
		movePath = newInstance(Path);
		Coord3D pos = *jet->getPosition();
		movePath->prependNode( &pos, LAYER_GROUND );
		movePath->markOptimized();

		if (R().taxiMode == TO_PARKING)
		{
			if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
			{
				//We're on an aircraft carrier.
				const std::vector<Coord3D> *pTaxiLocations = pp->getTaxiLocations( jet->getID() );
				if( pTaxiLocations )
				{
					std::vector<Coord3D>::const_iterator it;
					for( it = pTaxiLocations->begin(); it != pTaxiLocations->end(); it++ )
					{
						movePath->appendNode( &(*it), LAYER_GROUND );
					}
				}

				//We just landed... see if we can get a better space forward so we don't stop and pause
				//at our initially assigned spot.
				Coord3D pos;
				pp->calcBestParkingAssignment( jet->getID(), &pos );

				movePath->appendNode( &pos, LAYER_GROUND );
			}
			else
			{
				//We're on a normal airfield
				movePath->appendNode( &ppinfo.runwayPrep, LAYER_GROUND );
				if (intermed)
					movePath->appendNode( &intermedPt, LAYER_GROUND );
				movePath->appendNode( &ppinfo.parkingSpace, LAYER_GROUND );
			}
		}
		else if (R().taxiMode == FROM_PARKING)
		{
			if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
			{
				if( !(ppinfo.runwayStart == ppinfo.runwayPrep) )
				{
					movePath->appendNode( &ppinfo.runwayStart, LAYER_GROUND );
				}
			}
			else
			{
				if (intermed)
					movePath->appendNode( &intermedPt, LAYER_GROUND );
				movePath->appendNode( &ppinfo.runwayPrep, LAYER_GROUND );
				movePath->appendNode( &ppinfo.runwayStart, LAYER_GROUND );
			}
		}
		else if (R().taxiMode == FROM_HANGAR)
		{
			if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
			{
				//Aircraft carrier
				if( jet->testStatus( OBJECT_STATUS_REASSIGN_PARKING ) )
				{
					//This status means we are being reassigned a parking space. We're not actually moving from the
					//hangar. So simply move to the new parking spot which was just switched from under us in
					//FlightDeckBehavior::update()
					jet->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_REASSIGN_PARKING ) );
					movePath->appendNode( &ppinfo.runwayPrep, LAYER_GROUND );
				}
				else
				{
					const std::vector<Coord3D> *pCreationLocations = pp->getCreationLocations( jet->getID() );
					if( !pCreationLocations )
					{
						DEBUG_CRASH( ("No creation locations specified for runway for JetAIBehavior -- taxiing from hanger (Kris).") );
						return STATE_FAILURE;
					}
					std::vector<Coord3D>::const_iterator it;
					Bool firstNode = TRUE;
					for( it = pCreationLocations->begin(); it != pCreationLocations->end(); it++ )
					{
						if( firstNode )
						{
							//Skip the first node because it's the creation location.
							firstNode = FALSE;
							continue;
						}
						movePath->appendNode( &(*it), LAYER_GROUND );
					}
					movePath->appendNode( &ppinfo.runwayPrep, LAYER_GROUND );
				}
			}
			else
			{
				//Airfield
 				movePath->appendNode( &ppinfo.parkingSpace, LAYER_GROUND );
			}
		}

		AIInternalMoveToState::gxRow().waitingForPath = FALSE;
		TheAI->pathfinder()->setDebugPath(movePath);

		setAdjustsDestination(false);	// precision is necessary

		jetAI->friend_setPath( movePath );
		DEBUG_ASSERTCRASH(jetAI->getCurLocomotor(), ("no loco"));
		jetAI->getCurLocomotor()->setUsePreciseZPos(true);
		jetAI->getCurLocomotor()->setUltraAccurate(true);
		jetAI->getCurLocomotor()->setAllowInvalidPosition(true);
		jetAI->ignoreObstacleID(jet->getProducerID());

		StateReturnType ret = AIMoveOutOfTheWayState::onEnter();
		return ret;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		if( R().taxiMode == TO_PARKING || R().taxiMode == FROM_HANGAR )
		{
			//Keep checking to see if there is a better spot as it moves forward. If we find a better spot, then
			//append the position to our move.
			ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
			Coord3D bestPos;
			Int oldIndex, newIndex;
			// Check pp for null, as it is possible for your airfield to get destroyed while taxiing.jba [8/27/2003]
			if( pp!=nullptr && pp->calcBestParkingAssignment( jet->getID(), &bestPos, &oldIndex, &newIndex ) )
			{
				Path *path = jetAI->friend_getPath();
				if( path )
				{
					path->appendNode( &bestPos, LAYER_GROUND );
				}
			}
		}

		return AIMoveOutOfTheWayState::update();
	}

	virtual void onExit( StateExitType status )
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if (jetAI)
		{
			jetAI->getCurLocomotor()->setUsePreciseZPos(false);
			jetAI->getCurLocomotor()->setUltraAccurate(false);
			jetAI->getCurLocomotor()->setAllowInvalidPosition(false);
			jetAI->friend_setTakeoffInProgress(false);
			jetAI->friend_setLandingInProgress(false);
			jetAI->friend_setTaxiInProgress(false);
			jetAI->setCanPathThroughUnits(false);
		}

		AIMoveOutOfTheWayState::onExit(status);
	}

public:
	// web port (round 5, units are pure data): the per-unit state is a row of GxComponentPool<GxState>;
	// this module object is the facade (INI, xfer, crc) holding its row handle.
	struct GxState
	{
		TaxiType taxiMode;
	};
	GxState &R() const;
private:
	int m_gxRow;
};
inline JetOrHeliTaxiState::GxState &JetOrHeliTaxiState::R() const { return GxComponentPool<GxState>::the()[m_gxRow]; }
JetOrHeliTaxiState::~JetOrHeliTaxiState()
{
	GxComponentPool<GxState>::the().release(m_gxRow);
}

//-------------------------------------------------------------------------------------------------
/*
	Success: we are on the ground at the runway start
	Failure: we are unable to get on the ground
*/
class JetTakeoffOrLandingState : public AIFollowPathState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetTakeoffOrLandingState, "JetTakeoffOrLandingState")
private:
#ifdef CIRCLE_FOR_LANDING
#endif

public:
	JetTakeoffOrLandingState( StateMachine *machine, Bool landing ) :  AIFollowPathState( machine, "JetTakeoffOrLandingState" ) {
		m_gxRow = GxComponentPool<GxState>::the().alloc();
		R().landing = landing; }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if (!jetAI)
			return STATE_FAILURE;

		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		jetAI->friend_setTakeoffInProgress(!R().landing);
		jetAI->friend_setLandingInProgress(R().landing);
		jetAI->friend_setAllowAirLoco(true);
		jetAI->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		Locomotor* loco = jetAI->getCurLocomotor();
		DEBUG_ASSERTCRASH(loco, ("no loco"));
		loco->setMaxLift(BIGNUM);
		BodyDamageType bdt = jet->getBodyModule()->getDamageState();
		R().maxLift = loco->getMaxLift(bdt);
		R().maxSpeed = loco->getMaxSpeedForCondition(bdt);
		R().landingSoundPlayed = FALSE;
		if (R().landing)
		{
			loco->setMaxSpeed(loco->getMinSpeed());
		}
		else
		{
			loco->setMaxLift(0);
		}
		loco->setUsePreciseZPos(true);
		loco->setUltraAccurate(true);
		jetAI->ignoreObstacleID(jet->getProducerID());

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == nullptr)
			return STATE_SUCCESS;	// no airfield? just skip this step

		ParkingPlaceBehaviorInterface::PPInfo ppinfo;
		if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
		{
			// it's full.
			return STATE_FAILURE;
		}

		// only check this for landing; we might have already given up the reservation to the guy behind us for takeoff
		if (R().landing)
		{
			if (!pp->reserveRunway(jet->getID(), R().landing))
			{
				DEBUG_CRASH(("we should never get to this state unless we have a runway available"));
				return STATE_FAILURE;
			}
		}

		std::vector<Coord3D> path;
		if (R().landing)
		{
#ifdef CIRCLE_FOR_LANDING
			R().circleForLandingPos = ppinfo.runwayApproach;
			R().circleForLandingPos.z = (ppinfo.runwayEnd.z + ppinfo.runwayApproach.z)*0.5f;
#else
			path.push_back(ppinfo.runwayApproach);
#endif
			if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
			{
				//Assigned to an aircraft carrier which has separate landing strips.
				path.push_back( ppinfo.runwayLandingStart );
				path.push_back( ppinfo.runwayLandingEnd );
			}
			else
			{
				//Assigned to an airstrip -- land the same way we took off but in reverse.
				path.push_back(ppinfo.runwayEnd);
				path.push_back(ppinfo.runwayStart);
			}
		}
		else
		{
			ppinfo.runwayEnd.z = ppinfo.runwayApproach.z;
			path.push_back(ppinfo.runwayEnd);
			path.push_back(ppinfo.runwayExit);
		}

		setAdjustsDestination(false);	// precision is necessary
		setAdjustFinalDestination(false); // especially at the endpoint!

		jetAI->friend_setGoalPath( &path );

		StateReturnType ret = AIFollowPathState::onEnter();

		return ret;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		if (R().landing)
		{
#ifdef CIRCLE_FOR_LANDING
			if (jet->getPosition()->z > R().circleForLandingPos.z)
			{
				const Real THRESH = 4.0f;
				jetAI->getCurLocomotor()->setAltitudeChangeThresholdForCircling(THRESH);
				jetAI->setLocomotorGoalPositionExplicit(R().circleForLandingPos);
				return STATE_CONTINUE;
			}
			else
#endif
			{
				jetAI->getCurLocomotor()->setMaxLift(BIGNUM);
#ifdef CIRCLE_FOR_LANDING
				jetAI->getCurLocomotor()->setAltitudeChangeThresholdForCircling(0);
#endif
			}

			if( !R().landingSoundPlayed )
			{
				ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
				Real zPos = jet->getPosition()->z;
				Real zSlop = 0.25f;
				PathfindLayerEnum layer = TheTerrainLogic->getHighestLayerForDestination( jet->getPosition() );
				Real groundZ = TheTerrainLogic->getLayerHeight( jet->getPosition()->x, jet->getPosition()->y, layer );
				if( pp )
				{
					groundZ += pp->getLandingDeckHeightOffset();
				}

				if( zPos - zSlop <= groundZ )
				{
					R().landingSoundPlayed = TRUE;
					AudioEventRTS soundToPlay = TheAudio->getMiscAudio()->m_aircraftWheelScreech;
					soundToPlay.setPosition( jet->getPosition() );
					TheAudio->addAudioEvent( &soundToPlay );
				}
			}
		}
		else
		{
			ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
			if (pp)
				pp->transferRunwayReservationToNextInLineForTakeoff(jet->getID());

			//Calculate the distance of the jet from the end of the runway as a ratio from the start.
			//As it approaches the end of the runway, the plane will gain more lift, even if it's already
			//going quickly. Using speed for lift is bad in the case of the aircraft carrier, because
			//we don't want it to take off quickly.
			ParkingPlaceBehaviorInterface::PPInfo ppinfo;
			pp->calcPPInfo( jet->getID(), &ppinfo );
			Coord3D vector = ppinfo.runwayEnd;
			vector.sub( jet->getPosition() );
			Real dist = vector.length();

			Real ratio = 1.0f - (dist / ppinfo.runwayTakeoffDist);
			ratio *= ratio; //dampen it....
			if (ratio < 0.0f) ratio = 0.0f;
			if (ratio > 1.0f) ratio = 1.0f;
			jetAI->getCurLocomotor()->setMaxLift(R().maxLift * ratio);
		}

		StateReturnType ret = AIFollowPathState::update();
		return ret;
	}

	virtual void onExit( StateExitType status )
	{
		AIFollowPathState::onExit(status);

		// just in case.
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return;

		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);
		jetAI->friend_enableAfterburners(false);

		// Paranoia checks - sometimes onExit is called when we are
		// shutting down, and not all pieces are valid.  CurLocomotor
		// is definitely null in some cases. jba.
		Locomotor* loco = jetAI->getCurLocomotor();
		if (loco)
		{
			loco->setUsePreciseZPos(false);
			loco->setUltraAccurate(false);
			// don't restore lift if dead -- this may fight with JetSlowDeathBehavior!
			if (!jet->isEffectivelyDead())
				loco->setMaxLift(BIGNUM);
#ifdef CIRCLE_FOR_LANDING
			loco->setAltitudeChangeThresholdForCircling(0);
#endif
		}
		jetAI->ignoreObstacleID(INVALID_ID);
		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (!R().landing)
		{
			if (pp && !jetAI->friend_keepsParkingSpaceWhenAirborne())
				pp->releaseSpace(jet->getID());
		}
		if (pp)
			pp->releaseRunway(jet->getID());
	}
public:
	// web port (round 5, units are pure data): the per-unit state is a row of GxComponentPool<GxState>;
	// this module object is the facade (INI, xfer, crc) holding its row handle.
	struct GxState
	{
		Real maxLift;
		Real maxSpeed;
#ifdef CIRCLE_FOR_LANDING
		Coord3D circleForLandingPos;
#endif
		Bool landing;
		Bool landingSoundPlayed;
	};
	GxState &R() const;
private:
	int m_gxRow;
};
inline JetTakeoffOrLandingState::GxState &JetTakeoffOrLandingState::R() const { return GxComponentPool<GxState>::the()[m_gxRow]; }
JetTakeoffOrLandingState::~JetTakeoffOrLandingState()
{
	GxComponentPool<GxState>::the().release(m_gxRow);
}

//-------------------------------------------------------------------------------------------------
static Real calcDistSqr(const Coord3D& a, const Coord3D& b)
{
	return sqr(a.x-b.x) + sqr(a.y-b.y) + sqr(a.z-b.z);
}

//-------------------------------------------------------------------------------------------------
/*
	Success: we are on the ground at the runway start
	Failure: we are unable to get on the ground
*/
class HeliTakeoffOrLandingState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(HeliTakeoffOrLandingState, "HeliTakeoffOrLandingState")
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer )
	{
		// empty. jba.
	}

	virtual void xfer( Xfer *xfer )
	{
		// version
		XferVersion currentVersion = 1;
		XferVersion version = currentVersion;
		xfer->xferVersion( &version, currentVersion );

		// set on create. xfer->xferBool(&R().landing);
		xfer->xferCoord3D(&R().path[0]);
		xfer->xferCoord3D(&R().path[1]);
		xfer->xferInt(&R().index);
		xfer->xferCoord3D(&R().parkingLoc);
		xfer->xferReal(&R().parkingOrientation);
	}
	virtual void loadPostProcess()
	{
		// empty. jba.
	}

private:
public:
	HeliTakeoffOrLandingState( StateMachine *machine, Bool landing ) : 
		State( machine, "HeliTakeoffOrLandingState" )
		{
		m_gxRow = GxComponentPool<GxState>::the().alloc();
		R().index = 0;
		R().landing = landing;
			R().parkingLoc.zero();
		}

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		jetAI->friend_setTakeoffInProgress(!R().landing);
		jetAI->friend_setLandingInProgress(R().landing);
		jetAI->friend_setAllowAirLoco(true);
		jetAI->chooseLocomotorSet(LOCOMOTORSET_NORMAL);

		Locomotor* loco = jetAI->getCurLocomotor();
		DEBUG_ASSERTCRASH(loco, ("no loco"));
		loco->setUsePreciseZPos(true);
		loco->setUltraAccurate(true);
		jetAI->ignoreObstacleID(jet->getProducerID());

		Object* airfield;
		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID(), &airfield);
		if (pp == nullptr)
			return STATE_SUCCESS;	// no airfield? just skip this step

		Coord3D landingApproach;
		if (jet->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
		{
			if (R().landing)
			{
				R().parkingLoc = jetAI->friend_getLandingPosForHelipadStuff();
				R().parkingOrientation = jet->getOrientation();
			}
			else
			{
				R().parkingOrientation = jet->getOrientation();
				R().parkingLoc = *jet->getPosition();
			}
			landingApproach = R().parkingLoc;
			landingApproach.z += pp->getApproachHeight() + pp->getLandingDeckHeightOffset();
		}
		else
		{
			ParkingPlaceBehaviorInterface::PPInfo ppinfo;
			if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
				return STATE_FAILURE;
			R().parkingLoc = ppinfo.parkingSpace;
			R().parkingOrientation = ppinfo.parkingOrientation;
			landingApproach = R().parkingLoc;
			landingApproach.z += (ppinfo.runwayApproach.z - ppinfo.runwayEnd.z);
		}

		if (R().landing)
		{
			R().path[0] = landingApproach;
			R().path[1] = R().parkingLoc;
		}
		else
		{
			R().path[0] = R().parkingLoc;
			R().path[1] = landingApproach;
			R().path[1].z = landingApproach.z;
		}
		R().index = 0;

		return STATE_CONTINUE;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

// I have disabled this because it is no longer necessary and is a bit funky lookin' (srj)
#ifdef NOT_IN_USE
		// magically position it correctly.
		jet->getPhysics()->scrubVelocity2D(0);
		Coord3D hoverloc = R().path[R().index];
		hoverloc.z = jet->getPosition()->z;
#if 1
		Coord3D pos = *jet->getPosition();
		Real dx = hoverloc.x - pos.x;
		Real dy = hoverloc.y - pos.y;
		Real dSqr = dx*dx+dy*dy;
		const Real DARN_CLOSE = 0.25f;
		if (dSqr < DARN_CLOSE)
		{
			jet->setPosition(&hoverloc);
		}
		else
		{
			Real dist = sqrtf(dSqr);
			if (dist<1) dist = 1;
			pos.x += PATHFIND_CELL_SIZE_F*dx/(dist*LOGICFRAMES_PER_SECOND);
			pos.y += PATHFIND_CELL_SIZE_F*dy/(dist*LOGICFRAMES_PER_SECOND);
			jet->setPosition(&pos);
		}
#else
		jet->setPosition(&hoverloc);
#endif
		jet->setOrientation(R().parkingOrientation);
#endif

		if (jet->isKindOf(KINDOF_PRODUCED_AT_HELIPAD) || !R().landing)
		{
			TheAI->pathfinder()->adjustDestination(jet, jetAI->getLocomotorSet(), &R().path[R().index]);
			TheAI->pathfinder()->updateGoal(jet, &R().path[R().index], LAYER_GROUND);
		}

		jetAI->setLocomotorGoalPositionExplicit(R().path[R().index]);

		const Real THRESH = 3.0f;
		const Real THRESH_SQR = THRESH*THRESH;
		const Coord3D* a = jet->getPosition();
		const Coord3D* b = &R().path[R().index];
		Real distSqr = calcDistSqr(*a, *b);
		if (distSqr <= THRESH_SQR)
			++R().index;

		if (R().index >= 2)
			return STATE_SUCCESS;

		return STATE_CONTINUE;
	}

	virtual void onExit( StateExitType status )
	{
		// just in case.
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return;

		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);

		// Paranoia checks - sometimes onExit is called when we are
		// shutting down, and not all pieces are valid.  CurLocomotor
		// is definitely null in some cases. jba.
		Locomotor* loco = jetAI->getCurLocomotor();
		if (loco)
		{
			loco->setUsePreciseZPos(false);
			loco->setUltraAccurate(false);
			// don't restore lift if dead -- this may fight with JetSlowDeathBehavior!
			if (!jet->isEffectivelyDead())
				loco->setMaxLift(BIGNUM);
		}

		jetAI->ignoreObstacleID(INVALID_ID);
		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (R().landing)
		{
			jetAI->friend_setAllowAirLoco(false);
			jetAI->chooseLocomotorSet(LOCOMOTORSET_TAXIING);
		}
		else
		{
			if (pp && !jetAI->friend_keepsParkingSpaceWhenAirborne())
				pp->releaseSpace(jet->getID());
		}
	}

public:
	// web port (round 5, units are pure data): the per-unit state is a row of GxComponentPool<GxState>;
	// this module object is the facade (INI, xfer, crc) holding its row handle.
	struct GxState
	{
		Coord3D path[2];
		Int index;
		Coord3D parkingLoc;
		Real parkingOrientation;
		Bool landing;
	};
	GxState &R() const;
private:
	int m_gxRow;
};
inline HeliTakeoffOrLandingState::GxState &HeliTakeoffOrLandingState::R() const { return GxComponentPool<GxState>::the()[m_gxRow]; }
HeliTakeoffOrLandingState::~HeliTakeoffOrLandingState()
{
	GxComponentPool<GxState>::the().release(m_gxRow);
}

//-------------------------------------------------------------------------------------------------
class JetOrHeliParkOrientState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliParkOrientState, "JetOrHeliParkOrientState")
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};

public:
	JetOrHeliParkOrientState( StateMachine *machine ) : State( machine, "JetOrHeliParkOrientState") { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		if (jet->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
		{
			return STATE_SUCCESS;
		}

		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(true);

		jetAI->ignoreObstacleID(jet->getProducerID());
		return STATE_CONTINUE;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
		{
			return STATE_FAILURE;
		}

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == nullptr)
			return STATE_FAILURE;

		ParkingPlaceBehaviorInterface::PPInfo ppinfo;
		if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
			return STATE_FAILURE;

		const Real THRESH = 0.001f;
		if (fabs(stdAngleDiff(jet->getOrientation(), ppinfo.parkingOrientation)) <= THRESH)
			return STATE_SUCCESS;

		// magically position it correctly.
		jet->getPhysics()->scrubVelocity2D(0);
		Coord3D hoverloc = ppinfo.parkingSpace;
		if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
		{
			hoverloc = ppinfo.runwayPrep;
		}

		hoverloc.z = jet->getPosition()->z;
		jet->setPosition(&hoverloc);

		jetAI->setLocomotorGoalOrientation(ppinfo.parkingOrientation);

		return STATE_CONTINUE;
	}

	virtual void onExit( StateExitType status )
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return;

		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);
		jetAI->ignoreObstacleID(INVALID_ID);
	}
};
EMPTY_DTOR(JetOrHeliParkOrientState)

//-------------------------------------------------------------------------------------------------
class JetPauseBeforeTakeoffState : public AIFaceState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetPauseBeforeTakeoffState, "JetPauseBeforeTakeoffState")
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer )
	{
		// empty. jba.
	}

	virtual void xfer( Xfer *xfer )
	{
		// version
#if RETAIL_COMPATIBLE_CRC || RETAIL_COMPATIBLE_XFER_SAVE
		XferVersion currentVersion = 1;
#else
		XferVersion currentVersion = 2;
#endif
		XferVersion version = currentVersion;
		xfer->xferVersion( &version, currentVersion );

		// set on create. xfer->xferBool(&R().landing);
		xfer->xferUnsignedInt(&R().whenTakeoff);
		xfer->xferUnsignedInt(&R().whenTransfer);

#if RETAIL_COMPATIBLE_CRC || RETAIL_COMPATIBLE_XFER_SAVE
		if (version <= 1)
		{
			xfer->xferBool(&R().afterburners);
			xfer->xferBool(&R().resetTimer);
		}
#else
		if (version <= 1)
		{
			Bool afterburners = false;
			Bool resetTimer = false;
			xfer->xferBool(&afterburners);
			xfer->xferBool(&resetTimer);
		}
#endif

		xfer->xferObjectID(&R().waitedForTaxiID);
	}

	virtual void loadPostProcess()
	{
		// empty. jba.
	}

private:
#if RETAIL_COMPATIBLE_CRC || RETAIL_COMPATIBLE_XFER_SAVE
#endif

#if RETAIL_COMPATIBLE_CRC || RETAIL_COMPATIBLE_XFER_SAVE
	Bool findWaiter()
	{
		Object* jet = getMachineOwner();
		ParkingPlaceBehaviorInterface* pp = getPP(getMachineOwner()->getProducerID());
		if (pp)
		{
			Int count = pp->getRunwayCount();
			for (Int i = 0; i < count; ++i)
			{
				Object* otherJet = TheGameLogic->findObjectByID( pp->getRunwayReservation( i, RESERVATION_TAKEOFF ) );
				if (otherJet == nullptr || otherJet == jet)
					continue;

				AIUpdateInterface* ai = otherJet->getAIUpdateInterface();
				if (ai == nullptr)
					continue;

				if (ai->getCurrentStateID() == TAXI_TO_TAKEOFF)
				{
					if (R().waitedForTaxiID == INVALID_ID)
					{
						R().waitedForTaxiID = otherJet->getID();
					}
					return true;
				}
			}
		}
		return false;
	}
#else
	Object* findJetToWaitFor() const
	{
		const Object* thisJet = getMachineOwner();
		ParkingPlaceBehaviorInterface* pp = getPP(getMachineOwner()->getProducerID());

		if (pp != nullptr)
		{
			const Int thisJetRunway = pp->getRunwayIndex(thisJet->getID());
			const Int runwayCount = pp->getRunwayCount();

			for (Int runway = 0; runway < runwayCount; ++runway)
			{
				if (runway == thisJetRunway)
					continue;

				Object* otherJet = TheGameLogic->findObjectByID(pp->getRunwayReservation(runway, RESERVATION_TAKEOFF));
				if (otherJet == nullptr)
					continue;

				AIUpdateInterface* ai = otherJet->getAIUpdateInterface();
				if (ai == nullptr)
					continue;

				if (ai->getCurrentStateID() != TAXI_TO_TAKEOFF)
					continue;
				
				return otherJet;
			}
		}
		return nullptr;
	}
#endif

public:
	JetPauseBeforeTakeoffState( StateMachine *machine )
		: AIFaceState(machine, false)
		
#if RETAIL_COMPATIBLE_CRC || RETAIL_COMPATIBLE_XFER_SAVE
		
#endif
	{
		m_gxRow = GxComponentPool<GxState>::the().alloc();
		R().afterburners = false;
		R().resetTimer = false;
		R().waitedForTaxiID = INVALID_ID;
		R().whenTransfer = 0;
		R().whenTakeoff = 0;
		// nothing
	}

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		jetAI->friend_setTakeoffInProgress(true);
		jetAI->friend_setLandingInProgress(false);

		R().whenTakeoff = 0;
		R().whenTransfer = 0;
		R().waitedForTaxiID = INVALID_ID;
#if RETAIL_COMPATIBLE_CRC || RETAIL_COMPATIBLE_XFER_SAVE
		R().resetTimer = false;
		R().afterburners = false;
#endif

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == nullptr)
			return STATE_SUCCESS;	// no airfield? just skip this step.

		ParkingPlaceBehaviorInterface::PPInfo ppinfo;
		if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
			return STATE_SUCCESS;	// full?

		getMachine()->setGoalPosition(&ppinfo.runwayEnd);

		return AIFaceState::onEnter();
	}

#if RETAIL_COMPATIBLE_CRC || RETAIL_COMPATIBLE_XFER_SAVE
	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		// always call this.
		StateReturnType superStatus = AIFaceState::update();

		if (findWaiter())
			return STATE_CONTINUE;

		UnsignedInt now = TheGameLogic->getFrame();
		if (!R().resetTimer)
		{
			// we had to wait, but now everyone else is ready, so restart our countdown.
			R().whenTakeoff = now + jetAI->friend_getTakeoffPause();
			if (R().waitedForTaxiID == INVALID_ID)
			{
				R().waitedForTaxiID = jet->getID();	// just so we don't pick up anyone else
				R().whenTransfer = now + 1;
			}
			else
			{
				R().whenTransfer = now + 2;	// 2 seems odd, but is correct
			}
			R().resetTimer = true;
		}

		if (!R().afterburners)
		{
			jetAI->friend_enableAfterburners(true);
			R().afterburners = true;
		}

		DEBUG_ASSERTCRASH(R().whenTakeoff != 0, ("hmm"));
		DEBUG_ASSERTCRASH(R().whenTransfer != 0, ("hmm"));

			// once we start the final wait, release the runways for guys behind us, so they can start taxiing
		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp && now >= R().whenTransfer)
		{
			pp->transferRunwayReservationToNextInLineForTakeoff(jet->getID());
		}

		if (now >= R().whenTakeoff)
			return superStatus;

		return STATE_CONTINUE;
	}
#else
	// TheSuperHackers @bugfix Reimplements the update to wait for another Jet on another runway.
	// If this must work with more than 2 runways, then this logic needs another look.
	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();

		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		// always call this.
		StateReturnType superStatus = AIFaceState::update();

		if (Object* otherJet = findJetToWaitFor())
		{
			// Found a jet on another runway to wait for.
			if (R().waitedForTaxiID == INVALID_ID)
			{
				// Save the other Jet to wait for.
				R().waitedForTaxiID = otherJet->getID();
			}

			if (R().waitedForTaxiID == otherJet->getID())
			{
				// Wait for the other Jet to get ready.
				return STATE_CONTINUE;
			}
		}

		const UnsignedInt now = TheGameLogic->getFrame();

		if (R().whenTakeoff == 0)
		{
			// The other Jet (if any) is ready. Prepare runway transfer and takeoff.
			// Transfer the runway after one or two frames earliest to give the other
			// Jet a chance to update as well before the runway is transferred.
			if (R().waitedForTaxiID == INVALID_ID)
			{
				// Do not wait for any other Jet from now on.
				R().waitedForTaxiID = jet->getID();
				R().whenTransfer = now + 1;
			}
			else
			{
				R().whenTransfer = now + 2; // 2 seems odd, but is correct
			}

			// Take off soon, but not before the runway transfer.
			R().whenTakeoff = std::max(R().whenTransfer, now + jetAI->friend_getTakeoffPause());
			jetAI->friend_enableAfterburners(true);
		}
		else
		{
			// once we start the final wait, release the runways for guys behind us, so they can start taxiing
			if (now >= R().whenTransfer)
			{
				if (ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID()))
				{
					pp->transferRunwayReservationToNextInLineForTakeoff(jet->getID());
				}
				// Do not transfer the runway again in the next update.
				R().whenTransfer = ~0u;
			}

			if (now >= R().whenTakeoff)
			{
				// Ready to take off!
				return superStatus;
			}
		}

		return STATE_CONTINUE;
	}
#endif

	virtual void onExit(StateExitType status)
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);
		AIFaceState::onExit(status);
	}

public:
	// web port (round 5, units are pure data): the per-unit state is a row of GxComponentPool<GxState>;
	// this module object is the facade (INI, xfer, crc) holding its row handle.
	struct GxState
	{
		UnsignedInt whenTakeoff;
		UnsignedInt whenTransfer;
		ObjectID waitedForTaxiID;
#if RETAIL_COMPATIBLE_CRC || RETAIL_COMPATIBLE_XFER_SAVE
		Bool resetTimer;
		Bool afterburners;
#endif
	};
	GxState &R() const;
private:
	int m_gxRow;
};
inline JetPauseBeforeTakeoffState::GxState &JetPauseBeforeTakeoffState::R() const { return GxComponentPool<GxState>::the()[m_gxRow]; }
JetPauseBeforeTakeoffState::~JetPauseBeforeTakeoffState()
{
	GxComponentPool<GxState>::the().release(m_gxRow);
}

//-------------------------------------------------------------------------------------------------
class JetOrHeliReloadAmmoState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliReloadAmmoState, "JetOrHeliReloadAmmoState")
private:

protected:

	// snapshot interface
	virtual void crc( Xfer *xfer )
	{
		// empty. jba.
	}

	virtual void xfer( Xfer *xfer )
	{
		// version
		XferVersion currentVersion = 1;
		XferVersion version = currentVersion;
		xfer->xferVersion( &version, currentVersion );

		// set on create. xfer->xferBool(&R().landing);
		xfer->xferUnsignedInt(&R().reloadTime);
		xfer->xferUnsignedInt(&R().reloadDoneFrame);
	}
	virtual void loadPostProcess()
	{
		// empty. jba.
	}

public:
	JetOrHeliReloadAmmoState( StateMachine *machine ) : State( machine, "JetOrHeliReloadAmmoState") {
		m_gxRow = GxComponentPool<GxState>::the().alloc(); }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);
		jetAI->friend_setUseSpecialReturnLoco(false);

		R().reloadTime = 0;
		for (Int i = 0; i < WEAPONSLOT_COUNT;	++i)
		{
			const Weapon* w = jet->getWeaponInWeaponSlot((WeaponSlotType)i);
			if (w == nullptr)
				continue;

			Int remaining = w->getRemainingAmmo();
			Int clipSize = w->getClipSize();
			Int rt = w->getClipReloadTime(jet);
			if (clipSize > 0)
			{
				// bias by amount empty.
				Int needed = clipSize - remaining;
				rt = (rt * needed) / clipSize;
			}
			if (rt > R().reloadTime)
				R().reloadTime = rt;
		}

		if (R().reloadTime < 1)
			R().reloadTime = 1;
		R().reloadDoneFrame = R().reloadTime + TheGameLogic->getFrame();
		return STATE_CONTINUE;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		UnsignedInt now = TheGameLogic->getFrame();
		Bool allDone = true;
		for (Int i = 0; i < WEAPONSLOT_COUNT;	++i)
		{
			Weapon* w = jet->getWeaponInWeaponSlot((WeaponSlotType)i);
			if (w == nullptr)
				continue;

			if (now >= R().reloadDoneFrame)
				w->setClipPercentFull(1.0f, false);
			else
				w->setClipPercentFull((Real)(R().reloadTime - (R().reloadDoneFrame - now)) / R().reloadTime, false);

			if (w->getRemainingAmmo() != w->getClipSize())
				allDone = false;
		}

		if (allDone)
			return STATE_SUCCESS;

		return STATE_CONTINUE;
	}

	virtual void onExit(StateExitType status)
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);
	}

public:
	// web port (round 5, units are pure data): the per-unit state is a row of GxComponentPool<GxState>;
	// this module object is the facade (INI, xfer, crc) holding its row handle.
	struct GxState
	{
		UnsignedInt reloadTime;
		UnsignedInt reloadDoneFrame;
	};
	GxState &R() const;
private:
	int m_gxRow;
};
inline JetOrHeliReloadAmmoState::GxState &JetOrHeliReloadAmmoState::R() const { return GxComponentPool<GxState>::the()[m_gxRow]; }
JetOrHeliReloadAmmoState::~JetOrHeliReloadAmmoState()
{
	GxComponentPool<GxState>::the().release(m_gxRow);
}

//-------------------------------------------------------------------------------------------------
/*
	Success: we are close enough to a friendly airfield to land
	Failure: we are unable to get close enough to a friendly airfield to land
*/
class JetOrHeliReturnForLandingState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliReturnForLandingState, "JetOrHeliReturnForLandingState")
public:
	JetOrHeliReturnForLandingState( StateMachine *machine ) : AIInternalMoveToState( machine, "JetOrHeliReturnForLandingState") { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == nullptr)
		{
			// nuke the producer id, since it's dead
			jet->setProducer(nullptr);

			Object* airfield = findSuitableAirfield( jet );
			pp = airfield ? getPP(airfield->getID()) : nullptr;
			if (airfield && pp)
			{
				jet->setProducer(airfield);
			}
			else
			{
				return STATE_FAILURE;
			}
		}

		if (jet->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
		{
			AIInternalMoveToState::gxRow().goalPosition = jetAI->friend_getLandingPosForHelipadStuff();
		}
		else
		{
			ParkingPlaceBehaviorInterface::PPInfo ppinfo;
			if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
				return STATE_FAILURE;

			AIInternalMoveToState::gxRow().goalPosition = jetAI->friend_needsRunway() ? ppinfo.runwayApproach : ppinfo.parkingSpace;
		}
		setAdjustsDestination(false);		// precision is necessary

		return AIInternalMoveToState::onEnter();
	}
};
EMPTY_DTOR(JetOrHeliReturnForLandingState)

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
class JetAIStateMachine : public AIStateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( JetAIStateMachine, "JetAIStateMachine" );

public:
	JetAIStateMachine( Object *owner, AsciiString name );

};

//-------------------------------------------------------------------------------------------------
JetAIStateMachine::JetAIStateMachine(Object *owner, AsciiString name) : AIStateMachine(owner, name)
{
	defineState( RETURNING_FOR_LANDING, newInstance(JetOrHeliReturnForLandingState)( this ), LANDING_AWAIT_CLEARANCE, RETURN_TO_DEAD_AIRFIELD );
	defineState( TAKING_OFF_AWAIT_CLEARANCE, newInstance(JetAwaitingRunwayState)( this, false ), TAXI_TO_TAKEOFF, AI_IDLE );
	defineState( TAXI_TO_TAKEOFF, newInstance(JetOrHeliTaxiState)( this, FROM_PARKING ), PAUSE_BEFORE_TAKEOFF, AI_IDLE );
	defineState( PAUSE_BEFORE_TAKEOFF, newInstance(JetPauseBeforeTakeoffState)( this ), TAKING_OFF, AI_IDLE );
	defineState( TAKING_OFF, newInstance(JetTakeoffOrLandingState)( this, false ), AI_IDLE, AI_IDLE );
	defineState( LANDING_AWAIT_CLEARANCE, newInstance(JetAwaitingRunwayState)( this, true ), LANDING, AI_IDLE );
	defineState( LANDING, newInstance(JetTakeoffOrLandingState)( this, true ), TAXI_FROM_LANDING, AI_IDLE );
	defineState( TAXI_FROM_LANDING, newInstance(JetOrHeliTaxiState)( this, TO_PARKING ), ORIENT_FOR_PARKING_PLACE, AI_IDLE );
	defineState( TAXI_FROM_HANGAR, newInstance(JetOrHeliTaxiState)( this, FROM_HANGAR ), ORIENT_FOR_PARKING_PLACE, AI_IDLE );
	defineState( ORIENT_FOR_PARKING_PLACE, newInstance(JetOrHeliParkOrientState)( this ), RELOAD_AMMO, AI_IDLE );
	defineState( RELOAD_AMMO, newInstance(JetOrHeliReloadAmmoState)( this ), AI_IDLE, AI_IDLE );
	defineState( RETURN_TO_DEAD_AIRFIELD, newInstance(JetOrHeliReturningToDeadAirfieldState)( this ), CIRCLING_DEAD_AIRFIELD, RETURN_TO_DEAD_AIRFIELD );
	defineState( CIRCLING_DEAD_AIRFIELD, newInstance(JetOrHeliCirclingDeadAirfieldState)( this ), AI_IDLE, AI_IDLE );
}

//-------------------------------------------------------------------------------------------------
JetAIStateMachine::~JetAIStateMachine()
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
class HeliAIStateMachine : public AIStateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( HeliAIStateMachine, "HeliAIStateMachine" );

public:
	HeliAIStateMachine( Object *owner, AsciiString name );

};

//-------------------------------------------------------------------------------------------------
HeliAIStateMachine::HeliAIStateMachine(Object *owner, AsciiString name) : AIStateMachine(owner, name)
{
	defineState( RETURNING_FOR_LANDING, newInstance(JetOrHeliReturnForLandingState)( this ), LANDING_AWAIT_CLEARANCE, RETURN_TO_DEAD_AIRFIELD );
	defineState( TAKING_OFF_AWAIT_CLEARANCE, newInstance(SuccessState)( this ), TAKING_OFF, AI_IDLE );
	defineState( TAKING_OFF, newInstance(HeliTakeoffOrLandingState)( this, false ), AI_IDLE, AI_IDLE );
	defineState( LANDING_AWAIT_CLEARANCE, newInstance(SuccessState)( this ), ORIENT_FOR_PARKING_PLACE, AI_IDLE );
	defineState( ORIENT_FOR_PARKING_PLACE, newInstance(JetOrHeliParkOrientState)( this ), LANDING, AI_IDLE );
	defineState( LANDING, newInstance(HeliTakeoffOrLandingState)( this, true ), RELOAD_AMMO, AI_IDLE );
	defineState( RELOAD_AMMO, newInstance(JetOrHeliReloadAmmoState)( this ), AI_IDLE, AI_IDLE );
	defineState( RETURN_TO_DEAD_AIRFIELD, newInstance(JetOrHeliReturningToDeadAirfieldState)( this ), CIRCLING_DEAD_AIRFIELD, RETURN_TO_DEAD_AIRFIELD );
	defineState( CIRCLING_DEAD_AIRFIELD, newInstance(JetOrHeliCirclingDeadAirfieldState)( this ), AI_IDLE, AI_IDLE );
	defineState( TAXI_FROM_HANGAR, newInstance(JetOrHeliTaxiState)( this, FROM_HANGAR ), AI_IDLE, AI_IDLE );
}

//-------------------------------------------------------------------------------------------------
HeliAIStateMachine::~HeliAIStateMachine()
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
JetAIUpdateModuleData::JetAIUpdateModuleData()
{
	m_outOfAmmoDamagePerSecond = 0;
	m_needsRunway = true;
	m_keepsParkingSpaceWhenAirborne = true;
	m_takeoffDistForMaxLift = 0.0f;
	m_minHeight = 0.0f;
	m_parkingOffset = 0.0f;
	m_sneakyOffsetWhenAttacking = 0.0f;
	m_takeoffPause = 0;
	m_attackingLoco = LOCOMOTORSET_NORMAL;
	m_returningLoco = LOCOMOTORSET_NORMAL;
	m_attackLocoPersistTime = 0;
	m_attackersMissPersistTime = 0;
	m_lockonTime = 0;
	m_lockonCursor.clear();
	m_lockonInitialDist = 100;
	m_lockonFreq = 0.5;
	m_lockonAngleSpin = 720;
	m_returnToBaseIdleTime = 0;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void JetAIUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  AIUpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "OutOfAmmoDamagePerSecond",			INI::parsePercentToReal, nullptr, offsetof( JetAIUpdateModuleData, m_outOfAmmoDamagePerSecond ) },
		{ "NeedsRunway",									INI::parseBool, nullptr, offsetof( JetAIUpdateModuleData, m_needsRunway ) },
		{ "KeepsParkingSpaceWhenAirborne",INI::parseBool, nullptr, offsetof( JetAIUpdateModuleData, m_keepsParkingSpaceWhenAirborne ) },
		{ "TakeoffDistForMaxLift",				INI::parsePercentToReal, nullptr, offsetof( JetAIUpdateModuleData, m_takeoffDistForMaxLift ) },
		{ "TakeoffPause",									INI::parseDurationUnsignedInt, nullptr, offsetof( JetAIUpdateModuleData, m_takeoffPause ) },
		{ "MinHeight",										INI::parseReal, nullptr, offsetof( JetAIUpdateModuleData, m_minHeight ) },
		{ "ParkingOffset",								INI::parseReal, nullptr, offsetof( JetAIUpdateModuleData, m_parkingOffset ) },
		{ "SneakyOffsetWhenAttacking",		INI::parseReal, nullptr, offsetof( JetAIUpdateModuleData, m_sneakyOffsetWhenAttacking ) },
		{ "AttackLocomotorType",					INI::parseIndexList, TheLocomotorSetNames, offsetof( JetAIUpdateModuleData, m_attackingLoco ) },
		{ "AttackLocomotorPersistTime",		INI::parseDurationUnsignedInt, nullptr, offsetof( JetAIUpdateModuleData, m_attackLocoPersistTime ) },
		{ "AttackersMissPersistTime",			INI::parseDurationUnsignedInt, nullptr, offsetof( JetAIUpdateModuleData, m_attackersMissPersistTime ) },
		{ "ReturnForAmmoLocomotorType",		INI::parseIndexList, TheLocomotorSetNames, offsetof( JetAIUpdateModuleData, m_returningLoco ) },
		{ "LockonTime",										INI::parseDurationUnsignedInt, nullptr, offsetof( JetAIUpdateModuleData, m_lockonTime ) },
		{ "LockonCursor",									INI::parseAsciiString, nullptr, offsetof( JetAIUpdateModuleData, m_lockonCursor ) },
		{ "LockonInitialDist",						INI::parseReal, nullptr, offsetof( JetAIUpdateModuleData, m_lockonInitialDist ) },
		{ "LockonFreq",										INI::parseReal, nullptr, offsetof( JetAIUpdateModuleData, m_lockonFreq ) },
		{ "LockonAngleSpin",							INI::parseAngleReal, nullptr, offsetof( JetAIUpdateModuleData, m_lockonAngleSpin ) },
		{ "LockonBlinky",									INI::parseBool, nullptr, offsetof( JetAIUpdateModuleData, m_lockonBlinky ) },
		{ "ReturnToBaseIdleTime",					INI::parseDurationUnsignedInt, nullptr, offsetof( JetAIUpdateModuleData, m_returnToBaseIdleTime ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
AIStateMachine* JetAIUpdate::makeStateMachine()
{
	if (getJetAIUpdateModuleData()->m_needsRunway)
		return newInstance(JetAIStateMachine)( getObject(), "JetAIStateMachine");
	else
		return newInstance(HeliAIStateMachine)( getObject(), "HeliAIStateMachine");
}

//-------------------------------------------------------------------------------------------------
JetAIUpdate::JetAIUpdate( Thing *thing, const ModuleData* moduleData ) : AIUpdateInterface( thing, moduleData )
{
	m_gxRow = GxComponentPool<GxState>::the().alloc();
	R().flags = 0;
	R().afterburnerSound = *(getObject()->getTemplate()->getPerUnitSound("Afterburner"));
	R().afterburnerSound.setObjectID(getObject()->getID());
	R().attackLocoExpireFrame = 0;
	R().attackersMissExpireFrame = 0;
	R().untargetableExpireFrame = 0;
	R().returnToBaseFrame = 0;
	R().lockonDrawable = nullptr;
	R().landingPosForHelipadStuff.zero();

	R().producerLocation.zero();
	R().enginesOn = TRUE;
}

//-------------------------------------------------------------------------------------------------
JetAIUpdate::~JetAIUpdate()
{
	GxComponentPool<GxState>::the().release(m_gxRow);
	if (R().lockonDrawable)
	{
		TheGameClient->destroyDrawable(R().lockonDrawable);
		R().lockonDrawable = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isIdle() const
{
	// we need to do this because we enter an idle state briefly between takeoff/landing in these cases,
	// but scripting relies on us never claiming to be "idle"...
	if (getFlag(HAS_PENDING_COMMAND))
		return false;

	return AIUpdateInterface::isIdle();
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isReloading() const
{
	StateID stateID = getStateMachine()->getCurrentStateID();
	if( stateID == RELOAD_AMMO )
	{
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isTaxiingToParking() const
{
	StateID stateID = getStateMachine()->getCurrentStateID();
	switch( stateID )
	{
		case TAXI_FROM_HANGAR:
		case TAXI_FROM_LANDING:
		case ORIENT_FOR_PARKING_PLACE:
		case RELOAD_AMMO:
		case TAKING_OFF_AWAIT_CLEARANCE:
		case TAXI_TO_TAKEOFF:
		case PAUSE_BEFORE_TAKEOFF:
		case TAKING_OFF:
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::onObjectCreated()
{
	AIUpdateInterface::onObjectCreated();
	friend_setAllowAirLoco(false);
	chooseLocomotorSet(LOCOMOTORSET_TAXIING);
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::onDelete()
{
	AIUpdateInterface::onDelete();
	ParkingPlaceBehaviorInterface* pp = getPP(getObject()->getProducerID());
	if (pp)
		pp->releaseSpace(getObject()->getID());
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::getProducerLocation()
{
	if (getFlag(HAS_PRODUCER_LOCATION))
		return;

	Object* jet = getObject();
	Object* airfield = TheGameLogic->findObjectByID( jet->getProducerID() );
	if (airfield == nullptr)
		R().producerLocation = *jet->getPosition();
	else
		R().producerLocation = *airfield->getPosition();

	/*
		if we aren't allowed to fly, then we should be parked (or at least taxiing),
		which implies we have a parking place reserved. If we don't, it's probably
		because we were directly spawned via script (or directly placed on the map).
		So, check to see if we have no parking place, and if not, quietly enable flight.
	*/
	ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
	if (!pp || !pp->hasReservedSpace(jet->getID()))
	{
		friend_setAllowAirLoco(true);
		chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	}
	else
	{
		friend_setAllowAirLoco(false);
		chooseLocomotorSet(LOCOMOTORSET_TAXIING);
	}

	setFlag(HAS_PRODUCER_LOCATION, true);

}

//-------------------------------------------------------------------------------------------------
// web port (round 5 addendum): the jet timer kernel. Pure: attack-locomotor, attackers-miss and
// untargetable expiry. reads: attacking, the frame, the persist times; reads/writes its row
// (attackLocoExpireFrame, attackersMissExpireFrame, untargetableExpireFrame) only. True when the
// attack locomotor just expired (apply: back to the normal locomotor set).
static Bool gxJetTimerKernel(JetAIUpdate::GxState &r, Bool attacking, UnsignedInt now, UnsignedInt attackLocoPersist, UnsignedInt attackersMissPersist)
{
	Bool attackLocoExpired = FALSE;
	if (attacking)
	{
		r.attackLocoExpireFrame = now + attackLocoPersist;
		r.attackersMissExpireFrame = now + attackersMissPersist;
	}
	else
	{
		if (r.attackLocoExpireFrame != 0 && now >= r.attackLocoExpireFrame)
		{
			r.attackLocoExpireFrame = 0;
			attackLocoExpired = TRUE;
		}
		if (r.attackersMissExpireFrame != 0 && now >= r.attackersMissExpireFrame)
			r.attackersMissExpireFrame = 0;
	}
	if (r.untargetableExpireFrame != 0 && now >= r.untargetableExpireFrame)
		r.untargetableExpireFrame = 0;
	return attackLocoExpired;
}

UpdateSleepTime JetAIUpdate::update()
{
	const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();

	getProducerLocation();

	Object* jet = getObject();

	ParkingPlaceBehaviorInterface* pp = getPP(getObject()->getProducerID());

	// If idle & out of ammo, return
	// have to call our parent's isIdle, because we override it to never return true
	// when we have a pending command...
	UnsignedInt now = TheGameLogic->getFrame();

	// srj sez: not 100% sure on this. calling RELOAD_AMMO "idle" allows us to get healed while reloading,
	// but might have other side effects we didn't want. if this does prove to cause a bug, be sure
	// that jets (and ESPECIALLY comanches) are still getting healed at airfields.
	if (AIUpdateInterface::isIdle() || getStateMachine()->getCurrentStateID() == RELOAD_AMMO)
	{
		if (pp != nullptr)
		{
			if (!getFlag(ALLOW_AIR_LOCO) &&
					!getFlag(HAS_PENDING_COMMAND) &&
						jet->isKindOf(KINDOF_PRODUCED_AT_HELIPAD) &&
						jet->getBodyModule()->getHealth() == jet->getBodyModule()->getMaxHealth())
			{
				// we're completely healed, so take off again
				pp->setHealee(jet, false);
				friend_setAllowAirLoco(true);
				getStateMachine()->clear();
				setLastCommandSource( CMD_FROM_AI );
				getStateMachine()->setState( TAKING_OFF_AWAIT_CLEARANCE );
			}
			else
			{
				pp->setHealee(jet, !getFlag(ALLOW_AIR_LOCO));
			}
		}

		// note that we might still have weapons with ammo, but still be forced to return to reload.
		if (isOutOfSpecialReloadAmmo() && getFlag(ALLOW_AIR_LOCO))
		{
			R().returnToBaseFrame = 0;

			// this is really a "just-in-case" to ensure the targeter list doesn't spin out of control (srj)
			pruneDeadTargeters();

			setFlag(USE_SPECIAL_RETURN_LOCO, true);
			setLastCommandSource( CMD_FROM_AI );
			getStateMachine()->setState(RETURNING_FOR_LANDING);
		}
		else if (getFlag(HAS_PENDING_COMMAND)
			// srj sez: if we are reloading ammo, wait will we are done before processing the pending command.
			&& getStateMachine()->getCurrentStateID() != RELOAD_AMMO)
		{
			R().returnToBaseFrame = 0;

			AICommandParms parms(AICMD_MOVE_TO_POSITION, CMD_FROM_AI);	// values don't matter, will be wiped by next line
			R().mostRecentCommand.reconstitute(parms);
			setFlag(HAS_PENDING_COMMAND, false);

 			aiDoCommand(&parms);
		}
		else if (R().returnToBaseFrame != 0 && now >= R().returnToBaseFrame && getFlag(ALLOW_AIR_LOCO))
		{
			R().returnToBaseFrame = 0;
			DEBUG_ASSERTCRASH(isOutOfSpecialReloadAmmo() == false, ("Hmm, this seems unlikely -- isOutOfSpecialReloadAmmo()==false"));
			setFlag(USE_SPECIAL_RETURN_LOCO, false);
			setLastCommandSource( CMD_FROM_AI );
			getStateMachine()->setState(RETURNING_FOR_LANDING);
		}
		else if (R().returnToBaseFrame  == 0 && d->m_returnToBaseIdleTime > 0 && getFlag(ALLOW_AIR_LOCO))
		{
			R().returnToBaseFrame = now + d->m_returnToBaseIdleTime;
		}
	}
	else
	{
		if (pp != nullptr)
		{
			pp->setHealee(getObject(), false);
		}
		R().returnToBaseFrame = 0;
		if (getFlag(ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD) &&
						isOutOfSpecialReloadAmmo() && getFlag(ALLOW_AIR_LOCO))
		{
			setFlag(USE_SPECIAL_RETURN_LOCO, true);
			setFlag(HAS_PENDING_COMMAND, true);
			setFlag(ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD, false);
			setLastCommandSource( CMD_FROM_AI );
			getStateMachine()->setState(RETURNING_FOR_LANDING);
		}
	}

	Real minHeight = friend_getMinHeight();
	if( pp )
	{
		minHeight += pp->getLandingDeckHeightOffset();
	}

	Drawable* draw = jet->getDrawable();
	if (draw != nullptr)
	{
		StateID id = getStateMachine()->getCurrentStateID();
		Bool needToCheckMinHeight = (id >= JETAISTATETYPE_FIRST && id <= JETAISTATETYPE_LAST) ||
																	!jet->isAboveTerrain() ||
																	!getFlag(ALLOW_AIR_LOCO);
		if( needToCheckMinHeight || jet->getStatusBits().test( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
		{
			Real ht = jet->isAboveTerrain() ? jet->getHeightAboveTerrain() : 0;
			if (ht < minHeight)
			{
				Matrix3D tmp(1);
				tmp.Set_Z_Translation(minHeight - ht);
				draw->setInstanceMatrix(&tmp);
			}
			else
			{
				draw->setInstanceMatrix(nullptr);
			}
		}
		else
		{
			draw->setInstanceMatrix(nullptr);
		}
	}

	PhysicsBehavior* physics = jet->getPhysics();
	if (physics->getVelocityMagnitude() > 0 && getFlag(ALLOW_AIR_LOCO))
		jet->setModelConditionState(MODELCONDITION_JETEXHAUST);
	else
		jet->clearModelConditionState(MODELCONDITION_JETEXHAUST);

	// web port (round 5 addendum): the timers are the kernel; the locomotor reset is the apply step
	if (gxJetTimerKernel(R(), jet->testStatus(OBJECT_STATUS_IS_ATTACKING), now, d->m_attackLocoPersistTime, d->m_attackersMissPersistTime))
	{
#if !RETAIL_COMPATIBLE_CRC
		// TheSuperHackers @bugfix arcticdolphin 04/03/2026 Reset locomotor when attack locomotor timer expires.
		// Relevant for the Supersonic Mode of the USA Aurora.
		chooseLocomotorSet(LOCOMOTORSET_NORMAL);
#endif
	}

	positionLockon();

	if (R().attackLocoExpireFrame != 0)
	{
		chooseLocomotorSet(d->m_attackingLoco);
	}
	else if (getFlag(USE_SPECIAL_RETURN_LOCO))
	{
		chooseLocomotorSet(d->m_returningLoco);
	}


	if( !jet->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
	{
		Drawable *draw = jet->getDrawable();
		if( draw )
		{
			if( getFlag(TAKEOFF_IN_PROGRESS)
					|| getFlag(LANDING_IN_PROGRESS)
					|| getObject()->isSignificantlyAboveTerrain()
					|| isMoving()
					|| isWaitingForPath() )
			{
				if( !R().enginesOn )
				{
					//We just started moving, therefore turn on the engines!
					draw->enableAmbientSound( TRUE );
					R().enginesOn = TRUE;
				}
			}
			else if( R().enginesOn )
			{
				//We're no longer moving, so turn off the engines!
				draw->enableAmbientSound( FALSE );
				R().enginesOn = FALSE;
			}
		}
	}


	/*UpdateSleepTime ret =*/ AIUpdateInterface::update();
	//return (mine < ret) ? mine : ret;
	/// @todo srj -- someday, make sleepy. for now, must not sleep.
	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::chooseLocomotorSet(LocomotorSetType wst)
{
	const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();
	if (!getFlag(ALLOW_AIR_LOCO))
	{
		wst = LOCOMOTORSET_TAXIING;
	}
	else if (R().attackLocoExpireFrame != 0)
	{
		wst = d->m_attackingLoco;
	}
	else if (getFlag(USE_SPECIAL_RETURN_LOCO))
	{
		wst = d->m_returningLoco;
	}
	return AIUpdateInterface::chooseLocomotorSet(wst);
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::setLocomotorGoalNone()
{
	if ((getFlag(TAKEOFF_IN_PROGRESS) || getFlag(LANDING_IN_PROGRESS))
			&& getFlag(ALLOW_AIR_LOCO) && !getFlag(ALLOW_CIRCLING))
	{
		Object* jet = getObject();
		Coord3D desiredPos = *jet->getPosition();
		const Coord3D* dir = jet->getUnitDirectionVector2D();
		desiredPos.x += dir->x * 1000.0f;
		desiredPos.y += dir->y * 1000.0f;
		setLocomotorGoalPositionExplicit(desiredPos);
	}
	else
	{
		AIUpdateInterface::setLocomotorGoalNone();
	}
}

//----------------------------------------------------------------------------------------
Bool JetAIUpdate::getSneakyTargetingOffset(Coord3D* offset) const
{
	if (R().attackersMissExpireFrame != 0 && TheGameLogic->getFrame() < R().attackersMissExpireFrame)
	{
		if (offset)
		{
			const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();
			const Object* jet = getObject();
			const Coord3D* dir = jet->getUnitDirectionVector2D();
			offset->x = dir->x * d->m_sneakyOffsetWhenAttacking;
			offset->y = dir->y * d->m_sneakyOffsetWhenAttacking;
			offset->z = 0.0f;
		}
		return true;
	}
	else
	{
		return false;
	}
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::pruneDeadTargeters()
{
	if (!m_targetedBy.empty())
	{
		for (std::list<ObjectID>::iterator it = m_targetedBy.begin(); it != m_targetedBy.end(); /* empty */ )
		{
			if (TheGameLogic->findObjectByID(*it) == nullptr)
			{
				it = m_targetedBy.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::positionLockon()
{
	if (!R().lockonDrawable)
		return;

	if (R().untargetableExpireFrame == 0)
	{
		TheGameClient->destroyDrawable(R().lockonDrawable);
		R().lockonDrawable = nullptr;
		return;
	}

	const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();
	UnsignedInt now = TheGameLogic->getFrame();
	UnsignedInt remaining = R().untargetableExpireFrame - now;
	UnsignedInt elapsed = d->m_lockonTime - remaining;

	Coord3D pos = *getObject()->getPosition();
	Real frac = (Real)remaining / (Real)d->m_lockonTime;
	Real finalDist = getObject()->getBoundingCircleRadius();
	Real dist = finalDist + (d->m_lockonInitialDist - finalDist) * frac;
	Real angle = d->m_lockonAngleSpin * frac;

	pos.x += Cos(angle) * dist;
	pos.y += Sin(angle) * dist;
	// pos.z is untouched

	R().lockonDrawable->setPosition(&pos);
	Real dx = getObject()->getPosition()->x - pos.x;
	Real dy = getObject()->getPosition()->y - pos.y;
	if (dx || dy)
		R().lockonDrawable->setOrientation(atan2(dy, dx));

	// the Gaussian sum, to avoid keeping a running total:
	//
	//		1+2+3+...n = n*(n+1)/2
	//
	Real elapsedTimeSumPrev = 0.5f * (elapsed-1) * (elapsed);
	Real elapsedTimeSumCurr = elapsedTimeSumPrev + elapsed;
	Real factor = d->m_lockonFreq / d->m_lockonTime;
	Bool lastPhase = ((Int)(factor * elapsedTimeSumPrev) & 1) != 0;
	Bool thisPhase = ((Int)(factor * elapsedTimeSumCurr) & 1) != 0;

	if (lastPhase && (!thisPhase))
	{
		AudioEventRTS lockonSound = TheAudio->getMiscAudio()->m_lockonTickSound;
		lockonSound.setObjectID(getObject()->getID());
		TheAudio->addAudioEvent(&lockonSound);
		if (d->m_lockonBlinky)
			R().lockonDrawable->setDrawableHidden(false);
	}
	else
	{
		if (d->m_lockonBlinky)
			R().lockonDrawable->setDrawableHidden(true);
	}
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::buildLockonDrawableIfNecessary()
{
	if (R().untargetableExpireFrame == 0)
		return;

	const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();
	if (d->m_lockonCursor.isNotEmpty() && R().lockonDrawable == nullptr)
	{
		const ThingTemplate* tt = TheThingFactory->findTemplate(d->m_lockonCursor);
		if (tt)
		{
			R().lockonDrawable = TheThingFactory->newDrawable(tt);
		}
	}
	positionLockon();
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::addTargeter(ObjectID id, Bool add)
{
	const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();
	UnsignedInt lockonTime = d->m_lockonTime;
	if (lockonTime != 0)
	{
		std::list<ObjectID>::iterator it = std::find(m_targetedBy.begin(), m_targetedBy.end(), id);
		if (add)
		{
			if (it == m_targetedBy.end())
			{
				m_targetedBy.push_back(id);
				if (R().untargetableExpireFrame == 0 && m_targetedBy.size() == 1)
				{
					R().untargetableExpireFrame = TheGameLogic->getFrame() + lockonTime;
					buildLockonDrawableIfNecessary();
				}
			}
		}
		else
		{
			if (it != m_targetedBy.end())
			{
				m_targetedBy.erase(it);
				if (m_targetedBy.empty())
				{
					R().untargetableExpireFrame = 0;
				}
			}
		}
	}
}

//----------------------------------------------------------------------------------------
Bool JetAIUpdate::isTemporarilyPreventingAimSuccess() const
{
	return R().untargetableExpireFrame != 0 && (TheGameLogic->getFrame() < R().untargetableExpireFrame);
}

//----------------------------------------------------------------------------------------
Bool JetAIUpdate::isAllowedToMoveAwayFromUnit() const
{
	// parked (or landing) units don't get to do this.
	if (!getFlag(ALLOW_AIR_LOCO) || getFlag(TAKEOFF_IN_PROGRESS) || getFlag(LANDING_IN_PROGRESS))
		return false;

	return AIUpdateInterface::isAllowedToMoveAwayFromUnit();
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isDoingGroundMovement() const
{
	// srj per jba: Air units should never be doing ground movement, even when taxiing...
	// (exception: see getTreatAsAircraftForLocoDistToGoal)
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::getTreatAsAircraftForLocoDistToGoal() const
{
	// exception to isDoingGroundMovement: should never treat as aircraft for dist-to-goal when taxiing.
	if (getFlag(TAXI_IN_PROGRESS))
	{
		return false;
	}
	else
	{
		return AIUpdateInterface::getTreatAsAircraftForLocoDistToGoal();
	}
}

//----------------------------------------------------------------------------------------
/**
 * Follow the path defined by the given array of points
 */
void JetAIUpdate::privateFollowPath( std::vector<Coord3D>* path, Object *ignoreObject, CommandSourceType cmdSource, Bool exitProduction )
{
	if (exitProduction)
	{
		getStateMachine()->clear();
		if( ignoreObject )
			ignoreObstacle( ignoreObject );
		setLastCommandSource( cmdSource );
		if (getObject()->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
			getStateMachine()->setState( TAKING_OFF_AWAIT_CLEARANCE );
		else
			getStateMachine()->setState( TAXI_FROM_HANGAR );
	}
	else
	{
		AIUpdateInterface::privateFollowPath(path, ignoreObject, cmdSource, exitProduction);
	}
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::privateFollowPathAppend( const Coord3D *pos, CommandSourceType cmdSource )
{
	// nothing yet... might need to override. not sure. (srj)
	AIUpdateInterface::privateFollowPathAppend(pos, cmdSource);
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::doLandingCommand(Object *airfield, CommandSourceType cmdSource)
{
	if (getObject()->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
	{
		R().landingPosForHelipadStuff = *airfield->getPosition();

		Coord3D tmp;
		FindPositionOptions options;
		options.maxRadius = airfield->getBoundingCircleRadius() * 10.0f;
		if (ThePartitionManager->findPositionAround(&R().landingPosForHelipadStuff, &options, &tmp))
			R().landingPosForHelipadStuff = tmp;
	}

	for (BehaviorModule** i = airfield->getBehaviorModules(); *i; ++i)
	{
		ParkingPlaceBehaviorInterface* pp = (*i)->getParkingPlaceBehaviorInterface();
		if (pp == nullptr)
			continue;

		if (getObject()->isKindOf(KINDOF_PRODUCED_AT_HELIPAD) ||
				pp->reserveSpace(getObject()->getID(), friend_getParkingOffset(), nullptr))
		{
			// if we had a space at another airfield, release it
			ParkingPlaceBehaviorInterface* oldPP = getPP(getObject()->getProducerID());
			if (oldPP != nullptr && oldPP != pp)
			{
				oldPP->releaseSpace(getObject()->getID());
			}

			getObject()->setProducer(airfield);
			DEBUG_ASSERTCRASH(isOutOfSpecialReloadAmmo() == false, ("Hmm, this seems unlikely -- isOutOfSpecialReloadAmmo()==false"));
			setFlag(USE_SPECIAL_RETURN_LOCO, false);
			setFlag(ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD, false);
			setLastCommandSource( cmdSource );
			getStateMachine()->setState(RETURNING_FOR_LANDING);
			return;
		}
	}
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::notifyVictimIsDead()
{
	if (getJetAIUpdateModuleData()->m_needsRunway)
		R().returnToBaseFrame = TheGameLogic->getFrame();
}

//----------------------------------------------------------------------------------------
/**
 * Enter the given object
 */
void JetAIUpdate::privateEnter( Object *objectToEnter, CommandSourceType cmdSource )
{
	// we are already landing. just ignore it.
	if (getFlag(LANDING_IN_PROGRESS))
		return;

	if( !TheActionManager->canEnterObject( getObject(), objectToEnter, cmdSource, DONT_CHECK_CAPACITY ) )
		return;

	doLandingCommand(objectToEnter, cmdSource);
}

//----------------------------------------------------------------------------------------
/**
 * Get repaired at the repair depot
 */
void JetAIUpdate::privateGetRepaired( Object *repairDepot, CommandSourceType cmdSource )
{
	// we are already landing. just ignore it.
	if (getFlag(LANDING_IN_PROGRESS))
		return;

	// sanity, if we can't get repaired from here get out of here
	if( TheActionManager->canGetRepairedAt( getObject(), repairDepot, cmdSource ) == FALSE )
		return;

	// dock with the repair depot
	doLandingCommand( repairDepot, cmdSource );

}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isParkedAt(const Object* obj) const
{
	if (!getFlag(ALLOW_AIR_LOCO) &&
			!getObject()->isKindOf(KINDOF_PRODUCED_AT_HELIPAD) &&
			obj != nullptr)
	{
		Object* airfield;
		ParkingPlaceBehaviorInterface* pp = getPP(getObject()->getProducerID(), &airfield);
		if (pp != nullptr && airfield != nullptr && airfield == obj)
		{
			return true;
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::aiDoCommand(const AICommandParms* parms)
{
	// call this from aiDoCommand as well as update, because this can
	// be called before update ever is... if the unit is placed on a map,
	// and a script tells it to do something with a condition of TRUE!
	getProducerLocation();

	if (!isAllowedToRespondToAiCommands(parms))
		return;

	// note that we always store this, even if nothing will be "pending".
	R().mostRecentCommand.store(*parms);

	if (getFlag(TAKEOFF_IN_PROGRESS) || getFlag(LANDING_IN_PROGRESS))
	{
		// have to wait for takeoff or landing to complete, just store the sucker
		setFlag(HAS_PENDING_COMMAND, true);
		return;
	}
	else if (parms->m_cmd == AICMD_IDLE && getStateMachine()->getCurrentStateID() == RELOAD_AMMO)
	{
		// uber-special-case... if we are told to idle, but are reloading ammo, ignore it for now,
		// since we're already doing "nothing" and responding to this will cease our reload...
		// don't just return, tho, in case we were (say) reloading during a guard stint.
		setFlag(HAS_PENDING_COMMAND, true);
		return;
	}
	else if( parms->m_cmd == AICMD_IDLE && getObject()->isAirborneTarget() && !getObject()->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
	{
		getStateMachine()->clear();
		setLastCommandSource( CMD_FROM_AI );
		getStateMachine()->setState( RETURNING_FOR_LANDING );
		return;
	}
	else if (!getFlag(ALLOW_AIR_LOCO))
	{
		switch (parms->m_cmd)
		{
			case AICMD_IDLE:
			case AICMD_BUSY:
			case AICMD_FOLLOW_EXITPRODUCTION_PATH:
				// don't need (or want) to take off for these
				break;

#if !RETAIL_COMPATIBLE_CRC
			// TheSuperHackers @tweak Stubbjax 23/01/2026 Parked jets now defer offensive commands when out of ammo.
			case AICMD_ATTACKMOVE_TO_POSITION:
			case AICMD_ATTACK_AREA:
			case AICMD_ATTACK_OBJECT:
			case AICMD_ATTACK_POSITION:
			case AICMD_ATTACK_TEAM:
			case AICMD_FORCE_ATTACK_OBJECT:
			case AICMD_GUARD_AREA:
			case AICMD_GUARD_OBJECT:
			case AICMD_GUARD_POSITION:
			case AICMD_GUARD_RETALIATE:
			case AICMD_HUNT:
				if (isOutOfSpecialReloadAmmo())
				{
					setFlag(HAS_PENDING_COMMAND, true);
					return;
				}

				FALLTHROUGH;
#endif

			case AICMD_ENTER:
			case AICMD_GET_REPAIRED:

				// if we're already parked at the airfield in question, just ignore.
				if (isParkedAt(parms->m_obj))
					return;

				FALLTHROUGH; // else fall thru to the default case!

			default:
			{
				// nuke any existing pending cmd
				R().mostRecentCommand.store(*parms);
				setFlag(HAS_PENDING_COMMAND, true);

				getStateMachine()->clear();
				setLastCommandSource( CMD_FROM_AI );
				getStateMachine()->setState( TAKING_OFF_AWAIT_CLEARANCE );

				return;
			}
		}
	}

	switch (parms->m_cmd)
	{
		case AICMD_GUARD_POSITION:
		case AICMD_GUARD_OBJECT:
		case AICMD_GUARD_AREA:
		case AICMD_HUNT:
		case AICMD_GUARD_RETALIATE:
			setFlag(ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD, true);
			break;
		default:
			setFlag(ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD, false);
			break;
	}

	setFlag(HAS_PENDING_COMMAND, false);
	AIUpdateInterface::aiDoCommand(parms);
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::friend_setAllowAirLoco(Bool allowAirLoco)
{
	setFlag(ALLOW_AIR_LOCO, allowAirLoco);
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::friend_enableAfterburners(Bool v)
{
	Object* jet = getObject();
	if (v)
	{
		jet->setModelConditionState(MODELCONDITION_JETAFTERBURNER);
		if (!R().afterburnerSound.isCurrentlyPlaying())
		{
			R().afterburnerSound.setObjectID(jet->getID());
			R().afterburnerSound.setPlayingHandle(TheAudio->addAudioEvent(&R().afterburnerSound));
		}
	}
	else
	{
		jet->clearModelConditionState(MODELCONDITION_JETAFTERBURNER);
		if (R().afterburnerSound.isCurrentlyPlaying())
		{
			TheAudio->removeAudioEvent(R().afterburnerSound.getPlayingHandle());
		}
	}
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::friend_addWaypointToGoalPath( const Coord3D &bestPos )
{
	privateFollowPathAppend( &bestPos, CMD_FROM_AI );
}

//-------------------------------------------------------------------------------------------------
AICommandType JetAIUpdate::friend_getPendingCommandType() const
{
	if( getFlag( HAS_PENDING_COMMAND ) )
	{
		return R().mostRecentCommand.getCommandType();
	}
	return AICMD_NO_COMMAND;
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::friend_purgePendingCommand()
{
	setFlag(HAS_PENDING_COMMAND, false);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void JetAIUpdate::crc( Xfer *xfer )
{
	// extend base class
	AIUpdateInterface::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void JetAIUpdate::xfer( Xfer *xfer )
{

  // version
  XferVersion currentVersion = 2;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
	AIUpdateInterface::xfer(xfer);


	xfer->xferCoord3D(&R().producerLocation);
	R().mostRecentCommand.doXfer(xfer);
	xfer->xferUnsignedInt(&R().attackLocoExpireFrame);
	xfer->xferUnsignedInt(&R().attackersMissExpireFrame);
	xfer->xferUnsignedInt(&R().returnToBaseFrame);
	xfer->xferSTLObjectIDList(&m_targetedBy);

	xfer->xferUnsignedInt(&R().untargetableExpireFrame);

	// Set on create.
	//AudioEventRTS						R().afterburnerSound;		///< Sound when afterburners on

	AsciiString drawName;
	if (R().lockonDrawable) {
		drawName = R().lockonDrawable->getTemplate()->getName();
	}
	xfer->xferAsciiString(&drawName);
	if (drawName.isNotEmpty() && R().lockonDrawable==nullptr)
	{
		const ThingTemplate* tt = TheThingFactory->findTemplate(drawName);
		if (tt)
		{
			R().lockonDrawable = TheThingFactory->newDrawable(tt);
		}
	}
	xfer->xferInt(&R().flags);

	if( version >= 2 )
	{
		xfer->xferBool( &R().enginesOn );
	}
	else
	{
		//We don't have to be accurate -- this is a patch.
		if( getFlag(TAKEOFF_IN_PROGRESS) || getFlag(LANDING_IN_PROGRESS) || getObject()->isSignificantlyAboveTerrain() || getObject()->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
		{
			R().enginesOn = TRUE;
		}
		else
		{
			R().enginesOn = FALSE;
		}
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void JetAIUpdate::loadPostProcess()
{
	//When drawables are created, so are their ambient sounds. After loading, only turn off the
	//ambient sound if the engine is off.
	if( !R().enginesOn )
	{
		Drawable *draw = getObject()->getDrawable();
		if( draw )
		{
			draw->stopAmbientSound();
		}
	}

	// extend base class
	AIUpdateInterface::loadPostProcess();
}

