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

// FILE: RailedTransportAIUpdate.h ////////////////////////////////////////////////////////////////
// Author: Colin Day, August 2002
// Desc: Railed transport AI
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/GxComponentPool.h"
#include "GameLogic/Module/AIUpdate.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Waypoint;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class RailedTransportAIUpdateModuleData : public AIUpdateModuleData
{

public:

	RailedTransportAIUpdateModuleData();

	static void buildFieldParse( MultiIniFieldParse &p );

	AsciiString m_pathPrefixName;		///< prefix to use for waypont start and end points we'll look for

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class RailedTransportAIUpdate : public AIUpdateInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( RailedTransportAIUpdate, "RailedTransportAIUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( RailedTransportAIUpdate, RailedTransportAIUpdateModuleData )

public:

	RailedTransportAIUpdate( Thing *thing, const ModuleData *moduleData );
	// virtual destructor prototype provided by memory pool declaration

	// AIUpdate interface methods
	virtual void aiDoCommand( const AICommandParms *parms );
	virtual UpdateSleepTime update();

protected:

	// ai module methods
	virtual void privateExecuteRailedTransport( CommandSourceType cmdSource );
	virtual void privateEvacuate( Int exposeStealthUnits, CommandSourceType cmdSource );

	// our methods
	void setInTransit( Bool inTransit );
	void loadWaypointData();
	void pickAndMoveToInitialLocation();

	// our data
	struct WaypointPathInfo
	{
		UnsignedInt startWaypointID;
		UnsignedInt endWaypointID;
	};
	enum { MAX_WAYPOINT_PATHS = 32 };


public:
	// web port (round 5, units are pure data): the per-unit state is a row of GxComponentPool<GxState>;
	// this module object is the facade (INI, xfer, crc) holding its row handle.
	struct GxState
	{
		Bool inTransit; ///< in transit
		WaypointPathInfo path[ MAX_WAYPOINT_PATHS ]; ///< transit paths we can use
		Int numPaths; ///< how many waypoint paths are in m_path
		Int currentPath; ///< index into m_path for our current path
		Bool waypointDataLoaded; ///< TRUE once we've searched the map to load m_path
	};
	GxState &R() const;
private:
	int m_gxRow;
};
inline RailedTransportAIUpdate::GxState &RailedTransportAIUpdate::R() const { return GxComponentPool<GxState>::the()[m_gxRow]; }
