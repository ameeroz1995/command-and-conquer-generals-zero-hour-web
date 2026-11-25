// Web port (round 5, "units are pure data"): component tables.
//
// A module family's per-unit state lives in a GxComponentTable<Row>, not in the module objects. A
// module allocates its row when it is built (alloc) and frees it when it goes (release); it keeps
// only the row's handle, which stays valid for the module's life (an object's ID can change on load,
// the handle does not). The module class is a thin facade: INI data, the engine's existing calls, and
// xfer/CRC reading and writing the row's fields in the old order (saves and CRCs unchanged). The
// family's logic is a system function over rows; forEach visits the live rows in object-ID order.
//
// Rows never move (a deque), so a Row& held while objects are created stays valid until the row is released.
#pragma once

#include <vector>
#include <deque>
#include <unordered_map>
#include <new>
#include <algorithm>
#include "Lib/BaseType.h"

class Object;
extern UnsignedInt gxComponentObjectId(const Object *o);	///< Object::getID() without the header (GxComponents.cpp)

template <class Row>
class GxComponentTable
{
public:
	/// a new row for `owner`; returns a pointer that stays valid until release (rows never move)
	Row *alloc(const Object *owner)
	{
		const Int h = allocHandle(owner);
		Row *r = &m_rows[h];
		m_handleOf[r] = h;
		return r;
	}
	void release(Row *r)
	{
		if (!r) return;
		typename std::unordered_map<const Row *, Int>::iterator it = m_handleOf.find(r);
		if (it == m_handleOf.end()) return;
		const Int h = it->second;
		m_handleOf.erase(it);
		releaseHandle(h);
	}
	Row &at(Row *r) { return *r; }
	const Row &at(const Row *r) const { return *r; }

private:
	Int allocHandle(const Object *owner)
	{
		Int h;
		// rows are built in place (value-initialised: zeroed like the memory-pool objects they replace),
		// never copied from a temporary
		if (!m_free.empty()) { h = m_free.back(); m_free.pop_back(); m_rows[h].~Row(); new (&m_rows[h]) Row(); }
		else { h = (Int)m_rows.size(); m_rows.emplace_back(); m_owner.push_back(nullptr); m_live.push_back(0); m_inOrder.push_back(0); }
		m_owner[h] = owner;	// may be null (a mix-in without an object): such rows are never visited by forEach
		m_live[h] = 1;
		if (owner && !m_inOrder[h]) { m_inOrder[h] = 1; m_order.push_back(h); }
		return h;
	}
	void releaseHandle(Int h)
	{
		if (h < 0 || h >= (Int)m_rows.size() || !m_live[h]) return;
		m_owner[h] = nullptr;
		m_live[h] = 0;
		m_rows[h].~Row(); new (&m_rows[h]) Row();	// drop what the row held now (lists, ids), not at reuse
		m_free.push_back(h);	// m_order drops it lazily (forEach)
	}
public:
	Int liveCount() const { return (Int)(m_rows.size() - m_free.size()); }

	/// every live row in object-ID order: fn(owner, row)
	template <class Fn> void forEach(Fn fn)
	{
		size_t w = 0;	// drop released and ownerless handles
		for (size_t k = 0; k < m_order.size(); ++k)
		{
			const Int h = m_order[k];
			if (m_live[h] && m_owner[h]) m_order[w++] = h; else m_inOrder[h] = 0;
		}
		m_order.resize(w);
		sortByObjectId();
		for (size_t k = 0; k < m_order.size(); ++k)
		{
			const Int h = m_order[k];
			if (m_owner[h]) fn(m_owner[h], m_rows[h]);
		}
	}

private:
	std::deque<Row> m_rows;	///< a deque: rows never move when the table grows, so a Row& stays valid while objects are created
	std::vector<const Object *> m_owner;	///< nullptr for a free row or a row without an object
	std::vector<char> m_live;
	std::vector<char> m_inOrder;
	std::vector<Int> m_free;
	std::vector<Int> m_order;	///< live handles, kept in object-ID order by sortByObjectId
	std::unordered_map<const Row *, Int> m_handleOf;

	void sortByObjectId()
	{
		for (size_t k = 1; k < m_order.size(); ++k)
			if (gxComponentObjectId(m_owner[m_order[k - 1]]) > gxComponentObjectId(m_owner[m_order[k]]))	// null owners count as ID 0
			{
				std::stable_sort(m_order.begin(), m_order.end(), [this](Int a, Int b)
					{ return gxComponentObjectId(m_owner[a]) < gxComponentObjectId(m_owner[b]); });
				return;
			}
	}
};
