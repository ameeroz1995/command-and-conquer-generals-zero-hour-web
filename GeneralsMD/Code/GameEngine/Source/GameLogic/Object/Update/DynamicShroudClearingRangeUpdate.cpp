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

// FILE: DynamicShroudClearingRangeUpdate.cpp /////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, August 2002
// Desc:   Changes the Objects Shroud Clearing range
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/DynamicShroudClearingRangeUpdate.h"






//-------------------------------------------------------------------------------------------------
DynamicShroudClearingRangeUpdateModuleData::DynamicShroudClearingRangeUpdateModuleData()
{
	m_shrinkDelay = 0;
	m_shrinkTime = 0;
	m_growDelay = 0;
	m_growTime = 0;

	m_finalVision = 0.0f;
	m_changeInterval = 0;
	m_growInterval = 0;
	m_doSpySatFX = FALSE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void DynamicShroudClearingRangeUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  UpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "ChangeInterval",			INI::parseDurationUnsignedInt,	nullptr, offsetof( DynamicShroudClearingRangeUpdateModuleData, m_changeInterval ) },
		{ "GrowInterval",			INI::parseDurationUnsignedInt,	nullptr, offsetof( DynamicShroudClearingRangeUpdateModuleData, m_growInterval ) },
		{ "ShrinkDelay",				INI::parseDurationUnsignedInt,	nullptr, offsetof( DynamicShroudClearingRangeUpdateModuleData, m_shrinkDelay ) },
		{ "ShrinkTime",					INI::parseDurationUnsignedInt,	nullptr, offsetof( DynamicShroudClearingRangeUpdateModuleData, m_shrinkTime ) },
		{ "GrowDelay",					INI::parseDurationUnsignedInt,	nullptr, offsetof( DynamicShroudClearingRangeUpdateModuleData, m_growDelay ) },
		{ "GrowTime",						INI::parseDurationUnsignedInt,	nullptr, offsetof( DynamicShroudClearingRangeUpdateModuleData, m_growTime ) },
		{ "FinalVision",				INI::parseReal,									nullptr, offsetof( DynamicShroudClearingRangeUpdateModuleData, m_finalVision ) },
		{ "GridDecalTemplate", RadiusDecalTemplate::parseRadiusDecalTemplate, nullptr, offsetof( DynamicShroudClearingRangeUpdateModuleData, m_gridDecalTemplate ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//
//  doneForeverFrame
//  o                |                                     shrinkdelay                    |
//  |<-----------------------------------stateCountDown---------------------------------->|
//  |  shrinktime    |                                     |   growtime  |   growdelay    |
//  |                shrinkStartDeadline                   sustain       growStartDeadline|
//
//-------------------------------------------------------------------------------------------------
DynamicShroudClearingRangeUpdate::DynamicShroudClearingRangeUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{

	DynamicShroudClearingRangeUpdateModuleData *md = (DynamicShroudClearingRangeUpdateModuleData *)moduleData;
//	m_shrinkStartCountdown = md->m_shrinkDelay;
//	m_shrinkDeadline = 0;
	R().m_changeIntervalCountdown = 0;
	R().m_visionChangePerInterval = 0.0f;
	R().m_gxGrowTime = md->m_growTime;
	R().m_gxShrinkTime = md->m_shrinkTime;
	R().m_gxChangeInterval = md->m_changeInterval;
	R().m_gxGrowInterval = md->m_growInterval;
	R().m_gxFinalVision = md->m_finalVision;


	R().m_stateCountDown = md->m_shrinkDelay + md->m_shrinkTime;// total time
	R().m_totalFrames = max(1,R().m_stateCountDown);
	R().m_shrinkStartDeadline = R().m_stateCountDown - md->m_shrinkDelay;
	R().m_growStartDeadline =   R().m_stateCountDown - md->m_growDelay;
	R().m_sustainDeadline  = R().m_growStartDeadline - md->m_growTime;

	DEBUG_ASSERTCRASH((R().m_sustainDeadline >= R().m_shrinkStartDeadline),("DynamicShroudClearingRangeUpdate\nYOU WILL NEVER REACH FULL SHROUD CLEARING RANGE WITH THESE TIMES AND DELAYS"));
	DEBUG_ASSERTCRASH((R().m_growStartDeadline >= R().m_shrinkStartDeadline),("DynamicShroudClearingRangeUpdate\nYOU WILL NEVER REACH FULL SHROUD CLEARING RANGE WITH THESE TIMES AND DELAYS"));

	R().m_doneForeverFrame = TheGameLogic->getFrame() + R().m_stateCountDown;// a failsafe to force a shutdown on schedule

//	R().m_shrinkStartDeadline;
//	R().m_doneForeverFrame; //

	R().m_decalsCreated = FALSE;
	R().m_nativeClearingRange = 200;// a sensible default
	R().m_currentClearingRange = 0;

	R().m_state = DSCRU_NOT_STARTED_YET;
//	m_shrinkFinished = FALSE;
//	m_shrinkStarted = FALSE;

	Object* obj = getObject();
	if ( ! obj )
		return;
	Player* controller = obj->getControllingPlayer();
	if ( ! controller )
		return;

	R().m_nativeClearingRange = obj->getShroudClearingRange();//capture this before we start monkeying with it

	for (int d = 0; d < GRID_FX_DECAL_COUNT; ++d)
	{
		R().m_gridDecal[d].clear();
	}

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DynamicShroudClearingRangeUpdate::~DynamicShroudClearingRangeUpdate()
{
	killGridDecals();// just in case

}


//-------------------------------------------------------------------------------------------------
void DynamicShroudClearingRangeUpdate::createGridDecals( const RadiusDecalTemplate& tmpl, Real radius, const Coord3D& pos )
{
	for (int d = 0; d < GRID_FX_DECAL_COUNT; ++d)
	{
		R().m_gridDecal[d].clear();
		tmpl.createRadiusDecal(pos, radius, getObject()->getControllingPlayer(), R().m_gridDecal[d]);
		R().m_gridDecal[d].setPosition(pos);
	}


}


//-------------------------------------------------------------------------------------------------
void DynamicShroudClearingRangeUpdate::animateGridDecals()
{

	const Coord3D *ctr = getObject()->getPosition();
	Coord3D pos;
	pos.z = 0;

	Real radius = R().m_currentClearingRange + ((R().m_totalFrames - R().m_stateCountDown) * 2);
	Real angle = 0.0f;
	Real angleInc = (PI*2.0f) / (Real)GRID_FX_DECAL_COUNT;
	Real opacity = 1.0f - (R().m_currentClearingRange / R().m_nativeClearingRange);

	for (int d = 0; d < GRID_FX_DECAL_COUNT; ++d)
	{
		pos.x = ctr->x + (sinf(angle) * radius);
		pos.y = ctr->y + (cosf(angle) * radius);

		pos.x -= ((Int)pos.x)%23;
		pos.y -= ((Int)pos.y)%23;

		R().m_gridDecal[d].setPosition(pos);
		R().m_gridDecal[d].setOpacity( opacity );
		angle += angleInc;

	}


}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void DynamicShroudClearingRangeUpdate::killGridDecals()
{
	for (int d = 0; d < GRID_FX_DECAL_COUNT; ++d)
	{
		R().m_gridDecal[d].clear();
	}

}



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// Round 5 (rowKernels, fourth word bit 12): the state machine is a kernel, update() its apply step.
// Kernel. Reads: the row's state, stateCountDown, changeIntervalCountdown, currentClearingRange,
// nativeClearingRange, deadlines, doneForeverFrame and the copied module data. Writes: next only.
// No allocation, events, RNG or globals (the frame is an argument). The same arithmetic, with the same
// types, as the old update (stateCountDown is signed, the deadlines unsigned).
static void gxDscruStep(const DynamicShroudClearingRangeUpdateRow &r, UnsignedInt currentFrame, GxDscruNext &n)
{
	DSCRU_STATE state = r.m_state;
	Int count = r.m_stateCountDown;
	UnsignedInt interval = r.m_changeIntervalCountdown;
	Real range = r.m_currentClearingRange;
	n.inState = r.m_state; n.inCount = r.m_stateCountDown; n.inInterval = r.m_changeIntervalCountdown;
	n.inRange = r.m_currentClearingRange; n.inNative = r.m_nativeClearingRange;
	n.fx = 0;
	n.setRange = FALSE;

	if( count <= 0 || currentFrame > r.m_doneForeverFrame )
		state = DSCRU_DONE_FOREVER;
	else if ( count <= r.m_shrinkStartDeadline  )
		state = DSCRU_SHRINKING;
	else if ( count <= r.m_sustainDeadline )
		state = DSCRU_SUSTAINING;
	else if ( count <= r.m_growStartDeadline )
		state = DSCRU_GROWING;

	switch (state)
	{
		case DSCRU_NOT_STARTED_YET :
			n.fx = 1;
			break;
		case DSCRU_GROWING :
			n.fx = 1;
			range += r.m_nativeClearingRange / max(1.0f, (Real)r.m_gxGrowTime);
			if (range >= r.m_nativeClearingRange)
				state = DSCRU_SUSTAINING;
			break;
		case DSCRU_SUSTAINING :
			range = r.m_nativeClearingRange;
			n.fx = 2;
			break;
		case DSCRU_SHRINKING :
			range -= (r.m_nativeClearingRange-r.m_gxFinalVision) / max(1.0f, (Real)r.m_gxShrinkTime);
			break;
		case DSCRU_DONE_FOREVER :
			n.fx = 2;
			range = r.m_gxFinalVision;
			break;
		default:
			break;
	}

	if ( count > 0 ) count --;// it is important that this gets called every frame without sleeping

	if( interval > 0 )
		interval--;
	else
	{// reset per change timer
		interval = ( state == DSCRU_GROWING ? r.m_gxGrowInterval : r.m_gxChangeInterval);
		n.setRange = TRUE;
		if (state == DSCRU_DONE_FOREVER) // We are done forever, and have done the final update, so sleep.
			state = DSCRU_SLEEPING;
	}
	n.state = state; n.count = count; n.interval = interval; n.range = range;
	n.stamp = currentFrame + 1;
}

/// the kernel over a row range [begin, end) of the pool (rows of sleeping modules are skipped)
static void gxDscruKernel(GxComponentPool<DynamicShroudClearingRangeUpdateRow> &t, int begin, int end, UnsignedInt frame)
{
	for (int i = begin; i < end; ++i)
		if (t.live(i) && t[i].m_state != DSCRU_SLEEPING)
			gxDscruStep(t[i], frame, t[i].m_gxNext);
}

UpdateSleepTime DynamicShroudClearingRangeUpdate::update()
{
	if (R().m_state == DSCRU_SLEEPING) {
		return UPDATE_SLEEP_NONE;
	}
	//Housekeeping-----------------------------------------
	Object *me = getObject();
	const DynamicShroudClearingRangeUpdateModuleData *md = getDynamicShroudClearingRangeUpdateModuleData();
	UnsignedInt currentFrame = TheGameLogic->getFrame();

	if ( ! R().m_decalsCreated )
	{
		createGridDecals(md->m_gridDecalTemplate, 100, *(me->getPosition()));
		R().m_decalsCreated = TRUE;
	}
	//-----------------------------------------------------

	// apply step: the kernel's output for this row, or the same step inline when it is missing or its
	// inputs changed since
	DynamicShroudClearingRangeUpdateRow &r = R();
	GxDscruNext &n = r.m_gxNext;
	if ((GLES3_Page_Switches4() & 4096) && n.stamp != currentFrame + 1)
	{
		GxComponentPool<DynamicShroudClearingRangeUpdateRow> &t = GxComponentPool<DynamicShroudClearingRangeUpdateRow>::the();
		gxDscruKernel(t, 0, t.size(), currentFrame);
	}
	if (n.stamp != currentFrame + 1 || n.inState != r.m_state || n.inCount != r.m_stateCountDown || n.inInterval != r.m_changeIntervalCountdown
		|| !(n.inRange == r.m_currentClearingRange) || !(n.inNative == r.m_nativeClearingRange))
		gxDscruStep(r, currentFrame, n);
	n.stamp = 0;	// used

	// the decals first: animateGridDecals reads the range and countdown from before this frame's step, as
	// it did in the old update (called before they changed)
	if (n.fx == 1)
		animateGridDecals();
	else if (n.fx == 2)
		killGridDecals();
	r.m_state = n.state;
	r.m_stateCountDown = n.count;
	r.m_changeIntervalCountdown = n.interval;
	r.m_currentClearingRange = n.range;
	if (n.setRange)
		me->setShroudClearingRange( n.range );

	return UPDATE_SLEEP_NONE;



//	if( !m_shrinkStarted )
//	{
//		//To allow a start delay of zero, check before dec, and look for trigger after
//		if( m_shrinkStartCountdown > 0 )
//			m_shrinkStartCountdown--;
//		if( m_shrinkStartCountdown == 0 )
//		{
//			m_shrinkStarted = TRUE;
//
//			// Timer for how often to change
//			R().m_changeIntervalCountdown = md->m_changeInterval;
//			// Marker to make sure we end up precisely correct regardless of interval
//			m_shrinkDeadline = TheGameLogic->getFrame() + md->m_shrinkTime;
//			// Figure out how much to change each interval
//			Real visionDiff = me->getShroudClearingRange() - md->m_finalVision;
//			R().m_visionChangePerInterval = -visionDiff/(md->m_shrinkTime/md->m_changeInterval);
//		}
//		else
//		{
//			return UPDATE_SLEEP_NONE;// waiting to start
//		}
//	}

//	if( R().m_changeIntervalCountdown > 0 )
//		R().m_changeIntervalCountdown--;
//	if( R().m_changeIntervalCountdown != 0 )
//		return UPDATE_SLEEP_NONE;
//	// reset per change timer
//	R().m_changeIntervalCountdown = getDynamicShroudClearingRangeUpdateModuleData()->m_changeInterval;
//	UnsignedInt currentFrame = TheGameLogic->getFrame();

//	if( currentFrame > m_shrinkDeadline )
//	{
//		// Time to stop doing anything.
//		me->setShroudClearingRange( getDynamicShroudClearingRangeUpdateModuleData()->m_finalVision );
//		m_shrinkFinished = TRUE;
//		killGridDecals();
//	}
//	else
//	{
//		// bump the vision again
//		me->setShroudClearingRange( me->getShroudClearingRange() + R().m_visionChangePerInterval );
//	}
//	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DynamicShroudClearingRangeUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void DynamicShroudClearingRangeUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// shrink start countdown
	xfer->xferInt( &R().m_stateCountDown );

	xfer->xferInt( &R().m_totalFrames );

	xfer->xferUnsignedInt( &R().m_growStartDeadline );

	xfer->xferUnsignedInt( &R().m_sustainDeadline );

	xfer->xferUnsignedInt( &R().m_shrinkStartDeadline );

	xfer->xferUnsignedInt( &R().m_doneForeverFrame );

	xfer->xferUnsignedInt( &R().m_changeIntervalCountdown );

	// vision change per interval
	xfer->xferBool( &R().m_decalsCreated );

	xfer->xferReal( &R().m_visionChangePerInterval );

	xfer->xferReal( &R().m_nativeClearingRange );
	xfer->xferReal( &R().m_currentClearingRange );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DynamicShroudClearingRangeUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
