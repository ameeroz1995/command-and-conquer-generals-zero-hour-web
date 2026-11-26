// Web port (round 4): GxUnitStore, the shared per-unit data (see Common/GxUnitStore.h).
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GxUnitStore.h"
#include "Common/Player.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

#include <algorithm>
#include <cstring>

#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches3();
extern int GLES3_Page_Switches();
extern int GLES3_Page_Switches4();
#else
static int GLES3_Page_Switches4() { return 0; }
static int GLES3_Page_Switches3() { return 0; }
static int GLES3_Page_Switches() { return 0; }
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
		const size_t n = 16384;	// ponytail: past this the arrays reallocate (cost only; nobody keeps pointers into them)
		id.reserve(n); obj.reserve(n); x.reserve(n); y.reserve(n); z.reserve(n); vx.reserve(n); vy.reserve(n); vz.reserve(n); ax.reserve(n); ay.reserve(n); az.reserve(n);
		radius.reserve(n); player.reserve(n); playerBit.reserve(n); cell.reserve(n); mask.reserve(n);
		status.reserve(n); disabled.reserve(n); layer.reserve(n); geom.reserve(n); angle.reserve(n); xf.reserve(n);
		target.reserve(n); weaponReady.reserve(n); weaponSig.reserve(n);	// combat block
	}
	const Coord3D *p = o->getPosition();
	const Player *pl = o->getControllingPlayer();
	const int pi = pl ? pl->getPlayerIndex() : -1;

	id.push_back(oid);
	obj.push_back(o);
	x.push_back(p->x); y.push_back(p->y); z.push_back(p->z);
	vx.push_back(0.0f); vy.push_back(0.0f); vz.push_back(0.0f);
	ax.push_back(0.0f); ay.push_back(0.0f); az.push_back(0.0f);
	radius.push_back(o->getGeometryInfo().getBoundingCircleRadius());	// not yet in the store: from the geometry
	player.push_back((signed char)pi);
	playerBit.push_back(pi >= 0 ? (1u << pi) : 0u);
	cell.push_back(-1);
	mask.push_back(0);
	{
		static const GeometryInfo g0(GEOMETRY_SPHERE, FALSE, 1.0f, 1.0f, 1.0f);
		status.push_back(ObjectStatusMaskType()); disabled.push_back(DisabledMaskType()); layer.push_back(LAYER_GROUND);
		geom.push_back(g0); angle.push_back(0.0f); xf.push_back(Matrix3D(true));	// hotInStore: filled by gxSetHotInStore
	}
	target.push_back(0); weaponReady.push_back(0); weaponSig.push_back(~0u);	// combat block

	if (oid >= (int)m_slotById.size())
		m_slotById.resize(std::max<size_t>(oid + 1, m_slotById.size() * 2), -1);
	m_slotById[oid] = (int)obj.size() - 1;
	o->gxSetStoreSlot((int)obj.size() - 1);
	// switch hotInStore (round 5, one data switch): the store holds every hot field, position included;
	// positionInStore alone moves only the position (kept for comparison)
	if ((GLES3_Page_Switches3() & 128) || (GLES3_Page_Switches4() & 4))
		o->gxSetPosInStore(true);
	if (GLES3_Page_Switches4() & 4)
		o->gxSetHotInStore(true);	// status, disabled, layer, geometry, angle, transform in the columns
}

//-------------------------------------------------------------------------------------------------
void GxUnitStore::remove(Object *o)
{
	const int s = slotOf(o);
	if (s < 0)
		return;
	o->gxSetPosInStore(false);	// the object keeps its last position
	o->gxSetHotInStore(false);	// and its hot fields
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
	x[s] = p.x; y[s] = p.y; z[s] = p.z;
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

// ---- combat block ----
void GxUnitStore::setTarget(const Object *o, int victimId)
{
	const int s = slotOf(o);
	if (s >= 0 && target[s] != victimId) { target[s] = victimId; weaponReady[s] = 0; }
}
// ---- end combat block ----

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
	gxPermute(x, order); gxPermute(y, order); gxPermute(z, order);
	gxPermute(vx, order); gxPermute(vy, order); gxPermute(vz, order);
	gxPermute(ax, order); gxPermute(ay, order); gxPermute(az, order);
	gxPermute(radius, order); gxPermute(player, order); gxPermute(playerBit, order);
	gxPermute(cell, order); gxPermute(mask, order);
	gxPermute(status, order); gxPermute(disabled, order); gxPermute(layer, order); gxPermute(geom, order);
	gxPermute(angle, order); gxPermute(xf, order);
	gxPermute(target, order); gxPermute(weaponReady, order); gxPermute(weaponSig, order);	// combat block

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
// verify mode (hotInStore): a stored object's own rows keep the marks gxSetHotInStore left there, so
// nothing read or wrote them around the columns
void GxUnitStore::gxVerifyHot(unsigned int frame)
{
	static int checked = 0, bad = 0;
	int badNow = 0;
	for (int s = 0; s < size(); ++s)
	{
		const Object *o = obj[s];
		if (!o || !o->gxPosInStore())
			continue;
		const Matrix3D *m = o->getTransformMatrix();	// once placed, the translation must equal the x/y/z columns
		if (o->isPositioned() && (m->Get_X_Translation() != x[s] || m->Get_Y_Translation() != y[s] || m->Get_Z_Translation() != z[s]))
			++badNow;
		++checked;
		if (!o->gxHotInStore())
			continue;
		if (!o->gxHotRowsMarked())
			++badNow;
		++checked;
	}
	bad += badNow;
	if (badNow || frame % 300 == 0)
		printf("GXVERIFY store hot fields (rows untouched while stored, translation = x/y/z): checked %d, mismatches %d\n", checked, bad);
}

const std::vector<int> &GxUnitStore::pass(int bit)
{
	const unsigned int frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	if (frame != m_compactFrame)
	{
		m_compactFrame = frame;	// slots stay put for the rest of this frame
		if (GLES3_Page_Switches() & 2097152)
			gxVerifyHot(frame);
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
