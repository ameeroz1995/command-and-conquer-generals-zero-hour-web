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
// - radius: the truth for the object's bounding circle radius (Object::getBoundingCircleRadius()
//   reads it); set by Object::setGeometryInfo. The GeometryInfo keeps its own value for shape tests.
// - player, playerBit: the truth for the controlling-player index (Object::gxPlayerIndex() reads it);
//   set by Object::setOrRestoreTeam and TeamPrototype::setControllingPlayer (whole-team owner change).
// - cell: the partition cell (cy * cellCountX + cx) under the unit's centre, written by
//   PartitionData::updateCellsTouched when it changes (-1 off the map or before that).
// - mask: set when a unit's behaviour set changes (setMask); never re-tested per tick.
#pragma once

#include <vector>
#include "Lib/BaseType.h"	// Coord3D

class Object;

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
	std::vector<int>							id;				///< ObjectID (0 in a hole)
	std::vector<Object*>					obj;			///< nullptr in a hole
	std::vector<float>						x, y, z;		///< position (truth with positionInStore, else write-through)
	std::vector<float>						vx, vy, vz;		///< velocity (truth for physics units)
	std::vector<float>						radius;			///< bounding circle radius
	std::vector<signed char>			player;			///< controlling player index, -1 for none
	std::vector<unsigned int>			playerBit;		///< 1 << player (0 for none)
	std::vector<int>							cell;			///< partition cell index, -1 unknown
	std::vector<GxBehaviourMask>	mask;			///< behaviour bits (GXB_BIT)

	// ---- combat block (round 4, combat agent): weapons ----
	std::vector<int>							target;			///< current victim ObjectID, 0 none (write-through: AIUpdateInterface::setCurrentVictim)
	std::vector<unsigned int>			weaponReady;	///< frame a weapon's reload / pre-attack ends (weapon-status pass); 0 = look now
	std::vector<unsigned int>			weaponSig;		///< weapon-status pass: ammo/status signature last shown on the drawable
	std::vector<float>						sphere;			///< bounding sphere radius (splashSimd: the 3D blast test's reach)
	void setSphere(const Object *o, float r) { const int s = slotOf(o); if (s >= 0) sphere[s] = r; }
	void setTarget(const Object *o, int victimId);
	/// a weapon of this unit changed (fired, reloaded, ammo, pre-attack, slot): the pass looks at it now
	void weaponTouch(int objectId) { const int s = slotOfId(objectId); if (s >= 0) weaponReady[s] = 0; }
	// ---- end combat block ----

	Coord3D posOf(int s) const { Coord3D c; c.x = x[s]; c.y = y[s]; c.z = z[s]; return c; }
	Coord3D velOf(int s) const { Coord3D c; c.x = vx[s]; c.y = vy[s]; c.z = vz[s]; return c; }
	GxVec3Ref velRef(int s) { return GxVec3Ref(vx[s], vy[s], vz[s]); }

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
	void compact();									///< close holes (pass() calls it once per frame)
	void bumpEpoch() { ++m_epoch; }

private:
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
