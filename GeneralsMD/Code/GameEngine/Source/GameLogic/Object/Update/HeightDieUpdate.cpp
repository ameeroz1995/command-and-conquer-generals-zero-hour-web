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

// FILE: HeightDieUpdate.cpp //////////////////////////////////////////////////////////////////////
// Author: Objects that will die when the are a certain height above the terrain or objects
// Desc:   Colin Day, April 2002
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"
#include "Common/GxTime.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/HeightDieUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include <cstring>



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HeightDieUpdateModuleData::HeightDieUpdateModuleData()
{

	m_targetHeightAboveTerrain = 0.0f;
	m_targetHeightIncludesStructures = FALSE;
	m_onlyWhenMovingDown = FALSE;
	m_destroyAttachedParticlesAtHeight = -1.0f;
	m_snapToGroundOnDeath = FALSE;
	m_initialDelay = 0;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void HeightDieUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{

  UpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "TargetHeight", INI::parseReal, nullptr, offsetof( HeightDieUpdateModuleData, m_targetHeightAboveTerrain ) },
		{ "TargetHeightIncludesStructures", INI::parseBool, nullptr, offsetof( HeightDieUpdateModuleData, m_targetHeightIncludesStructures ) },
		{ "OnlyWhenMovingDown", INI::parseBool, nullptr, offsetof( HeightDieUpdateModuleData, m_onlyWhenMovingDown ) },
		{ "DestroyAttachedParticlesAtHeight", INI::parseReal, nullptr, offsetof( HeightDieUpdateModuleData, m_destroyAttachedParticlesAtHeight ) },
		{ "SnapToGroundOnDeath", INI::parseBool, nullptr, offsetof( HeightDieUpdateModuleData, m_snapToGroundOnDeath ) },
		{ "InitialDelay", INI::parseDurationUnsignedInt, nullptr, offsetof( HeightDieUpdateModuleData, m_initialDelay ) },
		{ nullptr, nullptr, nullptr, 0 }

	};

  p.add(dataFieldParse);

}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HeightDieUpdate::HeightDieUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	static Bool gxRegistered = FALSE;
	if (!gxRegistered)
	{
		gxRegistered = TRUE;
		GameLogic::gxRegisterKindKernel(NAMEKEY("HeightDieUpdate"), gxKernel, gxApply);
	}

	D().m_hasDied = FALSE;
	D().m_particlesDestroyed = FALSE;
	D().m_lastPosition.x = -1.0f;
	D().m_lastPosition.y = -1.0f;
	D().m_lastPosition.z = -1.0f;
	D().m_earliestDeathFrame = UINT_MAX;
	// D().m_lastPosition = *thing->getPosition();

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HeightDieUpdate::~HeightDieUpdate()
{

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime HeightDieUpdate::update()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (D().gxKernelFrame == now && now != 0)
		return UPDATE_SLEEP_NONE;	// this frame's turn was taken by the kind kernel + apply
	return gxFullUpdate();
}

//-------------------------------------------------------------------------------------------------
// the update's frames that change nothing but this row (same tests, same order); FALSE: run the full one
Bool HeightDieUpdate::gxQuiet( UnsignedInt now )
{
	Row &r = D();
	const HeightDieUpdateModuleData *modData = getHeightDieUpdateModuleData();
	if( r.m_earliestDeathFrame == UINT_MAX )
		r.m_earliestDeathFrame = now + modData->m_initialDelay;	// the full update would do the same first
	if( r.m_earliestDeathFrame > now )
		return TRUE;
	const Coord3D *pos = getObject()->getPosition();
	if( getObject()->getContainedBy() != nullptr )
	{
		r.m_lastPosition = *pos;
		return TRUE;
	}
	if( modData->m_targetHeightIncludesStructures )
		return FALSE;	// searches the partition for structures under us: left to apply
	Bool directionOK = TRUE;
	if( r.m_hasDied == FALSE )
	{
		if( modData->m_onlyWhenMovingDown && pos->z >= r.m_lastPosition.z )
			directionOK = FALSE;
		const Real terrainHeightAtPos = TheTerrainLogic->getGroundHeight( pos->x, pos->y );
		if( pos->z < terrainHeightAtPos + modData->m_targetHeightAboveTerrain && directionOK )
			return FALSE;	// dies
	}
	if( r.m_particlesDestroyed == FALSE && pos->z < modData->m_destroyAttachedParticlesAtHeight && (r.m_hasDied || directionOK) )
		return FALSE;	// destroys its particles
	r.m_lastPosition = *pos;
	return TRUE;
}

void HeightDieUpdate::gxKernel( UpdateModulePtr *mods, Int begin, Int end )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	for (Int k = begin; k < end; ++k)
	{
		HeightDieUpdate *m = static_cast<HeightDieUpdate *>(mods[k]);
		Row &r = m->D();
		r.gxKernelFrame = now;
		r.gxPrevLast = r.m_lastPosition;
		r.gxPos = *m->getObject()->getPosition();
		r.gxContained = m->getObject()->getContainedBy() != nullptr;
		r.gxFull = !m->gxQuiet(now);
	}
}

void HeightDieUpdate::gxApply( UpdateModulePtr *mods, Int count )
{
	for (Int k = 0; k < count; ++k)
	{
		HeightDieUpdate *m = static_cast<HeightDieUpdate *>(mods[k]);
		Row &r = m->D();
		if (!r.gxFull)
		{	// still what the kernel saw? (an earlier unit's death can move or release this one)
			const Coord3D *pos = m->getObject()->getPosition();
			if (memcmp(pos, &r.gxPos, sizeof(Coord3D)) == 0 && (m->getObject()->getContainedBy() != nullptr) == r.gxContained)
				continue;
			r.m_lastPosition = r.gxPrevLast;	// redo this frame as the full update would have
			gxCount(GXC_HEIGHTDIE_REDONE);
		}
		m->gxFullUpdate();
	}
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime HeightDieUpdate::gxFullUpdate()
{
	UnsignedInt now = TheGameLogic->getFrame();
	if( D().m_earliestDeathFrame == UINT_MAX )
		D().m_earliestDeathFrame = now + getHeightDieUpdateModuleData()->m_initialDelay;

	// If at least a one frame delay has been set, then stop for a while
	if( D().m_earliestDeathFrame > now )
		return UPDATE_SLEEP_NONE;

	// do nothing if we're contained within other objects ... like a transport
	if( getObject()->getContainedBy() != nullptr )
	{

		// keep track of our last position even though we're not doing anything yet
		D().m_lastPosition = *getObject()->getPosition();

		// get outta here
		return UPDATE_SLEEP_NONE;

	}

	// get the module data
	const HeightDieUpdateModuleData *modData = getHeightDieUpdateModuleData();

	// get our current position
	const Coord3D *pos = getObject()->getPosition();

	Bool directionOK = TRUE;
	if( D().m_hasDied == FALSE )
	{

		if( modData->m_onlyWhenMovingDown )
		{

			if( pos->z >= D().m_lastPosition.z )
				directionOK = FALSE;

		}

		// get the terrain height
		Real terrainHeightAtPos = TheTerrainLogic->getGroundHeight( pos->x, pos->y );

		// if including structures, check for bridges
		if (modData->m_targetHeightIncludesStructures)
		{
			PathfindLayerEnum layer = TheTerrainLogic->getHighestLayerForDestination(pos);
			if (layer != LAYER_GROUND)
			{
				Real layerHeight = TheTerrainLogic->getLayerHeight(pos->x, pos->y, layer);
				if (layerHeight > terrainHeightAtPos)
					terrainHeightAtPos = layerHeight;
			}
		}

		//
		// our target height to die at is by default the height specified in the INI entry above
		// the terrain ... we may change our target height if we care about dying above
		// objects under us (see below)
		//
		Real targetHeight = terrainHeightAtPos + modData->m_targetHeightAboveTerrain;

		//
		// if we consider objects under us ... we will die when we are the specified distance above
		// those objects
		//
		if( modData->m_targetHeightIncludesStructures == TRUE )
		{

			// scan all objects in the radius of our extent and find the tallest height among them
			PartitionFilterAcceptByKindOf filter1( MAKE_KINDOF_MASK( KINDOF_STRUCTURE ),KINDOFMASK_NONE );
			PartitionFilter *filters[] = { &filter1, nullptr };
			Real range = getObject()->getBoundingCircleRadius();
			ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( getObject(),
																																				 range,
																																				 FROM_BOUNDINGSPHERE_3D,
																																				 filters );
			MemoryPoolObjectHolder hold( iter );
			Object *obj;

			Real tallestHeight = 0.0f;
			Real thisHeight;
			for( obj = iter->first(); obj; obj = iter->next() )
			{

				// ignore ourselves
				if( obj == getObject() )
					continue;

				// store the height of the tallest object under us
				thisHeight = obj->getGeometryInfo().getMaxHeightAbovePosition();

				if( thisHeight > tallestHeight )
					tallestHeight = thisHeight;

			}

			//
			// our target height is either the height above the terrain as specified by the INI
			// entry for the object that has this update ... or it is the building height of the
			// tallest thing under us
			//
			if( tallestHeight > modData->m_targetHeightAboveTerrain )
				targetHeight = tallestHeight + terrainHeightAtPos;

		}

		// if we are below the target height ... DIE!
		if( pos->z < targetHeight && directionOK )
		{

			// if we're supposed to snap us to the ground on death do so
			// AND: even if we're not snapping to ground, be sure we don't go BELOW ground
			if( modData->m_snapToGroundOnDeath || pos->z < terrainHeightAtPos )
			{
				Coord3D ground;

				ground.x = pos->x;
				ground.y = pos->y;
				ground.z = terrainHeightAtPos;
				getObject()->setPosition( &ground );

			}

			// kill the object
			getObject()->kill();

			// we have died ... don't do this again
			D().m_hasDied = TRUE;

		}

	}

	//
	// if our height is below the destroy attached particles height above the terrain, clean
	// them up from the particle system
	//
	if( D().m_particlesDestroyed == FALSE && pos->z < modData->m_destroyAttachedParticlesAtHeight && (D().m_hasDied || directionOK) )
	{

		// destroy them
		TheParticleSystemManager->destroyAttachedSystems( getObject() );

		// don't do this again
		D().m_particlesDestroyed = TRUE;

	}

	// save our current position as the last position we monitored
	D().m_lastPosition = *pos;

	return UPDATE_SLEEP_NONE;

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void HeightDieUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: D().m_earliestDeathFrame
*/
// ------------------------------------------------------------------------------------------------
void HeightDieUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// has died
	xfer->xferBool( &D().m_hasDied );

	// particles destroyed
	xfer->xferBool( &D().m_particlesDestroyed );

	// last position
	xfer->xferCoord3D( &D().m_lastPosition );

	if( version >= 2 )
		xfer->xferUnsignedInt( &D().m_earliestDeathFrame );
	else
		D().m_earliestDeathFrame = 0;

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void HeightDieUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
