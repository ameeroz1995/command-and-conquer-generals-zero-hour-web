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

// FILE: UndeadBody.h ////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, June 2003
// Desc:	 First death is intercepted and sets flags and setMaxHealth.  Second death is handled normally.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/ActiveBody.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Object;

//-------------------------------------------------------------------------------------------------
class UndeadBodyModuleData : public ActiveBodyModuleData
{
public:
	Real m_secondLifeMaxHealth;

	UndeadBodyModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);
};

// web port (round 5, units are pure data): UndeadBody's per-unit state, one row per module in
// GxComponentPool<UndeadBodyRow> (the module keeps only the row, m_gxUndeadBody)
struct UndeadBodyRow
{
	Bool m_isSecondLife = false;	///< false until I detect death the first time, then I change my Max, Initial, and Current health and stop intercepting anything
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class UndeadBody : public ActiveBody
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( UndeadBody, "UndeadBody" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( UndeadBody, UndeadBodyModuleData )

public:

	UndeadBody( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void attemptDamage( DamageInfo *damageInfo );		///< try to damage this object

protected:

	GxRow<UndeadBodyRow> m_gxUndeadBody;	///< web port (round 5): this module's state, a row of its component pool
	void startSecondLife(DamageInfo *damageInfo);

};
