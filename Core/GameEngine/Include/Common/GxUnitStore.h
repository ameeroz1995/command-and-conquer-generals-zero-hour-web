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
// - x, y, z: write-through from Thing::setTransformMatrix (Object::reactToTransformChange); the
//   Object's matrix stays the truth for position and orientation.
// - vel: the truth for every unit with physics (PhysicsBehavior reads and writes it here; one
//   Coord3D per slot rather than three arrays, because getVelocity() hands out a Coord3D pointer).
// - radius: write-through from Object::setGeometryInfo (bounding circle radius).
// - player, playerBit: write-through from Object::setOrRestoreTeam (controlling player index).
// - cell: the partition cell (cy * cellCountX + cx) under the unit's centre, write-through with
//   x/y (-1 off the map or before the partition exists).
// - mask: set when a unit's behaviour set changes (setMask); never re-tested per tick.
#pragma once

#include <vector>
#include "Lib/BaseType.h"	// Coord3D

class Object;

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
	GXB_COUNT   = 64
};
#define GXB_BIT(b) (1ull << (b))

class GxUnitStore
{
public:
	// ---- the data: index by slot, 0 .. size()-1 ----
	std::vector<int>							id;				///< ObjectID (0 in a hole)
	std::vector<Object*>					obj;			///< nullptr in a hole
	std::vector<float>						x, y, z;		///< position (write-through)
	std::vector<Coord3D>					vel;			///< velocity (truth for physics units)
	std::vector<float>						radius;			///< bounding circle radius
	std::vector<signed char>			player;			///< controlling player index, -1 for none
	std::vector<unsigned int>			playerBit;		///< 1 << player (0 for none)
	std::vector<int>							cell;			///< partition cell index, -1 unknown
	std::vector<GxBehaviourMask>	mask;			///< behaviour bits (GXB_BIT)

	int size() const { return (int)obj.size(); }
	/// changes whenever any slot may have moved or been freed (compact, remove): cache slots against it
	unsigned int epoch() const { return m_epoch; }
	int liveCount() const { return (int)obj.size() - m_holes; }

	/// slot of an object / id, -1 if not stored
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
