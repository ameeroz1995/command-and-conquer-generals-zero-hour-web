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
#include <algorithm>
#include <vector>

// match rule weaponPass: the armed objects whose weapon status the end-of-tick pass keeps,
// sorted by object ID (a removed one leaves a null until the next pass).
static std::vector<ObjectWeaponStatusHelper*> s_gxWsList;

static bool gxWsLess(ObjectWeaponStatusHelper* a, ObjectID id)
{
	return a && a->gxObject()->getID() < id;
}

Bool ObjectWeaponStatusHelper::gxJoinWeaponStatusPass()
{
	if (!gxMatchRule(GXRULE_WEAPON_PASS))
		return false;
	// nulls sort anywhere; compact first so the list is strictly ordered for the search
	s_gxWsList.erase(std::remove(s_gxWsList.begin(), s_gxWsList.end(), (ObjectWeaponStatusHelper*)nullptr), s_gxWsList.end());
	const ObjectID id = getObject()->getID();
	std::vector<ObjectWeaponStatusHelper*>::iterator it = std::lower_bound(s_gxWsList.begin(), s_gxWsList.end(), id, gxWsLess);
	if (it == s_gxWsList.end() || *it != this)
		s_gxWsList.insert(it, this);
	return true;
}

void gxWeaponStatusPass()
{
	if (s_gxWsList.empty())
		return;
	if (!gxMatchRule(GXRULE_WEAPON_PASS))
	{
		// the rule went off (single player, Settings): hand every object back to its own update
		for (size_t i = 0; i < s_gxWsList.size(); ++i)
			if (s_gxWsList[i])
				s_gxWsList[i]->gxWake();
		s_gxWsList.clear();
		return;
	}
	size_t w = 0;
	for (size_t i = 0; i < s_gxWsList.size(); ++i)
	{
		ObjectWeaponStatusHelper* h = s_gxWsList[i];
		if (!h)
			continue;
		s_gxWsList[w++] = h;
		Object* obj = h->gxObject();
		// as the update queue would: a disabled object's helper does not run
		if (!obj->isDisabled())
			obj->adjustModelConditionForWeaponStatus();
	}
	s_gxWsList.resize(w);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ObjectWeaponStatusHelper::~ObjectWeaponStatusHelper()
{
	std::vector<ObjectWeaponStatusHelper*>::iterator it = std::find(s_gxWsList.begin(), s_gxWsList.end(), this);
	if (it != s_gxWsList.end())
		*it = nullptr;

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
