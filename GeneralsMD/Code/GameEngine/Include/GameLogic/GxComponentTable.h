// Web port (round 5, "units are pure data"): the query family's modules on collide's component pools.
//
// A module's per-unit state lives in a row of GxComponentPool<Row> (Common/GxComponentPool.h); the
// module keeps only its GxRow handle (a facade for INI, xfer/CRC and the engine's calls) and reads
// the row through R(). Its update is the apply step of a system whose pure part is a kernel over a
// row range (below).
#pragma once

#include "Common/GxComponentPool.h"

/// Put at the end of a module class: the row handle and R(). R() is const and returns the row
/// writable, as the old non-const members were (and mutable ones in const methods).
#define GX_COMPONENT_ROWS(Cls, RowT) \
	public: typedef RowT GxRowT; \
	RowT &R() const { return const_cast<RowT &>(*m_gxRow); } \
	private: ::GxRow<RowT> m_gxRow;

// ---- kernels (round 5 addendum: every system = kernel over a row range + apply step on main) ----
// The scan-type modules share one pure piece of per-unit math: a scan countdown. The countdown
// kernel precomputes it for a row range; the module's update (the apply step, main thread, object-ID
// order through the update loop) commits it where the old code counted down, then does the scan and
// the side effects. Both run on the main thread for now (fourth switch word bit 12, rowKernels).

/// one row's precomputed countdown: valid when stamp == frame + 1 and from == the delay it read
struct GxTick { unsigned int stamp; int from; int next; bool due; };

/// Kernel. Reads: row.*delay, row.*reload. Writes: row.m_gxTick only (its own rows).
/// No allocation, events, RNG or globals. due = the scan runs this call (delay not > 0); next = the
/// delay after this call.
template <class T, class U, class Row>
void gxCountdownKernel(GxComponentPool<Row> &t, int begin, int end, unsigned int frame, T Row::*delay, U Row::*reload)
{
	for (int i = begin; i < end; ++i)
	{
		if (!t.live(i)) continue;
		Row &r = t[i];
		GxTick &k = r.m_gxTick;
		const T d = r.*delay;
		k.due = !(d > 0);
		k.next = k.due ? (int)(r.*reload) : (int)(d - 1);
		k.from = (int)d;
		k.stamp = frame + 1;
	}
}

#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches4();
#else
static inline int GLES3_Page_Switches4() { return 0; }
#endif

/// Apply-step commit: the row's delay takes its next value; returns TRUE when the scan is due.
/// With the switch on, the first commit of a frame runs the kernel over the whole pool; a row the
/// kernel missed or whose delay changed since gets the same math inline (so the game is the same).
template <class T, class U, class Row>
bool gxCountdownCommit(Row &r, unsigned int frame, T Row::*delay, U Row::*reload)
{
	GxTick &k = r.m_gxTick;
	if ((GLES3_Page_Switches4() & 4096) && k.stamp != frame + 1)
	{
		GxComponentPool<Row> &t = GxComponentPool<Row>::the();
		gxCountdownKernel(t, 0, t.size(), frame, delay, reload);
	}
	const T d = r.*delay;
	if (k.stamp != frame + 1 || k.from != (int)d)
	{
		k.due = !(d > 0);
		k.next = k.due ? (int)(r.*reload) : (int)(d - 1);
	}
	k.stamp = 0;	// used: a second call this frame computes afresh
	r.*delay = (T)k.next;
	return k.due;
}
