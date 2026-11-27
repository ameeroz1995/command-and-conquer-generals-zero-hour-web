// Web port (round 4): GxUnitStore, the shared per-unit data of the manager design.
//
// One slot per live Object, in dense arrays (structure of arrays) kept in ascending object-ID order.
// Each manager pass asks for its dense list of slots (pass(bit)): the units whose behaviour mask has
// that bit, in object-ID order. Lists are rebuilt only when a mask changes or units come and go.
//
// Slots:
// - New objects are appended (IDs only grow, so the order holds). Removed objects leave a hole (obj
//   null, mask 0) that no pass list contains.
// - compact() closes the holes (and re-sorts after a load, when IDs can arrive out of order). It runs
//   at most once per logic frame, at the first pass(), so slot numbers never move inside a frame
//   once a pass has started: a pass may run inside another pass, and a slot read at the start of a
//   frame stays valid until the next frame.
// - A list handed out earlier in the frame can still hold a slot whose object was removed since:
//   skip slots with obj[s] == nullptr.
// - The arrays may grow (reallocate) when objects are created: index them by slot every time, never
//   keep a pointer into them across code that can create objects.
//
// Who writes what (source of truth vs write-through):
// - x, y, z (round 5: structure of arrays, one float column each, so loops do 4 units per SIMD
//   instruction): the position. With the switch positionInStore (fixed per object when it enters the
//   store) it is the truth: Thing::getPosition() reads through (copies the three columns into the
//   Thing's cached Coord3D and returns that), Thing's setters write it, and getTransformMatrix()
//   refreshes the matrix translation from it. Without the switch it is a write-through copy
//   (Object::reactToTransformChange). posOf(s) gives it as a Coord3D.
// - vx, vy, vz: the velocity, the truth for every unit with physics (PhysicsBehavior reads and
//   writes it through velRef(s); getVelocity() copies it out). velOf(s) gives it as a Coord3D.
// - ax, ay, az: the acceleration summed by the forces of this frame (PhysicsBehavior, accelRef(s)).
// - radius: the truth for the object's bounding circle radius (Object::getBoundingCircleRadius()
//   reads it); set by Object::setGeometryInfo. The GeometryInfo keeps its own value for shape tests.
// - player, playerBit: the truth for the controlling-player index (Object::gxPlayerIndex() reads it);
//   set by Object::setOrRestoreTeam and TeamPrototype::setControllingPlayer (whole-team owner change).
// - cell: the partition cell (cy * cellCountX + cx) under the unit's centre, written by
//   PartitionData::updateCellsTouched when it changes (-1 off the map or before that).
// - inCells: 1 while the unit's partition data has cells in use; written by the partition when it
//   fills or empties the unit's cells (updateCellsTouched, removeAllTouchedCells, freeCoiArray), read
//   by the storePairs broad phase instead of asking each Object (verify mode compares the two).
// - mask: set when a unit's behaviour set changes (setMask); never re-tested per tick.
// - status, disabled, layer, geom, angle, xf (round 5, switch hotInStore, fourth word bit 2, fixed per
//   object when it enters the store): the truth for the object's status bits, disabled mask,
//   pathfind layer, geometry, orientation and transform while it is stored. Object/Thing read and
//   write them through ref accessors (gxStatus(), gxDisabled(), gxLayerRef(), gxGeom(), gxAngle(),
//   gxXf()); add() moves the object's values in, remove() moves them back to its rows. Without the
//   switch the columns are unused (no copies). References into them are valid until objects are
//   created (the columns can reallocate): use them right away.
#pragma once

#include <vector>
#include <cstdlib>
#include <new>
#include "Lib/BaseType.h"	// Coord3D
#include "Common/GameType.h"			// PathfindLayerEnum
#include "Common/ObjectStatusTypes.h"
#include "Common/DisabledTypes.h"
#include "WWMath/matrix3d.h"

class Object;
class GeometryInfo;

#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches5();
extern int GLES3_Page_Switches6();
extern int GLES3_Page_Switches7();
#else
#ifndef GX_SWITCHES56_FALLBACK
#define GX_SWITCHES56_FALLBACK
static inline int GLES3_Page_Switches5() { return 0; }
static inline int GLES3_Page_Switches6() { return 0; }
static inline int GLES3_Page_Switches7() { return 0; }
#endif
#endif
/// switch geomColumns (round 5, fifth word bit 4, value 16; with hotInStore): geometry and transform also
/// as plain columns (gType .. gHeight, xfm[12]), read by the hot readers instead of the structs
inline bool gxGeomColumnsOn() { return (GLES3_Page_Switches5() & 16) != 0; }	// complete wherever geom is used (Common/Geometry.h)

/// A Coord3D-shaped view of one row of three float columns: `velRef(s).x += a` writes the column.
/// Use it right away (the columns can reallocate when objects are created).
struct GxVec3Ref
{
	float &x, &y, &z;
	GxVec3Ref(float &a, float &b, float &c) : x(a), y(b), z(c) {}
	operator Coord3D() const { Coord3D c; c.x = x; c.y = y; c.z = z; return c; }
	GxVec3Ref &operator=(const Coord3D &c) { x = c.x; y = c.y; z = c.z; return *this; }
	GxVec3Ref &operator=(const GxVec3Ref &r) { x = r.x; y = r.y; z = r.z; return *this; }
	void zero() { x = y = z = 0.0f; }
	void add(const Coord3D *c) { x += c->x; y += c->y; z += c->z; }
};

typedef unsigned long long GxBehaviourMask;

extern unsigned long long g_gxAllocs;	// GameMemory.cpp (tickTiming allocation count)

/// Round 6 false-sharing rule: every store column starts on a 64-byte boundary (and is a whole number of
/// 64-byte lines), so a job chunk of 16 slots of float never shares a cache line with the next one.
template <class T> struct GxAlign64
{
	typedef T value_type;
	GxAlign64() noexcept {}
	template <class U> GxAlign64(const GxAlign64<U> &) noexcept {}
	T *allocate(size_t n)
	{
		++g_gxAllocs;
		void *p = aligned_alloc(64, (n * sizeof(T) + 63) & ~(size_t)63);
		if (!p) throw std::bad_alloc();
		return (T *)p;
	}
	void deallocate(T *p, size_t) noexcept { free(p); }
	template <class U> bool operator==(const GxAlign64<U> &) const noexcept { return true; }
	template <class U> bool operator!=(const GxAlign64<U> &) const noexcept { return false; }
};
template <class T> using GxCol = std::vector<T, GxAlign64<T>>;	///< one store column


/// One bit per manager pass. Agents add their own below GXB_COUNT.
enum GxBehaviour
{
	GXB_MOVE    = 0,	///< locomotion / route following (paths)
	GXB_PHYS    = 1,	///< has a PhysicsBehavior (collide)
	GXB_COLLIDE = 2,	///< takes part in collisions (collide)
	GXB_SCAN    = 3,	///< runs enemy / target scans (query)
	GXB_WEAPON  = 4,	///< has weapons (combat)
	GXB_AI      = 5,	///< has an AIUpdateInterface (ai)
	GXB_ANIM    = 6,	///< animation / pose (combat)
	GXB_DAMAGE  = 7,	///< has a body that takes damage (combat)
	// 8-31: free for other passes. 32-63: reserved for ai, one bit per every-frame update-module kind
	// (the update loop's dispatch table, GameLogic.cpp, match rule 131072).
	GXB_AI_FIRST = 32,
	GXB_COUNT   = 64
};
#define GXB_BIT(b) (1ull << (b))

class GxUnitStore
{
public:
	// ---- the data: index by slot, 0 .. size()-1 ----
	GxCol<int>							id;				///< ObjectID (0 in a hole)
	GxCol<Object*>					obj;			///< nullptr in a hole
	GxCol<float>						x, y, z;		///< position (truth with positionInStore, else write-through)
	GxCol<float>						vx, vy, vz;		///< velocity (truth for physics units)
	GxCol<float>						ax, ay, az;		///< acceleration being summed this frame (truth for physics units)
	GxCol<float>						radius;			///< bounding circle radius
	GxCol<signed char>			player;			///< controlling player index, -1 for none
	GxCol<unsigned int>			playerBit;		///< 1 << player (0 for none)
	GxCol<int>							cell;			///< partition cell index, -1 unknown
	GxCol<unsigned char>				inCells;		///< 1 while the partition holds the unit in its cells (COIs in use; 0 e.g. inside a transport)
	GxCol<GxBehaviourMask>	mask;			///< behaviour bits (GXB_BIT)
	// hot fields (truth with hotInStore, see above)
	GxCol<ObjectStatusMaskType>	status;		///< status bits
	GxCol<DisabledMaskType>		disabled;		///< disabled mask
	GxCol<PathfindLayerEnum>		layer;			///< pathfind layer
	GxCol<GeometryInfo>			geom;			///< geometry
	GxCol<float>						angle;			///< orientation
	GxCol<Matrix3D>					xf;				///< transform
	// geomColumns: the same geometry and transform as plain columns, written with geom / xf (gxGeomSync,
	// gxXfSync) at their few writers; hot readers (partition fill, collision z and shape test, direction)
	// read these. height = the box / cylinder height (0 for spheres, which use majorRadius).
	GxCol<unsigned char>				gType, gSmall;	///< GeometryType, isSmall
	GxCol<float>						gMajor, gMinor, gHeight;
	GxCol<float>						xfm[12];		///< transform rows: xfm[r * 4 + c] = xf[s][r][c]
	void gxGeomSync(int s);							///< geom[s] -> gType .. gHeight (geomColumns)
	void gxXfSync(int s) { if (!gxGeomColumnsOn()) return; const Matrix3D &m = xf[s]; for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) xfm[r * 4 + c][s] = m[r][c]; }

	// ---- combat block (round 4, combat agent): weapons ----
	GxCol<int>							target;			///< current victim ObjectID, 0 none (write-through: AIUpdateInterface::setCurrentVictim)
	GxCol<unsigned int>			weaponReady;	///< frame a weapon's reload / pre-attack ends (weapon-status pass); 0 = look now
	GxCol<unsigned int>			weaponSig;		///< weapon-status pass: ammo/status signature last shown on the drawable
	void setTarget(const Object *o, int victimId);
	/// a weapon of this unit changed (fired, reloaded, ammo, pre-attack, slot): the pass looks at it now
	void weaponTouch(int objectId) { const int s = slotOfId(objectId); if (s >= 0) weaponReady[s] = 0; }
	// ---- end combat block ----

	Coord3D posOf(int s) const { Coord3D c; c.x = x[s]; c.y = y[s]; c.z = z[s]; return c; }
	Coord3D velOf(int s) const { Coord3D c; c.x = vx[s]; c.y = vy[s]; c.z = vz[s]; return c; }
	GxVec3Ref velRef(int s) { return GxVec3Ref(vx[s], vy[s], vz[s]); }
	GxVec3Ref accelRef(int s) { return GxVec3Ref(ax[s], ay[s], az[s]); }

	int size() const { return (int)obj.size(); }
	/// changes whenever any slot may have moved or been freed (compact, remove): cache slots against it
	unsigned int epoch() const { return m_epoch; }
	int liveCount() const { return (int)obj.size() - m_holes; }

	/// slot of an object / id, -1 if not stored (an Object also knows its own: gxStoreSlot())
	int slotOf(const Object *o) const;
	int slotOfId(int objectId) const { return (objectId > 0 && objectId < (int)m_slotById.size()) ? m_slotById[objectId] : -1; }

	/// the dense list of slots whose mask has `bit`, in object-ID order (valid until the next frame)
	const std::vector<int> &pass(int bit);

	// ---- writers ----
	void add(Object *o);							///< Object::setID (creation and load)
	void remove(Object *o);							///< Object::~Object and before an ID change
	void setMask(const Object *o, GxBehaviourMask set, GxBehaviourMask clear = 0);
	void setPos(const Object *o, const Coord3D &p);
	void setRadius(const Object *o, float r);
	void setPlayer(const Object *o, int playerIndex);
	void setCell(const Object *o, int cellIndex);
	void setInCells(const Object *o, bool in) { const int s = slotOf(o); if (s >= 0) inCells[s] = in ? 1 : 0; }
	/// hash-map sweep (round 5): the objects controlled by player `playerIndex` that are in GameLogic's object
	/// list, in that list's order (head first: descending Object::m_gxListSeq), from the player column instead
	/// of a walk over the whole world. `out` is cleared first.
	void playerObjectsInListOrder(int playerIndex, std::vector<Object*> &out) const;
	/// hash-map sweep #18 (switch playerKindCounts, fifth word bit 7, value 128): per player, how many stored
	/// objects have each of a few kinds (fixed by the template), kept by add / remove / setPlayer, so a walk over
	/// a player's objects looking for one of them can be skipped when there are none (same answer)
	enum GxKind { GXK_BLACK_MARKET, GXK_DRONE, GXK_COUNT };
	int kindCount(int playerIndex, GxKind k) const { return (playerIndex >= 0 && playerIndex < 32) ? m_kindCount[playerIndex][k] : 0; }
	/// walking such a list like the object list: element i, or null past the end
	static Object *gxListAtImpl(const std::vector<Object*> &v, size_t i) { return i < v.size() ? v[i] : nullptr; }
	void gxVerifyHot(unsigned int frame);				///< verify mode: stored objects' rows untouched (hotInStore)
	void compact();									///< close holes (pass() calls it once per frame)
	void bumpEpoch() { ++m_epoch; }

private:
	std::vector<int>							m_order;		///< compact(): scratch, kept between frames
	GxCol<unsigned char>						m_kinds;		///< GxKind bits of each slot (from its template)
	int													m_kindCount[32][GXK_COUNT] = {};
	void kindAdjust(int s, int d) { const int p = player[s]; if (p < 0 || p >= 32) return; for (int k = 0; k < GXK_COUNT; ++k) if (m_kinds[s] >> k & 1) m_kindCount[p][k] += d; }
	std::vector<int>							m_slotById;		///< ObjectID -> slot, -1 if none
	std::vector<int>							m_pass[GXB_COUNT];
	GxBehaviourMask								m_dirty = ~0ull;	///< pass lists to rebuild
	int														m_holes = 0;
	bool													m_unsorted = false;
	unsigned int									m_compactFrame = ~0u;
	int														m_lastId = 0;
	unsigned int									m_epoch = 1;
};

extern GxUnitStore TheGxUnitStore;
inline Object *gxListAt(const std::vector<Object*> &v, size_t i) { return GxUnitStore::gxListAtImpl(v, i); }
