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
