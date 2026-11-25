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

// FILE: DockUpdate.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood Feb 2002
// Desc:   Behavior common to all DockUpdates is here.  Everything but action()
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "GameLogic/Module/GxPathsKernels.h"
#include "Common/Debug.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/DockUpdate.h"

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
DockUpdateModuleData::DockUpdateModuleData()
{
	m_numberApproachPositionsData = 0;
	m_isAllowPassthrough = TRUE;
}

/*static*/ void DockUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{

	UpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "NumberApproachPositions"	,INI::parseInt,		nullptr, offsetof( DockUpdateModuleData, m_numberApproachPositionsData ) },
		{ "AllowsPassthrough"				,INI::parseBool,	nullptr, offsetof( DockUpdateModuleData, m_isAllowPassthrough ) },
		{ nullptr, nullptr, nullptr, 0 }

	};

  p.add(dataFieldParse);

}

DockUpdate::DockUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_gxRow = GxComponentPool<GxState>::the().alloc();

	R().dockOpen = TRUE;

	R().positionsLoaded = FALSE;
	R().numberApproachPositionBones = -1;
	R().activeDocker = INVALID_ID;
	R().dockerInside = FALSE;
	R().dockCrippled = FALSE;

	const DockUpdateModuleData *md = (const DockUpdateModuleData *)moduleData;

	R().exitPosition.zero();
	R().dockPosition.zero();
	R().enterPosition.zero();

	R().numberApproachPositions = md->m_numberApproachPositionsData;
	if( R().numberApproachPositions != DYNAMIC_APPROACH_VECTOR_FLAG )
	{
		// Not dynamic, so make this the size
		R().approachPositions.resize(R().numberApproachPositions);
		R().approachPositionOwners.resize(R().numberApproachPositions);
		R().approachPositionReached.resize(R().numberApproachPositions);
	}
	else
	{
		//Otherwise, make a default size, and plan on growing it later
		R().approachPositions.resize(DEFAULT_APPROACH_VECTOR_SIZE);
		R().approachPositionOwners.resize(DEFAULT_APPROACH_VECTOR_SIZE);
		R().approachPositionReached.resize(DEFAULT_APPROACH_VECTOR_SIZE);
	}

	for( size_t vectorIndex = 0; vectorIndex < R().approachPositions.size(); ++vectorIndex )
	{
		// Whatever size we are, init everything.
		R().approachPositions[vectorIndex].zero();
		R().approachPositionOwners[vectorIndex] = INVALID_ID;
		R().approachPositionReached[vectorIndex] = FALSE;
	}
}

DockUpdate::~DockUpdate()
{
	GxModuleList<DockUpdate>::leave(this, R().gxId);
	GxComponentPool<GxState>::the().release(m_gxRow);
}

Bool DockUpdate::isClearToApproach( Object const* docker ) const
{
	// If we allow infinite approaches, we don't even need to look up.  Just say yes.
	// The reserve code will handle appending a free spot to the end.
	if( R().numberApproachPositions == DYNAMIC_APPROACH_VECTOR_FLAG )
		return TRUE;

	ObjectID dockerID = docker->getID();

	for( size_t positionIndex = 0; positionIndex < R().approachPositionOwners.size(); ++positionIndex )
	{
		if( R().approachPositionOwners[positionIndex] == INVALID_ID )
		{
			return TRUE;
		}
		if( R().approachPositionOwners[positionIndex] == dockerID )
		{
			return TRUE;
		}
	}

	return FALSE;
}

Bool DockUpdate::reserveApproachPosition( Object* docker, Coord3D *position, Int *index )
{

	// load dock positions if not loaded yet
	if( R().positionsLoaded == FALSE )
		loadDockPositions();

	// sanity
	if( position == nullptr )
		return FALSE;

	ObjectID dockerID = docker->getID();

	Int positionIndex = 0;
	for( ; positionIndex < R().approachPositionOwners.size(); ++positionIndex )
	{
		if( R().approachPositionOwners[positionIndex] == dockerID )
		{
			*position = computeApproachPosition( positionIndex, docker );
			*index = positionIndex;
			return TRUE;
		}
		if( R().approachPositionOwners[positionIndex] == INVALID_ID )
		{
			R().approachPositionOwners[positionIndex] = dockerID;
			*position = computeApproachPosition( positionIndex, docker );
			*index = positionIndex;
			return TRUE;
		}
	}

	// If I make it out of the loop, I am full, so dynamic approach buildings should make a new entry instead of saying no
	if( R().numberApproachPositions == DYNAMIC_APPROACH_VECTOR_FLAG )
	{
		Coord3D zero;
		zero.zero();
		R().approachPositions.push_back( zero );
		R().approachPositionOwners.push_back( INVALID_ID );
		R().approachPositionReached.push_back( FALSE );

		loadDockPositions();// refresh this new one

		positionIndex = R().approachPositionOwners.size() - 1;// The new last spot
		R().approachPositionOwners[positionIndex] = dockerID;
		*position = computeApproachPosition( positionIndex, docker );
		*index = positionIndex;
		return TRUE;
	}

	return FALSE;
}

Bool DockUpdate::advanceApproachPosition( Object* docker, Coord3D *position, Int *index )
{
	// load dock positions if not loaded yet
	if( R().positionsLoaded == FALSE )
		loadDockPositions();

	// sanity
	if( position == nullptr )
		return FALSE;
	if( *index <= 0 )
		return FALSE;
	if( R().approachPositionOwners[(*index) - 1] != INVALID_ID )
		return FALSE;

	Int hisIndex = *index;
	Int previousIndex = hisIndex - 1;

	R().approachPositionOwners[previousIndex] = docker->getID();
	R().approachPositionReached[previousIndex] = FALSE;

	R().approachPositionOwners[hisIndex] = INVALID_ID;
	R().approachPositionReached[hisIndex] = FALSE;

	*position = computeApproachPosition( previousIndex, docker );
	*index = previousIndex;
	return TRUE;
}

Bool DockUpdate::isClearToEnter( Object const* docker ) const
{
	ObjectID dockerID = docker->getID();
	return dockerID == R().activeDocker;
}

Bool DockUpdate::isClearToAdvance( Object const* docker, Int dockerIndex ) const
{
	if( dockerIndex < 0 )
		return FALSE;

	ObjectID dockerID = docker->getID();
	Bool correctRequest = dockerID == R().approachPositionOwners[dockerIndex];
	Bool approachReached = R().approachPositionReached[dockerIndex];
	Bool nextSpotFree = (dockerIndex > 0)  &&  (R().approachPositionOwners[dockerIndex - 1] == INVALID_ID);

	return correctRequest && approachReached && nextSpotFree;
}

void DockUpdate::getEnterPosition( Object* docker, Coord3D *position )
{

	// load dock positions if not loaded yet
	if( R().positionsLoaded == FALSE )
		loadDockPositions();

	// sanity
	if( position == nullptr )
		return;

	// If I don't have a bone, you are fine where you are, unless you fly, in which case I should recenter you
	Coord3D zero;
	zero.zero();
	if( R().enterPosition == zero )
	{
		if( docker->isUsingAirborneLocomotor() )
		{
			*position = *getObject()->getPosition();
			return;
		}
		*position = *docker->getPosition();
		return;
	}

	// take local space position and convert to world space
	getObject()->convertBonePosToWorldPos( &R().enterPosition, nullptr, position, nullptr );

}

void DockUpdate::getDockPosition( Object* docker, Coord3D *position )
{

	// load dock positions if not loaded yet
	if( R().positionsLoaded == FALSE )
		loadDockPositions();

	// sanity
	if( position == nullptr )
		return;

	// If I don't have a bone, you are fine where you are.
	Coord3D zero;
	zero.zero();
	if( R().enterPosition == zero )
	{
		*position = *docker->getPosition();
		return;
	}

	// take local space position and convert to world space
	getObject()->convertBonePosToWorldPos( &R().dockPosition, nullptr, position, nullptr );

}

void DockUpdate::getExitPosition( Object* docker, Coord3D *position )
{

	// load dock positions if not loaded yet
	if( R().positionsLoaded == FALSE )
		loadDockPositions();

	// sanity
	if( position == nullptr )
		return;

	// If I don't have a bone, you are fine where you are.
	Coord3D zero;
	zero.zero();
	if( R().enterPosition == zero )
	{
		*position = *docker->getPosition();
		return;
	}

	// take local space position and convert to world space
	getObject()->convertBonePosToWorldPos( &R().exitPosition, nullptr, position, nullptr );

}

void DockUpdate::onApproachReached( Object* docker )
{
	ObjectID dockerID = docker->getID();
	for( size_t positionIndex = 0; positionIndex < R().approachPositionOwners.size(); ++positionIndex )
	{
		if( R().approachPositionOwners[positionIndex] == dockerID )
		{
			R().approachPositionReached[positionIndex] = TRUE;
			return;
		}
	}
}

void DockUpdate::onEnterReached( Object* docker )
{
	Object *me = getObject();
	me->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK(MODELCONDITION_DOCKING_ENDING),
																			MAKE_MODELCONDITION_MASK2(MODELCONDITION_DOCKING_BEGINNING, MODELCONDITION_DOCKING) );
	docker->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK(MODELCONDITION_DOCKING_ENDING),
																					MAKE_MODELCONDITION_MASK2(MODELCONDITION_DOCKING_BEGINNING, MODELCONDITION_DOCKING) );
	R().dockerInside = TRUE;

	ObjectID dockerID = docker->getID();
	for( size_t positionIndex = 0; positionIndex < R().approachPositionOwners.size(); ++positionIndex )
	{
		if( R().approachPositionOwners[positionIndex] == dockerID )
		{
			R().approachPositionOwners[positionIndex] = INVALID_ID;
			R().approachPositionReached[positionIndex] = FALSE;
			return;
		}
	}
}

void DockUpdate::onDockReached( Object* docker )
{
	Object *me = getObject();
	me->clearAndSetModelConditionState( MODELCONDITION_DOCKING_BEGINNING, MODELCONDITION_DOCKING_ACTIVE );
	docker->clearAndSetModelConditionState( MODELCONDITION_DOCKING_BEGINNING, MODELCONDITION_DOCKING_ACTIVE );
}

void DockUpdate::onExitReached( Object* docker )
{
	Object *me = getObject();
	me->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK2(MODELCONDITION_DOCKING_ACTIVE, MODELCONDITION_DOCKING),
																			MAKE_MODELCONDITION_MASK(MODELCONDITION_DOCKING_ENDING) );
	docker->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK2(MODELCONDITION_DOCKING_ACTIVE, MODELCONDITION_DOCKING),
																					MAKE_MODELCONDITION_MASK(MODELCONDITION_DOCKING_ENDING) );
	R().dockerInside = FALSE;

	ObjectID dockerID = docker->getID();
	if( dockerID == R().activeDocker )
		R().activeDocker = INVALID_ID;
	else
	{

		//
		// we only assert here if the dock is open, for closed docks it's OK to allow somebody
		// to continue moving to the exit position cause they are leaving after all
		//
		if( isDockOpen() )
			DEBUG_CRASH( ("Fiddle.  Someone said goodbye to a dock when the dock didn't think it was talking to that someone."));

	}
}

void DockUpdate::cancelDock( Object* docker )
{
	ObjectID dockerID = docker->getID();
	for( size_t positionIndex = 0; positionIndex < R().approachPositionOwners.size(); ++positionIndex )
	{
		if( R().approachPositionOwners[positionIndex] == dockerID )
		{
			R().approachPositionOwners[positionIndex] = INVALID_ID;
			R().approachPositionReached[positionIndex] = FALSE;
		}
	}
	if( R().activeDocker == dockerID )
	{
		Object *dockingObject = TheGameLogic->findObjectByID(R().activeDocker);
		R().activeDocker = INVALID_ID;
		R().dockerInside = FALSE;
		// clear any model conditions related to docking that may be set on us and them.
		// (Normal clear is part of each stage, but we won't get there.)
		ModelConditionFlags clear;
		clear.set( MODELCONDITION_DOCKING_ENDING );
		clear.set( MODELCONDITION_DOCKING_BEGINNING );
		clear.set( MODELCONDITION_DOCKING_ACTIVE );
		clear.set( MODELCONDITION_DOCKING );
		getObject()->clearModelConditionFlags( clear );
		if( dockingObject )
			dockingObject->clearModelConditionFlags( clear );
	}
}

void DockUpdate::setDockCrippled( Bool setting )
{
	// At this level, Crippling means I will accept Approach requests, but I will never grant Enter clearance.
	R().dockCrippled = setting;
}

// web port (round 5 addendum): the dock kernel. Pure: an open, uncrippled dock lets in the first
// docker that reached its approach position. reads/writes its row (activeDocker,
// approachPositionReached, approachPositionOwners) only. Apply (in update): a worker docker's
// model condition.
static void gxDockKernel(DockUpdate::GxState &r)
{
	for( size_t positionIndex = 0; positionIndex < r.approachPositionReached.size(); ++positionIndex )
	{
		if( r.approachPositionReached[positionIndex] )
		{
			r.activeDocker = r.approachPositionOwners[positionIndex];
			return;
		}
	}
}

// web port (round 5): the range kernel through ai's hook, at the start of this module's phase
GX_PATHS_KERNEL(DockUpdate, GXB_PK_DOCK, PHASE_NORMAL)
void DockUpdate::gxKernelStep(UnsignedInt now)
{
	// reads/writes: its row only (entrance permission); op 1 = the open-dock branch was taken
	GX_PK_WILL_UPDATE
	R().gxOp = 0;
	if( R().activeDocker == INVALID_ID  &&  !R().dockCrippled )
	{
		gxDockKernel(R());
		R().gxOp = 1;
	}
	R().gxOpFrame = now + 1;
}

UpdateSleepTime DockUpdate::update()
{
	GX_PK_JOIN(DockUpdate, GXB_PK_DOCK)
	const Bool kernelRan = GX_PK_HAVE(TheGameLogic->getFrame());
	if( kernelRan ? R().gxOp == 1 : ( R().activeDocker == INVALID_ID  &&  !R().dockCrippled ) )
	{
		if( !kernelRan )
			gxDockKernel(R());
	}
	else if ( getObject()->isKindOf( KINDOF_SUPPLY_SOURCE ) )
	{
		Object *docker = TheGameLogic->findObjectByID( R().activeDocker );
		if ( docker && docker->isKindOf( KINDOF_DOZER ) && docker->isKindOf( KINDOF_HARVESTER ))// a worker
		{
			ModelConditionFlags test;
			test.set( MODELCONDITION_DOCKING_BEGINNING );
			Drawable *dockerDraw = docker->getDrawable();
			if ( dockerDraw && dockerDraw->getModelConditionFlags().anyIntersectionWith( test ) )
				dockerDraw->clearModelConditionFlags( MAKE_MODELCONDITION_MASK(MODELCONDITION_MOVING) );
		}
	}


	return UPDATE_SLEEP_NONE;
}

Coord3D DockUpdate::computeApproachPosition( Int positionIndex, Object *forWhom )
{
	// load dock positions if not loaded yet
	if( R().positionsLoaded == FALSE )
		loadDockPositions();

	Coord3D bestPosition;// This answer is the best, as it includes findPositionAround
	Coord3D workingPosition;// But if findPositionAround fails, we need to say something.

	FindPositionOptions fpOptions;
	// Start with the pristine bone, then convert it to the world, then find a clean spot around it.

	Object *us = getObject();
	us->convertBonePosToWorldPos( &R().approachPositions[positionIndex], nullptr, &workingPosition, nullptr );

	if( R().numberApproachPositionBones == 0 )
	{
		Coord3D ourPosition = *us->getPosition();
		Coord3D theirPosition = *forWhom->getPosition();
		// A Boneless building wants to bias towards the caller for the arbitrary position
		Vector3 offset( theirPosition.x - ourPosition.x, theirPosition.y - ourPosition.y, theirPosition.z - ourPosition.z );
		offset.Normalize();
		offset = offset * (us->getGeometryInfo().getMajorRadius() / 2);

		workingPosition.x += offset.X;
		workingPosition.y += offset.Y;
		workingPosition.z += offset.Z;
	}

	fpOptions.minRadius = 0.0f;
	fpOptions.maxRadius = 100.0f;
	fpOptions.sourceToPathToDest = forWhom;// This makes it find a place forWhom can get to.
	if( forWhom->isUsingAirborneLocomotor() )
		fpOptions.ignoreObject = getObject();// Flyers can ignore us, so they can approach right over us if they want.

	Bool spotFound = ThePartitionManager->findPositionAround( &workingPosition, &fpOptions, &bestPosition );

	if( spotFound)
		return bestPosition;

	return workingPosition;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void DockUpdate::loadDockPositions()
{
	Object *obj = getObject();
	Drawable *myDrawable = obj->getDrawable();

	if (myDrawable)
	{
		//Patch 1.03 - Kris - Jan 19, 2005
		//Some of the GLASupplyStash assets still have docking bones in them. When found, the docking positions must be
		//observed. This occurs when upgrading to fortified structures which still have the bones. This has the negative
		//side-effect of workers suddenly slowing down their gathering rate. The proper fix would be to remove the bones
		//from the assets, but the artists could not find the original max files, hence the code solution.
		if( !obj->isKindOf( KINDOF_IGNORE_DOCKING_BONES ) )
		{

			myDrawable->getPristineBonePositions( "DockStart", 0, &R().enterPosition, nullptr, 1);
			myDrawable->getPristineBonePositions( "DockAction", 0, &R().dockPosition, nullptr, 1);
			myDrawable->getPristineBonePositions( "DockEnd", 0, &R().exitPosition, nullptr, 1);
			if( R().numberApproachPositions != DYNAMIC_APPROACH_VECTOR_FLAG )
			{
				// Dynamic means no bones

				// TheSuperHackers @logic-client-separation helmutbuhler 11/04/2025
				// We shouldn't depend on bones of a drawable here!

				// TheSuperHackers @fix helmutbuhler 19/04/2025 Zero initialize array to prevent uninitialized memory reads.
				// Important: the entire target vector is used for serialization and crc and must not contain random data.
				Coord3D approachBones[DEFAULT_APPROACH_VECTOR_SIZE] = {0};
				R().numberApproachPositionBones = myDrawable->getPristineBonePositions( "DockWaiting", 1, approachBones, nullptr, R().numberApproachPositions);
				if( R().numberApproachPositions == R().approachPositions.size() )//safeguard: will always be true
				{
					for( Int copyIndex = 0; copyIndex < R().numberApproachPositions; ++copyIndex )
					{
						R().approachPositions[copyIndex] = approachBones[copyIndex];
					}
				}
			}
			else
				R().numberApproachPositionBones = 0;

			R().positionsLoaded = TRUE;
		}
		else
		{
			R().numberApproachPositionBones = 0;
			R().positionsLoaded = TRUE;
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool DockUpdate::isAllowPassthroughType()
{
	return getDockUpdateModuleData()->m_isAllowPassthrough;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DockUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void DockUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// call base class
	UpdateModule::xfer( xfer );

	// enter position
	xfer->xferCoord3D( &R().enterPosition );

	// dock position
	xfer->xferCoord3D( &R().dockPosition );

	// exit position
	xfer->xferCoord3D( &R().exitPosition );

	// # approach positions
	xfer->xferInt( &R().numberApproachPositions );

	// positions loaded
	xfer->xferBool( &R().positionsLoaded );

	// approach positions
	Int vectorSize = R().approachPositions.size();
	xfer->xferInt( &vectorSize );
	R().approachPositions.resize(vectorSize);
	Int vectorIndex = 0;
	for( ; vectorIndex < vectorSize; ++vectorIndex )
	{
		// Okay, this is cool.  On save, the size and a bunch of coords will be written.
		// on load, vectorSize will be at 0 from the .size, but will then get set
		// by the xfer, and properly control the number of Coords.
		xfer->xferCoord3D( &R().approachPositions[vectorIndex] );
	}

	// approach position owners
	vectorSize = R().approachPositionOwners.size();
	xfer->xferInt( &vectorSize );
	R().approachPositionOwners.resize(vectorSize);
	for( vectorIndex = 0; vectorIndex < vectorSize; ++vectorIndex )
	{
		xfer->xferObjectID( &R().approachPositionOwners[vectorIndex] );
	}

	// approach positions reached
	vectorSize = R().approachPositionReached.size();
	xfer->xferInt( &vectorSize );
	R().approachPositionReached.resize(vectorSize);
	for( vectorIndex = 0; vectorIndex < vectorSize; ++vectorIndex )
	{
		// Vector of Bool gets packed as bitfield internally
		Bool unpack = R().approachPositionReached[vectorIndex];
		xfer->xferBool( &unpack );
	}

	// active docker
	xfer->xferObjectID( &R().activeDocker );

	// docker inside
	xfer->xferBool( &R().dockerInside );

	// docker crippled
	xfer->xferBool( &R().dockCrippled );

	// dock open
	xfer->xferBool( &R().dockOpen );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DockUpdate::loadPostProcess()
{

	// call base class
	UpdateModule::loadPostProcess();

}

