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

// FILE: DiscreteCircle.cpp ////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       EA Pacific.
//
//                       Confidential Information
//                Copyright (C) 2002 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: DiscreteCircle.cpp
//
// Created:   John McDonald, September 2002
//
// Desc:      ???
//
//-----------------------------------------------------------------------------

#include "PreRTS.h"
#include "Common/DiscreteCircle.h"

//-------------------------------------------------------------------------------------------------
// web port (switch iterPool, fifth word bit 29): edge buffers are kept in a per-thread pool between
// circles (threat, value and shroud maps build one per unit that crosses a cell edge), so a warm game
// allocates none. A stack, so circles alive at the same time each get their own buffer.
#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches5();
static inline bool gxCirclePool() { return (GLES3_Page_Switches5() & 536870912) != 0; }
#else
static inline bool gxCirclePool() { return false; }
#endif
static thread_local std::vector<VecHorzLine> s_gxEdgePool;

DiscreteCircle::~DiscreteCircle()
{
	if (m_edges.capacity() && gxCirclePool())
	{
		m_edges.clear();
		s_gxEdgePool.emplace_back();
		s_gxEdgePool.back().swap(m_edges);
	}
}

DiscreteCircle::DiscreteCircle(Int xCenter, Int yCenter, Int radius)
{
	if (!s_gxEdgePool.empty() && gxCirclePool())
	{
		m_edges.swap(s_gxEdgePool.back());
		s_gxEdgePool.pop_back();
	}
	m_yPos = yCenter;
	m_yPosDoubled = (yCenter << 1);
	m_edges.reserve(radius << 1);	// largest that it should ever be.

	generateEdgePairs(xCenter, yCenter, radius);
	removeDuplicates();
}

//-------------------------------------------------------------------------------------------------
void DiscreteCircle::reset(Int xCenter, Int yCenter, Int radius, Bool keepBuffer)
{
	if (keepBuffer)
		m_edges.clear();
	else
		VecHorzLine().swap(m_edges);
	m_yPos = yCenter;
	m_yPosDoubled = (yCenter << 1);
	m_edges.reserve(radius << 1);	// largest that it should ever be.

	generateEdgePairs(xCenter, yCenter, radius);
	removeDuplicates();
}

//-------------------------------------------------------------------------------------------------
void DiscreteCircle::drawCircle(ScanlineDrawFunc functionToDrawWith, void *parmToPass)
{
	for (VecHorzLine::const_iterator it = m_edges.begin(); it != m_edges.end(); ++it) {
		(functionToDrawWith)(it->xStart, it->xEnd, it->yPos, parmToPass);
		if (it->yPos != m_yPos) {
			(functionToDrawWith)(it->xStart, it->xEnd, m_yPosDoubled - it->yPos, parmToPass);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void DiscreteCircle::generateEdgePairs(Int xCenter, Int yCenter, Int radius)
{
	// Uses Bresenham to generate points.
	Int x = 0;
	Int y = radius;
	Int d = (1 - radius) << 1;

	while (y >= 0) {
		HorzLine hl;
		hl.xStart = xCenter - x;
		hl.xEnd		= xCenter + x;
		hl.yPos		= yCenter + y;
		m_edges.push_back(hl);

		if (d + y > 0) {
			--y;
			d -= ((y << 1) - 1);
		}

		if (x > d) {
			++x;
			d += ((x << 1) + 1);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void DiscreteCircle::removeDuplicates()
{
	VecHorzLineIt it, nextIt;
	for ( it = m_edges.begin(); it != m_edges.end(); /* empty */) {
		nextIt = it;
		++nextIt;
		if (nextIt == m_edges.end()) {
			break;
		}

		if (it->yPos == nextIt->yPos) {
			it = m_edges.erase(it);
		} else {
			++it;
		}
	}
}
