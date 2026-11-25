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

// FILE: SupplyWarehouseCripplingBehavior.cpp /////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, Septemmber 2002
// Desc:   Behavior that Disables the building on ReallyDamaged edge state, and manages an Update timer to heal
///////////////////////////////////////////////////////////////////////////////////////////////////


// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "Common/Xfer.h"
#include "GameLogic/Module/SupplyWarehouseCripplingBehavior.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"

// Web port (round 5, units are pure data): SupplyWarehouseCripplingBehavior's per-unit state is a row of TheGxSupplyWarehouseCripplingBehaviors;
// GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
#define GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR (const_cast<GxSupplyWarehouseCripplingBehaviorRow &>(*m_gxSupplyWarehouseCripplingBehaviorRow))


//-------------------------------------------------------------------------------------------------
SupplyWarehouseCripplingBehaviorModuleData::SupplyWarehouseCripplingBehaviorModuleData()
{
	m_selfHealSupression = 0; ///< Time since last damage until I can start to heal
	m_selfHealDelay = 0;			///< Once I am okay to heal, how often to do so
	m_selfHealAmount = 0;							///< And how much
}

//-------------------------------------------------------------------------------------------------
/*static*/ void SupplyWarehouseCripplingBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{

	static const FieldParse dataFieldParse[] =
	{
		{ "SelfHealSupression",	INI::parseDurationUnsignedInt,	nullptr, offsetof(SupplyWarehouseCripplingBehaviorModuleData, m_selfHealSupression) },
		{ "SelfHealDelay",			INI::parseDurationUnsignedInt,	nullptr, offsetof(SupplyWarehouseCripplingBehaviorModuleData, m_selfHealDelay) },
		{ "SelfHealAmount",			INI::parseReal,									nullptr, offsetof(SupplyWarehouseCripplingBehaviorModuleData, m_selfHealAmount) },
		{ nullptr, nullptr, nullptr, 0 }
	};

  UpdateModuleData::buildFieldParse(p);
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SupplyWarehouseCripplingBehavior::SupplyWarehouseCripplingBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{

	GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR.healingSupressedUntilFrame = 0;
	GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR.nextHealingFrame = 0;
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SupplyWarehouseCripplingBehavior::~SupplyWarehouseCripplingBehavior()
{
}

//-------------------------------------------------------------------------------------------------
/** Damage has been dealt, this is an opportunity to react to that damage */
//-------------------------------------------------------------------------------------------------
void SupplyWarehouseCripplingBehavior::onDamage( DamageInfo *damageInfo )
{
	UnsignedInt now = TheGameLogic->getFrame();
	resetSelfHealSupression();
	setWakeFrame(getObject(), UPDATE_SLEEP(GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR.healingSupressedUntilFrame - now));// we got hit, time to get up for work after a quick snooze
}

//-------------------------------------------------------------------------------------------------
void SupplyWarehouseCripplingBehavior::onBodyDamageStateChange(const DamageInfo* damageInfo, BodyDamageType oldState, BodyDamageType newState)
{
	if( newState == BODY_REALLYDAMAGED )
		startCrippledEffects();
	else if( oldState == BODY_REALLYDAMAGED )
		stopCrippledEffects();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
UpdateSleepTime SupplyWarehouseCripplingBehavior::update()
{
	// Suppression is handled by sleeping the module, so if I am here, I know it is time to heal.
	const SupplyWarehouseCripplingBehaviorModuleData* md = getSupplyWarehouseCripplingBehaviorModuleData();
	UnsignedInt now = TheGameLogic->getFrame();
	GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR.nextHealingFrame = now + md->m_selfHealDelay;

	getObject()->attemptHealing(md->m_selfHealAmount, nullptr);

	if( getObject()->getBodyModule()->getHealth() == getObject()->getBodyModule()->getMaxHealth() )
		return UPDATE_SLEEP_FOREVER;// this can't be in onHealing, as the healing comes from here
	// in the update, and sleep settings in onHealing would be overridden by my return value.

	// Delay between heals is also handled by sleeping the module.  How cool is that?
	return UPDATE_SLEEP(GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR.nextHealingFrame - now);
}

// ------------------------------------------------------------------------------------------------
void SupplyWarehouseCripplingBehavior::resetSelfHealSupression()
{
	const SupplyWarehouseCripplingBehaviorModuleData* md = getSupplyWarehouseCripplingBehaviorModuleData();
	UnsignedInt now = TheGameLogic->getFrame();

	GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR.healingSupressedUntilFrame = now + md->m_selfHealSupression;
	GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR.nextHealingFrame = GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR.healingSupressedUntilFrame;
}

// ------------------------------------------------------------------------------------------------
void SupplyWarehouseCripplingBehavior::startCrippledEffects()
{
	DockUpdateInterface *myDock = getObject()->getDockUpdateInterface();
	myDock->setDockCrippled( TRUE );
}

// ------------------------------------------------------------------------------------------------
void SupplyWarehouseCripplingBehavior::stopCrippledEffects()
{
	DockUpdateInterface *myDock = getObject()->getDockUpdateInterface();
	myDock->setDockCrippled( FALSE );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SupplyWarehouseCripplingBehavior::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SupplyWarehouseCripplingBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// healing suppressed until frame
	xfer->xferUnsignedInt( &GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR.healingSupressedUntilFrame );

	// next healing frame
	xfer->xferUnsignedInt( &GXROW_SUPPLYWAREHOUSECRIPPLINGBEHAVIOR.nextHealingFrame );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SupplyWarehouseCripplingBehavior::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
