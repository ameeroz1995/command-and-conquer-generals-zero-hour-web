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

// FILE: BridgeTowerBehavior.h ////////////////////////////////////////////////////////////////////
// Author: Colin Day, July 2002
// Desc:   Behavior module for the towers attached to bridges that can be targeted
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/DieModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class BridgeTowerBehaviorInterface
{

public:

	virtual void setBridge( Object *bridge ) = 0;
	virtual ObjectID getBridgeID() = 0;
	virtual void setTowerType( BridgeTowerType type ) = 0;

};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include "Common/GxComponentPool.h"
struct GxBridgeTowerBehaviorRow;
class BridgeTowerBehavior : public BehaviorModule,
														public BridgeTowerBehaviorInterface,
														public DieModuleInterface,
														public DamageModuleInterface
{

	MAKE_STANDARD_MODULE_MACRO( BridgeTowerBehavior );
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( BridgeTowerBehavior, "BridgeTowerBehavior" )

public:

	BridgeTowerBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	static Int getInterfaceMask() { return (MODULEINTERFACE_DAMAGE) | (MODULEINTERFACE_DIE); }
	BridgeTowerBehaviorInterface* getBridgeTowerBehaviorInterface() { return this; }

	virtual void setBridge( Object *bridge );
	virtual ObjectID getBridgeID();
	virtual void setTowerType( BridgeTowerType type );

	static BridgeTowerBehaviorInterface *getBridgeTowerBehaviorInterfaceFromObject( Object *obj );

	// Damage methods
	virtual DamageModuleInterface* getDamage() { return this; }
	virtual void onDamage( DamageInfo *damageInfo );
	virtual void onHealing( DamageInfo *damageInfo );
	virtual void onBodyDamageStateChange( const DamageInfo* damageInfo,
																				BodyDamageType oldState,
																				BodyDamageType newState );

	// Die methods
	virtual DieModuleInterface* getDie() { return this; }
	virtual void onDie( const DamageInfo *damageInfo );

protected:

	GxRow<struct GxBridgeTowerBehaviorRow> m_gxBridgeTowerBehaviorRow;	///< web port (round 5): this unit's row in TheGxBridgeTowerBehaviors
	friend struct GxBridgeTowerBehaviorRow;
	inline struct GxBridgeTowerBehaviorRow &gxRow() const;

};

// Web port (round 5, units are pure data): BridgeTowerBehavior's per-unit state, one row per unit in TheGxBridgeTowerBehaviors;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxBridgeTowerBehaviorRow
{
	ObjectID bridgeID;
	BridgeTowerType type;
};


inline GxBridgeTowerBehaviorRow &BridgeTowerBehavior::gxRow() const { return const_cast<GxBridgeTowerBehaviorRow &>(*m_gxBridgeTowerBehaviorRow); }

