// #11 (hash-map sweep, switch eventIndex, fifth word bit 27): the mines and the AI objects, in
// object-list order, for the rare events that walked the whole world for them (building sale and
// capture: the building's mines; sticky bombs moving to a rebuilt building; attackers moving to a
// rebuild hole or a replacement object). Objects are kept by ID (dead ones drop out on use).
// New objects are always prepended to the object list, so they go to the back of `rev` (list order is
// back to front); a reset or a load (GameLogic::m_gxListEpoch) rebuilds the lists from a walk.
#pragma once
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include <vector>
#include <algorithm>

#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches5();
inline bool gxEventIndexOn() { return (GLES3_Page_Switches5() & 134217728) != 0; }
#else
inline bool gxEventIndexOn() { return false; }
#endif

enum GxEventKind { GX_EV_MINES, GX_EV_AI, GX_EV_COUNT };

struct GxEventIndex
{
	std::vector<ObjectID> rev[GX_EV_COUNT];	///< matches, newest last
	std::vector<ObjectID> pending;			///< registered since the last use, oldest first
	UnsignedInt epoch = 0xffffffff;

	static bool match(int k, Object *o) { return k == GX_EV_MINES ? o->isKindOf(KINDOF_MINE) : o->getAI() != nullptr; }

	void note(Object *o) { pending.push_back(o->getID()); if (pending.size() > 8192) sync(); }

	void sync()
	{
		if (epoch != TheGameLogic->gxListEpoch())
		{
			for (int k = 0; k < GX_EV_COUNT; ++k) rev[k].clear();
			for (Object *o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
				for (int k = 0; k < GX_EV_COUNT; ++k)
					if (match(k, o)) rev[k].push_back(o->getID());
			for (int k = 0; k < GX_EV_COUNT; ++k) std::reverse(rev[k].begin(), rev[k].end());
			pending.clear();
			epoch = TheGameLogic->gxListEpoch();
			return;
		}
		for (ObjectID id : pending)
			if (Object *o = TheGameLogic->findObjectByID(id))
				for (int k = 0; k < GX_EV_COUNT; ++k)
					if (match(k, o)) rev[k].push_back(id);
		pending.clear();
	}

	/// the live objects of kind k, in object-list order
	void get(int k, std::vector<Object *> &out)
	{
		sync();
		out.clear();
		std::vector<ObjectID> &r = rev[k];
		size_t w = 0;
		for (size_t i = 0; i < r.size(); ++i)
			if (TheGameLogic->findObjectByID(r[i])) r[w++] = r[i];
		r.resize(w);
		for (size_t i = w; i-- > 0;)
			out.push_back(TheGameLogic->findObjectByID(r[i]));
	}
};
extern GxEventIndex TheGxEventIndex;

/// verify mode: the index's objects of kind k are the walk's, in order
inline void gxEventIndexVerify(int k, const std::vector<Object *> &got)
{
#ifdef __EMSCRIPTEN__
	extern int GLES3_Page_Switches();
	if (!(GLES3_Page_Switches() & 2097152)) return;
	static unsigned long long s_checked = 0, s_bad = 0;
	std::vector<Object *> walk;
	for (Object *o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
		if (GxEventIndex::match(k, o)) walk.push_back(o);
	++s_checked;
	if (walk != got) ++s_bad;
	printf("GXVERIFY eventIndex checked %llu, mismatches %llu\n", s_checked, s_bad);
#endif
}
