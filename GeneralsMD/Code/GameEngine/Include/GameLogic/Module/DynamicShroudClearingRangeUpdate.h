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

// FILE: DynamicShroudClearingRangeUpdate.h /////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, August 2002
// Desc:   Changes the Objects shroud clearing range
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/UpdateModule.h"
#include "GameClient/RadiusDecal.h"///< For the pseudo-wireframe decal effect
#include "GameLogic/GxComponentTable.h"


#define GRID_FX_DECAL_COUNT (30)

//-------------------------------------------------------------------------------------------------
class DynamicShroudClearingRangeUpdateModuleData : public UpdateModuleData
{
public:
	UnsignedInt m_shrinkDelay;	///< wait until
	UnsignedInt m_shrinkTime;		///< then shrink this fast
	UnsignedInt m_growDelay;    ///< wait until
	UnsignedInt m_growTime;     ///< then grow this fast

	Real m_finalVision;										///< Then change to this
	UnsignedInt m_changeInterval;			///< And update my Object every this long
	UnsignedInt m_growInterval;				///< Update every this long while growing
	Bool m_doSpySatFX;										///< Do I do the pseudo-wireframe decal and blip effects?
	RadiusDecalTemplate m_gridDecalTemplate;///< For the pseudo-wireframe decal effect


	DynamicShroudClearingRangeUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

private:

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// Web port (round 5): DynamicShroudClearingRangeUpdate's per-unit state, one row per module in its component table
enum DSCRU_STATE
{
	DSCRU_NOT_STARTED_YET,
	DSCRU_GROWING,
	DSCRU_SUSTAINING,
	DSCRU_SHRINKING,
	DSCRU_DONE_FOREVER,
	DSCRU_SLEEPING
};

// round 5 kernel output (see gxDscruStep): the row's next state and what the apply step must do
struct GxDscruNext
{
	unsigned int stamp;					///< frame + 1 when valid
	DSCRU_STATE inState; Int inCount; UnsignedInt inInterval; Real inRange, inNative;	///< the inputs it was made from
	DSCRU_STATE state; Int count; UnsignedInt interval; Real range;
	unsigned char fx;						///< 0 nothing, 1 animate the grid decals, 2 kill them
	Bool setRange;							///< set the object's shroud clearing range to `range`
};

struct DynamicShroudClearingRangeUpdateRow
{
	// copies of the module data the kernel reads (constant per module)
	UnsignedInt m_gxGrowTime, m_gxShrinkTime, m_gxChangeInterval, m_gxGrowInterval;
	Real m_gxFinalVision;
	GxDscruNext m_gxNext;
	DSCRU_STATE m_state;
	Int m_stateCountDown;
	Int m_totalFrames;
	UnsignedInt m_growStartDeadline;
	UnsignedInt m_sustainDeadline;
	UnsignedInt m_shrinkStartDeadline;
	UnsignedInt m_doneForeverFrame; ///< Just in case interval and state timing goes awry
	UnsignedInt m_changeIntervalCountdown;///< How long till I change my vision range again
	Bool m_decalsCreated;											///< Have I created the fx decals yet?
	Real m_visionChangePerInterval;						///< How much I change each time.
	Real m_nativeClearingRange;										///< What is my objects native vision range?
	Real m_currentClearingRange;							///<ToKeepTrackOfWhere We are at
	RadiusDecal m_gridDecal[GRID_FX_DECAL_COUNT];///< For the pseudo-wireframe decal effect
};

class DynamicShroudClearingRangeUpdate : public UpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( DynamicShroudClearingRangeUpdate, "DynamicShroudClearingRangeUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( DynamicShroudClearingRangeUpdate, DynamicShroudClearingRangeUpdateModuleData )

public:

	DynamicShroudClearingRangeUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update();

	void createGridDecals( const RadiusDecalTemplate& tmpl, Real radius, const Coord3D& pos );
	void killGridDecals();
	void animateGridDecals();


protected:





//	Bool m_shrinkFinished;								///< Nothing left to do
//	Bool m_shrinkStarted;									///< Working it


//	UnsignedInt m_shrinkStartCountdown;		///< When I start to shrink
//	UnsignedInt m_shrinkDeadline;					///< When I am done

	//New Timer Parameters

																	///< This supersedes and makes us quit

	GX_COMPONENT_ROWS(DynamicShroudClearingRangeUpdate, DynamicShroudClearingRangeUpdateRow)
};
