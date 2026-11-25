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

// FILE: BridgeScaffoldBehavior.h /////////////////////////////////////////////////////////////////
// Author: Colin Day, September 2002
// Desc:   Bridge scaffold
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/UpdateModule.h"

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
enum ScaffoldTargetMotion CPP_11(: Int)
{
	STM_STILL,
	STM_RISE,
	STM_BUILD_ACROSS,
	STM_TEAR_DOWN_ACROSS,
	STM_SINK,
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class BridgeScaffoldBehaviorInterface
{

public:

	virtual void setPositions( const Coord3D *createPos,
														 const Coord3D *riseToPos,
														 const Coord3D *buildPos ) = 0;
	virtual void setMotion( ScaffoldTargetMotion targetMotion ) = 0;
	virtual ScaffoldTargetMotion getCurrentMotion() = 0;
	virtual void reverseMotion() = 0;
	virtual void setLateralSpeed( Real lateralSpeed ) = 0;
	virtual void setVerticalSpeed( Real verticalSpeed ) = 0;

};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include "GameLogic/GxComponents.h"
struct GxBridgeScaffoldBehaviorRow;
class BridgeScaffoldBehavior : public UpdateModule,
															 public BridgeScaffoldBehaviorInterface
{

	MAKE_STANDARD_MODULE_MACRO( BridgeScaffoldBehavior );
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( BridgeScaffoldBehavior, "BridgeScaffoldBehavior" )

public:

	BridgeScaffoldBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	// behavior module methods
	virtual BridgeScaffoldBehaviorInterface* getBridgeScaffoldBehaviorInterface() { return this; }

	// update methods
	virtual UpdateSleepTime update();

	// bridge scaffold interface methods
	virtual void setPositions( const Coord3D *createPos,
														 const Coord3D *riseToPos,
														 const Coord3D *buildPos );
	virtual void setMotion( ScaffoldTargetMotion targetMotion );
	virtual ScaffoldTargetMotion getCurrentMotion();
	virtual void reverseMotion();
	virtual void setLateralSpeed( Real lateralSpeed );
	virtual void setVerticalSpeed( Real verticalSpeed );

	// public interface acquisition
	static BridgeScaffoldBehaviorInterface *getBridgeScaffoldBehaviorInterfaceFromObject( Object *obj );

protected:

	void doVerticalMotion();				///< do rise/sink vertical motion
	void doLateralmotion();					///< do lateral motion

	struct GxBridgeScaffoldBehaviorRow *m_gxBridgeScaffoldBehaviorRow;	///< web port (round 5): this unit's row in TheGxBridgeScaffoldBehaviors
	friend struct GxBridgeScaffoldBehaviorRow;
	inline struct GxBridgeScaffoldBehaviorRow &gxRow() const;

};

// Web port (round 5, units are pure data): BridgeScaffoldBehavior's per-unit state, one row per unit in TheGxBridgeScaffoldBehaviors;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxBridgeScaffoldBehaviorRow
{
	ScaffoldTargetMotion targetMotion;
	Coord3D createPos;
	Coord3D riseToPos;
	Coord3D buildPos;
	Real lateralSpeed;
	Real verticalSpeed;
	Coord3D targetPos;
};
extern GxComponentTable<GxBridgeScaffoldBehaviorRow> TheGxBridgeScaffoldBehaviors;


inline GxBridgeScaffoldBehaviorRow &BridgeScaffoldBehavior::gxRow() const { return TheGxBridgeScaffoldBehaviors.at(m_gxBridgeScaffoldBehaviorRow); }
inline ScaffoldTargetMotion BridgeScaffoldBehavior::getCurrentMotion() { return gxRow().targetMotion; }
inline void BridgeScaffoldBehavior::setLateralSpeed( Real lateralSpeed ) { gxRow().lateralSpeed = lateralSpeed; }
inline void BridgeScaffoldBehavior::setVerticalSpeed( Real verticalSpeed ) { gxRow().verticalSpeed = verticalSpeed; }

