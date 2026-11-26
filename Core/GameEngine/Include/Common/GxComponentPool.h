// Web port (round 5, "units are pure data"): component tables for module state.
//
// A module's per-unit state lives in a row of its family's pool (GxComponentPool<Row>), not inside
// the module object. The module keeps only its row (it stays as a thin facade: INI data, the
// engine's existing calls, xfer/CRC read and write the row). Systems loop the pool's rows.
//
// - One row per module instance (an object can carry several modules of one kind, e.g. die modules),
//   so rows are keyed by module, not by store slot.
// - Rows live in a std::deque of raw slots: a row never moves while it is alive, so `Row &r = row();`
//   and GxRow's cached pointer stay valid across code that creates or destroys other modules.
// - Rows are built in place on alloc and destroyed in place on release (no copy or assignment, so a
//   row may hold non-copyable members such as another GxRow; a freed row holds nothing). Freed slots
//   are reused (free list); live() tells a system which rows to visit.
#pragma once

#include <deque>
#include <new>
#include <vector>

template <class Row>
class GxComponentPool
{
	struct Slot { alignas(Row) unsigned char b[sizeof(Row)]; };
public:
	GxComponentPool() = default;
	GxComponentPool(const GxComponentPool &) = delete;
	GxComponentPool &operator=(const GxComponentPool &) = delete;
	~GxComponentPool() { for (int i = 0; i < size(); ++i) if (m_live[i]) (*this)[i].~Row(); }

	int alloc()
	{
		int i;
		if (!m_free.empty()) { i = m_free.back(); m_free.pop_back(); }
		else { i = (int)m_rows.size(); m_rows.emplace_back(); m_live.push_back(0); }
		new (m_rows[i].b) Row();
		m_live[i] = 1;
		++m_count;
		return i;
	}
	void release(int i)
	{
		if (i < 0 || !m_live[i]) return;
		(*this)[i].~Row();
		m_live[i] = 0;
		m_free.push_back(i);
		--m_count;
	}
	Row &operator[](int i) { return *std::launder(reinterpret_cast<Row *>(m_rows[i].b)); }
	const Row &operator[](int i) const { return *std::launder(reinterpret_cast<const Row *>(m_rows[i].b)); }
	int size() const { return (int)m_rows.size(); }	///< rows ever made (visit those with live(i))
	bool live(int i) const { return m_live[i] != 0; }
	int liveCount() const { return m_count; }

	/// the one pool of this row type
	static GxComponentPool &the() { static GxComponentPool p; return p; }

private:
	std::deque<Slot> m_rows;
	std::vector<char> m_live;
	std::vector<int> m_free;
	int m_count = 0;
};

/// The module side: `GxRow<FooRow> m_gx;` as the module's only state member, then `m_gx->field`.
/// Holds the row's pointer (rows never move), so a field access is one load.
template <class Row>
class GxRow
{
public:
	GxRow() : m_i(GxComponentPool<Row>::the().alloc()), m_p(&GxComponentPool<Row>::the()[m_i]) {}
	~GxRow() { GxComponentPool<Row>::the().release(m_i); }
	GxRow(const GxRow &) = delete;
	GxRow &operator=(const GxRow &) = delete;
	Row *operator->() { return m_p; }
	const Row *operator->() const { return m_p; }
	Row &operator*() { return *m_p; }
	const Row &operator*() const { return *m_p; }
	int index() const { return m_i; }
private:
	int m_i;
	Row *m_p;
};
