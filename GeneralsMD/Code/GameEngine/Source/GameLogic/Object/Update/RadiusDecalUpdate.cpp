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

// FILE: RadiusDecalUpdate.cpp ///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/RadiusDecalUpdate.h"
#include "GameLogic/Object.h"


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
RadiusDecalUpdate::RadiusDecalUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{

	D().m_deliveryDecal.clear();
	D().m_killWhenNoLongerAttacking = false;
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
RadiusDecalUpdate::~RadiusDecalUpdate()
{
	D().m_deliveryDecal.clear();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void RadiusDecalUpdate::createRadiusDecal( const RadiusDecalTemplate& tmpl, Real radius, const Coord3D& pos )
{
	D().m_deliveryDecal.clear();
	tmpl.createRadiusDecal(pos, radius, getObject()->getControllingPlayer(), D().m_deliveryDecal);
	setWakeFrame(getObject(), D().m_deliveryDecal.isEmpty() ? UPDATE_SLEEP_FOREVER : UPDATE_SLEEP_NONE);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void RadiusDecalUpdate::killRadiusDecal()
{
	D().m_deliveryDecal.clear();
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime RadiusDecalUpdate::update()
{
	if (D().m_killWhenNoLongerAttacking && !getObject()->testStatus( OBJECT_STATUS_IS_ATTACKING ))
	{
		D().m_deliveryDecal.clear();
		return UPDATE_SLEEP_FOREVER;
	}

	D().m_deliveryDecal.update();
	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void RadiusDecalUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void RadiusDecalUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// decal, if any
	D().m_deliveryDecal.xferRadiusDecal(xfer);

	xfer->xferBool(&D().m_killWhenNoLongerAttacking);

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void RadiusDecalUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
