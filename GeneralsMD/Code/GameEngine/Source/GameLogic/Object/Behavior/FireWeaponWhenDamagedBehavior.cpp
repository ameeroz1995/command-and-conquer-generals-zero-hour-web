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

// FILE: FireWeaponWhenDamagedBehavior.cpp ///////////////////////////////////////////////////////////////////////
// Author:
// Desc:
///////////////////////////////////////////////////////////////////////////////////////////////////


// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#define DEFINE_SLOWDEATHPHASE_NAMES

#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/INI.h"
#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/FireWeaponWhenDamagedBehavior.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"

// Web port (round 5, units are pure data): FireWeaponWhenDamagedBehavior's per-unit state is a row of TheGxFireWeaponWhenDamagedBehaviors;
// GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
#define GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR (const_cast<GxFireWeaponWhenDamagedBehaviorRow &>(*m_gxFireWeaponWhenDamagedBehaviorRow))


const Int MAX_IDX = 32;

const Real BEGIN_MIDPOINT_RATIO = 0.35f;
const Real END_MIDPOINT_RATIO = 0.65f;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
FireWeaponWhenDamagedBehavior::FireWeaponWhenDamagedBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponPristine = nullptr;
	GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponDamaged = nullptr;
	GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponReallyDamaged = nullptr;
	GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponRubble = nullptr;
	GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponPristine = nullptr;
	GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponDamaged = nullptr;
	GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponReallyDamaged = nullptr;
	GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponRubble = nullptr;


	const FireWeaponWhenDamagedBehaviorModuleData *d = getFireWeaponWhenDamagedBehaviorModuleData();
	const Object* obj = getObject();

	if ( d->m_reactionWeaponPristine )
	{
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponPristine				= TheWeaponStore->allocateNewWeapon(
			d->m_reactionWeaponPristine,					PRIMARY_WEAPON);
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponPristine->reloadAmmo( obj );
	}
	if ( d->m_reactionWeaponDamaged )
	{
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponDamaged					= TheWeaponStore->allocateNewWeapon(
			d->m_reactionWeaponDamaged,					PRIMARY_WEAPON);
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponDamaged->reloadAmmo( obj );
	}
	if ( d->m_reactionWeaponReallyDamaged )
	{
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponReallyDamaged		= TheWeaponStore->allocateNewWeapon(
			d->m_reactionWeaponReallyDamaged,		PRIMARY_WEAPON);
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponReallyDamaged->reloadAmmo( obj );
	}
	if ( d->m_reactionWeaponRubble )
	{
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponRubble					= TheWeaponStore->allocateNewWeapon(
			d->m_reactionWeaponRubble,						PRIMARY_WEAPON);
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponRubble->reloadAmmo( obj );
	}


	if ( d->m_continuousWeaponPristine )
	{
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponPristine			= TheWeaponStore->allocateNewWeapon(
			d->m_continuousWeaponPristine,				PRIMARY_WEAPON);
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponPristine->reloadAmmo( obj );
	}
	if ( d->m_continuousWeaponDamaged )
	{
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponDamaged				= TheWeaponStore->allocateNewWeapon(
			d->m_continuousWeaponDamaged,				PRIMARY_WEAPON);
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponDamaged->reloadAmmo( obj );
	}
	if ( d->m_continuousWeaponReallyDamaged )
	{
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponReallyDamaged = TheWeaponStore->allocateNewWeapon(
			d->m_continuousWeaponReallyDamaged,	PRIMARY_WEAPON);
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponReallyDamaged->reloadAmmo( obj );
	}
	if ( d->m_continuousWeaponRubble )
	{
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponRubble				= TheWeaponStore->allocateNewWeapon(
			d->m_continuousWeaponRubble,					PRIMARY_WEAPON);
		GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponRubble->reloadAmmo( obj );
	}

	if (d->m_initiallyActive)
	{
		giveSelfUpgrade();
	}

	if (isUpgradeActive() &&
			(d->m_continuousWeaponPristine != nullptr ||
			d->m_continuousWeaponDamaged != nullptr ||
			d->m_continuousWeaponReallyDamaged != nullptr ||
			d->m_continuousWeaponRubble != nullptr))
	{
		setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
	}
	else
	{
		setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
	}

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
FireWeaponWhenDamagedBehavior::~FireWeaponWhenDamagedBehavior()
{
	deleteInstance(GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponPristine);
	deleteInstance(GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponDamaged);
	deleteInstance(GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponReallyDamaged);
	deleteInstance(GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponRubble);

	deleteInstance(GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponPristine);
	deleteInstance(GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponDamaged);
	deleteInstance(GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponReallyDamaged);
	deleteInstance(GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponRubble);
}

//-------------------------------------------------------------------------------------------------
/** Damage has been dealt, this is an opportunity to reach to that damage */
//-------------------------------------------------------------------------------------------------
void FireWeaponWhenDamagedBehavior::onDamage( DamageInfo *damageInfo )
{
	if (!isUpgradeActive())
		return;

	const FireWeaponWhenDamagedBehaviorModuleData* d = getFireWeaponWhenDamagedBehaviorModuleData();

	// right type?
	if (!getDamageTypeFlag(d->m_damageTypes, damageInfo->in.m_damageType))
		return;

	// right amount? (use actual [post-armor] damage dealt)
	if (damageInfo->out.m_actualDamageDealt < d->m_damageAmount)
		return;

	const Object *obj = getObject();
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();

	if ( bdt == BODY_RUBBLE )
	{
		if( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponRubble && GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponRubble->getStatus() == READY_TO_FIRE )
		{
			GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponRubble->forceFireWeapon( obj, obj->getPosition() );
		}

	}
	else if ( bdt == BODY_REALLYDAMAGED )
	{
		if( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponReallyDamaged && GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponReallyDamaged->getStatus() == READY_TO_FIRE )
		{
			GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponReallyDamaged->forceFireWeapon( obj, obj->getPosition() );
		}
	}
	else if ( bdt == BODY_DAMAGED )
	{
		if( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponDamaged && GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponDamaged->getStatus() == READY_TO_FIRE )
		{
			GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponDamaged->forceFireWeapon( obj, obj->getPosition() );
		}
	}
	else // not damaged yet
	{
		if( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponPristine && GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponPristine->getStatus() == READY_TO_FIRE )
		{
			GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponPristine->forceFireWeapon( obj, obj->getPosition() );
		}
	}

}

//-------------------------------------------------------------------------------------------------
/** if object fires weapon constantly, figure out which one and do it */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime FireWeaponWhenDamagedBehavior::update()
{
	if (!isUpgradeActive())
	{
		DEBUG_ASSERTCRASH(isUpgradeActive(), ("hmm, this should not be possible"));
		return UPDATE_SLEEP_FOREVER;
	}

	const Object *obj = getObject();
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();

	if ( bdt == BODY_RUBBLE )
	{
		if( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponRubble && GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponRubble->getStatus() == READY_TO_FIRE )
		{
			GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponRubble->forceFireWeapon( obj, obj->getPosition() );
		}

	}
	else if ( bdt == BODY_REALLYDAMAGED )
	{
		if( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponReallyDamaged && GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponReallyDamaged->getStatus() == READY_TO_FIRE )
		{
			GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponReallyDamaged->forceFireWeapon( obj, obj->getPosition() );
		}
	}
	else if ( bdt == BODY_DAMAGED )
	{
		if( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponDamaged && GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponDamaged->getStatus() == READY_TO_FIRE )
		{
			GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponDamaged->forceFireWeapon( obj, obj->getPosition() );
		}
	}
	else // not damaged yet
	{
		if( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponPristine && GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponPristine->getStatus() == READY_TO_FIRE )
		{
			GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponPristine->forceFireWeapon( obj, obj->getPosition() );
		}
	}

	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void FireWeaponWhenDamagedBehavior::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

	// extend upgrade mux
	UpgradeMux::upgradeMuxCRC( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void FireWeaponWhenDamagedBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// extend upgrade mux
	UpgradeMux::upgradeMuxXfer( xfer );

	Bool weaponPresent;

	// reaction pristine
	weaponPresent = GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponPristine ? TRUE : FALSE;
	xfer->xferBool( &weaponPresent );
	if( weaponPresent )
		xfer->xferSnapshot( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponPristine );

	// reaction damaged
	weaponPresent = GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponDamaged ? TRUE : FALSE;
	xfer->xferBool( &weaponPresent );
	if( weaponPresent )
		xfer->xferSnapshot( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponDamaged );

	// reaction really damaged
	weaponPresent = GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponReallyDamaged ? TRUE : FALSE;
	xfer->xferBool( &weaponPresent );
	if( weaponPresent )
		xfer->xferSnapshot( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponReallyDamaged );

	// reaction rubble
	weaponPresent = GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponRubble ? TRUE : FALSE;
	xfer->xferBool( &weaponPresent );
	if( weaponPresent )
		xfer->xferSnapshot( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.reactionWeaponRubble );

	// continuous pristine
	weaponPresent = GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponPristine ? TRUE : FALSE;
	xfer->xferBool( &weaponPresent );
	if( weaponPresent )
		xfer->xferSnapshot( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponPristine );

	// continuous damaged
	weaponPresent = GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponDamaged ? TRUE : FALSE;
	xfer->xferBool( &weaponPresent );
	if( weaponPresent )
		xfer->xferSnapshot( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponDamaged );

	// continuous really damaged
	weaponPresent = GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponReallyDamaged ? TRUE : FALSE;
	xfer->xferBool( &weaponPresent );
	if( weaponPresent )
		xfer->xferSnapshot( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponReallyDamaged );

	// continuous rubble
	weaponPresent = GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponRubble ? TRUE : FALSE;
	xfer->xferBool( &weaponPresent );
	if( weaponPresent )
		xfer->xferSnapshot( GXROW_FIREWEAPONWHENDAMAGEDBEHAVIOR.continuousWeaponRubble );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void FireWeaponWhenDamagedBehavior::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

	// extend upgrade mux
	UpgradeMux::upgradeMuxLoadPostProcess();

}
