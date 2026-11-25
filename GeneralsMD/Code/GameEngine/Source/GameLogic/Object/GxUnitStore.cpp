// Web port (round 4): GxUnitStore, the shared per-unit data (see Common/GxUnitStore.h).
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GxUnitStore.h"
#include "Common/Player.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

#include <algorithm>

#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches3();
#else
static int GLES3_Page_Switches3() { return 0; }
#endif

GxUnitStore TheGxUnitStore;

//-------------------------------------------------------------------------------------------------
int GxUnitStore::slotOf(const Object *o) const
{
	if (!o)
		return -1;
	const int s = o->gxStoreSlot();
	return (s >= 0 && s < (int)obj.size() && obj[s] == o) ? s : -1;
}

//-------------------------------------------------------------------------------------------------
void GxUnitStore::add(Object *o)
{
	const int oid = (int)o->getID();
	if (oid <= 0 || slotOf(o) >= 0)
		return;
	// many holes and no pass has run yet this frame (so nobody holds this frame's slots): close them
	if (m_holes > 4096 && m_holes * 2 > (int)obj.size() && (!TheGameLogic || TheGameLogic->getFrame() != m_compactFrame))
		compact();
	if (!obj.empty() && oid <= m_lastId)
		m_unsorted = true;	// after a load IDs can come out of order: compact() sorts
	else
		m_lastId = oid;

	if (obj.capacity() == 0)
	{
		const size_t n = 16384;	// ponytail: past this the arrays reallocate, which moves getPosition()/getVelocity() pointers
		id.reserve(n); obj.reserve(n); pos.reserve(n); vel.reserve(n);
		radius.reserve(n); player.reserve(n); playerBit.reserve(n); cell.reserve(n); mask.reserve(n);
	}
	const Coord3D *p = o->getPosition();
	const Player *pl = o->getControllingPlayer();
	const int pi = pl ? pl->getPlayerIndex() : -1;

	id.push_back(oid);
	obj.push_back(o);
	pos.push_back(*p);
	Coord3D zero; zero.zero();
	vel.push_back(zero);
	radius.push_back(o->getGeometryInfo().getBoundingCircleRadius());	// not yet in the store: from the geometry
	player.push_back((signed char)pi);
	playerBit.push_back(pi >= 0 ? (1u << pi) : 0u);
	cell.push_back(-1);
	mask.push_back(0);

	if (oid >= (int)m_slotById.size())
		m_slotById.resize(std::max<size_t>(oid + 1, m_slotById.size() * 2), -1);
	m_slotById[oid] = (int)obj.size() - 1;
	o->gxSetStoreSlot((int)obj.size() - 1);
	if (GLES3_Page_Switches3() & 128)
		o->gxSetPosInStore(true);	// switch positionInStore: from now on the store holds the position
}

//-------------------------------------------------------------------------------------------------
void GxUnitStore::remove(Object *o)
{
	const int s = slotOf(o);
	if (s < 0)
		return;
	o->gxSetPosInStore(false);	// the object keeps its last position
	o->gxSetStoreSlot(-1);
	m_dirty |= mask[s];
	m_slotById[id[s]] = -1;
	obj[s] = nullptr;
	id[s] = 0;
	mask[s] = 0;
	++m_holes;
}

//-------------------------------------------------------------------------------------------------
void GxUnitStore::setMask(const Object *o, GxBehaviourMask set, GxBehaviourMask clear)
{
	const int s = slotOf(o);
	if (s < 0)
		return;
	const GxBehaviourMask m = (mask[s] | set) & ~clear;
	m_dirty |= m ^ mask[s];
	mask[s] = m;
}

void GxUnitStore::setPos(const Object *o, const Coord3D &p)
{
	const int s = slotOf(o);
	if (s < 0)
		return;
	pos[s] = p;
}

void GxUnitStore::setRadius(const Object *o, float r)
{
	const int s = slotOf(o);
	if (s >= 0) radius[s] = r;
}

void GxUnitStore::setPlayer(const Object *o, int playerIndex)
{
	const int s = slotOf(o);
	if (s >= 0) { player[s] = (signed char)playerIndex; playerBit[s] = playerIndex >= 0 ? (1u << playerIndex) : 0u; }
}

void GxUnitStore::setCell(const Object *o, int cellIndex)
{
	const int s = slotOf(o);
	if (s >= 0) cell[s] = cellIndex;
}

//-------------------------------------------------------------------------------------------------
template <class T> static void gxPermute(std::vector<T> &v, const std::vector<int> &order)
{
	std::vector<T> out;
	out.reserve(v.capacity());	// keep the reserve, so pointers stay put between compactions
	for (size_t i = 0; i < order.size(); ++i)
		out.push_back(v[order[i]]);
	v.swap(out);
}

void GxUnitStore::compact()
{
	if (m_holes == 0 && !m_unsorted)
		return;
	std::vector<int> order;
	order.reserve(obj.size() - m_holes);
	for (int s = 0; s < (int)obj.size(); ++s)
		if (obj[s])
			order.push_back(s);
	if (m_unsorted)
		std::sort(order.begin(), order.end(), [this](int a, int b) { return id[a] < id[b]; });

	gxPermute(id, order); gxPermute(obj, order);
	gxPermute(pos, order);
	gxPermute(vel, order);
	gxPermute(radius, order); gxPermute(player, order); gxPermute(playerBit, order);
	gxPermute(cell, order); gxPermute(mask, order);

	for (int s = 0; s < (int)obj.size(); ++s)
	{
		m_slotById[id[s]] = s;
		obj[s]->gxSetStoreSlot(s);
	}
	m_lastId = obj.empty() ? 0 : id.back();
	m_holes = 0;
	m_unsorted = false;
	++m_epoch;
	m_dirty = ~0ull;
}

//-------------------------------------------------------------------------------------------------
const std::vector<int> &GxUnitStore::pass(int bit)
{
	const unsigned int frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	if (frame != m_compactFrame)
	{
		m_compactFrame = frame;	// slots stay put for the rest of this frame
		compact();
	}
	std::vector<int> &list = m_pass[bit];
	const GxBehaviourMask b = GXB_BIT(bit);
	if (m_dirty & b)
	{
		m_dirty &= ~b;
		list.clear();
		for (int s = 0; s < (int)obj.size(); ++s)
			if (mask[s] & b)
				list.push_back(s);
	}
	return list;
}
