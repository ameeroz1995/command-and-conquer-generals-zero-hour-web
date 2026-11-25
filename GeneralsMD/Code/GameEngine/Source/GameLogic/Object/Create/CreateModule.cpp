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

// FILE: CreateModule.cpp /////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2002
// Desc:   Create module base class
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"
#include "Common/Xfer.h"
#include "GameLogic/Module/CreateModule.h"

// Web port (round 5, units are pure data): CreateModule's per-unit state is a row of TheGxCreateModules;
// GXROW_CREATEMODULE is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
GxComponentTable<GxCreateModuleRow> TheGxCreateModules;
#define GXROW_CREATEMODULE (TheGxCreateModules.at(m_gxCreateModuleRow))


// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CreateModule::CreateModule( Thing *thing, const ModuleData* moduleData ) : BehaviorModule( thing, moduleData )
{
	m_gxCreateModuleRow = TheGxCreateModules.alloc( getObject() );	// web port (round 5): this unit's row (zeroed, like the module was)
	GXROW_CREATEMODULE.needToRunOnBuildComplete = TRUE;


}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
CreateModule::~CreateModule()
{

	TheGxCreateModules.release( m_gxCreateModuleRow );	// web port (round 5)
}

//-------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void CreateModule::crc( Xfer *xfer )
{

	// extend base class
	BehaviorModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void CreateModule::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	BehaviorModule::xfer( xfer );

	// need to run on build complete
	xfer->xferBool( &GXROW_CREATEMODULE.needToRunOnBuildComplete );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void CreateModule::loadPostProcess()
{

	// extend base class
	BehaviorModule::loadPostProcess();

}


