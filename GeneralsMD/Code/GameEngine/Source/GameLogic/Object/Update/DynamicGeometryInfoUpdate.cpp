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

// FILE: DynamicGeometryInfoUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, April 2002
// Desc:   Update module that changes the object's GeometryInfo
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Xfer.h"
#include "GameLogic/Module/DynamicGeometryInfoUpdate.h"
#include "GameLogic/Object.h"


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DynamicGeometryInfoUpdateModuleData::DynamicGeometryInfoUpdateModuleData()
{

	m_initialDelay = 0;

	m_initialHeight = 0;
	m_initialMajorRadius = 0;
	m_initialMinorRadius = 0;

	m_finalHeight = 0;
	m_finalMajorRadius = 0;
	m_finalMinorRadius = 0;

	m_transitionTime = 1;
	m_reverseAtTransitionTime = FALSE;

}

//-------------------------------------------------------------------------------------------------
/*static*/ void DynamicGeometryInfoUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{

		{ "InitialDelay",					INI::parseDurationUnsignedInt,		nullptr, offsetof(DynamicGeometryInfoUpdateModuleData, m_initialDelay) },

		{ "InitialHeight",				INI::parseReal,										nullptr, offsetof(DynamicGeometryInfoUpdateModuleData, m_initialHeight) },
		{ "InitialMajorRadius",		INI::parseReal,										nullptr, offsetof(DynamicGeometryInfoUpdateModuleData, m_initialMajorRadius) },
		{ "InitialMinorRadius",		INI::parseReal,										nullptr, offsetof(DynamicGeometryInfoUpdateModuleData, m_initialMinorRadius) },

		{ "FinalHeight",					INI::parseReal, nullptr, offsetof(DynamicGeometryInfoUpdateModuleData, m_finalHeight) },
		{ "FinalMajorRadius",			INI::parseReal, nullptr, offsetof(DynamicGeometryInfoUpdateModuleData, m_finalMajorRadius) },
		{ "FinalMinorRadius",			INI::parseReal, nullptr, offsetof(DynamicGeometryInfoUpdateModuleData, m_finalMinorRadius) },

		{ "TransitionTime",				INI::parseDurationUnsignedInt,		nullptr, offsetof(DynamicGeometryInfoUpdateModuleData, m_transitionTime) },
		{ "ReverseAtTransitionTime", INI::parseBool,	 nullptr, offsetof( DynamicGeometryInfoUpdateModuleData, m_reverseAtTransitionTime ) },

		{ nullptr, nullptr, nullptr, 0 }

	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
DynamicGeometryInfoUpdate::DynamicGeometryInfoUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{

	DynamicGeometryInfoUpdateModuleData *modData = (DynamicGeometryInfoUpdateModuleData *)moduleData;
	GD().m_startingDelayCountdown = modData->m_initialDelay;
	GD().m_startingDelayCountdown = max( GD().m_startingDelayCountdown, 1u );
	GD().m_timeActive = 0;

	GD().m_started = FALSE;
	GD().m_finished = FALSE;

	GD().m_reverseAtTransitionTime = modData->m_reverseAtTransitionTime;
	GD().m_switchedDirections = FALSE;

	// record in our instance what initial and final height are
	GD().m_initialHeight = modData->m_initialHeight;
	GD().m_initialMajorRadius = modData->m_initialMajorRadius;
	GD().m_initialMinorRadius = modData->m_initialMinorRadius;
	GD().m_finalHeight = modData->m_finalHeight;
	GD().m_finalMajorRadius = modData->m_finalMajorRadius;
	GD().m_finalMinorRadius = modData->m_finalMinorRadius;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DynamicGeometryInfoUpdate::~DynamicGeometryInfoUpdate()
{
}

//-------------------------------------------------------------------------------------------------
/** The update callback. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime DynamicGeometryInfoUpdate::update()
{
/// @todo srj use SLEEPY_UPDATE here
	if( GD().m_finished )
		return UPDATE_SLEEP_NONE;

	if( !GD().m_started )
	{

		GD().m_startingDelayCountdown--;
		if( GD().m_startingDelayCountdown > 0 )
			return UPDATE_SLEEP_NONE;

		GD().m_started = TRUE;

	}

	// Either we've been running, or we just started right now.  Doesn't matter.
	const DynamicGeometryInfoUpdateModuleData *data = getDynamicGeometryInfoUpdateModuleData();
	Object *me = getObject();
	Real newHeight, newMajor, newMinor;

	Real ratio = (float)GD().m_timeActive / (float)data->m_transitionTime;

	newHeight = GD().m_initialHeight + (ratio * (GD().m_finalHeight - GD().m_initialHeight));
	newMajor = GD().m_initialMajorRadius + (ratio * (GD().m_finalMajorRadius - GD().m_initialMajorRadius));
	newMinor = GD().m_initialMinorRadius + (ratio * (GD().m_finalMinorRadius - GD().m_initialMinorRadius));

	// make a new geometry info with the new values
	const GeometryInfo oldGeom = me->getGeometryInfo();
	GeometryInfo newGeom( oldGeom.getGeomType(), oldGeom.getIsSmall(), newHeight, newMajor, newMinor );
	me->setGeometryInfo( newGeom );

	// we've not been active another frame .. increment out counter
	GD().m_timeActive++;

	// greater, so an update that is exactly right will be done
	if( GD().m_timeActive > data->m_transitionTime )
	{

		// if we're supposed to reverse at transition time do so now if we haven't already
		if( GD().m_reverseAtTransitionTime == TRUE )
		{

			// switch directions, reset time active, and turn off the switch at transition time
			GD().m_switchedDirections = TRUE;
			GD().m_timeActive = 0;
			GD().m_reverseAtTransitionTime = FALSE;

			// swap the initial and final values
			GD().m_initialHeight = data->m_finalHeight;
			GD().m_initialMajorRadius = data->m_finalMajorRadius;
			GD().m_initialMinorRadius = data->m_finalMinorRadius;
			GD().m_finalHeight = data->m_initialHeight;
			GD().m_finalMajorRadius = data->m_initialMajorRadius;
			GD().m_finalMinorRadius = data->m_initialMinorRadius;

		}
		else
		{

			// no switch needed ... we're all done
			GD().m_finished = TRUE;

		}

	}

	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DynamicGeometryInfoUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void DynamicGeometryInfoUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// starting delay countdown
	xfer->xferUnsignedInt( &GD().m_startingDelayCountdown );

	// time active
	xfer->xferUnsignedInt( &GD().m_timeActive );

	// started
	xfer->xferBool( &GD().m_started );

	// finished
	xfer->xferBool( &GD().m_finished );

	// reverse at transition time
	xfer->xferBool( &GD().m_reverseAtTransitionTime );

	// direction
	xfer->xferUser( &GD().m_direction, sizeof( DynamicGeometryDirection ) );

	// switched directions
	xfer->xferBool( &GD().m_switchedDirections );

	// initial height
	xfer->xferReal( &GD().m_initialHeight );

	// initial major radius
	xfer->xferReal( &GD().m_initialMajorRadius );

	// initial minor radius
	xfer->xferReal( &GD().m_initialMinorRadius );

	// final height
	xfer->xferReal( &GD().m_finalHeight );

	// final major radius
	xfer->xferReal( &GD().m_finalMajorRadius );

	// final minor radius
	xfer->xferReal( &GD().m_finalMinorRadius );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DynamicGeometryInfoUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
