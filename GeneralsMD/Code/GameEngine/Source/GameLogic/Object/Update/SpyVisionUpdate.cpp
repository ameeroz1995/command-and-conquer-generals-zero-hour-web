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

// FILE: SpyVisionUpdate.cpp /////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, September 2002
// Desc:   Special Power will spy on the vision of all enemy players.
//				Putting a SpecialPower in a behavior takes a big huge amount of code duplication and
//				has no precedent.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpyVisionUpdate.h"

GX_COMPONENT_TABLE_DEFINE(SpyVisionUpdate)


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SpyVisionUpdate::SpyVisionUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_gxRow = gxTable().add(this);

	R().m_deactivateFrame = 0;
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
	R().m_currentlyActive = FALSE;
	R().m_resetTimersNextUpdate = FALSE;
	R().m_disabledUntilFrame = 0;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SpyVisionUpdate::~SpyVisionUpdate()
{
	gxTable().remove(m_gxRow);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::activateSpyVision( UnsignedInt duration )
{
	UnsignedInt now = TheGameLogic->getFrame();
	if( duration == 0 )
		R().m_deactivateFrame = UINT_MAX;
	else
		R().m_deactivateFrame = now + duration;

	doActivationWork( getObject()->getControllingPlayer(), TRUE );

	if( duration == 0 )
		setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
	else
		setWakeFrame( getObject(), UPDATE_SLEEP(duration) );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::setDisabledUntilFrame( UnsignedInt frame )
{
	UnsignedInt now = TheGameLogic->getFrame();
	if( frame > now )
	{
		//Turn off the spy vision now since we are disabled
		if( R().m_currentlyActive )
		{
			doActivationWork( getObject()->getControllingPlayer(), FALSE );
		}

		//When should we wake up?
		R().m_disabledUntilFrame = frame;

		//Mark it so we can turn it on again on next update.
		R().m_resetTimersNextUpdate = TRUE;

		//Now sleep until the disabled has expired. If it's expired again when we come back due to another
		//sabotage or something else... we'll do the same thing over again.
		setWakeFrame( getObject(), (UpdateSleepTime)(R().m_disabledUntilFrame - now) );
	}
	else
	{
		// Else it is a wakeup message.  Update does the turning on.
		R().m_disabledUntilFrame = now;
		R().m_resetTimersNextUpdate = TRUE;
		setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::onCapture( Player *oldOwner, Player *newOwner )
{
	if( R().m_currentlyActive )
	{
		// If on, flick the switch real fast to get everything to update for the new owner
		doActivationWork( oldOwner, FALSE );
		doActivationWork( newOwner, TRUE );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::onDisabledEdge( Bool nowDisabled )
{
	if( nowDisabled )
		setDisabledUntilFrame(UINT_MAX);
	else
		setDisabledUntilFrame(0);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime SpyVisionUpdate::update()
{
	const SpyVisionUpdateModuleData *data = getSpyVisionUpdateModuleData();
	UnsignedInt now = TheGameLogic->getFrame();

	//Once disable has complete and we are waking up from it, then reset the interval so
	//it has to wait before going on again. If it doesn't have an interval, we turn it on
	//immediately, but only for self powered objects.
	if( R().m_resetTimersNextUpdate )
	{
		R().m_resetTimersNextUpdate = FALSE;

		if( data->m_selfPowered )
		{
			if( data->m_selfPoweredInterval == 0 )
			{
				//It's an always on self-powered special. So turn it back on now.
				doActivationWork( getObject()->getControllingPlayer(), TRUE );
				return UPDATE_SLEEP( UPDATE_SLEEP_FOREVER );
			}
			else
			{
				//Reset the interval timer via sleeping (so we have to wait a longer time before going on)
				return UPDATE_SLEEP( data->m_selfPoweredInterval );
			}
		}
	}

	if( R().m_currentlyActive && (R().m_deactivateFrame <= TheGameLogic->getFrame()) )
	{
		// Turn off SpyVision.
		doActivationWork( getObject()->getControllingPlayer(), FALSE );

		R().m_deactivateFrame = 0;
	}
	else if( !R().m_currentlyActive && data->m_selfPowered )
	{
		doActivationWork( getObject()->getControllingPlayer(), TRUE );
		if( data->m_selfPoweredDuration == 0 )
			R().m_deactivateFrame = UINT_MAX;
		else
			R().m_deactivateFrame = now + data->m_selfPoweredDuration;
	}

	if( data->m_selfPowered )
	{
		if( R().m_currentlyActive )
			return UPDATE_SLEEP(data->m_selfPoweredDuration);
		else
			return UPDATE_SLEEP(data->m_selfPoweredInterval);
	}

	return UPDATE_SLEEP_FOREVER;
}

//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::doActivationWork( Player *playerToSetFor, Bool setting )
{
	const SpyVisionUpdateModuleData *data = getSpyVisionUpdateModuleData();
	if( playerToSetFor == nullptr  ||  ThePlayerList == nullptr )
		return;

	for (Int i=0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *player = ThePlayerList->getNthPlayer(i);
		if( playerToSetFor->getRelationship(player->getDefaultTeam()) == ENEMIES )
		{
			player->setUnitsVisionSpied( setting, data->m_spyOnKindof, playerToSetFor->getPlayerIndex() );
		}
	}

	R().m_currentlyActive = setting;
}

//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::onDelete()
{
	// If I was left on at the time of death, then turn me off.
	if( R().m_currentlyActive )
	{
		doActivationWork( getObject()->getControllingPlayer(), FALSE );
	}
}

// ------------------------------------------------------------------------------------------------
void SpyVisionUpdate::upgradeImplementation()
{
	const SpyVisionUpdateModuleData *data = getSpyVisionUpdateModuleData();
	if( data->m_needsUpgrade && !isAlreadyUpgraded() )
	{
		activateSpyVision(data->m_selfPoweredDuration);// If zero, will turn on permanently.  And it does the wake up setting
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SpyVisionUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SpyVisionUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// deactivate frame
	xfer->xferUnsignedInt( &R().m_deactivateFrame );

	xfer->xferBool( &R().m_currentlyActive );

	if( version >= 2 )
	{
		xfer->xferBool( &R().m_resetTimersNextUpdate );
		xfer->xferUnsignedInt( &R().m_disabledUntilFrame );
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SpyVisionUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
