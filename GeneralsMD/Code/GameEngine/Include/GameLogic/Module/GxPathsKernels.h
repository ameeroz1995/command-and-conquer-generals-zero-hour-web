// Web port (round 5 addendum, paths): range kernels through ai's per-kind hook
// (GameLogic::gxRegisterKernel). Each paths module type with a kernel that reads only its own unit
// has a store bit; the module joins that list on its first update and leaves it when destroyed.
// At the start of the module's phase the hook calls the type's kernel over the list (object-ID
// order): each unit runs its kernel step and keeps the result (an op) in its row, stamped with the
// frame. The module's update() then applies that op, in its usual place. With match rule updatePasses
// off the kernels do nothing and update() runs as before (the inputs are read a little earlier with
// the rule on, which is why it rides on that rule).
#pragma once

#include <vector>
#include "Common/GxUnitStore.h"
#include "Common/GxMatchRules.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

/// paths kernel bits in the unit store (8-31 are free for passes; paths takes 20-27)
enum
{
	GXB_PK_STEER = 20,	///< AnimationSteeringUpdate
	GXB_PK_FLOAT,		///< FloatUpdate
	GXB_PK_MISSILE,		///< MissileAIUpdate
	GXB_PK_JET,			///< JetAIUpdate
	GXB_PK_QUEUE_EXIT,	///< QueueProductionExitUpdate
	GXB_PK_DOCK,		///< DockUpdate (and the docks derived from it)
	GXB_PK_TRAIN,		///< RailroadBehavior
	GXB_PK_DEPLOY,		///< DeployStyleAIUpdate
};

inline bool gxPathsKernelsOn() { return gxMatchRule(GXRULE_UPDATE_PASSES); }

/// the modules of one type by object ID (filled on join; the kernel reaches its module from a slot)
template <class M>
struct GxModuleList
{
	static std::vector<M*> &byId() { static std::vector<M*> v; return v; }
	static void join(M *m, const Object *o, int bit)
	{
		const int id = (int)o->getID();
		std::vector<M*> &v = byId();
		if (id >= (int)v.size()) v.resize(id + 1024, nullptr);
		v[id] = m;
		TheGxUnitStore.setMask(o, GXB_BIT(bit));
	}
	static void leave(const M *m, int id)
	{
		std::vector<M*> &v = byId();
		if (id > 0 && id < (int)v.size() && v[id] == m) v[id] = nullptr;
	}
	static M *atSlot(int s)
	{
		const int id = TheGxUnitStore.id[s];
		const std::vector<M*> &v = byId();
		return (id > 0 && id < (int)v.size()) ? v[id] : nullptr;
	}
};

/// the kernel over a list: each module's gxKernelStep(now) (reads its own unit, writes its own row)
template <class M>
void gxModuleKernel(const Int *slots, Int begin, Int end)
{
	if (!gxPathsKernelsOn())
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	for (Int i = begin; i < end; ++i)
	{
		M *m = GxModuleList<M>::atSlot(slots[i]);
		if (m)
			m->gxKernelStep(now);
	}
}
inline void gxNoApply(const Int *, Int) {}	///< the apply step is the module's own update()

/// register at startup: GX_PATHS_KERNEL(Module, bit, phase) in the module's .cpp
#define GX_PATHS_KERNEL(M, BIT, PHASE) \
	static struct GxRegister##M { GxRegister##M() { GameLogic::gxRegisterKernel((BIT), (PHASE), &gxModuleKernel<M>, &gxNoApply); } } s_gxRegister##M;

/// inside a module's gxKernelStep: skip the unit unless its update() runs this frame (due, not disabled)
#define GX_PK_WILL_UPDATE \
	{ if (getWakeFrame() != UPDATE_SLEEP_NONE) return; \
	  DisabledMaskType gxDis = getObject()->getDisabledFlags(); \
	  if (gxDis.any() && !gxDis.anyIntersectionWith(getDisabledTypesToProcess())) return; }

/// every kernel-run module's row carries: UnsignedInt gxOpFrame (frame + 1 of the result), Int gxOp,
/// Bool gxJoined, Int gxId

/// at the top of update(): join the kernel list once
#define GX_PK_JOIN(M, BIT) \
	if (!R().gxJoined) { R().gxJoined = TRUE; R().gxId = (Int)getObject()->getID(); GxModuleList<M>::join(this, getObject(), (BIT)); }
#define GX_PK_HAVE(now) (R().gxOpFrame == (now) + 1)
