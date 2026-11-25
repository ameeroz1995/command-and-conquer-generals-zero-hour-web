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

// FILE: RebuildHoleBehavior.h ////////////////////////////////////////////////////////////////////
// Author: Colin Day, June 2002
// Desc:   GLA Hole behavior that reconstructs a building after death
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/UpdateModule.h"

//-------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class RebuildHoleBehaviorModuleData : public UpdateModuleData
{

public:

	RebuildHoleBehaviorModuleData();

	static void buildFieldParse( MultiIniFieldParse &p );

	Real m_workerRespawnDelay;							///< delay in frames from death of object till respawn of worker
	Real m_holeHealthRegenPercentPerSecond; ///< the hole recovers this % of the max hit points per second
	AsciiString m_workerTemplateName;				///< name of worker object

private:

};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class RebuildHoleBehaviorInterface
{

public:

	virtual void startRebuildProcess( const ThingTemplate *rebuild, ObjectID spawnerID ) = 0;
	virtual ObjectID getSpawnerID() = 0;
	virtual ObjectID getReconstructedBuildingID() = 0;
	virtual const ThingTemplate* getRebuildTemplate() const = 0;

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#include "Common/GxComponentPool.h"
struct GxRebuildHoleBehaviorRow;
class RebuildHoleBehavior : public UpdateModule,
														public DieModuleInterface,
														public RebuildHoleBehaviorInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( RebuildHoleBehavior, "RebuildHoleBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( RebuildHoleBehavior, RebuildHoleBehaviorModuleData )

public:

	RebuildHoleBehavior( Thing *thing, const ModuleData *moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual RebuildHoleBehaviorInterface* getRebuildHoleBehaviorInterface() { return this; }

	static Int getInterfaceMask() { return UpdateModule::getInterfaceMask() | (MODULEINTERFACE_DIE); }

	// BehaviorModule
	virtual DieModuleInterface* getDie() { return this; }

	// UpdateModuleInterface
	virtual UpdateSleepTime update();

	// DieModuleInterface
	virtual void onDie( const DamageInfo *damageInfo );

	// RebuildHole specific methods
	virtual void startRebuildProcess( const ThingTemplate *rebuild, ObjectID spawnerID );
	virtual ObjectID getSpawnerID();
	virtual ObjectID getReconstructedBuildingID();
	virtual const ThingTemplate* getRebuildTemplate() const;
	void transferBombs( Object *reconstruction );

	// interface acquisition
	static RebuildHoleBehaviorInterface* getRebuildHoleBehaviorInterfaceFromObject( Object *obj );

protected:

	void newWorkerRespawnProcess( Object *existingWorker );		///< start the worker respawn process (again if existingWorker is non nullptr)

	GxRow<struct GxRebuildHoleBehaviorRow> m_gxRebuildHoleBehaviorRow;	///< web port (round 5): this unit's row in TheGxRebuildHoleBehaviors
	friend struct GxRebuildHoleBehaviorRow;
	inline struct GxRebuildHoleBehaviorRow &gxRow() const;

};

// Web port (round 5, units are pure data): RebuildHoleBehavior's per-unit state, one row per unit in TheGxRebuildHoleBehaviors;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxRebuildHoleBehaviorRow
{
	ObjectID workerID;
	ObjectID reconstructingID;
	ObjectID spawnerObjectID;
	UnsignedInt workerWaitCounter;
	const ThingTemplate* workerTemplate;
	const ThingTemplate* rebuildTemplate;
};


inline GxRebuildHoleBehaviorRow &RebuildHoleBehavior::gxRow() const { return const_cast<GxRebuildHoleBehaviorRow &>(*m_gxRebuildHoleBehaviorRow); }
inline ObjectID RebuildHoleBehavior::getSpawnerID() { return gxRow().spawnerObjectID; }
inline ObjectID RebuildHoleBehavior::getReconstructedBuildingID() { return gxRow().reconstructingID; }
inline const ThingTemplate* RebuildHoleBehavior::getRebuildTemplate() const { return gxRow().rebuildTemplate; }

