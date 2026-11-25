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

// FILE: ProjectileStreamUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, May 2002
// Desc:   Tracks all projectiles fired so they can be drawn as a stream
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ProjectileStreamUpdate.h"
#include "WWMath/vector3.h"



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ProjectileStreamUpdate::ProjectileStreamUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{

	std::fill(D().m_projectileIDs, D().m_projectileIDs + ARRAY_SIZE(D().m_projectileIDs), INVALID_ID);
	D().m_nextFreeIndex = 0;
	D().m_firstValidIndex = 0;
	D().m_owningObject = INVALID_ID;

	D().m_targetObject = INVALID_ID;
	D().m_targetPosition.zero();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ProjectileStreamUpdate::~ProjectileStreamUpdate()
{

}

//-------------------------------------------------------------------------------------------------
/** The update callback. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime ProjectileStreamUpdate::update()
{
	cullFrontOfList();

	// Update the draw module about our points (arrange array so they can read?)

	if( considerDying() )
		TheGameLogic->destroyObject( getObject() );

	return UPDATE_SLEEP_NONE;
}

void ProjectileStreamUpdate::addProjectile( ObjectID sourceID, ObjectID newID, ObjectID victimID, const Coord3D *victimPos )
{
	DEBUG_ASSERTCRASH( D().m_owningObject == INVALID_ID  ||  D().m_owningObject == sourceID, ("Two objects are trying to use the same Projectile stream.") );//Don't cross the streams!
	if( D().m_owningObject == INVALID_ID )
		D().m_owningObject = sourceID;

	// Keep track of target, and insert a hole if the target has changed
	if( victimID != INVALID_ID )
	{
		// Object shot
		if( victimID != D().m_targetObject )
		{
			// Changed targets, insert a hole to break the stream
			D().m_projectileIDs[ D().m_nextFreeIndex ] = INVALID_ID;
			D().m_nextFreeIndex = (D().m_nextFreeIndex + 1) % MAX_PROJECTILE_STREAM;

			// And mark this as our new target
			D().m_targetObject = victimID;
		}

		// Clear position so we know we are an object shot
		D().m_targetPosition.zero();
	}
	else if( victimPos != nullptr )
	{
		if( ! (D().m_targetPosition == (*victimPos)) )
		{
			// New position, so insert hole
			D().m_projectileIDs[ D().m_nextFreeIndex ] = INVALID_ID;
			D().m_nextFreeIndex = (D().m_nextFreeIndex + 1) % MAX_PROJECTILE_STREAM;

			// And mark this as our new target
			D().m_targetPosition = (*victimPos);
		}

		// Clear object so we know we are a position shot
		D().m_targetObject = INVALID_ID;
	}
	else
	{
		DEBUG_CRASH(("A projectile stream was fired at neither an object nor a position.  Probably bad."));
	}

	// Keep track of the id in a circular array
	D().m_projectileIDs[ D().m_nextFreeIndex ] = newID;
	D().m_nextFreeIndex = (D().m_nextFreeIndex + 1) % MAX_PROJECTILE_STREAM;
	DEBUG_ASSERTCRASH( D().m_nextFreeIndex != D().m_firstValidIndex, ("Need to increase the allowed number of simultaneous particles in ProjectileStreamUpdate.") );
}

void ProjectileStreamUpdate::cullFrontOfList()
{
	while( (D().m_firstValidIndex != D().m_nextFreeIndex)  &&  (TheGameLogic->findObjectByID( D().m_projectileIDs[D().m_firstValidIndex] ) == nullptr) )
	{
		// Chew off the front if they are gone.  Don't chew on the middle, as bad ones there are just a break in the chain
		D().m_firstValidIndex = (D().m_firstValidIndex + 1) % MAX_PROJECTILE_STREAM;
	}
}

Bool ProjectileStreamUpdate::considerDying()
{
	if( D().m_firstValidIndex == D().m_nextFreeIndex  &&  D().m_owningObject != INVALID_ID )
	{
		//If I have no projectiles to watch, and my master is dead, then yes, I want to die
		if( TheGameLogic->findObjectByID(D().m_owningObject) == nullptr )
			return TRUE;
	}

	return FALSE;
}

void ProjectileStreamUpdate::getAllPoints( Vector3 *points, Int *count )
{
	Int pointCount = 0;
	Int pointIndex = D().m_firstValidIndex;


	Object *obj = TheGameLogic->findObjectByID(D().m_owningObject);


	while( pointIndex != D().m_nextFreeIndex )
	{
		// Go through the array I think of as good.  Holes in the middle get 0,0,0.  I write
		// to pointCount because I am unrolling a circular array into the same sized flat one
		// since I am writing anyway.
		Object *projectile = TheGameLogic->findObjectByID( D().m_projectileIDs[pointIndex] );

		if( projectile )
		{
			Coord3D thisPoint = *projectile->getPosition();
			points[pointCount].X = thisPoint.x;
			points[pointCount].Y = thisPoint.y;
			points[pointCount].Z = thisPoint.z;


			if ( obj && obj->isKindOf( KINDOF_VEHICLE ) )				// this makes the stream skim along my roof, if I have a roof
			{
				const Coord3D *pos = obj->getPosition();
				Real myTop = obj->getGeometryInfo().getMaxHeightAbovePosition() + pos->z + 0.5f;
				Coord3D delta;
				delta.x = pos->x - points[pointCount].X;
				delta.y = pos->y - points[pointCount].Y;
				delta.z = 0.0f;
				if( delta.length() <= obj->getGeometryInfo().getMajorRadius() * 1.5f )
					points[pointCount].Z = MAX( points[pointCount].Z, myTop );
			}




		}
		else
		{
			points[pointCount].X = 0;
			points[pointCount].Y = 0;
			points[pointCount].Z = 0;
		}

		pointIndex = (pointIndex + 1) % MAX_PROJECTILE_STREAM;
		pointCount++;
	}

	*count = pointCount;
}

void ProjectileStreamUpdate::setPosition( const Coord3D *newPosition )
{
	Object *me = getObject();
	me->setPosition( newPosition );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ProjectileStreamUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Target tracking for line breaking
*/
// ------------------------------------------------------------------------------------------------
void ProjectileStreamUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// projectile ids
	xfer->xferUser( D().m_projectileIDs, sizeof( ObjectID ) * MAX_PROJECTILE_STREAM );

	// next free index
	xfer->xferInt( &D().m_nextFreeIndex );

	// first valid index
	xfer->xferInt( &D().m_firstValidIndex );

	// owning object
	xfer->xferObjectID( &D().m_owningObject );

	if( version >= 2 )
	{
		xfer->xferObjectID( &D().m_targetObject );
		xfer->xferCoord3D( &D().m_targetPosition );
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ProjectileStreamUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
