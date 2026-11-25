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

// FILE: HordeUpdate.h //////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, Feb 2002
// Desc:   Horde update module
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/GxComponentPool.h"
#include "GameLogic/Module/UpdateModule.h"
#include "Common/KindOf.h"

class SimpleObjectIterator;
class UpgradeTemplate;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

// TheSuperHackers @bugfix xezon 15/09/2025 Adds a new horde action to select a fixed implementation.
//
// TheSuperHackers @todo If we want more control over the horde setup, then we need to implement
// filters for separate weapon bonuses and required upgrades. But adding more behavior modules will
// be a performance concern. Example INI setup:
//
// Behavior = HordeUpdate ModuleTag
//   Action = NATIONALISM
//   ;ApplyWeaponBonus = NATIONALISM
//   UpgradeRequired = Upgrade_Nationalism
// End
//
enum HordeActionType CPP_11(: Int)
{
	HORDEACTION_HORDE, ///< Classic action, applies the Horde bonus correctly, but Nationalism and Fanaticism bonuses are not removed after leaving horde.
	HORDEACTION_HORDE_FIXED, ///< Applies the Horde, Nationalism and Fanaticism bonuses correctly.

	HORDEACTION_COUNT,

#if RETAIL_COMPATIBLE_CRC || PRESERVE_RETAIL_BEHAVIOR
	HORDEACTION_DEFAULT = HORDEACTION_HORDE,
#else
	HORDEACTION_DEFAULT = HORDEACTION_HORDE_FIXED, ///< Does not change unmodified retail game behavior, because all its horde update modules explicitly set Action = HORDE.
#endif
};

#ifdef DEFINE_HORDEACTION_NAMES
static const char *const TheHordeActionTypeNames[] =
{
	"HORDE",
	"HORDE_FIXED",

	nullptr
};
static_assert(ARRAY_SIZE(TheHordeActionTypeNames) == HORDEACTION_COUNT + 1, "Incorrect array size");
#endif

//-------------------------------------------------------------------------------------------------
class HordeUpdateModuleData : public ModuleData
{
public:
  UnsignedInt								m_updateRate;   ///< how often to recheck our horde status
	KindOfMaskType						m_kindof;				///< the kind(s) of units that count towards hordeness
	Int												m_minCount;		  ///< min count to get "horde" status
  Real											m_minDist;      ///< min dist to contribute to hordeness
	Bool											m_alliesOnly;		///< if true, only allied units count towards hordeness
	Bool											m_exactMatch;		///< if true, only exact same type of units count towards hordeness
	Real											m_rubOffRadius;///< If I am this close to another guy who is a true hordesman, it'll rub off on me
	HordeActionType						m_action;				///< what to do if we get hordeness
	Bool											m_allowedNationalism; ///< Nationalism is hard coded.  Yeah!  Add to the goodness with this flag instead of rewriting after Alpha.
	std::vector<AsciiString>	m_flagSubObjNames;		///< name(s) of the flag sub obj

	HordeUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);

private:

};

// ------------------------------------------------------------------------------------------------
class HordeUpdateInterface
{
public:
	virtual Bool isInHorde() const = 0;
	virtual Bool hasFlag() const = 0;
	virtual Bool isTrueHordeMember() const = 0;
	virtual Bool isAllowedNationalism() const = 0;
	virtual HordeActionType getHordeActionType() const = 0;
};

//-------------------------------------------------------------------------------------------------
class HordeUpdate : public UpdateModule, public HordeUpdateInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( HordeUpdate, "HordeUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( HordeUpdate, HordeUpdateModuleData )

public:
	HordeUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	HordeUpdateInterface *getHordeUpdateInterface() { return this; }

	virtual void onDrawableBoundToObject();
	virtual UpdateSleepTime update();	///< update this object's AI

	virtual Bool isInHorde() const { return R().inHorde; }
	virtual Bool hasFlag() const { return R().hasFlag; }
	virtual Bool isTrueHordeMember() const { return R().trueHordeMember && R().inHorde; }
	virtual Bool isAllowedNationalism() const;
	virtual HordeActionType getHordeActionType() const { return getHordeUpdateModuleData()->m_action; };

protected:

	void showHideFlag(Bool show);
	void joinOrLeaveHorde(SimpleObjectIterator *iter, Bool join);

private:

public:
	// web port (round 5, units are pure data): the per-unit state is a row of GxComponentPool<GxState>;
	// this module object is the facade (INI, xfer, crc) holding its row handle.
	struct GxState
	{
		UnsignedInt lastHordeRefreshFrame; //Just like it sounds
		Bool inHorde; //I may be a true member, or I may merely inherit hordehood from a neighbor who is
		Bool trueHordeMember; //meaning, I have enough hordesmen near me to qualify
		Bool hasFlag;
	};
	GxState &R() const;
private:
	int m_gxRow;
};
inline HordeUpdate::GxState &HordeUpdate::R() const { return GxComponentPool<GxState>::the()[m_gxRow]; }
