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

// FILE:		DumbProjectileBehavior.h
// Author:	Steven Johnson, July 2002
// Desc:

#pragma once

#include "Common/GameType.h"
#include "Common/GlobalData.h"
#include "Common/STLTypedefs.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/WeaponBonusConditionFlags.h"
#include "Common/INI.h"
#include "WWMath/matrix3d.h"

class ParticleSystem;
class FXList;


//-------------------------------------------------------------------------------------------------
class DumbProjectileBehaviorModuleData : public UpdateModuleData
{
public:
	/**
		These four data define a Bezier curve.  The first and last control points are the firer and victim.
	*/
	Real m_firstHeight;					///< The first airborne control point will be this high above the highest intervening terrain
	Real m_secondHeight;				///< And the second, this.
	Real m_firstPercentIndent;	///< The first point will be this percent along the target line
	Real m_secondPercentIndent;	///< And the second, this.
	UnsignedInt m_maxLifespan;
	Bool m_tumbleRandomly;
	Bool m_orientToFlightPath;
	Bool m_detonateCallsKill;
	Int							m_garrisonHitKillCount;
	KindOfMaskType	m_garrisonHitKillKindof;			///< the kind(s) of units that can be collided with
	KindOfMaskType	m_garrisonHitKillKindofNot;		///< the kind(s) of units that CANNOT be collided with
	const FXList*		m_garrisonHitKillFX;
	Real m_flightPathAdjustDistPerFrame;


	DumbProjectileBehaviorModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

};

//-------------------------------------------------------------------------------------------------
#include "GameLogic/GxComponents.h"
struct GxDumbProjectileBehaviorRow;
class DumbProjectileBehavior : public UpdateModule, public ProjectileUpdateInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( DumbProjectileBehavior, "DumbProjectileBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( DumbProjectileBehavior, DumbProjectileBehaviorModuleData );

public:

	DumbProjectileBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor provided by memory pool object

	// UpdateModuleInterface
	virtual UpdateSleepTime update();
	virtual ProjectileUpdateInterface* getProjectileUpdateInterface() { return this; }

	// ProjectileUpdateInterface
	virtual void projectileLaunchAtObjectOrPosition(const Object *victim, const Coord3D* victimPos, const Object *launcher, WeaponSlotType wslot, Int specificBarrelToUse, const WeaponTemplate* detWeap, const ParticleSystemTemplate* exhaustSysOverride);
	virtual void projectileFireAtObjectOrPosition( const Object *victim, const Coord3D *victimPos, const WeaponTemplate *detWeap, const ParticleSystemTemplate* exhaustSysOverride );
	virtual Bool projectileHandleCollision( Object *other );
	virtual Bool projectileIsArmed() const { return true; }
	virtual ObjectID projectileGetLauncherID() const;
	virtual void setFramesTillCountermeasureDiversionOccurs( UnsignedInt frames ) {}
	virtual void projectileNowJammed() {}

protected:

	void positionForLaunch(const Object *launcher, WeaponSlotType wslot, Int specificBarrelToUse);
	void detonate();

private:

	struct GxDumbProjectileBehaviorRow *m_gxDumbProjectileBehaviorRow;	///< web port (round 5): this unit's row in TheGxDumbProjectileBehaviors
	friend struct GxDumbProjectileBehaviorRow;
	inline struct GxDumbProjectileBehaviorRow &gxRow() const;


	Bool calcFlightPath(Bool recalcNumSegments);
#if defined(RTS_DEBUG)
	void displayFlightPath();	///< Uses little debug icons in worldspace to show the path chosen when it is decided upon
#endif

};

// Web port (round 5, units are pure data): DumbProjectileBehavior's per-unit state, one row per unit in TheGxDumbProjectileBehaviors;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxDumbProjectileBehaviorRow
{
	ObjectID launcherID;
	ObjectID victimID;
	const WeaponTemplate* detonationWeaponTmpl;
	UnsignedInt lifespanFrame;
	VecCoord3D flightPath;
	Coord3D flightPathStart;
	Coord3D flightPathEnd;
	Real flightPathSpeed;
	Int flightPathSegments;
	Int currentFlightPathStep;
	WeaponBonusConditionFlags extraBonusFlags;
	Bool hasDetonated;
};
extern GxComponentTable<GxDumbProjectileBehaviorRow> TheGxDumbProjectileBehaviors;


inline GxDumbProjectileBehaviorRow &DumbProjectileBehavior::gxRow() const { return TheGxDumbProjectileBehaviors.at(m_gxDumbProjectileBehaviorRow); }
inline ObjectID DumbProjectileBehavior::projectileGetLauncherID() const { return gxRow().launcherID; }

