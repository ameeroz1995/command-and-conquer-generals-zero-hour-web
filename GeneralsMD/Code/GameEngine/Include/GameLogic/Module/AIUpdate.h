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

// AIUpdate.h //
// AI interface
// Author: Michael S. Booth, 2001-2002

#pragma once

#include "GameLogic/Module/UpdateModule.h"
#include "Common/GxComponentPool.h"	// web port (round 5)
#include "GameLogic/AI.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LocomotorSet.h"

class AIGroup;
class AIStateMachine;
class AttackPriorityInfo;
class DamageInfo;
class DozerAIInterface;
class Object;
class Path;
class PathfindServicesInterface;
class PathNode;
class PhysicsBehavior;
#ifdef ALLOW_SURRENDER
class POWTruckAIUpdateInterface;
#endif
class SupplyTruckAIInterface;
class TurretAI;
class TurretAIData;
class Waypoint;
class WorkerAIInterface;
class HackInternetAIInterface;
class AssaultTransportAIInterface;
class JetAIUpdate;

enum AIStateType CPP_11(: Int);
enum HordeActionType CPP_11(: Int);
enum ObjectID CPP_11(: Int);


//-------------------------------------------------------------------------------------------------
const Real FAST_AS_POSSIBLE = 999999.0f;

//-------------------------------------------------------------------------------------------------
//
// Note: these values are saved in save files, so you MUST NOT REMOVE OR CHANGE
// existing values!
//
enum LocomotorSetType CPP_11(: Int)
{
	LOCOMOTORSET_INVALID = -1,

	LOCOMOTORSET_NORMAL = 0,
	LOCOMOTORSET_NORMAL_UPGRADED,
	LOCOMOTORSET_FREEFALL,
	LOCOMOTORSET_WANDER,
	LOCOMOTORSET_PANIC,
	LOCOMOTORSET_TAXIING,			// set used for normally-airborne items while taxiing on ground
	LOCOMOTORSET_SUPERSONIC,	// set used for high-speed attacks
	LOCOMOTORSET_SLUGGISH,		// set used for abnormally slow (but not damaged) speeds

	LOCOMOTORSET_COUNT
};

//-------------------------------------------------------------------------------------------------
enum GuardTargetType CPP_11(: Int)
{
	GUARDTARGET_LOCATION,	// Guard a coord3d
	GUARDTARGET_OBJECT,		// Guard an object
	GUARDTARGET_AREA,			// Guard a Polygon trigger
	GUARDTARGET_NONE				// Currently not guarding
};

#ifdef DEFINE_LOCOMOTORSET_NAMES
static const char *const TheLocomotorSetNames[] =
{
	"SET_NORMAL",
	"SET_NORMAL_UPGRADED",
	"SET_FREEFALL",
	"SET_WANDER",
	"SET_PANIC",
	"SET_TAXIING",
	"SET_SUPERSONIC",
	"SET_SLUGGISH",

	nullptr
};
static_assert(ARRAY_SIZE(TheLocomotorSetNames) == LOCOMOTORSET_COUNT + 1, "Incorrect array size");
#endif

enum AutoAcquireStates CPP_11(: Int)
{
	AAS_Idle											= 0x01,
	AAS_Idle_Stealthed						= 0x02,
	AAS_Idle_No										= 0x04,
	AAS_Idle_Not_While_Attacking	= 0x08,
	AAS_Idle_Attack_Buildings			= 0x10,
};

#ifdef DEFINE_AUTOACQUIRE_NAMES
static const char *const TheAutoAcquireEnemiesNames[] =
{
	"YES",
	"STEALTHED",
	"NO",
	"NOTWHILEATTACKING",
	"ATTACK_BUILDINGS",

	nullptr
};
#endif


//-------------------------------------------------------------------------------------------------
enum MoodMatrixParameters CPP_11(: Int)
{
	// Controller_Player and Controller_AI are mutually exclusive
	MM_Controller_Player		= 0x00000001,
	MM_Controller_AI				= 0x00000002,
	MM_Controller_Bitmask		= (MM_Controller_Player | MM_Controller_AI),

	// UnitTypes are mutually exclusive, ie: you cannot be turreted and air at the same time (we do
	// not make such a distinction, so air takes precedence over turretedness)
	MM_UnitType_NonTurreted = 0x00000010,
	MM_UnitType_Turreted		= 0x00000020,
	MM_UnitType_Air					= 0x00000040,
	MM_UnitType_Bitmask			= (MM_UnitType_NonTurreted | MM_UnitType_Turreted | MM_UnitType_Air),

	// A unit can be in only one mood at a time.
	MM_Mood_Sleep						= 0x00000100,
	MM_Mood_Passive					= 0x00000200,
	MM_Mood_Normal					= 0x00000400,
	MM_Mood_Alert						= 0x00000800,
	MM_Mood_Aggressive			= 0x00001000,
	MM_Mood_Bitmask					= (MM_Mood_Sleep | MM_Mood_Passive | MM_Mood_Normal | MM_Mood_Alert | MM_Mood_Aggressive)
};

//-------------------------------------------------------------------------------------------------
enum MoodMatrixAction CPP_11(: Int)
{
	MM_Action_Idle,
	MM_Action_Move,
	MM_Action_Attack,
	MM_Action_AttackMove
};

//-------------------------------------------------------------------------------------------------
enum MoodActionAdjustment CPP_11(: Int)
{
	MAA_Action_Ok										= 0x00000001,
	MAA_Action_To_Idle							= 0x00000002,
	MAA_Action_To_AttackMove				= 0x00000004,
	MAA_Action_To_Bitmask						= (MAA_Action_Ok | MAA_Action_To_Idle | MAA_Action_To_AttackMove),

	MAA_Affect_Range_IgnoreAll			= 0x00000010,
	MAA_Affect_Range_WaitForAttack	= 0x00000020,
	// Normal doesn't affect ranges.
	MAA_Affect_Range_Alert					= 0x00000040,
	MAA_Affect_Range_Aggressive			= 0x00000080,
	MAA_Affect_Range_Bitmask				= (MAA_Affect_Range_IgnoreAll | MAA_Affect_Range_WaitForAttack | MAA_Affect_Range_Alert | MAA_Affect_Range_Aggressive)
};

//-------------------------------------------------------------------------------------------------
typedef std::vector< const LocomotorTemplate* > LocomotorTemplateVector;
typedef std::map< LocomotorSetType, LocomotorTemplateVector, std::less<LocomotorSetType> > LocomotorTemplateMap;

//-------------------------------------------------------------------------------------------------
class AIUpdateModuleData : public UpdateModuleData
{
public:
	LocomotorTemplateMap	m_locomotorTemplates;					///< locomotors for object
	const TurretAIData*		m_turretData[MAX_TURRETS];
	UnsignedInt						m_moodAttackCheckRate;				///< how frequently we should recheck for enemies due to moods, when idle
  Bool        m_forbidPlayerCommands;     ///< Should isAllowedToRespondToAiCommands() filter out commands from the player, thus making it ai-controllable only?
  Bool        m_turretsLinked;						///< Turrets are linked together and attack together.
	UnsignedInt						m_autoAcquireEnemiesWhenIdle;
#ifdef ALLOW_SURRENDER
 	UnsignedInt						m_surrenderDuration;					///< when we surrender, how long we stay surrendered.
#endif


  AIUpdateModuleData();
	virtual ~AIUpdateModuleData();

	virtual Bool isAiModuleData() const { return true; }

	const LocomotorTemplateVector* findLocomotorTemplateVector(LocomotorSetType t) const;
	static void buildFieldParse(MultiIniFieldParse& p);
	static void parseLocomotorSet( INI* ini, void *instance, void *store, const void* /*userData*/ );

private:
	static void parseTurret( INI* ini, void *instance, void *store, const void* /*userData*/ );


};

//-------------------------------------------------------------------------------------------------
enum AIFreeToExitType CPP_11(: Int) // Note - written out in save/load xfer, don't change these numbers.  jba.
{
	FREE_TO_EXIT=0,
	NOT_FREE_TO_EXIT=1,
	WAIT_TO_EXIT=2
};

//-------------------------------------------------------------------------------------------------
/**
 * The AIUpdateInterface module contains the interface to the AI system,
 * and performs the actual AI behaviors.
 */
struct GxCpopRow;
struct ClosestPointOnPathInfo;
struct GxAIUpdateInterfaceRow;
class AIUpdateInterface : public UpdateModule, public AICommandInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AIUpdateInterface, "AIUpdateInterface" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( AIUpdateInterface, AIUpdateModuleData )

protected:

	// yes, protected, NOT public.
	virtual void privateMoveToPosition( const Coord3D *pos, CommandSourceType cmdSource );			///< move to given position(s) tightening the formation.
	virtual void privateMoveToObject( Object *obj, CommandSourceType cmdSource );			///< move to given object
	virtual void privateMoveToAndEvacuate( const Coord3D *pos, CommandSourceType cmdSource );			///< move to given position(s)
	virtual void privateMoveToAndEvacuateAndExit( const Coord3D *pos, CommandSourceType cmdSource );			///< move to given position & unload transport.
	virtual void privateIdle(CommandSourceType cmdSource);						///< Enter idle state.
	virtual void privateTightenToPosition( const Coord3D *pos, CommandSourceType cmdSource );			///< move to given position(s) tightening the formation.
	virtual void privateFollowWaypointPath( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	virtual void privateFollowWaypointPathAsTeam( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	virtual void privateFollowWaypointPathExact( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	virtual void privateFollowWaypointPathAsTeamExact( const Waypoint *way, CommandSourceType cmdSource );///< start following the path from the given point
	virtual void privateFollowPath( std::vector<Coord3D>* path, Object *ignoreObject, CommandSourceType cmdSource, Bool exitProduction );///< follow the path defined by the given array of points
	virtual void privateFollowPathAppend( const Coord3D *pos, CommandSourceType cmdSource );
	virtual void privateAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource );					///< attack given object
	virtual void privateForceAttackObject( Object *victim, Int maxShotsToFire, CommandSourceType cmdSource );					///< attack given object
	virtual void privateGuardRetaliate( Object *victim, const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource );				///< retaliate and attack attacker -- but with guard restrictions
	virtual void privateAttackTeam( const Team *team, Int maxShotsToFire, CommandSourceType cmdSource );							///< attack the given team
	virtual void privateAttackPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource );						///< attack given spot
	virtual void privateAttackMoveToPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource );			///< attack move to the given location
	virtual void privateAttackFollowWaypointPath( const Waypoint *way, Int maxShotsToFire, Bool asTeam, CommandSourceType cmdSource );			///< attack move along the following waypoint path, potentially as a team
	virtual void privateHunt( CommandSourceType cmdSource );														///< begin "seek and destroy"
	virtual void privateRepair( Object *obj, CommandSourceType cmdSource );						///< repair the given object
#ifdef ALLOW_SURRENDER
	virtual void privatePickUpPrisoner( Object *prisoner, CommandSourceType cmdSource );			///< pick up prisoner
	virtual void privateReturnPrisoners( Object *prison, CommandSourceType cmdSource );			///< return picked up prisoners to the 'prison'
#endif
	virtual void privateResumeConstruction( Object *obj, CommandSourceType cmdSource );	///< resume construction of object
	virtual void privateGetHealed( Object *healDepot, CommandSourceType cmdSource );		///< get healed at heal depot
	virtual void privateGetRepaired( Object *repairDepot, CommandSourceType cmdSource );///< get repaired at repair depot
	virtual void privateEnter( Object *obj, CommandSourceType cmdSource );							///< enter the given object
	virtual void privateDock( Object *obj, CommandSourceType cmdSource );							///< get near given object and wait for enter clearance
	virtual void privateExit( Object *objectToExit, CommandSourceType cmdSource );			///< get out of this Object
	virtual void privateExitInstantly( Object *objectToExit, CommandSourceType cmdSource );			///< get out of this Object this frame
	virtual void privateEvacuate( Int exposeStealthUnits, CommandSourceType cmdSource );												///< empty its contents
	virtual void privateEvacuateInstantly( Int exposeStealthUnits, CommandSourceType cmdSource );												///< empty its contents this frame
	virtual void privateExecuteRailedTransport( CommandSourceType cmdSource );					///< execute next leg in railed transport sequence
	virtual void privateGoProne( const DamageInfo *damageInfo, CommandSourceType cmdSource );												///< life altering state change, if this AI can do it
	virtual void privateGuardTunnelNetwork( GuardMode guardMode, CommandSourceType cmdSource );			///< guard the given spot
	virtual void privateGuardPosition( const Coord3D *pos, GuardMode guardMode, CommandSourceType cmdSource );			///< guard the given spot
	virtual void privateGuardObject( Object *objectToGuard, GuardMode guardMode, CommandSourceType cmdSource );		///< guard the given object
	virtual void privateGuardArea( const PolygonTrigger *areaToGuard, GuardMode guardMode, CommandSourceType cmdSource );	///< guard the given area
	virtual void privateAttackArea( const PolygonTrigger *areaToGuard, CommandSourceType cmdSource );	///< guard the given area
	virtual void privateHackInternet( CommandSourceType cmdSource );	///< Hack money from the heavens (free money)
	virtual void privateFaceObject( Object *target, CommandSourceType cmdSource );
	virtual void privateFacePosition( const Coord3D *pos, CommandSourceType cmdSource );
	virtual void privateRappelInto( Object *target, const Coord3D& pos, CommandSourceType cmdSource );
	virtual void privateCombatDrop( Object *target, const Coord3D& pos, CommandSourceType cmdSource );
	virtual void privateCommandButton( const CommandButton *commandButton, CommandSourceType cmdSource );
	virtual void privateCommandButtonPosition( const CommandButton *commandButton, const Coord3D *pos, CommandSourceType cmdSource );
	virtual void privateCommandButtonObject( const CommandButton *commandButton, Object *obj, CommandSourceType cmdSource );
	virtual void privateWander( const Waypoint *way, CommandSourceType cmdSource );	///< Wander around the waypoint path.
	virtual void privateWanderInPlace( CommandSourceType cmdSource );	///< Wander around the current position.
	virtual void privatePanic( const Waypoint *way, CommandSourceType cmdSource );	///< Run screaming down the waypoint path.
	virtual void privateBusy( CommandSourceType cmdSource );	///< Transition to the busy state
	virtual void privateMoveAwayFromUnit( Object *unit, CommandSourceType cmdSource );	///< Move out of the way of a unit.


public:
	AIUpdateInterface( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual AIUpdateInterface* getAIUpdateInterface() { return this; }

	// Disabled conditions to process (AI will still process held status)
	virtual DisabledMaskType getDisabledTypesToProcess() const { return MAKE_DISABLED_MASK( DISABLED_HELD ); }

	// Some very specific, complex behaviors are used by more than one AIUpdate.  Here are their interfaces.
	virtual DozerAIInterface* getDozerAIInterface() {return nullptr;}
	virtual SupplyTruckAIInterface* getSupplyTruckAIInterface() {return nullptr;}
	virtual const DozerAIInterface* getDozerAIInterface() const {return nullptr;}
	virtual const SupplyTruckAIInterface* getSupplyTruckAIInterface() const {return nullptr;}
#ifdef ALLOW_SURRENDER
	virtual POWTruckAIUpdateInterface *getPOWTruckAIUpdateInterface() { return nullptr; }
#endif
	virtual WorkerAIInterface* getWorkerAIInterface() { return nullptr; }
	virtual const WorkerAIInterface* getWorkerAIInterface() const { return nullptr; }
	virtual HackInternetAIInterface* getHackInternetAIInterface() { return nullptr; }
	virtual const HackInternetAIInterface* getHackInternetAIInterface() const { return nullptr; }
	virtual AssaultTransportAIInterface* getAssaultTransportAIInterface() { return nullptr; }
	virtual const AssaultTransportAIInterface* getAssaultTransportAIInterface() const { return nullptr; }
	virtual JetAIUpdate* getJetAIUpdate() { return nullptr; }
	virtual const JetAIUpdate* getJetAIUpdate() const { return nullptr; }

#ifdef ALLOW_SURRENDER
	void setSurrendered( const Object *objWeSurrenderedTo, Bool surrendered );
	inline Bool isSurrendered() const { return gxRow().surrenderedFramesLeft > 0; }
	inline Int getSurrenderedPlayerIndex() const { return gxRow().surrenderedPlayerIndex; }
#endif

	virtual void joinTeam();			///< This unit just got added to a team & needs to catch up.

	Bool areTurretsLinked() const { return getAIUpdateModuleData()->m_turretsLinked; }

	// this is present solely for some transports to override, so that they can land before
	// allowing people to exit...
	virtual AIFreeToExitType getAiFreeToExit(const Object* exiter) const { return FREE_TO_EXIT; }

	// this is present solely to allow some special-case things to override, like landed choppers.
	virtual Bool isAllowedToAdjustDestination() const { return true; }
	virtual Bool isAllowedToMoveAwayFromUnit() const { return true; }
	// this is used for Chinooks, so the pathfinder doesn't make 'em avoid 'em... for combat drops!
	virtual ObjectID getBuildingToNotPathAround() const { return INVALID_ID; }

	// AI Interface implementation -----------------------------------------------------------------------
	virtual Bool isIdle() const;
	virtual Bool isAttacking() const;
	virtual Bool isClearingMines() const;
	virtual Bool isTaxiingToParking() const { return FALSE; } //only applies to jets interacting with runways.

	//Definition of busy -- when explicitly in the busy state. Moving or attacking is not considered busy!
	virtual Bool isBusy() const;

	virtual void onObjectCreated();
	virtual void doQuickExit( std::vector<Coord3D>* path );			///< get out of this Object

	virtual void aiDoCommand(const AICommandParms* parms);

	virtual const Coord3D * getGuardLocation() const;
	virtual ObjectID getGuardObject() const;
	virtual const PolygonTrigger * getAreaToGuard() const;
	virtual GuardTargetType getGuardTargetType() const;
	virtual void clearGuardTargetType();
	virtual GuardMode getGuardMode() const;

	virtual Object* construct( const ThingTemplate *what,
														 const Coord3D *pos, Real angle,
														 Player *owningPlayer,
														 Bool isRebuild ) { return nullptr; }///< construct a building


	void ignoreObstacle( const Object *obj );			///< tell the pathfinder to ignore the given object as an obstacle
	void ignoreObstacleID( ObjectID id );			///< tell the pathfinder to ignore the given object as an obstacle

	AIStateType getAIStateType() const;							///< What general state is the AIState Machine in?

	AsciiString getCurrentStateName() const;

	virtual Object* getEnterTarget();					///< if we are trying to enter something (see enter() above), what is the thing in question? (or null)

	/**
		This exists solely to be overridden by children... the idea is that when a unit is targeted by someone else, it
		asks for an offset to add to its targeting position... if this returns true, the offset should be added to the
		true target pos. This is primarily for the Aurora Bomber, which uses this technique to make itself "unhittable"
		when in Supersonic Attack Mode -- things still target it, they just aim "behind" it!
	*/
	virtual Bool getSneakyTargetingOffset(Coord3D* offset) const { return false; }

	/**
		Exists solely to be overridden by JetAIUpdate...
	*/
	virtual void addTargeter(ObjectID id, Bool add) { return; }
	virtual Bool isTemporarilyPreventingAimSuccess() const { return false; }


	void setPriorWaypointID( UnsignedInt id );
	void setCurrentWaypointID( UnsignedInt id );

	// Group ----------------------------------------------------------------------------------------------
	// these three methods allow a group leader's path to be communicated to the other group members

	AIGroup *getGroup();

	// it's VERY RARE you want to call this function; you should normally use Object::isEffectivelyDead()
	// instead. the exception would be for things that need to know whether to call markIsDead or not.
	Bool isAiInDeadState() const;				///< return true if we are dead
	void markAsDead();

	Bool isRecruitable() const;
	void setIsRecruitable(Bool isRecruitable);

	Real getDesiredSpeed() const;
	void setDesiredSpeed( Real speed );	///< how fast we want to go

	// these are virtual because subclasses might need to override them. (srj)
	virtual void setLocomotorGoalPositionOnPath();
	virtual void setLocomotorGoalPositionExplicit(const Coord3D& newPos);
	virtual void setLocomotorGoalOrientation(Real angle);
	virtual void setLocomotorGoalNone();
	virtual Bool isDoingGroundMovement() const;  ///< True if moving along ground.

	Bool isValidLocomotorPosition(const Coord3D* pos) const;
	Bool isAircraftThatAdjustsDestination() const;  ///< True if is aircraft that doesn't stack destinations (missles for example do stack destinations.)
	Real getCurLocomotorSpeed() const;
	Real getLocomotorDistanceToGoal();
	const Locomotor * getCurLocomotor() const;
	Locomotor * getCurLocomotor();
	LocomotorSetType getCurLocomotorSetType() const;
	Bool hasLocomotorForSurface(LocomotorSurfaceType surfaceType);

	// turret stuff.
	WhichTurretType getWhichTurretForWeaponSlot(WeaponSlotType wslot, Real* turretAngle, Real* turretPitch = nullptr) const;
	WhichTurretType getWhichTurretForCurWeapon() const;
	/**
		return true iff the weapon is on a turret, that turret is trying to aim at the victim,
		BUT is not yet pointing in the right dir.
	*/
	Bool isWeaponSlotOnTurretAndAimingAtTarget(WeaponSlotType wslot, const Object* victim) const;
	Bool getTurretRotAndPitch(WhichTurretType tur, Real* turretAngle, Real* turretPitch) const;
	Real getTurretTurnRate(WhichTurretType tur) const;
	void setTurretTargetObject(WhichTurretType tur, Object* o, Bool isForceAttacking = FALSE);
	Object *getTurretTargetObject( WhichTurretType tur, Bool clearDeadTargets = TRUE );
	void setTurretTargetPosition(WhichTurretType tur, const Coord3D* pos);
	void setTurretEnabled(WhichTurretType tur, Bool enabled);
	void recenterTurret(WhichTurretType tur);
	Bool isTurretEnabled( WhichTurretType tur ) const;
	Bool isTurretInNaturalPosition(WhichTurretType tur) const;

	// "Planning Mode" -----------------------------------------------------------------------------------
	Bool queueWaypoint( const Coord3D *pos );				///< add waypoint to end of move list. return true if success, false if queue was full and those the waypoint not added
	void clearWaypointQueue();								///< reset the waypoint queue to empty
	void executeWaypointQueue();							///< start moving along queued waypoints

	// Pathfinding ---------------------------------------------------------------------------------------
private:
	Bool computePath( PathfindServicesInterface *pathfinder, Coord3D *destination );	///< computes path to destination, returns false if no path
	Bool computeAttackPath(PathfindServicesInterface *pathfinder,  const Object *victim, const Coord3D* victimPos );	///< computes path to attack the current target, returns false if no path
#ifdef ALLOW_SURRENDER
	void doSurrenderUpdateStuff();
#endif

public:
	void doPathfind( PathfindServicesInterface *pathfinder );
	void requestPath( Coord3D *destination, Bool isGoalDestination );	///< Queues a request to pathfind to destination.
	void requestAttackPath( ObjectID victimID, const Coord3D* victimPos );	///< computes path to attack the current target, returns false if no path
	void requestApproachPath( Coord3D *destination );	///< computes path to attack the current target, returns false if no path
	void requestSafePath( ObjectID repulsor1 );	///< computes path to attack the current target, returns false if no path

	Bool isWaitingForPath() const;
	void gxMovementPassStep();
	void gxSetMoving(Bool on);	///< web port: in / out of the store's MOVE list (state change only)
	void gxMoveKernelStep(GxCpopRow &row, UnsignedInt frame);	///< web port: movement kernel, this unit's route point
	Bool gxTakeKernelPoint(const Coord3D &pos, ClosestPointOnPathInfo &info) const;	///< web port: the kernel point, if still valid
	Bool gxWantsClosestSpot() const;
	const Coord3D * gxRequestedDest() const;	///< web port: the destination of the waiting request
	Bool gxPlainRequest() const;	///< web port: a plain move request is waiting	///< web port (match rule routeQueue): the queued search is a closest-spot one
	void gxStartCoarseDeferred();	///< web port (match rule routeQueue): coarse start spread over frames
	void gxSetRouteHint( const std::vector<Coord3D> &pts, UnsignedInt frame );	///< web port (match rule groupRoute)	///< web port (match rule moveAfterTick): this unit's locomotion, from the movement pass
	Bool gxOnCoarsePath() const;	///< web port (match rule coarseStart): following the zone-level route while the full one is queued
	Bool isAttackPath() const; ///< True if we have a path to an attack location.
	void cancelPath(); ///< Called if we no longer need the path.
	Path* getPath();				///< return the agent's current path
	const Path* getPath() const;				///< return the agent's current path
	void destroyPath();												///< destroy the current path, setting it to null
	UnsignedInt getPathAge() const;	///< return the "age" of the path
	Bool isPathAvailable( const Coord3D *destination ) const; ///< does a path exist between us and the destination
	Bool isQuickPathAvailable( const Coord3D *destination ) const;  ///< does a path (using quick pathfind) exist between us and the destination
	Int getNumFramesBlocked() const;
	Bool isBlockedAndStuck() const;
	Bool canComputeQuickPath(); ///< Returns true if we can quickly comput a path.  Usually missiles & the like that just move straight to the destination.
	Bool computeQuickPath(const Coord3D *destination); ///< Computes a quick path to the destination.

	Bool isMoving() const;
	Bool isMovingAwayFrom(Object *obj) const;

	// the following routines should only be called by the AIInternalMoveToState.
	// They are used to determine when we are really through moving.  Due to the nature of the beast,
	// we often exit one move state & immediately enter another.  This should not cause a "stop start".
	// jba.
	void friend_startingMove();
	void friend_endingMove();

	void friend_setPath(Path *newPath);
	Path* friend_getPath();

	void friend_setGoalObject(Object *obj);

	virtual Bool processCollision(PhysicsBehavior *physics, Object *other); ///< Returns true if the physics collide should apply the force.  Normally not.  jba.
	ObjectID getIgnoredObstacleID() const;

	// "Waypoint Mode" -----------------------------------------------------------------------------------
	const Waypoint * getCompletedWaypoint() const;
	void setCompletedWaypoint(const Waypoint *pWay);

	const LocomotorSet& getLocomotorSet() const;
	void setPathExtraDistance(Real dist);
	Real getPathExtraDistance() const;

	virtual Bool chooseLocomotorSet(LocomotorSetType wst);

	virtual CommandSourceType getLastCommandSource() const { return m_lastCommandSource; }

	const AttackPriorityInfo * getAttackInfo();
	void setAttackInfo(const AttackPriorityInfo *info);

	void setCurPathfindCell(const ICoord2D &cell);
	void setPathfindGoalCell(const ICoord2D &cell);

	void setPathFromWaypoint(const Waypoint *way, const Coord2D *offset);

	const ICoord2D * getCurPathfindCell() const;
	const ICoord2D * getPathfindGoalCell() const;

	/// Return true if our path has higher priority.
	Bool hasHigherPathPriority(AIUpdateInterface *otherAI) const;
	void setFinalPosition(const Coord3D *pos);

	virtual UpdateSleepTime update();	///< update this object's AI

	/// if we are attacking "fromID", stop that and attack "toID" instead
	void transferAttack(ObjectID fromID, ObjectID toID);

	void setCurrentVictim( const Object *nemesis );			///<  Current victim.
	Object *getCurrentVictim() const;
	virtual void notifyVictimIsDead() { }

	// if we are attacking a position (and NOT an object), return it. otherwise return null.
	const Coord3D *getCurrentVictimPos() const;

	void setLocomotorUpgrade(Bool set);

	// This function is used to notify the unit that it may have a target of opportunity to attack.
	void wakeUpAndAttemptToTarget();

	void resetNextMoodCheckTime();

	//Specifically set a frame to check next mood time -- added for the purpose of ordering a stealth combat unit that can't
	//autoacquire while stealthed, but isn't stealthed and can stealth and is not detected, and the player specifically orders
	//that unit to stop. In this case, instead of the unit autoacquiring another unit, and preventing him from stealthing,
	//we will instead delay the autoacquire until later to give him enough time to stealth properly.
	void setNextMoodCheckTime( UnsignedInt frame );

	///< States should call this with calledByAI set true to prevent them from checking every frame
	///< States that are doing idle checks should call with calledDuringIdle set true so that they check their

	Object *getNextMoodTarget( Bool calledByAI, Bool calledDuringIdle );
	UnsignedInt getNextMoodCheckTime() const;

	// This function will return a combination of MoodMatrixParameter flags.
	UnsignedInt getMoodMatrixValue() const;
	Bool gxMoodScanArgs( Real &range, UnsignedInt &flags );	///< batchScan: the routine (calledByAI) mood scan's arguments, if one is due now
	static void gxMoodScanKernel( UpdateModulePtr *mods, Int begin, Int end );	///< batchScan: collect due scans of one AI kind
	static void gxMoodScanFlush();	///< batchScan: search every unit collected this tick in one call
	UnsignedInt getMoodMatrixActionAdjustment( MoodMatrixAction action ) const;
	void setAttitude( AttitudeType tude );	///< set the behavior modifier for this agent

	// Common AI "status" effects -------------------------------------------------------------------
	Bool hasNationalism() const;
	Bool hasFanaticism() const;
	void evaluateMoraleBonus( Bool inHorde, Bool allowNationalism, HordeActionType type );
	void evaluateNationalismBonusClassic( Bool inHorde, Bool allowNationalism );
	void evaluateNationalismBonus( Bool inHorde, Bool allowNationalism );

#ifdef ALLOW_DEMORALIZE
	// demoralization ... what a nifty word to write.
	Bool isDemoralized() const;
	void setDemoralized( UnsignedInt durationInFrames );
#endif

	Bool canPathThroughUnits() const;
	void setCanPathThroughUnits( Bool canPath );

	// Notify the ai that it has caused a crate to be created (usually by killing something.)
	void notifyCrate(ObjectID id);
	ObjectID getCrateID() const;
	Object* checkForCrateToPickup();

	void setIgnoreCollisionTime(Int frames);

	void setQueueForPathTime(Int frames);

	// For the attack move, that switches from move to attack, and the attack is CMD_FROM_AI,
	// while the move is the original command source.  John A.
	void friend_setLastCommandSource( CommandSourceType source ) {m_lastCommandSource = source;}

	Bool canAutoAcquire() const { return getAIUpdateModuleData()->m_autoAcquireEnemiesWhenIdle; }

  Bool canAutoAcquireWhileStealthed() const ;


protected:

	/*
		AIUpdates run in the initial phase.

		It's actually quite important that AI (the thing that drives Locomotors) and Physics
		run in the same order, relative to each other, for a given object; otherwise,
		interesting oscillations can occur in some situations, with friction being applied
		either before or after the locomotive force, making for huge stuttery messes. (srj)
	*/
	virtual SleepyUpdatePhase getUpdatePhase() const { return PHASE_INITIAL; }

	void setGoalPositionClipped(const Coord3D* in, CommandSourceType cmdSource);

	virtual Bool isAllowedToRespondToAiCommands(const AICommandParms* parms) const;

	// getAttitude is protected because other places should call getMoodMatrixValue to get all the facts they need to consider.
	AttitudeType getAttitude() const;				///< get the current behavior modifier state.

	Bool blockedBy(Object *other); ///< Returns true if we are blocked by "other"
	Bool needToRotate(); ///< Returns true if we are not pointing in the right direction for movement.
	Real calculateMaxBlockedSpeed(Object *other) const;

	virtual UpdateSleepTime doLocomotor();	// virtual so subclasses can override
	void chooseGoodLocomotorFromCurrentSet();

	void setLastCommandSource( CommandSourceType source );

	// subclasses may want to override this, to use a subclass of AIStateMachine.
	virtual AIStateMachine* makeStateMachine();

	virtual Bool getTreatAsAircraftForLocoDistToGoal() const;

	// yes, protected, NOT public... try to avoid exposing the state machine directly, please
	AIStateMachine* getStateMachine();
	const AIStateMachine* getStateMachine() const;

	void wakeUpNow();

public:

	StateID getCurrentStateID() const { return getStateMachine()->getCurrentStateID(); }	///< return the id of the current state of the machine
/// @ todo -- srj sez: JBA NUKE THIS CODE, IT IS EVIL
	void friend_addToWaypointGoalPath( const Coord3D *pathPoint ) { getStateMachine()->addToGoalPath(pathPoint); }

	// this is intended for use ONLY by W3dWaypointBuffer and AIFollowPathState.
	const Coord3D* friend_getGoalPathPosition( Int index ) const { return getStateMachine()->getGoalPathPosition( index ); }

	// this is intended for use ONLY by W3dWaypointBuffer.
	Int friend_getWaypointGoalPathSize() const;

	// this is intended for use ONLY by W3dWaypointBuffer.
	Int friend_getCurrentGoalPathIndex() const;

	// this is intended for use ONLY by AIFollowPathState.
	void friend_setCurrentGoalPathIndex( Int index );
#ifdef DEBUG_LOGGING
	inline const Coord3D *friend_getRequestedDestination() const { return &gxRow().requestedDestination; }
	inline const Coord3D *friend_getRequestedDestination2() const { return &gxRow().requestedDestination2; }
#endif

	Object* getGoalObject() { return getStateMachine()->getGoalObject(); }	///< return the id of the current state of the machine
	const Coord3D* getGoalPosition() const { return getStateMachine()->getGoalPosition(); }	///< return the id of the current state of the machine

	WhichTurretType friend_getTurretSync() const;
	void friend_setTurretSync(WhichTurretType t);

	UnsignedInt getPriorWaypointID();
	UnsignedInt getCurrentWaypointID();

	void clearMoveOutOfWay();

	void setTmpValue(Int val);
	Int getTmpValue();

	Bool getRetryPath();
	static Bool gxAiRule(Int switchBit);	///< web port: the one gate for game-changing AI rules (AIUpdate.cpp)

	void setAllowedToChase( Bool allow );
	Bool isAllowedToChase() const;

	// only for AIStateMachine.
	virtual void friend_notifyStateMachineChanged();

private:
	// this should only be called by load/save, or by chooseLocomotorSet.
	// it does no sanity checking; it just jams it in.
	Bool chooseLocomotorSetExplicit(LocomotorSetType wst);

private:
	GxRow<struct GxAIUpdateInterfaceRow> m_gxAIUpdateInterfaceRow;	///< web port (round 5): this unit's row in TheGxAIUpdateInterfaces
	friend struct GxAIUpdateInterfaceRow;
	inline struct GxAIUpdateInterfaceRow &gxRow() const;

	CommandSourceType		m_lastCommandSource;			/**< Keep track of the source of the last command we got.
																									This is set immediately before the SetState that goes
																									to the state machine, so onEnter in there can know where
																									the command came from.
																								*/


	// Attack Info --------------------------------------------------------------------------------------------

	// "Planning Mode" -----------------------------------------------------------------------------------------
	enum { MAX_WAYPOINTS = 16 };

	// Pathfinding ---------------------------------------------------------------------------------------------
	/**
		If nonzero and >= now, frame when we should queueForPath.

		NEVER EVER EVER set this directly. you must call setQueueForPathTime() in ALL CASES.
		NEVER EVER EVER set this directly. you must call setQueueForPathTime() in ALL CASES.
		NEVER EVER EVER set this directly. you must call setQueueForPathTime() in ALL CASES.
		NEVER EVER EVER set this directly. you must call setQueueForPathTime() in ALL CASES.
	*/

	// Locomotors -------------------------------------------------------------------------------------------------
	enum LocoGoalType	 // Note - written out in save/load xfer, don't change these numbers.  jba.
	{
		NONE = 0,
		POSITION_ON_PATH = 1,
		POSITION_EXPLICIT = 2,
		ANGLE = 3
	};

	// Turrets -------------------------------------------------------------------------------------------------

	// AI -------------------------------------------------------------------------------------------

	// Common AI "status" effects -------------------------------------------------------------------
#ifdef ALLOW_DEMORALIZE
#endif
#ifdef ALLOW_SURRENDER
#endif

	// Note the id of a crate we caused to be created.


	// -------------------------------------------------------------------
	void				gxStartCoarse( const Coord3D *destination );
};

// Web port (round 5, units are pure data): AIUpdateInterface's per-unit state, one row per unit in TheGxAIUpdateInterfaces;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxAIUpdateInterfaceRow
{
	UnsignedInt priorWaypointID;
	UnsignedInt currentWaypointID;
	AIStateMachine* stateMachine;
	UnsignedInt nextEnemyScanTime;
	ObjectID currentVictimID;
	Real desiredSpeed;
	GuardMode guardMode;
	GuardTargetType guardTargetType[2];
	Coord3D locationToGuard;
	ObjectID objectToGuard;
	const PolygonTrigger* areaToGuard;
	const AttackPriorityInfo* attackInfo;
	Coord3D waypointQueue[ AIUpdateInterface::MAX_WAYPOINTS ];
	Int waypointCount;
	Int waypointIndex;
	const Waypoint* completedWaypoint;
	Path* path;
	ObjectID requestedVictimID;
	Coord3D requestedDestination;
	Coord3D requestedDestination2;
	UnsignedInt pathTimestamp;
	ObjectID ignoreObstacleID;
	Real pathExtraDistance;
	ICoord2D pathfindGoalCell;
	ICoord2D pathfindCurCell;
	Int blockedFrames;
	Real curMaxBlockedSpeed;
	Real bumpSpeedLimit;
	UnsignedInt ignoreCollisionsUntil;
	UnsignedInt queueForPathFrame;
	Coord3D finalPosition;
	ObjectID repulsor1;
	ObjectID repulsor2;
	Int nextGoalPathIndex;
	ObjectID moveOutOfWay1;
	ObjectID moveOutOfWay2;
	LocomotorSet locomotorSet;
	Locomotor* curLocomotor;
	LocomotorSetType curLocomotorSet;
	AIUpdateInterface::LocoGoalType locomotorGoalType;
	Coord3D locomotorGoalData;
	TurretAI* turretAI[MAX_TURRETS];
	WhichTurretType turretSyncFlag;
	AttitudeType attitude;
	UnsignedInt nextMoodCheckTime;
	UnsignedInt demoralizedFramesLeft;
	UnsignedInt surrenderedFramesLeft;
	Int surrenderedPlayerIndex;
	ObjectID crateCreated;
	Int tmpInt;
	Bool doFinalPosition;
	Bool waitingForPath;
	Bool gxCoarse = FALSE;
	Coord3D gxCoarseTo;
	std::vector<Coord3D> gxHintPts;
	UnsignedInt gxHintFrame = 0;
	Bool isAttackPath;
	Bool isFinalGoal;
	Bool isApproachPath;
	Bool isSafePath;
	Bool movementComplete;
	Bool isMoving;
	Bool isBlocked;
	Bool isBlockedAndStuck;
	Bool upgradedLocomotors;
	Bool canPathThroughUnits;
	Bool randomlyOffsetMoodCheck;
	Bool isAiDead;
	Bool isRecruitable;
	Bool executingWaypointQueue;
	Bool retryPath;
	Bool allowedToChase;
	Bool isInUpdate;
	Bool gxMoveOn = FALSE;
	Bool fixLocoInPostProcess;
	// web port (dataBehaviors): last chooseBestLocomotorForPosition answer and its key (cell, layer,
	// pathfinder cell-type version); invalid whenever the locomotor set is rebuilt
	Bool gxLocoValid = false;
	UnsignedInt gxLocoVer = 0;
	Int gxLocoLayer = 0, gxLocoX = 0, gxLocoY = 0;
	Locomotor *gxLocoResult = nullptr;
	// web port (batchScan, fifth word bit 1; rides on match rule updatePasses): this tick's batched mood
	// scan answer, taken in the unit's turn only if the scan it is about to make has the same arguments
	UnsignedInt gxScanFrame = 0;	///< batchScan: frame the answer below was searched
	UnsignedInt gxScanQueued = 0;	///< batchScan: frame this unit was collected (arguments below)
	Bool gxScanMiss = FALSE;	///< batchScan: left last tick's answer unused; not collected again until its own scan runs
	Real gxScanRange = 0.0f;
	UnsignedInt gxScanFlags = 0;
	const AttackPriorityInfo *gxScanInfo = nullptr;
	ObjectID gxScanResult = INVALID_ID;
};


inline GxAIUpdateInterfaceRow &AIUpdateInterface::gxRow() const { return const_cast<GxAIUpdateInterfaceRow &>(*m_gxAIUpdateInterfaceRow); }
inline const Coord3D * AIUpdateInterface::getGuardLocation() const
{ return &gxRow().locationToGuard;	}
inline ObjectID AIUpdateInterface::getGuardObject() const
{ return gxRow().objectToGuard; }
inline const PolygonTrigger * AIUpdateInterface::getAreaToGuard() const
{ return gxRow().areaToGuard; }
inline GuardTargetType AIUpdateInterface::getGuardTargetType() const
{ return gxRow().guardTargetType[1]; }
inline void AIUpdateInterface::clearGuardTargetType()
{ gxRow().guardTargetType[1] = gxRow().guardTargetType[0]; gxRow().guardTargetType[0] = GUARDTARGET_NONE; }
inline GuardMode AIUpdateInterface::getGuardMode() const
{ return gxRow().guardMode; }
inline AsciiString AIUpdateInterface::getCurrentStateName() const
{ return gxRow().stateMachine->getCurrentStateName(); }
inline void AIUpdateInterface::setPriorWaypointID( UnsignedInt id )
{ gxRow().priorWaypointID = id; }
inline void AIUpdateInterface::setCurrentWaypointID( UnsignedInt id )
{ gxRow().currentWaypointID = id; }
inline Bool AIUpdateInterface::isAiInDeadState() const
{ return gxRow().isAiDead; }
inline Bool AIUpdateInterface::isRecruitable() const
{return gxRow().isRecruitable;}
inline void AIUpdateInterface::setIsRecruitable(Bool isRecruitable)
{gxRow().isRecruitable = isRecruitable;}
inline Real AIUpdateInterface::getDesiredSpeed() const
{ return gxRow().desiredSpeed; }
inline void AIUpdateInterface::setDesiredSpeed( Real speed )
{ gxRow().desiredSpeed = speed; }
inline const Locomotor * AIUpdateInterface::getCurLocomotor() const
{return gxRow().curLocomotor;}
inline Locomotor * AIUpdateInterface::getCurLocomotor()
{ return gxRow().curLocomotor; }
inline LocomotorSetType AIUpdateInterface::getCurLocomotorSetType() const
{ return gxRow().curLocomotorSet; }
inline Bool AIUpdateInterface::isWaitingForPath() const
{return gxRow().waitingForPath;}
inline Bool AIUpdateInterface::gxWantsClosestSpot() const
{ return gxRow().isApproachPath; }
inline const Coord3D * AIUpdateInterface::gxRequestedDest() const
{ return &gxRow().requestedDestination; }
inline Bool AIUpdateInterface::gxPlainRequest() const
{ return gxRow().waitingForPath && !gxRow().isApproachPath && !gxRow().isAttackPath && !gxRow().isSafePath; }
inline void AIUpdateInterface::gxStartCoarseDeferred()
{ if (gxRow().waitingForPath) gxStartCoarse(&gxRow().requestedDestination); }
inline void AIUpdateInterface::gxSetRouteHint( const std::vector<Coord3D> &pts, UnsignedInt frame )
{ gxRow().gxHintPts = pts; gxRow().gxHintFrame = frame; }
inline Bool AIUpdateInterface::gxOnCoarsePath() const
{ return gxRow().gxCoarse && gxRow().path != nullptr; }
inline Bool AIUpdateInterface::isAttackPath() const
{return gxRow().isAttackPath;}
inline Path* AIUpdateInterface::getPath()
{ return gxRow().path; }
inline const Path* AIUpdateInterface::getPath() const
{ return gxRow().path; }
inline UnsignedInt AIUpdateInterface::getPathAge() const
{ return TheGameLogic->getFrame() - gxRow().pathTimestamp; }
inline Int AIUpdateInterface::getNumFramesBlocked() const
{return gxRow().blockedFrames;}
inline Bool AIUpdateInterface::isBlockedAndStuck() const
{return gxRow().isBlockedAndStuck;}
inline Path* AIUpdateInterface::friend_getPath()
{ return gxRow().path; }
inline const Waypoint * AIUpdateInterface::getCompletedWaypoint() const
{return gxRow().completedWaypoint;}
inline void AIUpdateInterface::setCompletedWaypoint(const Waypoint *pWay)
{gxRow().completedWaypoint = pWay;}
inline const LocomotorSet& AIUpdateInterface::getLocomotorSet() const
{return gxRow().locomotorSet;}
inline void AIUpdateInterface::setPathExtraDistance(Real dist)
{gxRow().pathExtraDistance = dist;}
inline Real AIUpdateInterface::getPathExtraDistance() const
{ return gxRow().pathExtraDistance; }
inline const AttackPriorityInfo * AIUpdateInterface::getAttackInfo()
{return gxRow().attackInfo;}
inline void AIUpdateInterface::setAttackInfo(const AttackPriorityInfo *info)
{gxRow().attackInfo = info;}
inline void AIUpdateInterface::setCurPathfindCell(const ICoord2D &cell)
{gxRow().pathfindCurCell = cell;}
inline void AIUpdateInterface::setPathfindGoalCell(const ICoord2D &cell)
{gxRow().pathfindGoalCell = cell;}
inline const ICoord2D * AIUpdateInterface::getCurPathfindCell() const
{return &gxRow().pathfindCurCell;}
inline const ICoord2D * AIUpdateInterface::getPathfindGoalCell() const
{return &gxRow().pathfindGoalCell;}
inline void AIUpdateInterface::setFinalPosition(const Coord3D *pos)
{ gxRow().finalPosition = *pos; gxRow().doFinalPosition = false;}
inline UnsignedInt AIUpdateInterface::getNextMoodCheckTime() const
{ return gxRow().nextMoodCheckTime; }
#ifdef ALLOW_DEMORALIZE
inline Bool AIUpdateInterface::isDemoralized() const
{ return gxRow().demoralizedFramesLeft > 0; }
#endif
inline Bool AIUpdateInterface::canPathThroughUnits() const
{ return gxRow().canPathThroughUnits; }
inline void AIUpdateInterface::setCanPathThroughUnits( Bool canPath )
{ gxRow().canPathThroughUnits = canPath; if (canPath) gxRow().isBlockedAndStuck=false;}
inline void AIUpdateInterface::notifyCrate(ObjectID id)
{ gxRow().crateCreated = id; }
inline ObjectID AIUpdateInterface::getCrateID() const
{ return gxRow().crateCreated; }
inline void AIUpdateInterface::setIgnoreCollisionTime(Int frames)
{ gxRow().ignoreCollisionsUntil = TheGameLogic->getFrame() + frames; }
inline AIStateMachine* AIUpdateInterface::getStateMachine()
{ return gxRow().stateMachine; }
inline const AIStateMachine* AIUpdateInterface::getStateMachine() const
{ return gxRow().stateMachine; }
inline Int AIUpdateInterface::friend_getCurrentGoalPathIndex() const
{ return gxRow().nextGoalPathIndex; }
inline void AIUpdateInterface::friend_setCurrentGoalPathIndex( Int index )
{ gxRow().nextGoalPathIndex = index; }
inline WhichTurretType AIUpdateInterface::friend_getTurretSync() const
{ return gxRow().turretSyncFlag; }
inline void AIUpdateInterface::friend_setTurretSync(WhichTurretType t)
{ gxRow().turretSyncFlag = t; }
inline UnsignedInt AIUpdateInterface::getPriorWaypointID()
{ return gxRow().priorWaypointID; }
inline UnsignedInt AIUpdateInterface::getCurrentWaypointID()
{ return gxRow().currentWaypointID; }
inline void AIUpdateInterface::clearMoveOutOfWay()
{gxRow().moveOutOfWay1 = INVALID_ID; gxRow().moveOutOfWay2 = INVALID_ID;}
inline void AIUpdateInterface::setTmpValue(Int val)
{gxRow().tmpInt = val;}
inline Int AIUpdateInterface::getTmpValue()
{return gxRow().tmpInt;}
inline Bool AIUpdateInterface::getRetryPath()
{return gxRow().retryPath;}
inline void AIUpdateInterface::setAllowedToChase( Bool allow )
{ gxRow().allowedToChase = allow; }
inline Bool AIUpdateInterface::isAllowedToChase() const
{ return gxRow().allowedToChase; }


//------------------------------------------------------------------------------------------------------------
// Inlines
//
