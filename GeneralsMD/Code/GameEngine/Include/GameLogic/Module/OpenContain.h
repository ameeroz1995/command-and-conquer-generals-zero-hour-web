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

// FILE: OpenContain.h ////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, November 2001
// Desc:   The OpenContainer ContainModule allows objects to be contained inside of other
//				 objects.  There is a set of functionality that will be common to
//				 all container modules that provides the actual containment
//				 implementations, those implementations are found here
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/DamageModule.h"
#include "Common/AudioEventRTS.h"
#include "Common/KindOf.h"
#include "Common/GameMemory.h"
#include "Common/ModelState.h"

// ------------------------------------------------------------------------------------------------
enum { CONTAIN_MAX_UNKNOWN = -1 };  // means we don't care, infinite, unassigned, whatever

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class OpenContainModuleData : public UpdateModuleData
{

public:
	DieMuxData m_dieMuxData;
	Int m_containMax;								///< how many things we can have inside (-1 = "I don't care")
	AudioEventRTS m_enterSound;			///< sound to play on entering
	AudioEventRTS m_exitSound;			///< sound to play on exiting
	Bool m_passengersAllowedToFire;	///< Can the passengers shoot out of us?
	Bool m_passengersInTurret;			///< The Firepoint bones are in our turret, not our chassis
	Int m_numberOfExitPaths;				///< Will alternate through ExitStart/End paths as we exit people.
	Real m_damagePercentageToUnits;
	Bool m_isBurnedDeathToUnits;		///< Turn off the hardcoded burn death when killing guys in transport
	UnsignedInt m_doorOpenTime;
	KindOfMaskType m_allowInsideKindOf;			///< objects must have at least one of these kind of bits set to be contained by us
	KindOfMaskType m_forbidInsideKindOf;		///< objects must have NONE of these kind of bits set to be contained by us
	Bool m_weaponBonusPassedToPassengers;		///< Do our passengers get to use our weapon bonuses?
 	Bool m_allowAlliesInside;				///< allow allies inside us
 	Bool m_allowEnemiesInside;			///< allow enemies inside us
 	Bool m_allowNeutralInside;			///< allow neutral inside us

	OpenContainModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);
};

//-------------------------------------------------------------------------------------------------
/** An open container can actually contain other objects */
//-------------------------------------------------------------------------------------------------
#include "Common/GxComponentPool.h"
struct GxOpenContainRow;
class OpenContain : public UpdateModule,
										public ContainModuleInterface,
										public CollideModuleInterface,
										public DieModuleInterface,
										public DamageModuleInterface,
										public ExitInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( OpenContain, "OpenContain" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( OpenContain, OpenContainModuleData )

public:

	OpenContain( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual ContainModuleInterface* getContain() { return this; }
	virtual CollideModuleInterface* getCollide() { return this; }
	virtual DieModuleInterface* getDie() { return this; }
	virtual DamageModuleInterface* getDamage() { return this; }
	static Int getInterfaceMask() { return UpdateModule::getInterfaceMask() | (MODULEINTERFACE_CONTAIN) | (MODULEINTERFACE_COLLIDE) | (MODULEINTERFACE_DIE) | (MODULEINTERFACE_DAMAGE); }

	virtual void onDie( const DamageInfo *damageInfo );  ///< the die callback
	virtual void onDelete();	///< Last possible moment cleanup
	virtual void onCapture( Player *oldOwner, Player *newOwner ){}

	// CollideModuleInterface
	virtual void onCollide( Object *other, const Coord3D *loc, const Coord3D *normal );
	virtual Bool wouldLikeToCollideWith(const Object* other) const { return false; }
	virtual Bool isCarBombCrateCollide() const { return false; }
	virtual Bool isHijackedVehicleCrateCollide() const { return false; }
	virtual Bool isRailroad() const { return false;}
	virtual Bool isSalvageCrateCollide() const { return false; }
	virtual Bool isSabotageBuildingCrateCollide() const { return FALSE; }

	// UpdateModule
	virtual UpdateSleepTime update();				///< called once per frame

	// ContainModuleInterface
	virtual OpenContain *asOpenContain() { return this; }  ///< treat as open container

	// DamageModuleInterface
	virtual void onDamage( DamageInfo *damageInfo ){};	///< damage callback
	virtual void onHealing( DamageInfo *damageInfo ){};	///< healing callback
	virtual void onBodyDamageStateChange( const DamageInfo* damageInfo,
																				BodyDamageType oldState,
																				BodyDamageType newState){};  ///< state change callback


	// our object changed position... react as appropriate.
	virtual void containReactToTransformChange();

	virtual Bool calcBestGarrisonPosition( Coord3D *sourcePos, const Coord3D *targetPos ) { return FALSE; }
	virtual Bool attemptBestFirePointPosition( Object *source, Weapon *weapon, Object *victim ) { return FALSE; }
	virtual Bool attemptBestFirePointPosition( Object *source, Weapon *weapon, const Coord3D *targetPos ) { return FALSE; }

	///< if my object gets selected, then my visible passengers should, too
	///< this gets called from
	virtual void clientVisibleContainedFlashAsSelected() {};

	virtual const Player* getApparentControllingPlayer(const Player* observingPlayer) const { return nullptr; }
	virtual void recalcApparentControllingPlayer() { }

	virtual void onContaining( Object *obj, Bool wasSelected );		///< object now contains 'obj'
	virtual void onRemoving( Object *obj );			///< object no longer contains 'obj'
	virtual void onSelling();///< Container is being sold.  Open responds by kicking people out

	virtual void orderAllPassengersToExit( CommandSourceType commandSource, Bool instantly ); ///< All of the smarts of exiting are in the passenger's AIExit. removeAllFrommContain is a last ditch system call, this is the game Evacuate
	virtual void orderAllPassengersToIdle( CommandSourceType commandSource ); ///< Just like it sounds
	virtual void orderAllPassengersToHackInternet( CommandSourceType ); ///< Just like it sounds
	virtual void markAllPassengersDetected();										///< Cool game stuff got added to the system calls since this layer didn't exist, so this regains that functionality

	// default OpenContain has unlimited capacity...!
	virtual Bool isValidContainerFor(const Object* obj, Bool checkCapacity) const;
	virtual void addToContain( Object *obj );				///< add 'obj' to contain list
	virtual void addToContainList( Object *obj );		///< The part of AddToContain that inheritors can override (Can't do whole thing because of all the private stuff involved)
	virtual void removeFromContain( Object *obj, Bool exposeStealthUnits = FALSE );	///< remove 'obj' from contain list
	virtual void removeAllContained( Bool exposeStealthUnits = FALSE );				///< remove all objects on contain list
	virtual void killAllContained();				///< kill all objects on contain list
  virtual void harmAndForceExitAllContained( DamageInfo *info ); // apply canned damage against those contains
	virtual Bool isEnclosingContainerFor( const Object *obj ) const;	///< Does this type of Contain Visibly enclose its contents?
	virtual Bool isPassengerAllowedToFire( ObjectID id = INVALID_ID ) const;	///< Hey, can I shoot out of this container?

  virtual void setPassengerAllowedToFire( Bool permission = TRUE ); ///< Hey, can I shoot out of this container?

  virtual void setOverrideDestination( const Coord3D * ){} ///< Instead of falling peacefully towards a clear spot, I will now aim here
	virtual Bool isDisplayedOnControlBar() const {return FALSE;}///< Does this container display its contents on the ControlBar?
	virtual Int getExtraSlotsInUse() { return 0; }
	virtual Bool isKickOutOnCapture(){ return TRUE; }///< By default, yes, all contain modules kick passengers out on capture

	// contain list access
	virtual void iterateContained( ContainIterateFunc func, void *userData, Bool reverse );
	virtual UnsignedInt getContainCount() const;
	virtual const ContainedItemsList* getContainedItemsList() const;
	virtual const Object *friend_getRider() const{return nullptr;} ///< Damn.  The draw order dependency bug for riders means that our draw module needs to cheat to get around it.
	virtual Real getContainedItemsMass() const;
	virtual UnsignedInt getStealthUnitsContained() const;
	virtual UnsignedInt getHeroUnitsContained() const;

	virtual PlayerMaskType getPlayerWhoEntered() const;

	virtual Int getContainMax() const;

	// ExitInterface
	virtual Bool isExitBusy() const {return FALSE;}	///< Contain style exiters are getting the ability to space out exits, so ask this before reserveDoor as a kind of no-commitment check.
	virtual ExitDoorType reserveDoorForExit( const ThingTemplate* objType, Object *specificObject ) { return DOOR_1; }
	virtual void exitObjectViaDoor( Object *newObj, ExitDoorType exitDoor );
	virtual void exitObjectInAHurry( Object *newObj );


	virtual void unreserveDoorForExit( ExitDoorType exitDoor ) { /*nothing*/ }
	virtual void exitObjectByBudding( Object *newObj, Object *budHost ) { return; };

	virtual void setRallyPoint( const Coord3D *pos );				///< define a "rally point" for units to move towards
	virtual const Coord3D *getRallyPoint() const;			///< define a "rally point" for units to move towards
	virtual Bool getExitPosition(Coord3D& exitPosition ) const { return FALSE; };					///< access to the "Door" position of the production object
	virtual Bool getNaturalRallyPoint( Coord3D& rallyPoint, Bool offset = TRUE ) const;			///< get the natural "rally point" for units to move towards

	virtual ExitInterface* getContainExitInterface() { return this; }

	virtual Bool isGarrisonable() const { return false; }		///< can this unit be Garrisoned? (ick)
	virtual Bool isBustable() const { return false; }		///< can this container get busted by a bunkerbuster
	virtual Bool isHealContain() const { return false; } ///< true when container only contains units while healing (not a transport!)
	virtual Bool isTunnelContain() const { return FALSE; }
	virtual Bool isRiderChangeContain() const { return FALSE; }
	virtual Bool isSpecialZeroSlotContainer() const { return false; }
	virtual Bool isImmuneToClearBuildingAttacks() const { return true; }
  virtual Bool isSpecialOverlordStyleContainer() const { return false; }
  virtual Bool isAnyRiderAttacking() const;

	/**
		this is used for containers that must do something to allow people to enter or exit...
		eg, land (for Chinook), open door (whatever)... it's called with wants=WANTS_TO_ENTER
		when something is in the enter state, and wants=ENTS_NOTHING when the unit has
		either entered, or given up...
	*/
	virtual void onObjectWantsToEnterOrExit(Object* obj, ObjectEnterExitType wants);

	// returns true iff there are objects currently waiting to enter.
	virtual Bool hasObjectsWantingToEnterOrExit() const;

	virtual void processDamageToContained(Real percentDamage); ///< Do our % damage to units now.

	virtual Bool isWeaponBonusPassedToPassengers() const;
	virtual WeaponBonusConditionFlags getWeaponBonusPassedToPassengers() const;

	virtual void enableLoadSounds( Bool enable );

  Real getDamagePercentageToUnits();
  virtual Object* getClosestRider ( const Coord3D *pos );

  virtual void setEvacDisposition( EvacDisposition disp ) {};
protected:

	virtual void monitorConditionChanges();				///< check to see if we need to update our occupant positions from a model change or anything else
	virtual void putObjAtNextFirePoint( Object *obj );	///< place object at position of the next fire point to use
	virtual void redeployOccupants();							///< redeploy any objects at firepoints due to a model condition change

	const ContainedItemsList& getContainList() const;

	void scatterToNearbyPosition(Object* obj);
	void removeFromContainViaIterator( ContainedItemsList::iterator it, Bool exposeStealthUnits = FALSE );  ///< remove item from contain list
	void removeFromPassengerViaIterator( ContainedItemsList::iterator it );///< remove item from passenger list

	virtual void doLoadSound();
	virtual void doUnloadSound();
	virtual void positionContainedObjectsRelativeToContainer(){}

	virtual void addOrRemoveObjFromWorld(Object* obj, Bool add);

	// exists primarily for TransportContain to override
	virtual void killRidersWhoAreNotFreeToExit() { }

	void pruneDeadWanters();

	// round 5 addendum (drifting, rides on match rule updatePasses, switch containKernels): the kind
	// kernel steps every container's own row (entered mask, door countdown) ahead of the turns;
	// update() does the rest in the unit's own turn
	UnsignedInt gxStep();
	void gxApplyStep( UnsignedInt due );
	static void gxKernel( UpdateModulePtr *mods, Int begin, Int end );

	GxRow<struct GxOpenContainRow> m_gxOpenContainRow;	///< web port (round 5): this unit's row in TheGxOpenContains
	friend struct GxOpenContainRow;
	inline struct GxOpenContainRow &gxRow() const;
private:

	typedef std::map< ObjectID, ObjectEnterExitType, std::less<ObjectID> > ObjectEnterExitMap;



/// @todo srj -- move this to a lazily-allocated subobject
	enum { MAX_FIRE_POINTS = 32 };

};

// Web port (round 5, units are pure data): OpenContain's per-unit state, one row per unit in TheGxOpenContains;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxOpenContainRow
{
	ContainedItemsList containList;
	UnsignedInt containListSize;
	OpenContain::ObjectEnterExitMap objectEnterExitInfo;
	UnsignedInt stealthUnitsContained;
	UnsignedInt heroUnitsContained;
	Int whichExitPath;
	UnsignedInt doorCloseCountdown;
	std::list<ObjectID> xferContainIDList;
	PlayerMaskType playerEnteredMask;
	UnsignedInt lastUnloadSoundFrame;
	UnsignedInt lastLoadSoundFrame;
	ModelConditionFlags conditionState;
	Matrix3D firePoints[ OpenContain::MAX_FIRE_POINTS ];
	Int firePointStart;
	Int firePointNext;
	Int firePointSize;
	Bool noFirePointsInArt;
	Coord3D rallyPoint;
	Bool rallyPointExists;
	Bool loadSoundsEnabled;
	Bool passengerAllowedToFire;
	UnsignedInt gxKernelFrame = 0;	///< frame the container kernel last stepped this row
	UnsignedInt gxDue = 0;	///< its result (1: the door closes this frame)
};


inline GxOpenContainRow &OpenContain::gxRow() const { return const_cast<GxOpenContainRow &>(*m_gxOpenContainRow); }
inline void OpenContain::setPassengerAllowedToFire( Bool permission) { gxRow().passengerAllowedToFire = permission; }
inline UnsignedInt OpenContain::getContainCount() const { return gxRow().containListSize; }
inline const ContainedItemsList* OpenContain::getContainedItemsList() const { return &gxRow().containList; }
inline UnsignedInt OpenContain::getStealthUnitsContained() const { return gxRow().stealthUnitsContained; }
inline UnsignedInt OpenContain::getHeroUnitsContained() const { return gxRow().heroUnitsContained; }
inline PlayerMaskType OpenContain::getPlayerWhoEntered() const { return gxRow().playerEnteredMask; }
inline void OpenContain::enableLoadSounds( Bool enable ) { gxRow().loadSoundsEnabled = enable; }
inline const ContainedItemsList& OpenContain::getContainList() const { return gxRow().containList; }

