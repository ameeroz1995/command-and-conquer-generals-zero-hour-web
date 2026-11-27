// Web port (round 5 addendum, paths): range kernels through ai's per-kind batch hook
// (GameLogic::gxRegisterKindKernel). Each paths module type with a kernel that reads only its own unit
// registers its kind (the module's INI name key) on its first update. Right before the kind's update
// loop the hook passes the kind's awake modules (object-ID order): each runs its kernel step and keeps
// the result (an op) in its row, stamped with the frame. The module's update() then applies that op in
// its usual place. The hook runs only under match rule updatePasses (host / replay header decide);
// otherwise, and for modules woken from sleep, update() computes inline as before.
#pragma once

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/UpdateModule.h"

/// the kernel over a kind's modules: each module's gxKernelStep(now) (reads its own unit, writes its own row)
template <class M>
void gxModuleKernel(UpdateModulePtr *mods, Int begin, Int end)
{
	const UnsignedInt now = TheGameLogic->getFrame();
	for (Int i = begin; i < end; ++i)
		static_cast<M*>(mods[i])->gxKernelStep(now);
}

/// inside a module's gxKernelStep: skip the unit unless its update() runs this frame (due, not disabled)
#define GX_PK_WILL_UPDATE \
	{ if (getWakeFrame() != UPDATE_SLEEP_NONE) return; \
	  DisabledMaskType gxDis = getObject()->getDisabledFlags(); \
	  if (gxDis.any() && !gxDis.anyIntersectionWith(getDisabledTypesToProcess())) return; }

/// every kernel-run module's row carries: UnsignedInt gxOpFrame (frame + 1 of the result), Int gxOp,
/// Bool gxJoined

/// at the top of update(): register the module's kind once (the registry keeps one entry per kind)
#define GX_PK_JOIN(M) \
	if (!R().gxJoined) { R().gxJoined = TRUE; GameLogic::gxRegisterKindKernel(getModuleNameKey(), &gxModuleKernel<M>, nullptr); }
#define GX_PK_HAVE(now) (R().gxOpFrame == (now) + 1)
