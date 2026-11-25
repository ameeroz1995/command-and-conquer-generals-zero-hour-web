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

// FILE: ParachuteContain.h ////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, March 2002
// Desc:   Contain module for transport units.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/OpenContain.h"

//-------------------------------------------------------------------------------------------------
class ParachuteContainModuleData : public OpenContainModuleData
{
public:
	Real m_pitchRateMax;
	Real m_rollRateMax;
	Real m_lowAltitudeDamping;
	Real m_paraOpenDist;		///< deploy the parachute when we have traveled this far
	Real m_freeFallDamagePercent;
	Real m_killWhenLandingInWaterSlop;
	AudioEventRTS m_parachuteOpenSound;

	ParachuteContainModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);
};

//-------------------------------------------------------------------------------------------------
#include "GameLogic/GxComponents.h"
struct GxParachuteContainRow;
class ParachuteContain : public OpenContain
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( ParachuteContain, "ParachuteContain" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( ParachuteContain, ParachuteContainModuleData )

public:

	ParachuteContain( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void onDrawableBoundToObject();

	virtual Bool isValidContainerFor( const Object* obj, bool checkCapacity) const;
	virtual Bool isEnclosingContainerFor( const Object *obj ) const { return FALSE; }	///< Does this type of Contain Visibly enclose its contents?
	virtual Bool isSpecialZeroSlotContainer() const { return true; }

	virtual void onContaining( Object *obj, Bool wasSelected );		///< object now contains 'obj'
	virtual void onRemoving( Object *obj );			///< object no longer contains 'obj'

	virtual UpdateSleepTime update();							///< called once per frame

	virtual void containReactToTransformChange();

	virtual void onCollide( Object *other, const Coord3D *loc, const Coord3D *normal );
	virtual void onDie( const DamageInfo * damageInfo );

	virtual void setOverrideDestination( const Coord3D *dest ); ///< Instead of falling peacefully towards a clear spot, I will now aim here

protected:

	virtual Bool isFullyEnclosingContainer() const { return false; }
	virtual void positionContainedObjectsRelativeToContainer();

private:

	void positionRider(Object* obj);
	void updateBonePositions();
	void updateOffsetsFromBones();
	void calcSwayMtx(const Coord3D* offset, Matrix3D* mtx);
	void setSwayMatrices();

	struct GxParachuteContainRow *m_gxParachuteContainRow;	///< web port (round 5): this unit's row in TheGxParachuteContains
	friend struct GxParachuteContainRow;
	inline struct GxParachuteContainRow &gxRow() const;
};

// Web port (round 5, units are pure data): ParachuteContain's per-unit state, one row per unit in TheGxParachuteContains;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxParachuteContainRow
{
	Real pitch;
	Real roll;
	Real pitchRate;
	Real rollRate;
	Real startZ;
	Bool isLandingOverrideSet;
	Coord3D landingOverride;
	Coord3D riderAttachBone;
	Coord3D riderSwayBone;
	Coord3D paraAttachBone;
	Coord3D paraSwayBone;
	Coord3D riderAttachOffset;
	Coord3D riderSwayOffset;
	Coord3D paraAttachOffset;
	Coord3D paraSwayOffset;
	Bool needToUpdateRiderBones;
	Bool needToUpdateParaBones;
	Bool opened;
};
extern GxComponentTable<GxParachuteContainRow> TheGxParachuteContains;


inline GxParachuteContainRow &ParachuteContain::gxRow() const { return TheGxParachuteContains.at(m_gxParachuteContainRow); }

