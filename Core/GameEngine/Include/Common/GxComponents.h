// Web port (round 5, "units are pure data"): a component table.
//
// A module's per-unit state lives in one row of a table owned by its family (structure of rows, one
// vector per module type); the module object only keeps its row handle and stays as the facade the
// engine calls (INI, xfer, crc). Handles are stable for the row's life (freed rows are reused), so a
// row is reached the same way whether the system loops the store's pass list or the engine calls the
// facade. Rows live in a deque, so a row never moves while it is alive (members whose address is
// handed out, such as sub-objects passed to xferSnapshot, stay valid).
#pragma once

#include <deque>
#include <vector>

template <class Row>
class GxComponentTable
{
public:
	std::deque<Row> rows;

	int alloc()
	{
		if (!m_free.empty())
		{
			const int h = m_free.back();
			m_free.pop_back();
			rows[h] = Row();
			return h;
		}
		rows.push_back(Row());
		return (int)rows.size() - 1;
	}
	void release(int h) { if (h >= 0) { rows[h] = Row(); m_free.push_back(h); } }
	Row &operator[](int h) { return rows[h]; }
	int liveCount() const { return (int)rows.size() - (int)m_free.size(); }

private:
	std::vector<int> m_free;
};
