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

// FILE: CreateModule.h /////////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, September 2001
// Desc:
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/BehaviorModule.h"

//-------------------------------------------------------------------------------------------------
/** OBJECT CREATE MODULE base class */
//-------------------------------------------------------------------------------------------------
class CreateModuleInterface
{
public:
	virtual void onCreate() = 0;				///< This is called when you become a code Object
	virtual void onBuildComplete() = 0;	///< This is called when you are a finished game object
	virtual Bool shouldDoOnBuildComplete() const = 0;

};

//-------------------------------------------------------------------------------------------------
class CreateModuleData : public BehaviorModuleData
{
public:

	static void buildFieldParse(MultiIniFieldParse& p)
	{
		BehaviorModuleData::buildFieldParse(p);
	}
};

//-------------------------------------------------------------------------------------------------
#include "Common/GxComponentPool.h"
struct GxCreateModuleRow;
class CreateModule : public BehaviorModule, public CreateModuleInterface
{

	MEMORY_POOL_GLUE_ABC( CreateModule )
	MAKE_STANDARD_MODULE_MACRO_ABC( CreateModule )
	//MAKE_STANDARD_MODULE_DATA_MACRO_ABC(CreateModule, CreateModuleData)

public:

	CreateModule( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype defined by MemoryPoolObject

	static Int getInterfaceMask() { return MODULEINTERFACE_CREATE; }

	// BehaviorModule
	virtual CreateModuleInterface* getCreate() { return this; }

	virtual void onCreate() = 0;				///< This is called when you become a code Object
	virtual void onBuildComplete(); ///< This is called when you are a finished game object
	virtual Bool shouldDoOnBuildComplete() const;

private:

	GxRow<struct GxCreateModuleRow> m_gxCreateModuleRow;	///< web port (round 5): this unit's row in TheGxCreateModules
	friend struct GxCreateModuleRow;
	inline struct GxCreateModuleRow &gxRow() const;

};

// Web port (round 5, units are pure data): CreateModule's per-unit state, one row per unit in TheGxCreateModules;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxCreateModuleRow
{
	Bool needToRunOnBuildComplete;
};


inline GxCreateModuleRow &CreateModule::gxRow() const { return const_cast<GxCreateModuleRow &>(*m_gxCreateModuleRow); }
inline void CreateModule::onBuildComplete() { gxRow().needToRunOnBuildComplete = FALSE; }
inline Bool CreateModule::shouldDoOnBuildComplete() const { return gxRow().needToRunOnBuildComplete; }

