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

// FILE: ObjectSMCHelper.cpp //////////////////////////////////////////////////////////////////////
// Author: Colin Day, Steven Johnson, September 2002
// Desc:   Weapon status helper
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"
#include "Common/Xfer.h"
#include "GameLogic/Module/ObjectWeaponStatusHelper.h"
#include "Common/GxMatchRules.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "Common/GxUnitStore.h"
#include <algorithm>
#include <vector>

// match rule weaponPass: an armed object whose helper joined the pass has the store's GXB_WEAPON bit;
// the end-of-tick pass walks the store's WEAPON list (object-ID order).
Bool ObjectWeaponStatusHelper::gxJoinWeaponStatusPass()
{
	if (!gxMatchRule(GXRULE_WEAPON_PASS))
		return false;
	TheGxUnitStore.setMask(getObject(), GXB_BIT(GXB_WEAPON));
	return true;
}

void gxWeaponStatusPass()
{
	GxUnitStore &st = TheGxUnitStore;
	const std::vector<int> &list = st.pass(GXB_WEAPON);
	if (list.empty())
		return;
	if (!gxMatchRule(GXRULE_WEAPON_PASS))
	{
		// the rule went off (single player, Settings): hand every object back to its own update
		const std::vector<int> slots(list);	// setMask rebuilds the list
		for (size_t i = 0; i < slots.size(); ++i)
		{
			Object* obj = st.obj[slots[i]];
			if (!obj)
				continue;
			if (ObjectWeaponStatusHelper* h = obj->gxWeaponStatusHelper())
				h->gxWake();
			st.setMask(obj, 0, GXB_BIT(GXB_WEAPON));
		}
		return;
	}
	// weaponSkip (third word bit 19, same game): skip the units for which the update changes nothing
	extern int GLES3_Page_Switches3();
	const bool skip = (GLES3_Page_Switches3() & 524288) != 0;
	const UnsignedInt now = TheGameLogic->getFrame();
	for (size_t i = 0; i < list.size(); ++i)
	{
		const int s = list[i];
		Object* obj = st.obj[s];
		// as the update queue would: a disabled object's helper does not run
		if (!obj || obj->isDisabled())
			continue;
		if (skip)
		{
			// cooldown column: no target, quiet last time, nothing touched its weapons, no reload or
			// pre-attack ending yet: skip outright
			if (st.target[s] == 0 && st.weaponReady[s] > now && st.weaponSig[s] != ~0u && obj->gxWeaponStillQuiet())
				continue;
			if (obj->gxWeaponStatusQuiet(st.target[s], st.weaponReady[s], st.weaponSig[s]))
				continue;
		}
		obj->adjustModelConditionForWeaponStatus();
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ObjectWeaponStatusHelper::~ObjectWeaponStatusHelper()
{

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ObjectWeaponStatusHelper::crc( Xfer *xfer )
{

	// object helper crc
	ObjectHelper::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info;
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ObjectWeaponStatusHelper::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// object helper base class
	ObjectHelper::xfer( xfer );

	// a save made with the weaponPass rule on has this helper asleep; it wakes on load and then
	// rejoins the pass (or resumes its own updates).
	if (xfer->getXferMode() == XFER_LOAD && friend_getNextCallFrame() >= UPDATE_SLEEP_FOREVER)
		friend_setNextCallFrame(TheGameLogic->getFrame());

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ObjectWeaponStatusHelper::loadPostProcess()
{

	// object helper base class
	ObjectHelper::loadPostProcess();

}
