// Web port (round 5, "units are pure data"): component tables for module state.
//
// A module's per-unit state lives in a row of its family's pool (GxComponentPool<Row>), not inside
// the module object. The module keeps only its row index (it stays as a thin facade: INI data, the
// engine's existing calls, xfer/CRC read and write the row). Systems loop the pool's rows.
//
// - One row per module instance (an object can carry several modules of one kind, e.g. die modules),
//   so rows are keyed by module, not by store slot.
// - Rows live in a std::deque: a row never moves while it is alive, so `Row &r = row();` stays valid
//   across code that creates or destroys other modules.
// - Freed rows are reused (free list); live() tells a system which rows to visit.
#pragma once

#include <deque>
#include <vector>

template <class Row>
class GxComponentPool
{
public:
	int alloc()
	{
		int i;
		if (!m_free.empty()) { i = m_free.back(); m_free.pop_back(); m_rows[i] = Row(); m_live[i] = 1; }
		else { i = (int)m_rows.size(); m_rows.emplace_back(); m_live.push_back(1); }
		++m_count;
		return i;
	}
	void release(int i)
	{
		if (i < 0 || !m_live[i]) return;
		m_live[i] = 0;
		m_free.push_back(i);
		--m_count;
	}
	Row &operator[](int i) { return m_rows[i]; }
	const Row &operator[](int i) const { return m_rows[i]; }
	int size() const { return (int)m_rows.size(); }	///< rows ever made (visit those with live(i))
	bool live(int i) const { return m_live[i] != 0; }
	int liveCount() const { return m_count; }

	/// the one pool of this row type
	static GxComponentPool &the() { static GxComponentPool p; return p; }

private:
	std::deque<Row> m_rows;
	std::vector<char> m_live;
	std::vector<int> m_free;
	int m_count = 0;
};

/// The module side: `GxRow<FooData> m_gx;` as the module's only state member, then `m_gx->field`.
template <class Row>
class GxRow
{
public:
	GxRow() : m_i(GxComponentPool<Row>::the().alloc()) {}
	~GxRow() { GxComponentPool<Row>::the().release(m_i); }
	GxRow(const GxRow &) = delete;
	GxRow &operator=(const GxRow &) = delete;
	Row *operator->() { return &GxComponentPool<Row>::the()[m_i]; }
	const Row *operator->() const { return &GxComponentPool<Row>::the()[m_i]; }
	Row &operator*() { return GxComponentPool<Row>::the()[m_i]; }
	const Row &operator*() const { return GxComponentPool<Row>::the()[m_i]; }
	int index() const { return m_i; }
private:
	int m_i;
};
