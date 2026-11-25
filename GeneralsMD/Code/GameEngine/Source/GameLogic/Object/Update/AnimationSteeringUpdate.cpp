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

// FILE: AnimationSteeringUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Author: Kris Morness, May 2003
// Desc:   Uses animation states to handle steering.
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "GameLogic/Module/GxPathsKernels.h"

#include "Common/ModelState.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AnimationSteeringUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AnimationSteeringUpdateModuleData::AnimationSteeringUpdateModuleData()
{
	m_transitionFrames = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AnimationSteeringUpdate::AnimationSteeringUpdate( Thing *thing, const ModuleData *moduleData )
												: UpdateModule( thing, moduleData )
{
	m_gxRow = GxComponentPool<GxState>::the().alloc();
	R().currentTurnAnim = MODELCONDITION_INVALID;
	R().nextTransitionFrame = 0;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AnimationSteeringUpdate::~AnimationSteeringUpdate()
{
	GxComponentPool<GxState>::the().release(m_gxRow);

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// web port (round 5 addendum): kernel + apply. The kernel is pure: it moves this unit's turn
// animation state on and says which model-condition change the apply step makes.
// reads: its row (currentTurnAnim, nextTransitionFrame), its physics turning, the frame, the
//        module data's transition frames; writes: its row only. No allocation, events, RNG, globals.
enum GxSteerOp { GXSTEER_NONE, GXSTEER_TO_RIGHT, GXSTEER_TO_LEFT, GXSTEER_RIGHT_BACK, GXSTEER_LEFT_BACK, GXSTEER_DONE };

static GxSteerOp gxSteerKernel(AnimationSteeringUpdate::GxState &r, PhysicsTurningType currentTurn, UnsignedInt now, UnsignedInt transitionFrames)
{
	if (now < r.nextTransitionFrame)
		return GXSTEER_NONE;
	switch (r.currentTurnAnim)
	{
		case MODELCONDITION_INVALID:	// going straight: start a turn?
			if (currentTurn == TURN_NEGATIVE)
			{
				r.nextTransitionFrame = now + transitionFrames;
				r.currentTurnAnim = MODELCONDITION_CENTER_TO_RIGHT;
				return GXSTEER_TO_RIGHT;
			}
			if (currentTurn == TURN_POSITIVE)
			{
				r.nextTransitionFrame = now + transitionFrames;
				r.currentTurnAnim = MODELCONDITION_CENTER_TO_LEFT;
				return GXSTEER_TO_LEFT;
			}
			break;
		case MODELCONDITION_CENTER_TO_RIGHT:	// turning right: recenter unless still turning
			if (currentTurn != TURN_NEGATIVE)
			{
				r.nextTransitionFrame = now + transitionFrames;
				r.currentTurnAnim = MODELCONDITION_RIGHT_TO_CENTER;
				return GXSTEER_RIGHT_BACK;
			}
			break;
		case MODELCONDITION_CENTER_TO_LEFT:
			if (currentTurn != TURN_POSITIVE)
			{
				r.nextTransitionFrame = now + transitionFrames;
				r.currentTurnAnim = MODELCONDITION_LEFT_TO_CENTER;
				return GXSTEER_LEFT_BACK;
			}
			break;
		case MODELCONDITION_LEFT_TO_CENTER:
		case MODELCONDITION_RIGHT_TO_CENTER:	// finish the turn
			if (currentTurn == TURN_NONE)
			{
				r.nextTransitionFrame = now;
				r.currentTurnAnim = MODELCONDITION_INVALID;
				return GXSTEER_DONE;
			}
			break;
	}
	return GXSTEER_NONE;
}

// apply (main thread): the drawable's model condition
static void gxSteerApply(Drawable *draw, GxSteerOp op)
{
	switch (op)
	{
		case GXSTEER_TO_RIGHT:   draw->setModelConditionState( MODELCONDITION_CENTER_TO_RIGHT ); break;
		case GXSTEER_TO_LEFT:    draw->setModelConditionState( MODELCONDITION_CENTER_TO_LEFT ); break;
		case GXSTEER_RIGHT_BACK: draw->clearAndSetModelConditionState( MODELCONDITION_CENTER_TO_RIGHT, MODELCONDITION_RIGHT_TO_CENTER ); break;
		case GXSTEER_LEFT_BACK:  draw->clearAndSetModelConditionState( MODELCONDITION_CENTER_TO_LEFT, MODELCONDITION_LEFT_TO_CENTER ); break;
		case GXSTEER_DONE:       draw->clearModelConditionFlags( MAKE_MODELCONDITION_MASK2( MODELCONDITION_LEFT_TO_CENTER, MODELCONDITION_RIGHT_TO_CENTER ) ); break;
		default: break;
	}
}

// web port (round 5): the range kernel through ai's hook, at the start of this module's phase
void AnimationSteeringUpdate::gxKernelStep(UnsignedInt now)
{
	// reads: its row, its physics turning, the frame, the module data; writes: its row (state + op)
	GX_PK_WILL_UPDATE
	PhysicsBehavior *physics = getObject()->getPhysics();
	if (!physics || !getObject()->getDrawable())
		return;
	R().gxOp = gxSteerKernel(R(), physics->getTurning(), now, getAnimationSteeringUpdateModuleData()->m_transitionFrames);
	R().gxOpFrame = now + 1;
}

UpdateSleepTime AnimationSteeringUpdate::update()
{
	GX_PK_JOIN(AnimationSteeringUpdate)
	PhysicsBehavior *physics = getObject()->getPhysics();
	Drawable *draw = getObject()->getDrawable();
	const UnsignedInt now = TheGameLogic->getFrame();
	if (physics && draw)	// apply the kernel's op (computed now when the kernels are off)
		gxSteerApply(draw, GX_PK_HAVE(now) ? (GxSteerOp)R().gxOp : gxSteerKernel(R(), physics->getTurning(), now,
			getAnimationSteeringUpdateModuleData()->m_transitionFrames));
	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AnimationSteeringUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void AnimationSteeringUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AnimationSteeringUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}

