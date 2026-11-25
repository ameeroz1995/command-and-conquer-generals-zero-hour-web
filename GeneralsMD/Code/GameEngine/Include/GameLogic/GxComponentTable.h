// Web port (round 5, "units are pure data"): a component table, one per module family.
//
// A module's per-unit state lives in a Row of its family's table instead of inside the module object;
// the module keeps only the row index (a facade for INI, xfer/CRC and the engine's calls), and its
// update is a system step that reads and writes the row.
//
// - Rows sit in a std::deque: adding rows never moves existing ones, so a Row& stays valid across
//   code that creates objects. Rows are never copied (some hold RadiusDecal, which must not be).
// - A freed row is reset in place and reused by the next add (dense, no compaction, so indexes never
//   change while a module holds one). Row indexes carry no game meaning: same game.
// - each(): every live row with its owner, in table order (for passes and debug counts).
#pragma once

#include <deque>
#include <new>
#include <vector>

template <class Row, class Owner = void>
class GxComponentTable
{
public:
	int add(Owner *owner)
	{
		int i;
		if (!m_free.empty())
		{
			i = m_free.back();
			m_free.pop_back();
		}
		else
		{
			i = (int)m_rows.size();
			m_rows.emplace_back();
			m_owner.push_back(nullptr);
		}
		m_owner[i] = owner;
		return i;
	}
	void remove(int i)
	{
		if (i < 0 || i >= (int)m_rows.size() || !m_owner[i])
			return;
		m_rows[i].~Row();
		new (&m_rows[i]) Row();
		m_owner[i] = nullptr;
		m_free.push_back(i);
	}
	Row &operator[](int i) { return m_rows[i]; }
	int live() const { return (int)m_rows.size() - (int)m_free.size(); }
	int size() const { return (int)m_rows.size(); }			///< row range for kernels: [0, size())
	bool used(int i) const { return m_owner[i] != nullptr; }
	template <class F> void each(F f)
	{
		for (int i = 0; i < (int)m_rows.size(); ++i)
			if (m_owner[i]) f(m_owner[i], m_rows[i]);
	}

private:
	std::deque<Row>			m_rows;
	std::vector<Owner*>	m_owner;
	std::vector<int>		m_free;
};

/// Declares a module's row accessor and index. Put inside the module class; the table itself is a
/// function-local static in the module's .cpp (GX_COMPONENT_TABLE_DEFINE), so it exists before any
/// module is constructed.
#define GX_COMPONENT_ROWS(Cls, RowT) \
	public: typedef RowT GxRow; static GxComponentTable<RowT, Cls> &gxTable(); \
	RowT &R() const { return gxTable()[m_gxRow]; } \
	private: int m_gxRow;

#define GX_COMPONENT_TABLE_DEFINE(Cls) \
	GxComponentTable<Cls::GxRow, Cls> &Cls::gxTable() { static GxComponentTable<Cls::GxRow, Cls> t; return t; }

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
template <class T, class U, class Row, class Owner>
void gxCountdownKernel(GxComponentTable<Row, Owner> &t, int begin, int end, unsigned int frame, T Row::*delay, U Row::*reload)
{
	for (int i = begin; i < end; ++i)
	{
		if (!t.used(i)) continue;
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
/// With the switch on, the first commit of a frame runs the kernel over the whole table; a row the
/// kernel missed or whose delay changed since gets the same math inline (so the game is the same).
template <class T, class U, class Row, class Owner>
bool gxCountdownCommit(GxComponentTable<Row, Owner> &t, Row &r, unsigned int frame, T Row::*delay, U Row::*reload)
{
	GxTick &k = r.m_gxTick;
	if ((GLES3_Page_Switches4() & 4096) && k.stamp != frame + 1)
		gxCountdownKernel(t, 0, t.size(), frame, delay, reload);
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
