/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// SimpleObjectIterator
// Implementation of a simple object iterator
// Author: Steven Johnson, September 2001
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/ObjectIter.h"

#include "Common/ThingTemplate.h"
#include "GameLogic/Object.h"

// web port (switch iterPool, fifth word bit 29): an emptied iterator puts its clumps on a per-thread
// free list and inserts take from it before the pool, so a warm game allocates no clump per object found.
// ponytail: the list keeps its high-water count of clumps for the whole session.
#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches5();
static inline bool gxIterPool() { return (GLES3_Page_Switches5() & 536870912) != 0; }
#else
static inline bool gxIterPool() { return false; }
#endif
static thread_local void *s_gxFreeClumps = nullptr;


/// @todo Doxygenize this file

SimpleObjectIterator::ClumpCompareProc SimpleObjectIterator::theClumpCompareProcs[] =
{
	nullptr,						// "fastest" gets no proc
	SimpleObjectIterator::sortNearToFar,
	SimpleObjectIterator::sortFarToNear,
	SimpleObjectIterator::sortCheapToExpensive,
	SimpleObjectIterator::sortExpensiveToCheap
};

//=============================================================================
SimpleObjectIterator::Clump::Clump()
{
	m_nextClump = nullptr;
}

//=============================================================================
SimpleObjectIterator::Clump::~Clump()
{
}

//=============================================================================
SimpleObjectIterator::SimpleObjectIterator()
{
	m_firstClump = nullptr;
	m_curClump = nullptr;
	m_clumpCount = 0;
}

//=============================================================================
SimpleObjectIterator::~SimpleObjectIterator()
{
	makeEmpty();
}

//=============================================================================
// web port (heap churn): emptied iterators keep their clumps in a per-thread free list (linked through
// m_nextClump) instead of giving them back to the pool, so a search allocates only past the high-water
// mark. Nothing resets the pools mid-run (MemoryPoolFactory::reset has no caller).
#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches4();
#else
static int GLES3_Page_Switches4() { return 0; }
#endif
static inline bool gxIterReuse() { return (GLES3_Page_Switches4() & 268435456) != 0; }	// fourth word bit 28

void SimpleObjectIterator::insert(Object *obj, Real numeric)
{
	DEBUG_ASSERTCRASH(obj, ("sorry, no nulls allowed here"));

	Clump *clump;
	if (s_gxFreeClumps && (gxIterPool() || gxIterReuse()))	// iterPool (query) and iterReuse (combat) share one free list
	{
		clump = (Clump *)s_gxFreeClumps;
		s_gxFreeClumps = clump->m_nextClump;
	}
	else
		clump = newInstance(Clump)();

	clump->m_nextClump = m_firstClump;
	m_firstClump = clump;

	clump->m_obj = obj;
	clump->m_numeric = numeric;

	++m_clumpCount;
}

//=============================================================================
Object *SimpleObjectIterator::nextWithNumeric(Real *num)
{
	Object *obj = nullptr;
	if (num)
		*num = 0.0f;

	if (m_curClump)
	{
		obj = m_curClump->m_obj;
		if (num)
			*num = m_curClump->m_numeric;
		m_curClump = m_curClump->m_nextClump;
	}

	return obj;
}

//=============================================================================
void SimpleObjectIterator::reset()
{
	m_curClump = m_firstClump;
}

//=============================================================================
void SimpleObjectIterator::makeEmpty()
{
	if (m_firstClump && gxIterPool())
	{
		Clump *tail = m_firstClump;
		while (tail->m_nextClump)
			tail = tail->m_nextClump;
		tail->m_nextClump = (Clump *)s_gxFreeClumps;
		s_gxFreeClumps = m_firstClump;
		m_firstClump = nullptr;
		m_clumpCount = 0;
	}
	while (m_firstClump)
	{
		Clump *next = m_firstClump->m_nextClump;
		if (gxIterReuse())
		{
			m_firstClump->m_nextClump = static_cast<Clump *>(s_gxFreeClumps);	// to the free list (see insert)
			s_gxFreeClumps = m_firstClump;
		}
		else
			deleteInstance(m_firstClump);
		m_firstClump = next;
		--m_clumpCount;
	}
	DEBUG_ASSERTCRASH(m_clumpCount == 0, ("hmm"));

	m_firstClump = nullptr;
	m_curClump = nullptr;
	m_clumpCount = 0;
}

//=============================================================================
void SimpleObjectIterator::sort(IterOrderType order)
{
	if (m_clumpCount == 0)
		return;

#ifdef INTENSE_DEBUG
{
	DEBUG_LOG(("\n\n---------- BEFORE sort for %d -----------",order));
	for (Clump *p = m_firstClump; p; p = p->m_nextClump)
	{
		DEBUG_LOG(("    obj %08lx numeric %f",p->m_obj,p->m_numeric));
	}
}
#endif

	ClumpCompareProc cmpProc = theClumpCompareProcs[order];

	if (!cmpProc)
		return;	// my, that was easy

	// do a basic mergesort, which works nicely for linked lists,
	// and is reasonably efficient (N log N).

  for ( Int n = 1 ; ; n *= 2 )
	{
		Clump *to_do = m_firstClump;
		Clump *tail = nullptr;
		m_firstClump = nullptr;

		Int mergeCount = 0;

		while (to_do)
		{
			++mergeCount;

			Int to_do_count = 0;

			// make two lists of length 'n' (to_do is one, sub is the other)
			Clump *sub = to_do;
			for (Int i = 0; i < n; i++)
			{
				++to_do_count;
				sub = sub->m_nextClump;
				if (!sub)
					break;
			}
			Int subCount = sub ? n : 0;

			// merge the two lists.
			DEBUG_ASSERTCRASH(to_do_count + subCount >= 0, ("uhoh"));
			while (to_do_count + subCount > 0) {

				DEBUG_ASSERTCRASH(to_do_count + subCount >= 0, ("uhoh"));

				Clump *tmp;

				// bleah, coalesce into more elegant test case
				if (subCount == 0)
				{
					DEBUG_ASSERTCRASH(to_do_count > 0, ("hmm, expected nonzero to_do_count"));
					tmp = to_do;
					to_do = to_do->m_nextClump;
					--to_do_count;
				}
				else if (to_do_count == 0)
				{
					DEBUG_ASSERTCRASH(subCount > 0, ("hmm, expected nonzero subCount"));
					tmp = sub;
					sub = sub->m_nextClump;
					--subCount;
				}
				else if ((*cmpProc)(to_do, sub) <= 0.0f)
				{
					DEBUG_ASSERTCRASH(to_do_count > 0, ("hmm, expected nonzero to_do_count"));
					tmp = to_do;
					to_do = to_do->m_nextClump;
					--to_do_count;
				}
				else
				{
					DEBUG_ASSERTCRASH(subCount > 0, ("hmm, expected nonzero subCount"));
					tmp = sub;
					sub = sub->m_nextClump;
					--subCount;
				}
				if (!sub) subCount = 0;
				if (!to_do) to_do_count = 0;

				if (tail)
					tail->m_nextClump = tmp;
				else
					m_firstClump = tmp;
				tail = tmp;
			}

			to_do = sub;
		}
		if (tail)
			tail->m_nextClump = nullptr;

		if (mergeCount <= 1)	// when we have done just one (or none) swap, we're done
			break;
	}

#ifdef INTENSE_DEBUG
{
	DEBUG_LOG(("\n\n---------- sort for %d -----------",order));
	for (Clump *p = m_firstClump; p; p = p->m_nextClump)
	{
		DEBUG_LOG(("    obj %08lx numeric %f",p->m_obj,p->m_numeric));
	}
}
#endif

	// always reset after sorting, to prevent weirdness
	reset();
}

//-----------------------------------------------------------------------------
Real SimpleObjectIterator::sortNearToFar(Clump *a, Clump *b)
{
	return a->m_numeric - b->m_numeric;
}

//-----------------------------------------------------------------------------
Real SimpleObjectIterator::sortFarToNear(Clump *a, Clump *b)
{
	return b->m_numeric - a->m_numeric;
}

//-----------------------------------------------------------------------------
Real SimpleObjectIterator::sortCheapToExpensive(Clump *a, Clump *b)
{
	return a->m_obj->getTemplate()->friend_getBuildCost() -
				 b->m_obj->getTemplate()->friend_getBuildCost();
}

//-----------------------------------------------------------------------------
Real SimpleObjectIterator::sortExpensiveToCheap(Clump *a, Clump *b)
{
	return b->m_obj->getTemplate()->friend_getBuildCost() -
				 a->m_obj->getTemplate()->friend_getBuildCost();
}

