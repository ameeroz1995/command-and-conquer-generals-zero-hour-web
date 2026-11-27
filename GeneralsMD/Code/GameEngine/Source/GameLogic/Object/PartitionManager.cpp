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

// FILE: PartitionManager.cpp ////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: PartitionManager.cpp
//
// Created:   Steven Johnson, September 2001
//
// Desc:      Partition management, this system will allow us to partition the
//					  objects in space, iterate objects in specified volumes,
//					  regions, by types and other properties.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "Common/GxUnitStore.h"
#include "Common/GxMatchRules.h"
#include "Common/ActionManager.h"
#include "Common/DiscreteCircle.h"
#include "Common/GxTime.h"	// also declares GLES3_Page_Switches4
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GameUtility.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Radar.h"
#include "Common/ThingFactory.h"	// for bullet type hack
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"

#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/PartitionManager.h"
#include "Common/GxUnitStore.h"
#include "GameLogic/AI.h"
#include "GameLogic/ScriptEngine.h"
#include <unordered_map>
#include <typeinfo>
#include "Common/Recorder.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/Squad.h"
#include "GameLogic/GhostObject.h"

#include "GameClient/Line2D.h"
#include "GameClient/ControlBar.h"

#ifdef RTS_DEBUG
//#include "GameClient/InGameUI.h"	// for debugHints
#endif

#ifdef PM_CACHE_TERRAIN_HEIGHT
#include "Common/MapObject.h"
#endif

// web port: performance switches (gles3_wrapper.cpp) and verify counters
#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches();
extern int GLES3_Page_Switches2();
extern int gx_verify_mismatch, gx_verify_checked;	// GameLogic.cpp
extern int GLES3_Page_Switches2();
#else
static int GLES3_Page_Switches() { return 0; }
static int GLES3_Page_Switches2() { return 0; }
static int gx_verify_mismatch, gx_verify_checked;
#endif
static Bool g_gxPairOff = false;	// timing mode (gxPairTiming): this frame runs without pairPlace / pairOffer

#ifdef DUMP_PERF_STATS
	long s_countInClosestObjects = 0;
	long s_countInClosestObjectsThisFrame = 0;
	Int64 s_timeInClosestObjects = 0;
	Int64 s_timeInClosestObjectsThisFrame = 0;
	UnsignedInt s_gcoPerfFrame = 0xffffffff;
#endif


extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);

const Real HUGE_DIST_SQR = (HUGE_DIST*HUGE_DIST);

#define DISABLE_INVALID_PREVENTION	//Steven, I had to turn this off because it was causing problem with map border resizing (USA04). -MW

//------------------------------------------------------------------------------ Performance Timers
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//-------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//         Defines
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
static PartitionContactList* TheContactList = nullptr;

//-----------------------------------------------------------------------------
//         Local Types
//-----------------------------------------------------------------------------
struct ThreatValueParms
{
	Int playerIndex;
	Real xCenter;
	Real yCenter;
	Real radius;
	UnsignedInt threatOrValue;
};

struct CollideInfo
{
	Coord3D position;
	GeometryInfo geom;
	Real angle;

	CollideInfo(const Coord3D* p, const GeometryInfo& g, Real a) : position(*p), geom(g), angle(a) { }
};

struct CellValueProcParms
{
	Int valueRequired;
	Bool greaterThan;
	ValueOrThreat valueType;
	PlayerMaskType allPlayersMask[MAX_PLAYER_COUNT];
	PlayerMaskType allowedPlayersMasks;
};

static int cellValueProc(PartitionCell* cell, void* userData);

/*
	Notes:
	-- collideLoc and collideNormal will only be filled in if result is true;
			they are undefined if result is false.
	-- either collideLoc or collideNormal may be null if you don't need 'em.
	-- collideLoc is not guaranteed to lie precisely on either 'a' or 'b'!
			(this matters iff the objects are overlapping and thus have multiple/ambiguous
			collision points; we pick an 'in-between' spot to simplify matters, such that
			the same spot will be used for the collision in both directions)
	-- collideNormal is the normal the collision surface at 'a' at collideLoc.
			(thus the normal for b is exactly opposite in direction)
	-- collideNormal is guaranteed to be a unit vector
*/
typedef Bool (*CollideTestProc)(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);

// if the dist is greater than maxDist, return false, and the output stuff is undefined.
typedef Bool (*DistCalcProc)
(
	const Coord3D *posA,
	const Object *objA,
	const Coord3D *posB,
	const Object *objB,
	Real& abDistSqr,
	Coord3D& abVec,
	Real maxDistSqr
);

//-----------------------------------------------------------------------------
//         Inline Functions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
#define NO_FILTER_PROFILING

#ifdef FILTER_PROFILING
Bool DoFilterProfiling = false;
#endif

//DECLARE_PERF_TIMER(filtersAllow)
inline Bool filtersAllow(PartitionFilter **filters, Object *objOther)
{
	//USE_PERF_TIMER(filtersAllow)
#ifdef FILTER_PROFILING
	const Int MAXR = 32;
	static const char* names[MAXR];
	static Int rejections[MAXR];
	static Int usefulRejections[MAXR];
	static Int calls = 0;
	static Int maxEver = 0;

	Int idx;
	Bool allow = true;

	if (DoFilterProfiling)
	{
		if (calls == 0)
		{
			for (idx = 0; idx < MAXR; ++idx)
			{
				names[idx] = nullptr;
				rejections[idx] = 0;
				usefulRejections[idx] = 0;
			}
			maxEver = 0;
		}
		++calls;
	}

	for (PartitionFilter **fp = filters; fp && *fp; fp++)
	{
		if (!(*fp)->allow(objOther))
		{
			if (DoFilterProfiling)
			{
				const char** namesarr = names;
				for (idx = 0; idx < maxEver; ++idx)
					if (namesarr[idx] == (*fp)->debugGetName())
						break;
				if (idx == maxEver)
				{
					namesarr[maxEver++] = (*fp)->debugGetName();
					DEBUG_ASSERTCRASH(maxEver<MAXR,("hmm, enlarge this array"));
				}
				++rejections[idx];
				if (allow)
					++usefulRejections[idx];
				allow = false;
				if (maxEver < idx)
					maxEver = idx;
			}
			else
			{
				return false;
			}
		}
	}

	if (DoFilterProfiling && calls > 0 && calls % 1000==0)
	{
		DEBUG_LOG(("\n"));
		for (idx = 0; idx < maxEver; idx++)
		{
			DEBUG_LOG(("rejections[%s] = %d (useful = %d)",names[idx],rejections[idx],usefulRejections[idx]));
		}
	}

	return allow;
#else
	for (PartitionFilter **fp = filters; fp && *fp; fp++)
	{
		if (!(*fp)->allow(objOther))
		{
			return false;
		}
	}
	// assume true if no filters rejected it
	return true;
#endif
}

//-----------------------------------------------------------------------------
inline void vecDiff_2D(const Coord3D *posA, const Coord3D *posB, Coord3D *resultVec)
{
	DEBUG_ASSERTCRASH(posA && posB && resultVec, ("null parm"));
	resultVec->x = posA->x - posB->x;
	resultVec->y = posA->y - posB->y;
	resultVec->z = 0.0f;
}

//-----------------------------------------------------------------------------
inline void vecDiff_3D(const Coord3D *posA, const Coord3D *posB, Coord3D *resultVec)
{
	DEBUG_ASSERTCRASH(posA && posB && resultVec, ("null parm"));
	resultVec->x = posA->x - posB->x;
	resultVec->y = posA->y - posB->y;
	resultVec->z = posA->z - posB->z;
}

//-----------------------------------------------------------------------------
inline Real calcSqrDist_2D(const Coord3D *dist)
{
	return sqr(dist->x) + sqr(dist->y);
}

//-----------------------------------------------------------------------------
inline Real calcSqrDist_3D(const Coord3D *dist)
{
	return sqr(dist->x) + sqr(dist->y) + sqr(dist->z);
}

//-----------------------------------------------------------------------------
inline Int absInt(Int a)
{
	if (a < 0) return -a; else return a;
}

//-----------------------------------------------------------------------------
inline Int minInt(Int a, Int b)
{
	if (a < b) return a; else return b;
}

//-----------------------------------------------------------------------------
inline Int maxInt(Int a, Int b)
{
	if (a > b) return a; else return b;
}

//-----------------------------------------------------------------------------
inline Real minReal(Real a, Real b)
{
	if (a < b) return a; else return b;
}

//-----------------------------------------------------------------------------
inline Real maxReal(Real a, Real b)
{
	if (a > b) return a; else return b;
}

//-----------------------------------------------------------------------------
//         Local Functions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
void hLineAddLooker(Int x1, Int x2, Int y, void *playerIndexVoid);
void hLineRemoveLooker(Int x1, Int x2, Int y, void *playerIndexVoid);
void hLineAddShrouder(Int x1, Int x2, Int y, void *playerIndexVoid);
void hLineRemoveShrouder(Int x1, Int x2, Int y, void *playerIndexVoid);
void hLineAddThreat(Int x1, Int x2, Int y, void *threatValueParms);
void hLineRemoveThreat(Int x1, Int x2, Int y, void *threatValueParms);
void hLineAddValue(Int x1, Int x2, Int y, void *threatValueParms);
void hLineRemoveValue(Int x1, Int x2, Int y, void *threatValueParms);

static void projectCoord3D(Coord3D *coord, const Coord3D *unitDir, Real dist);
static void flipCoord3D(Coord3D *coord);

static void rectToFourPoints(
	const CollideInfo *a,		// z is ignored
	Coord2D pts[]
);
static void testRotatedPointsAgainstRect(
	const Coord2D *pts,				// an array of 4
	const CollideInfo *a,
	Coord2D *avg,
	Int *avgTot
);

static Bool xy_collideTest_Rect_Rect(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool xy_collideTest_Rect_Circle(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool xy_collideTest_Circle_Rect(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool xy_collideTest_Circle_Circle(const Coord3D *a_pos, const Coord3D *b_pos, const Real a_radius, const Real b_radius, CollideLocAndNormal *cinfo);

static Bool collideTest_Sphere_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Sphere_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Sphere_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Cylinder_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Cylinder_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Cylinder_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Box_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Box_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);
static Bool collideTest_Box_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo);

static Bool distCalcProc_CenterAndCenter_2D(const Coord3D *posA, const Object *objA, const Coord3D *posB, const Object *objB, Real& abDistSqr, Coord3D& abVec, Real maxDistSqr);
static Bool distCalcProc_BoundaryAndBoundary_2D(const Coord3D *posA, const Object *objA, const Coord3D *posB, const Object *objB, Real& abDistSqr, Coord3D& abVec, Real maxDistSqr);
static Bool distCalcProc_CenterAndCenter_3D(const Coord3D *posA, const Object *objA, const Coord3D *posB, const Object *objB, Real& abDistSqr, Coord3D& abVec, Real maxDistSqr);
static Bool distCalcProc_BoundaryAndBoundary_3D(const Coord3D *posA, const Object *objA, const Coord3D *posB, const Object *objB, Real& abDistSqr, Coord3D& abVec, Real maxDistSqr);

static Bool doesCircleOverlapCell(Real centerX, Real centerY, Real radius, Real cellX, Real cellY, Real cellSize);

//-----------------------------------------------------------------------------
inline void projectCoord3D(Coord3D *coord, const Coord3D *unitDir, Real dist)
{
	coord->x += unitDir->x * dist;
	coord->y += unitDir->y * dist;
	coord->z += unitDir->z * dist;
}

//-----------------------------------------------------------------------------
inline void flipCoord3D(Coord3D *coord)
{
	coord->x = -coord->x;
	coord->y = -coord->y;
	coord->z = -coord->z;
}

//-----------------------------------------------------------------------------
static void testRotatedPointsAgainstRect(
	const Coord2D *pts,				// an array of 4
	const CollideInfo *a,
	Coord2D *avg,
	Int *avgTot
)
{
	//DEBUG_ASSERTCRASH(a->geom.getGeomType() == GEOMETRY_BOX, ("only boxes are ok here"));
	Real major = a->geom.getMajorRadius();
	Real minor = (a->geom.getGeomType() == GEOMETRY_SPHERE) ? a->geom.getMajorRadius() : a->geom.getMinorRadius();

	Real c = (Real)Cos(-a->angle);
	Real s = (Real)Sin(-a->angle);

	for (Int i = 0; i < 4; ++i, ++pts)
	{
		// convert to a delta relative to rect ctr
		Real ptx = pts->x - a->position.x;
		Real pty = pts->y - a->position.y;

		// inverse-rotate it to the right coord system
		Real ptx_new = (Real)fabs(ptx*c - pty*s);
		Real pty_new = (Real)fabs(ptx*s + pty*c);

		#ifdef INTENSE_DEBUG
		Real mag_a = sqr(ptx)+sqr(pty);
		Real mag_b = sqr(ptx_new)+sqr(pty_new);
		DEBUG_ASSERTCRASH(fabs(mag_a - mag_b) <= 1.0, ("hmm, unlikely"));
		#endif

		if (ptx_new <= major && pty_new <= minor)
		{
			avg->x += pts->x;
			avg->y += pts->y;
			*avgTot += 1;
		}
	}
}

//-----------------------------------------------------------------------------
static void rectToFourPoints(
	const CollideInfo *a,		// z is ignored
	Coord2D pts[]
)
{
	Real c = (Real)Cos(a->angle);
	Real s = (Real)Sin(a->angle);

	Real exc = a->geom.getMajorRadius()*c;
	Real eyc = a->geom.getMinorRadius()*c;
	Real exs = a->geom.getMajorRadius()*s;
	Real eys = a->geom.getMinorRadius()*s;

	// tl
	pts[0].x = a->position.x - exc - eys;
	pts[0].y = a->position.y + eyc - exs;

	// tr
	pts[1].x = a->position.x + exc - eys;
	pts[1].y = a->position.y + eyc + exs;

	// bl
	pts[2].x = a->position.x - exc + eys;
	pts[2].y = a->position.y - eyc - exs;

	// br
	pts[3].x = a->position.x + exc + eys;
	pts[3].y = a->position.y - eyc + exs;
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for rect/cyl & rect/sphere collisions testing.
*/
static Bool xy_collideTest_Circle_Rect(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Bool result = xy_collideTest_Rect_Circle(b, a, cinfo);
	if (cinfo)
		flipCoord3D(&cinfo->normal);
	return result;
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for rect/cyl & rect/sphere collisions testing.
*/
static Bool xy_collideTest_Rect_Circle(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
#if 1
	/// @todo srj -- this is better than the other one, since it actually handles rotated rects,
	// but still not as accurate as is could be. (srj)
	CollideInfo btmp = *b;
	btmp.geom.setMinorRadius(btmp.geom.getMajorRadius());
	return xy_collideTest_Rect_Rect(a, &btmp, cinfo);
#else
	// note, this actually tests the intersection of the the rect with the circle's
	// bounding box. in practice, this is usually good enough, since most of
	// our sphere/cyl shapes are small relative to boxes. (in fact, the WWMath
	// library takes a similar shortcut when colliding spheres with boxes in 3d.
	// so I figured it was probably good enough for us too.)

	Real circ_l = b->position.x - b->geom.getMajorRadius();
	Real circ_r = b->position.x + b->geom.getMajorRadius();
	Real circ_t = b->position.y - b->geom.getMajorRadius();
	Real circ_b = b->position.y + b->geom.getMajorRadius();
	Real rect_l = a->position.x - a->geom.getMajorRadius();
	Real rect_r = a->position.x + a->geom.getMajorRadius();
	Real rect_t = a->position.y - a->geom.getMinorRadius();
	Real rect_b = a->position.y + a->geom.getMinorRadius();

	if (circ_r >= rect_l &&	circ_l <= rect_r &&
		circ_b >= rect_t &&	circ_t <= rect_b)
	{
		if (cinfo)
		{
			vecDiff_2D(&b->position, &a->position, &cinfo->normal);
			cinfo->normal.normalize();
			cinfo->loc.x = (maxReal(circ_l, rect_l) + minReal(circ_r, rect_r)) * 0.5f;
			cinfo->loc.y = (maxReal(circ_t, rect_t) + minReal(circ_b, rect_b)) * 0.5f;
			cinfo->loc.z = (a->position.z + b->position.z) * 0.5f;
		}
		return true;
	}
	return false;
#endif
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for cyl & sphere collisions testing.
*/
static Bool xy_collideTest_Circle_Circle(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Coord3D diff;
	vecDiff_2D(&b->position, &a->position, &diff);
	Real distSqr = calcSqrDist_2D(&diff);
	Real touchingDistSqr = sqr(a->geom.getMajorRadius() + b->geom.getMajorRadius());
	if (distSqr <= touchingDistSqr)
	{
		// calc the approximate location and normal.
		if (cinfo)
		{
			cinfo->normal = diff;
			cinfo->normal.normalize();
			cinfo->loc = a->position;
			projectCoord3D(&cinfo->loc, &cinfo->normal, a->geom.getMajorRadius());
		}

		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
/**
	2d utility routine for rect collisions testing.
*/
static Bool xy_collideTest_Rect_Rect(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Coord2D pts[4];
	Coord2D avg; avg.x = avg.y = 0.0f;
	Int avgTot = 0;

	rectToFourPoints(a, pts);
	testRotatedPointsAgainstRect(pts, b, &avg, &avgTot);

	rectToFourPoints(b, pts);
	testRotatedPointsAgainstRect(pts, a, &avg, &avgTot);

	if (avgTot > 0)
	{
		if (cinfo)
		{
			// this is a little hokey, but generally adequate...
			cinfo->loc.x = avg.x / avgTot;
			cinfo->loc.y = avg.y / avgTot;
			cinfo->loc.z = (a->position.z + b->position.z) * 0.5f;

			// yow, what exactly does it mean to find the normal to two *intersecting*
			// rects? with "true" physics we could project ahead and determine
			// what the collision point would be, which would of course always
			// give a perfect, symmetrical normal. however, we almost always determine
			// collisions "after the fact" and thus the rects already intersect, meaning
			// that it's tricky to find the normal. we could do it the "right" way, which
			// would be to back up the objects to find the perfect point (see above), but
			// we don't have velocity information at this point, so we can't really
			// do that. so, naturally, we provide a (poor) guess by just doing the center-to-center
			// normal. this is rarely the correct surface normal for a box-box collision,
			// but until I either (a) rewrite everything to have predictive collisions,
			// or (b) come up with a better definition of a useful normal in this case,
			// I'm not sure we can do a whole lot better... (srj)
			vecDiff_2D(&b->position, &a->position, &cinfo->normal);
			cinfo->normal.normalize();
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
inline Bool z_collideTest_Sphere_Nonsphere(CollideTestProc xyproc, const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	// case 1: center of sphere is within the nonsphere z-space.
	if (a->position.z >= b->position.z && a->position.z <= b->position.z + b->geom.getMaxHeightAbovePosition())
	{
		if (xyproc(a, b, cinfo))
		{
			// a side-to-side collision; just need to adjust the z-coord of collideLoc
			if (cinfo)
			{
				cinfo->loc.z = a->position.z;
			}
			return true;
		}
		return false;
	}

	// case 2: center of sphere is above or below nonsphere, but within
	// major-radius of top or bottom.

	// 2a: sphere is below the nonsphere
	Real b_bot = b->position.z;
	if (a->position.z < b_bot && a->position.z + a->geom.getMajorRadius() >= b_bot)
	{
		// find the radius of the slice of the sphere that is at b_bot
		CollideInfo amod = *a;
		amod.position.z = b_bot;
		amod.geom.setMajorRadius((Real)sqrtf(sqr(a->geom.getMajorRadius()) - sqr(b_bot - a->position.z)));
		if (xyproc(&amod, b, cinfo))
		{
			// if you want to have 'end' collisions, you should add something like:
			//		if (circle a is contained by circle b) normal points straight up;
			// but since that's currently never possible (and even if it was, wouldn't matter)
			// we don't bother with the calculation.
			return true;
		}
		return false;
	}

	// 2b: sphere is above the nonsphere
	Real b_top = b->position.z + b->geom.getMaxHeightAbovePosition();
	if (a->position.z > b_top && a->position.z - a->geom.getMajorRadius() <= b_top)
	{
		CollideInfo amod = *a;
		amod.position.z = b_top;
		amod.geom.setMajorRadius((Real)sqrtf(sqr(a->geom.getMajorRadius()) - sqr(a->position.z - b_top)));
		if (xyproc(&amod, b, cinfo))
		{
			// if you want to have 'end' collisions, you should add something like:
			//		if (circle a is contained by circle b) normal points straight down;
			// but since that's currently never possible (and even if it was, wouldn't matter)
			// we don't bother with the calculation.
			return true;
		}
		return false;
	}

	return false;
}

//-----------------------------------------------------------------------------
inline Bool z_collideTest_Nonsphere_Nonsphere(CollideTestProc xyproc, const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	// note: we already know that there is a z-intersection (our caller filters that out)
	// so we need not recheck that here.

	// Preflight for close objects.
	Real dSqr = sqr(a->position.x-b->position.x) + sqr(a->position.y-b->position.y);
	Real minRadius = a->geom.getMajorRadius();
	Real r = a->geom.getMinorRadius();
	if (minRadius>r) minRadius = r;
	r = b->geom.getMajorRadius();
	if (minRadius>r) minRadius = r;
	r = b->geom.getMinorRadius();
	if (minRadius>r) minRadius = r;

	Bool closeEnough = sqr(minRadius) > dSqr;

	if (closeEnough || xyproc(a, b, cinfo))
	{
		// since we allow objects to overlap at times, how do we distinguish
		// between a "top-to-bottom" vs "side-to-side" collision?
		// currently, we don't: cylindrical shapes currently never collide
		// top-to-bottom, so we just declare everything to be side-to-side.
		// if this changes, need to smarten this.

		// case 1: side-to-side collision
		// just need to adjust the z-coord of collideLoc
		if (cinfo)
		{
			if (b->position.z > a->position.z)
				cinfo->loc.z = (b->position.z + a->position.z + a->geom.getMaxHeightAbovePosition()) * 0.5f;
			else
				cinfo->loc.z = (a->position.z + b->position.z + b->geom.getMaxHeightAbovePosition()) * 0.5f;
		}
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Sphere_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Coord3D diff;
	vecDiff_3D(&b->position, &a->position, &diff);
	Real distSqr = calcSqrDist_3D(&diff);
	Real touchingDistSqr = sqr(a->geom.getMajorRadius() + b->geom.getMajorRadius());
	if (distSqr <= touchingDistSqr)
	{
		if (cinfo)
		{
			cinfo->normal = diff;
			cinfo->normal.normalize();
			cinfo->loc = a->position;
			projectCoord3D(&cinfo->loc, &cinfo->normal, a->geom.getMajorRadius());
		}

		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Sphere_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Circle, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Sphere_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Rect, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Cylinder_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Bool result = z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Circle, b, a, cinfo);
	if (cinfo)
		flipCoord3D(&cinfo->normal);
	return result;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Cylinder_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Circle_Circle, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Cylinder_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Circle_Rect, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Box_Sphere(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	Bool result = z_collideTest_Sphere_Nonsphere(xy_collideTest_Circle_Rect, b, a, cinfo);
	if (cinfo)
		flipCoord3D(&cinfo->normal);
	return result;
}

//-----------------------------------------------------------------------------
static Bool collideTest_Box_Cylinder(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Rect_Circle, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool collideTest_Box_Box(const CollideInfo *a, const CollideInfo *b, CollideLocAndNormal *cinfo)
{
	return z_collideTest_Nonsphere_Nonsphere(xy_collideTest_Rect_Rect, a, b, cinfo);
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_CenterAndCenter_2D(
	const Coord3D *posA,
	const Object *objA,
	const Coord3D *posB,
	const Object *objB,
	Real& abDistSqr,
	Coord3D& abVec,
	Real maxDistSqr
)
{
	// note that object positions are defined as the bottom center of the geometry,
	// thus we must add the radius to the z coord to get the proper center of the bounding sphere.
	Coord3D diff;
	diff.x = posB->x - posA->x;
	diff.y = posB->y - posA->y;
	diff.z = 0.0f;

	//if (abDistSqr)
	{
		abDistSqr = sqr(diff.x) + sqr(diff.y);
	}

	//if (abVec)
	{
		abVec = diff;
	}

	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_BoundaryAndBoundary_2D(
	const Coord3D *posA,
	const Object *objA,
	const Coord3D *posB,
	const Object *objB,
	Real& abDistSqr,
	Coord3D& abVec,
	Real maxDistSqr
)
{
	Coord3D diff;
	diff.x = posB->x - posA->x;
	diff.y = posB->y - posA->y;
	diff.z = 0.0f;

	Real actualDistSqr = sqr(diff.x) + sqr(diff.y);

	Real shrinkFactor = 1.0f;
	Real shrunkenDistSqr = actualDistSqr;
	Real totalRad = (objA ? objA->getBoundingCircleRadius() : 0.0f) +
							(objB ? objB->getBoundingCircleRadius() : 0.0f);

	if (totalRad > 0.0f)
	{
		Real actualDist = sqrtf(actualDistSqr);
		Real shrunkenDist = actualDist - totalRad;
		if (shrunkenDist <= 0.0f)
		{
			shrinkFactor = 0.0f;
			shrunkenDistSqr = 0.0f;	// sorry, distances can't be negative
		}
		else
		{
			shrinkFactor = shrunkenDist / actualDist;
			shrunkenDistSqr = sqr(shrunkenDist);
		}
	}

	//if (abDistSqr)
	{
		abDistSqr = shrunkenDistSqr;
	}

	//if (abVec)
	{
		DEBUG_ASSERTCRASH(shrinkFactor >= 0.0f && shrinkFactor <= 1.0f, ("Hmm, this should not be possible."));
		diff.x *= shrinkFactor;
		diff.y *= shrinkFactor;
		abVec = diff;
	}
	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_CenterAndCenter_3D(
	const Coord3D *posA,
	const Object *objA,
	const Coord3D *posB,
	const Object *objB,
	Real& abDistSqr,
	Coord3D& abVec,
	Real maxDistSqr
)
{
	// note that object positions are defined as the bottom center of the geometry,
	// thus we must add the radius to the z coord to get the proper center of the bounding sphere.
	Coord3D diff;
	diff.x = posB->x - posA->x;
	diff.y = posB->y - posA->y;
	diff.z = posB->z - posA->z;

	//if (abDistSqr)
	{
		abDistSqr = sqr(diff.x) + sqr(diff.y) + sqr(diff.z);
	}

	//if (abVec)
	{
		abVec = diff;
	}
	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
static Bool distCalcProc_BoundaryAndBoundary_3D(
	const Coord3D *posA,
	const Object *objA,
	const Coord3D *posB,
	const Object *objB,
	Real& abDistSqr,
	Coord3D& abVec,
	Real maxDistSqr
)
{
	const GeometryInfo* geomA = objA ? &objA->getGeometryInfo() : nullptr;
	const GeometryInfo* geomB = objB ? &objB->getGeometryInfo() : nullptr;

	// note that object positions are defined as the bottom center of the geometry,
	// thus we must add the radius to the z coord to get the proper center of the bounding sphere.
	Coord3D diff;
	diff.x = posB->x - posA->x;
	diff.y = posB->y - posA->y;
	diff.z = ((posB->z + (geomB ? geomB->getZDeltaToCenterPosition() : 0.0f))
						- (posA->z + (geomA ? geomA->getZDeltaToCenterPosition() : 0.0f)));

	Real actualDistSqr = sqr(diff.x) + sqr(diff.y) + sqr(diff.z);

	Real shrinkFactor = 1.0f;
	Real shrunkenDistSqr = actualDistSqr;
	Real totalRad = (geomA?geomA->getBoundingSphereRadius():0) + (geomB?geomB->getBoundingSphereRadius():0);
	if (totalRad > 0.0f)
	{
		Real actualDist = sqrtf(actualDistSqr);
		Real shrunkenDist = actualDist - totalRad;
		if (shrunkenDist <= 0.0f)
		{
			shrinkFactor = 0.0f;
			shrunkenDistSqr = 0.0f;	// sorry, distances can't be negative
		}
		else
		{
			shrinkFactor = shrunkenDist / actualDist;
			shrunkenDistSqr = sqr(shrunkenDist);
		}
	}

	//if (abDistSqr)
	{
		abDistSqr = shrunkenDistSqr;
	}

	//if (abVec)
	{
		DEBUG_ASSERTCRASH(shrinkFactor >= 0.0f && shrinkFactor <= 1.0f, ("Hmm, this should not be possible."));
		diff.x *= shrinkFactor;
		diff.y *= shrinkFactor;
		diff.z *= shrinkFactor;
		abVec = diff;
	}
	return abDistSqr < maxDistSqr;
}

//-----------------------------------------------------------------------------
//         Private Types
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Private Data
//-----------------------------------------------------------------------------

static DistCalcProc theDistCalcProcs[] =
{
	distCalcProc_CenterAndCenter_2D,
	distCalcProc_CenterAndCenter_3D,
	distCalcProc_BoundaryAndBoundary_2D,
	distCalcProc_BoundaryAndBoundary_3D,
};

// NOTE: This *DEPENDS* on the order of the geometry enum defines
static CollideTestProc theCollideTestProcs[] =
{
	collideTest_Sphere_Sphere,
	collideTest_Sphere_Cylinder,
	collideTest_Sphere_Box,
	collideTest_Cylinder_Sphere,
	collideTest_Cylinder_Cylinder,
	collideTest_Cylinder_Box,
	collideTest_Box_Sphere,
	collideTest_Box_Cylinder,
	collideTest_Box_Box
};

//-----------------------------------------------------------------------------
//         Public Data
//-----------------------------------------------------------------------------
PartitionManager *ThePartitionManager = nullptr;  ///< the object manager singleton

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

class PartitionContactListNode : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(PartitionContactListNode, "PartitionContactListNode" )

public:
	PartitionContactListNode*			m_nextHash;	///< next node with same hash value
	PartitionContactListNode*			m_next;			///< next node
	PartitionData*								m_obj;			///< one object that is possibly colliding
	PartitionData*								m_other;		///< the other object (or null for collisions with the terrain)
	Int														m_hashValue;///< index into hash table
};

inline PartitionContactListNode::~PartitionContactListNode() { }

//-----------------------------------------------------------------------------

class PartitionContactList
{
private:

	/*
		socketcount should be prime (and "not too close to a power of 2) for best results.

		if this one isn't large enough, try this website:
		http://www.utm.edu/research/primes/lists/small/1000.txt

		So how is this chosen? Eh, pretty much based on experimentation.
	*/
	enum { PartitionContactList_SOCKET_COUNT = 5381 };


	PartitionContactListNode* m_contactHash[PartitionContactList_SOCKET_COUNT];
	PartitionContactListNode* m_contactList;

	// Web port (switch contactTable, second switch word bit 0): big battles add tens of thousands
	// of pairs a frame, so 5381 buckets meant long chains to walk for every pair. This table has
	// 2^18 buckets (allocated once, emptied through the list). Only the duplicate check uses the
	// table; the list order, and so the game, is the same.
	enum { GX_BIG_BITS = 18 };
	static PartitionContactListNode** s_gxBig;
	PartitionContactListNode** m_gxHash;	// the big table, or null for m_contactHash

	// Web port (switch contactArray, second switch word bit 3): the pair list rebuilt as flat
	// arrays. Pairs go into one array in the order they are found; the original list put each new
	// pair at its head, so it is walked backwards here. Repeats (either order) are turned away by
	// an open-addressing table of pointer pairs whose slots belong to one frame's generation, so
	// nothing is allocated, freed or cleared per pair. Same pairs, same order, same reactions.
	struct GxPair { PartitionData* a; PartitionData* b; };
	struct GxSlot { unsigned long long key; UnsignedInt gen; };
	static std::vector<GxPair> s_gxPairs;
	static GxSlot* s_gxSlots;
	static UnsignedInt s_gxSlotBits, s_gxGen;
	Bool m_gxArray;
	Bool m_gxVerify;	///< verify mode: the old list is built too and compared before processing
	static void gxGrow();

	// Web port (switch leanPairs, second switch word bit 9; needs contactArray): offers read only
	// the two partition-data pointers. Pairs with an object-less side (ghosts of fogged dead
	// buildings) are kept and skipped when processed instead of being turned away here; that
	// does not change the order of the other pairs. Repeats are found in a table of 8-byte keys
	// (0 = empty) a quarter of the size of the generation table; the used slots are listed and
	// emptied when the list is reset.
	static unsigned long long* s_gxKeys;
	static UnsignedInt s_gxKeyBits;
	static std::vector<UnsignedInt> s_gxUsed;
	Bool m_gxLean;
	static void gxGrowLean();

public:

	PartitionContactList()
	{
		memset(m_contactHash, 0, sizeof(m_contactHash));
		m_contactList = nullptr;
		m_gxHash = nullptr;
		m_gxArray = false;
		m_gxVerify = false;
		m_gxLean = false;
#ifdef __EMSCRIPTEN__
		if (GLES3_Page_Switches2() & 8)
		{
			m_gxArray = true;
			m_gxVerify = (GLES3_Page_Switches() & 2097152) != 0;
			s_gxPairs.clear();
			if (GLES3_Page_Switches2() & 512)
			{
				m_gxLean = true;
				if (!s_gxKeys)
				{
					s_gxKeyBits = 17;
					s_gxKeys = (unsigned long long*)calloc((size_t)1 << s_gxKeyBits, sizeof(unsigned long long));
				}
			}
			if (!s_gxSlots)
			{
				s_gxSlotBits = 18;
				s_gxSlots = (GxSlot*)calloc((size_t)1 << s_gxSlotBits, sizeof(GxSlot));
			}
			if (++s_gxGen == 0)	// wrapped: old stamps could match again
			{
				memset(s_gxSlots, 0, sizeof(GxSlot) << s_gxSlotBits);
				s_gxGen = 1;
			}
		}
		else if (GLES3_Page_Switches2() & 1)
		{
			if (!s_gxBig)
				s_gxBig = (PartitionContactListNode**)calloc((size_t)1 << GX_BIG_BITS, sizeof(PartitionContactListNode*));
			m_gxHash = s_gxBig;
		}
#endif
	}

	~PartitionContactList()
	{
		resetContactList();
	}

	/**
		add a pair of objects to the contact list. If the pair is already
		present (in either this order or reverse order), nothing is done.
		Note that it is OK for other==null (this indicates a collisions with
		the ground) but it is not OK for obj==null.
	*/
	void addToContactList(PartitionData *obj, PartitionData *other);

	/**
		process all pairs in the contact list: first, determine if they
		truly collide, based on their geometry. if so, call the collide
		actions for each object in the pair.
	*/
	void processContactList();
	void gxProcessPair(PartitionData*& a, PartitionData*& b);

	/**
		discard the contents of the contact list.
	*/
	void resetContactList();

	/**
		remove any contacts that refer to the given data.
	*/
	void removeSpecificPartitionData(PartitionData* data);

};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

#ifdef FASTER_GCO
// nothing
#else
//-----------------------------------------------------------------------------
CellOutwardIterator::CellOutwardIterator(PartitionManager *mgr, Int x, Int y)
{
	m_mgr = mgr;
	m_cellCenterX = x;
	m_cellCenterY = y;
	m_cellRadius = 0;
	m_maxRadius = maxInt(maxInt(x, mgr->getCellCountX() - x), maxInt(y, mgr->getCellCountY() - y));
	m_delta[0] = m_delta[1] = 0;
	m_inc = -1;
	m_cnt = 0;
	m_axis = 1;
	m_iter = 1;
	m_delta[m_axis] -= m_inc;
}

//-----------------------------------------------------------------------------
CellOutwardIterator::~CellOutwardIterator()
{
}

//-----------------------------------------------------------------------------
PartitionCell *CellOutwardIterator::nextCell(Bool skipEmpties)
{

try_again:

	if (m_cellRadius <= m_maxRadius)
	{

		if (m_iter > 0)
		{
			m_delta[m_axis] += m_inc;
			--m_iter;
			PartitionCell *cell = m_mgr->getCellAt(m_cellCenterX + m_delta[0], m_cellCenterY + m_delta[1]);
			if (!cell)
				goto try_again;
			if (skipEmpties && !cell->getFirstCoiInCell())
				goto try_again;
			return cell;
		}

		m_axis += 1;
		if (m_axis >= 2)
		{
			m_axis = 0;
			m_inc = -m_inc;
			m_cnt += 1;
			m_cellRadius = maxInt(absInt(m_delta[0]), absInt(m_delta[1]));
		}

		m_iter = m_cnt;
		goto try_again;
	}

	return nullptr;
}
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
CellAndObjectIntersection::CellAndObjectIntersection()
{
	m_gxX = m_gxY = 0.0f;
	m_gxR = HUGE_DIST;
	m_gxSlotPrev = m_gxSlotNext = nullptr;
	m_gxSeq = 0;
	m_cell = nullptr;
	m_module = nullptr;
	m_prevCoi = nullptr;
	m_nextCoi = nullptr;
	m_gxPlayer = MAX_PLAYER_COUNT;
}

// Web port: the per-cell player counts (PartitionCell::m_gxCount, switch enemySearchSkip).
// The slot is the object's controlling player when it enters the cell, moved by
// PartitionData::gxTeamChanged; ghosts and ownerless objects use MAX_PLAYER_COUNT.
static Int gxSlotOf(const PartitionData *module)
{
	const Object *obj = module ? module->getObject() : nullptr;
	const Player *p = obj ? obj->getControllingPlayer() : nullptr;
	Int i = p ? p->getPlayerIndex() : MAX_PLAYER_COUNT;
	return (i >= 0 && i < MAX_PLAYER_COUNT) ? i : MAX_PLAYER_COUNT;
}
// Set by Team::setControllingPlayer: a whole team changed owner without its objects hearing of
// it, so the counts may be stale; searches stop skipping for the rest of the game.
Bool g_gxCellCountsStale = false;
UnsignedInt *g_gxCellMasks = nullptr;
Short *g_gxShroudCur = nullptr, *g_gxShroudAct = nullptr;
Int g_gxShroudN = 0;
Int g_gxCellMaskW = 0;
UnsignedInt *g_gxBlockMasks = nullptr;
Int g_gxBlockMaskW = 0;
static Int g_gxCellMaskH = 0;

void gxRefreshBlockMask(Int cellX, Int cellY)
{
	const Int x0 = (cellX >> GX_BLOCK_SHIFT) << GX_BLOCK_SHIFT, y0 = (cellY >> GX_BLOCK_SHIFT) << GX_BLOCK_SHIFT;
	const Int x1 = min(x0 + (1 << GX_BLOCK_SHIFT), g_gxCellMaskW), y1 = min(y0 + (1 << GX_BLOCK_SHIFT), g_gxCellMaskH);
	UnsignedInt m = 0;
	for (Int y = y0; y < y1; ++y)
		for (Int x = x0; x < x1; ++x)
			m |= g_gxCellMasks[y * g_gxCellMaskW + x];
	g_gxBlockMasks[(cellY >> GX_BLOCK_SHIFT) * g_gxBlockMaskW + (cellX >> GX_BLOCK_SHIFT)] = m;
}

void CellAndObjectIntersection::gxSetPlayerSlot(Int slot)
{
	if (m_cell && slot != m_gxPlayer)
	{
		m_cell->gxCount(m_gxPlayer, -1);
		m_cell->gxCount(slot, +1);
		gxSlotUnlink();
		gxSlotLink(slot);
	}
	m_gxPlayer = slot;
}

void CellAndObjectIntersection::gxSlotLink(Int slot)
{
	// the list runs from high to low sequence, like the cell list; a new COI has the highest, so
	// it normally goes first (a slot change walks to its place)
	CellAndObjectIntersection *prev = nullptr, *next = m_cell->gxSlotHead(slot);
	while (next && next->m_gxSeq > m_gxSeq)
	{
		prev = next;
		next = next->m_gxSlotNext;
	}
	m_gxSlotPrev = prev;
	m_gxSlotNext = next;
	if (prev)
		prev->m_gxSlotNext = this;
	else
		m_cell->gxSlotHead(slot) = this;
	if (next)
		next->m_gxSlotPrev = this;
}

void CellAndObjectIntersection::gxMoveToFront()
{
	// the cell list runs from high to low sequence (switch searchSlotSkip merges per-slot lists by
	// it), so the COI put at the head takes a new, highest sequence and moves in its slot list too
	m_cell->friend_removeFromCellList(this);
	m_cell->friend_addToCellList(this);
	gxSlotUnlink();
	m_gxSeq = m_cell->gxTakeSeq();
	gxSlotLink(m_gxPlayer);
}

void CellAndObjectIntersection::gxSlotUnlink()
{
	if (m_gxSlotPrev)
		m_gxSlotPrev->m_gxSlotNext = m_gxSlotNext;
	else
		m_cell->gxSlotHead(m_gxPlayer) = m_gxSlotNext;
	if (m_gxSlotNext)
		m_gxSlotNext->m_gxSlotPrev = m_gxSlotPrev;
	m_gxSlotPrev = m_gxSlotNext = nullptr;
}

//-----------------------------------------------------------------------------
CellAndObjectIntersection::~CellAndObjectIntersection()
{
	DEBUG_ASSERTCRASH(m_prevCoi == nullptr && m_nextCoi == nullptr, ("destroying a linked COI"));
	DEBUG_ASSERTCRASH(!getModule(), ("destroying an in-use COI"));
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::friend_addToCellList(CellAndObjectIntersection **pListHead)
{
	DEBUG_ASSERTCRASH(m_prevCoi == nullptr && m_nextCoi == nullptr && *pListHead != this, ("trying to add a cell to list, but it appears to already be in a list"));

	this->m_nextCoi = *pListHead;
	if (*pListHead)
		(*pListHead)->m_prevCoi = this;
	*pListHead = this;
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::friend_removeFromCellList(CellAndObjectIntersection **pListHead)
{
#define DEBUG_ASSERTINLIST(c) \
	DEBUG_ASSERTCRASH((c)->m_prevCoi != nullptr || (c)->m_nextCoi != nullptr || *pListHead == (c), ("cell is not in list"));

	DEBUG_ASSERTINLIST(this);

	if (this->m_prevCoi)
	{
		DEBUG_ASSERTINLIST(this->m_prevCoi);
		DEBUG_ASSERTCRASH(*pListHead != this, ("bad linkage"));
		this->m_prevCoi->m_nextCoi = this->m_nextCoi;
	}
	else
	{
		DEBUG_ASSERTCRASH(*pListHead == this, ("bad linkage"));
		*pListHead = this->m_nextCoi;
	}

	if (this->m_nextCoi)
	{
		DEBUG_ASSERTINLIST(this->m_nextCoi);
		this->m_nextCoi->m_prevCoi = this->m_prevCoi;
	}

	this->m_prevCoi = nullptr;
	this->m_nextCoi = nullptr;

#undef DEBUG_ASSERTINLIST
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::addCoverage(PartitionCell *cell, PartitionData *module)
{
	DEBUG_ASSERTCRASH(m_cell == nullptr || m_cell == cell, ("mismatch"));
	DEBUG_ASSERTCRASH(m_module == nullptr || m_module == module, ("mismatch"));

	if (m_module != nullptr && m_module != module)
	{
		DEBUG_CRASH(("COI already in use by another module!"));
		return;
	}

	if (m_cell == nullptr)
	{
		cell->friend_addToCellList(this);
		m_gxPlayer = gxSlotOf(module);
		cell->gxCount(m_gxPlayer, +1);
		m_cell = cell;
		m_gxSeq = cell->gxTakeSeq();
		gxSlotLink(m_gxPlayer);
	}

	m_cell = cell;
	m_module = module;

	// web port (switch searchCache): where the object is now; no object (a ghost): never turned
	// away by the cached test
	const Object *o = module->getObject();
	if (o)
	{
		const GeometryInfo &g = o->getGeometryInfo();
		gxSetPlace(o->getPosition()->x, o->getPosition()->y, max(g.getBoundingCircleRadius(), g.getBoundingSphereRadius()));
	}
	else
		gxSetPlace(0.0f, 0.0f, HUGE_DIST);
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::removeAllCoverage()
{
	if (m_module == nullptr)
	{
		DEBUG_CRASH(("COI not in use"));
		return;
	}

	m_cell->friend_removeFromCellList(this);
	m_cell->gxCount(m_gxPlayer, -1);
	gxSlotUnlink();
	m_cell = nullptr;
	m_module = nullptr;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionCell::PartitionCell()
{
	m_cellX = m_cellY = 0;
	m_firstCoiInCell = nullptr;
	m_coiCount = 0;
	memset(m_gxCount, 0, sizeof(m_gxCount));
	m_gxMask = 0;
	memset(m_gxSlotHead, 0, sizeof(m_gxSlotHead));
	m_gxNextSeq = 0;
#ifdef PM_CACHE_TERRAIN_HEIGHT
	m_loTerrainZ = HUGE_DIST;		// huge positive
	m_hiTerrainZ = -HUGE_DIST;	// huge negative
#endif
	/*
		You may be asking yourself: why do we model the shroud for all players,
		rather than just the local player? And the answer is: because this allows
		us to checksum these values for net games, to help prevent "shroud cheaters"
		(who use a trainer to disable the shroud on their system).
	*/
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		// Default is "passive shroud".  1,0.
		m_shroudLevel[i].m_currentShroud = 1;
		m_shroudLevel[i].m_activeShroudLevel = 0;

		// default cash value is 0
		m_cashValue[i] = 0;

		// default threat value is 0
		m_threatValue[i] = 0;
	}
}

//-----------------------------------------------------------------------------
PartitionCell::~PartitionCell()
{
	DEBUG_ASSERTCRASH(m_firstCoiInCell == nullptr && m_coiCount == 0, ("destroying a nonempty PartitionCell"));
	// but don't destroy the Cois; they don't belong to us
}

//-----------------------------------------------------------------------------
void PartitionCell::invalidateShroudedStatusForAllCois(Int playerIndex)
{
	for (CellAndObjectIntersection* coi = m_firstCoiInCell; coi; coi = coi->getNextCoi())
	{
		coi->getModule()->invalidateShroudedStatusForPlayer(playerIndex);
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::gxShroudToStruct()
{
	if (!g_gxShroudCur)
		return;
	for (Int p = 0; p < MAX_PLAYER_COUNT; ++p)
	{
		m_shroudLevel[p].m_currentShroud = gxCur(p);
		m_shroudLevel[p].m_activeShroudLevel = gxAct(p);
	}
}

void PartitionCell::gxShroudFromStruct()
{
	if (!g_gxShroudCur)
		return;
	for (Int p = 0; p < MAX_PLAYER_COUNT; ++p)
	{
		gxCur(p) = m_shroudLevel[p].m_currentShroud;
		gxAct(p) = m_shroudLevel[p].m_activeShroudLevel;
	}
}

void PartitionCell::gxShroudEdge(Int playerIndex, CellShroudStatus newShroud)
{
	// as addLooker / removeLooker on an edge trigger
	invalidateShroudedStatusForAllCois( playerIndex );
	if( playerIndex == rts::getObservedOrLocalPlayer()->getPlayerIndex() )
	{
		TheDisplay->setShroudLevel(m_cellX, m_cellY, newShroud);
		TheRadar->setShroudLevel(m_cellX, m_cellY, newShroud);
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::addLooker(Int playerIndex)
{
	CellShroudStatus oldShroud = getShroudStatusForPlayer( playerIndex );
	// The decreasing Algorithm: A 1 will go straight to -1, otherwise it just gets decremented
	gxCur(playerIndex) = min( gxCur(playerIndex) - 1, -1 );

	CellShroudStatus newShroud = getShroudStatusForPlayer( playerIndex );

//	DEBUG_LOG(( "ADD    %d, %d.  CS = %d, AS = %d for player %d.",
//							m_cellX,
//							m_cellY,
//							gxCur(playerIndex),
//							gxAct(playerIndex),
//							playerIndex
//							));

	if( oldShroud != newShroud )
	{
		// On an edge trigger, tell all objects to think about their shroudedness
		invalidateShroudedStatusForAllCois( playerIndex );

		if( playerIndex == rts::getObservedOrLocalPlayer()->getPlayerIndex() )
		{
			// and if this is the local player, do the Client update.
			TheDisplay->setShroudLevel(m_cellX, m_cellY, newShroud);
			TheRadar->setShroudLevel(m_cellX, m_cellY, newShroud);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeLooker(Int playerIndex)
{
	CellShroudStatus oldShroud = getShroudStatusForPlayer( playerIndex );
	// the increasing Algorithm: a -1 goes up to min(1,activeLevel), otherwise it just gets incremented
	if( gxCur(playerIndex) == -1 )
		gxCur(playerIndex) = min( gxAct(playerIndex), (Short)1 );
	else
	{
		DEBUG_ASSERTCRASH( gxCur(playerIndex) < 0, ("Someone is RemoveLooker-ing on a cell that is not looked at.  This will make a permanent shroud blob.") );
		gxCur(playerIndex)++;
	}
	CellShroudStatus newShroud = getShroudStatusForPlayer( playerIndex );

//	DEBUG_LOG(( "REMOVE %d, %d.  CS = %d, AS = %d for player %d.",
//							m_cellX,
//							m_cellY,
//							gxCur(playerIndex),
//							gxAct(playerIndex),
//							playerIndex
//							));

	if( oldShroud != newShroud )
	{
		// On an edge trigger, tell all objects to think about their shroudedness
		invalidateShroudedStatusForAllCois( playerIndex );

		if( playerIndex == rts::getObservedOrLocalPlayer()->getPlayerIndex() )
		{
			// and if this is the local player, do the Client update.
			TheDisplay->setShroudLevel(m_cellX, m_cellY, newShroud);
			TheRadar->setShroudLevel(m_cellX, m_cellY, newShroud);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::addShrouder( Int playerIndex )
{
	CellShroudStatus oldShroud = getShroudStatusForPlayer( playerIndex );
	// Increasing active shroud: activeLevel gets incremented, and CS is set to 1 if at zero
	// do the algorithm
	gxAct(playerIndex)++;
	if( gxCur(playerIndex) == 0 )
	{
		gxCur(playerIndex) = 1;
	}
	CellShroudStatus newShroud = getShroudStatusForPlayer( playerIndex );

	if( oldShroud != newShroud )
	{
		// On an edge trigger, tell all objects to think about their shroudedness
		invalidateShroudedStatusForAllCois( playerIndex );

		// and update the client if we are on the local player
		if( playerIndex == rts::getObservedOrLocalPlayer()->getPlayerIndex() )
		{
			TheDisplay->setShroudLevel(m_cellX, m_cellY, newShroud);
			TheRadar->setShroudLevel(m_cellX, m_cellY, newShroud);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeShrouder( Int playerIndex )
{
	// Decreasing active shroud: just decrement activeLevel.  This will never result in a client change.
	// Either it was passive shroud and is now active, or it was being looked at and still is.
	gxAct(playerIndex)--;
	DEBUG_ASSERTCRASH( gxAct(playerIndex) >= 0, ("Shroud generation has gone negative.  This can't happen.") );
}

//-----------------------------------------------------------------------------
//Bool PartitionCell::isShroudedForPlayer( Int playerIndex ) const
//{
	// There isn't an absolute answer.  This cell is only shrouded in regards to a person
//	return (gxCur(playerIndex) == 1);
//}

//-----------------------------------------------------------------------------
CellShroudStatus PartitionCell::getShroudStatusForPlayer( Int playerIndex ) const
{
	// There are now three answers, but the question still requires "to whom"

	if( gxCur(playerIndex) == 1 )
		return CELLSHROUD_SHROUDED;
	else if( gxCur(playerIndex) == 0 )
		return CELLSHROUD_FOGGED;// ie Nobody actively looking
	else
		return CELLSHROUD_CLEAR;
}

//-----------------------------------------------------------------------------
UnsignedInt PartitionCell::getThreatValue( Int playerIndex )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
		return m_threatValue[playerIndex];
	}
	return 0;
}

//-----------------------------------------------------------------------------
void PartitionCell::addThreatValue( Int playerIndex, UnsignedInt threatValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef DEBUG_CRASHING
		UnsignedInt oldThreatVal = m_threatValue[playerIndex];
		DEBUG_ASSERTCRASH(oldThreatVal <= oldThreatVal + threatValue, ("adding new threat value overflowed allotted storage."));
#endif
		m_threatValue[playerIndex] += threatValue;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeThreatValue( Int playerIndex, UnsignedInt threatValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef DEBUG_CRASHING
		UnsignedInt oldThreatVal = m_threatValue[playerIndex];
		DEBUG_ASSERTCRASH(oldThreatVal >= oldThreatVal - threatValue, ("removing new threat value underflowed allotted storage."));
#endif
		m_threatValue[playerIndex] -= threatValue;
	}
}

//-----------------------------------------------------------------------------
UnsignedInt PartitionCell::getCashValue( Int playerIndex )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
		return m_cashValue[playerIndex];
	}
	return 0;
}

//-----------------------------------------------------------------------------
void PartitionCell::addCashValue( Int playerIndex, UnsignedInt cashValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef DEBUG_CRASHING
		UnsignedInt oldCashVal = m_cashValue[playerIndex];
		DEBUG_ASSERTCRASH(oldCashVal <= oldCashVal + cashValue, ("adding new cash value overflowed allotted storage."));
#endif
		m_cashValue[playerIndex] += cashValue;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeCashValue( Int playerIndex, UnsignedInt cashValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef DEBUG_CRASHING
		UnsignedInt oldCashVal = m_cashValue[playerIndex];
		DEBUG_ASSERTCRASH(oldCashVal >= oldCashVal - cashValue, ("removing new cash value underflowed allotted storage."));
#endif
		m_cashValue[playerIndex] -= cashValue;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::friend_addToCellList(CellAndObjectIntersection *coi)
{
	if (coi)
	{
		coi->friend_addToCellList(&m_firstCoiInCell);
		++m_coiCount;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::friend_removeFromCellList(CellAndObjectIntersection *coi)
{
	if (coi)
	{
		coi->friend_removeFromCellList(&m_firstCoiInCell);
		--m_coiCount;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::getCellCenterPos(Real& x, Real& y)
{
	ThePartitionManager->getCellCenterPos(m_cellX, m_cellY, x, y);
}

//-----------------------------------------------------------------------------
#ifdef RTS_DEBUG
void PartitionCell::validateCoiList()
{
	CellAndObjectIntersection *nextCoi = 0, *prevCoi = 0;
	for (CellAndObjectIntersection *coi = getFirstCoiInCell(); coi; prevCoi = coi, coi = nextCoi)
	{
		nextCoi = coi->getNextCoi();
		DEBUG_ASSERTCRASH(coi->getPrevCoi() == prevCoi, ("coi link mismatch"));
		DEBUG_ASSERTCRASH(prevCoi == nullptr || prevCoi->getNextCoi() == coi, ("coi link mismatch"));
		DEBUG_ASSERTCRASH((coi == getFirstCoiInCell()) == (prevCoi == nullptr) , ("coi link mismatch"));
		DEBUG_ASSERTCRASH(nextCoi == nullptr || nextCoi->getPrevCoi() == coi, ("coi link mismatch"));
	}
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PartitionCell::crc( Xfer *xfer )
{

	gxShroudToStruct();
	xfer->xferUser(&m_shroudLevel, sizeof(ShroudLevel) * MAX_PLAYER_COUNT);
	xfer->xferUser(&m_cellX, sizeof(m_cellX));
	xfer->xferUser(&m_cellY, sizeof(m_cellY));

}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void PartitionCell::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// xfer shroud data (web port: the dense counters are the truth; saved and loaded through the struct)
	gxShroudToStruct();
	xfer->xferUser( &m_shroudLevel, sizeof( ShroudLevel ) * MAX_PLAYER_COUNT );
	gxShroudFromStruct();

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PartitionCell::loadPostProcess()
{

}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionData::PartitionData()
{
	//DEBUG_LOG(("create pd %08lx",this));
	m_next = nullptr;
	m_prev = nullptr;
	m_nextDirty = nullptr;
	m_prevDirty = nullptr;
	m_object = nullptr;
	m_ghostObject = nullptr;
	m_coiArrayCount = 0;
	m_coiArray = nullptr;
	m_coiInUseCount = 0;
	m_doneFlag = 0;
	m_dirtyStatus = NOT_DIRTY;
	m_lastCell = nullptr;
	memset(m_gxFillKey, 0, sizeof(m_gxFillKey));
	m_gxDeferStamp = 0;
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		m_everSeenByPlayer[i] = false;
		m_shroudedness[i] = OBJECTSHROUD_INVALID;
		m_shroudednessPrevious[i] = OBJECTSHROUD_INVALID;
	}
}

//-----------------------------------------------------------------------------
PartitionData::~PartitionData()
{
	//DEBUG_LOG(("toss pd for pd %08lx obj %08lx",this,m_object));
	removeAllTouchedCells();
	freeCoiArray();
	DEBUG_ASSERTCRASH(ThePartitionManager, ("ThePartitionManager is null"));
	if (ThePartitionManager && ThePartitionManager->isInListDirtyModules(this))
	{
		//DEBUG_LOG(("remove pd %08lx from dirty list (%08lx %08lx)",this,m_prevDirty,m_nextDirty));
		ThePartitionManager->removeFromDirtyModules(this);
		//DEBUG_ASSERTCRASH(!ThePartitionManager->isInListDirtyModules(this), ("hmm"));
	}
}

//-----------------------------------------------------------------------------
Int PartitionData::getControllingPlayerIndex() const
{
	const Player* p = getObject()->getControllingPlayer();
	if (p)
	{
		Int playerIndex = p->getPlayerIndex();
		DEBUG_ASSERTCRASH(playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT, ("bad playerIndex"));
		return playerIndex;
	}

	DEBUG_CRASH(("this should never happen"));
	throw ERROR_BUG;
	return 0;
}

//-----------------------------------------------------------------------------
// only used to restore state after map border resizing and/or xfer!
void PartitionData::friend_setShroudednessPrevious(Int playerIndex, ObjectShroudStatus status)
{
	m_shroudednessPrevious[playerIndex] = status;

	// if our status is anything but shrouded, assume we've never been seen by the player.
	m_everSeenByPlayer[playerIndex] = (status == OBJECTSHROUD_SHROUDED) ? false : true;

	// if we're already invalid, mark us as special-invalid.
#ifndef DISABLE_INVALID_PREVENTION
	if (m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID)
		m_shroudedness[playerIndex] = OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID;
#endif
}

//-----------------------------------------------------------------------------
ObjectShroudStatus PartitionData::getShroudedStatus(Int playerIndex)
{
	gxCount(GXC_FOG_STATUS_CALLS);	// tickTiming
	// sanity
	DEBUG_ASSERTCRASH( playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT,
										 ("PartitionData::getShroudedStatus - Invalid player index '%d'", playerIndex) );

	if (!ThePartitionManager->getUpdatedSinceLastReset())
	{
		// The shroud should be invalid until update has been called.
		// I'll set myself as invalid so the next time you ask I'll recompute.  (If I just return Invalid, then the old state will persist unfairly over a reset)
		invalidateShroudedStatusForPlayer(playerIndex);
		return m_shroudedness[playerIndex];
	}

#ifndef DISABLE_INVALID_PREVENTION
	if (m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID || m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID)
	{
		Bool updateShroudednessPrevious = (m_shroudedness[playerIndex] != OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID);
#else
	if (m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID)
	{
#endif
		gxCount(GXC_FOG_RECOMPUTES);	// tickTiming
		Int shroudedCells = 0;
		Int foggedCells = 0;
		CellAndObjectIntersection* coi = m_coiArray;
		for (Int i = m_coiInUseCount; i; --i, ++coi)
		{
			if( coi->getCell()->getShroudStatusForPlayer(playerIndex) == CELLSHROUD_SHROUDED )
				++shroudedCells;
			else if( coi->getCell()->getShroudStatusForPlayer(playerIndex) == CELLSHROUD_FOGGED )
				++foggedCells;
		}

		if( m_coiInUseCount == 0 )
		{	m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;				// Stuff off the map have no COIs since they aren't in the PartitionCell range
			m_everSeenByPlayer[playerIndex] = false;	//force object as never seen by the player
		}
		else if( shroudedCells == m_coiInUseCount )
		{	m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;				// every cell I use is shrouded
			m_everSeenByPlayer[playerIndex] = false; //force object as never seen by the player
			if (m_ghostObject && m_shroudednessPrevious[playerIndex] == OBJECTSHROUD_FOGGED)
			{	//we are shrouding an area that used to be fogged so release our memory of what was there.
				m_ghostObject->freeSnapShot(playerIndex);
			}
		}
		else if( shroudedCells + foggedCells == m_coiInUseCount )
		{
			m_shroudedness[playerIndex] = OBJECTSHROUD_FOGGED;	//object is visible but fogged.
			if (m_object && m_ghostObject)	//object does not exist for modules holding only GhostObjects
			{
				//fogged but may not be visible if faction unit or faction building that has not been seen before
				Player *player=ThePlayerList->getNthPlayer(playerIndex);
				if (player->getRelationship(m_object->getTeam()) == NEUTRAL)
				{	//anything neutral that moves around will not be rendered inside fog.
					if (!m_object->isKindOf(KINDOF_IMMOBILE))
						m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;
				}
				else	//Not neutral
				{	//enemy unit will always be shrouded unless it's a building that's already been seen by the player.  Fogged Mines are also always
					//shroued no matter what.
					if (!(m_object->isKindOf(KINDOF_IMMOBILE) && m_everSeenByPlayer[playerIndex]) || m_object->isKindOf(KINDOF_MINE))
						m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;
				}
				if (m_shroudedness[playerIndex] == OBJECTSHROUD_FOGGED)
				{	//successfully applied fog to object so check if we need to freeze it's state
					if (m_shroudednessPrevious[playerIndex] < OBJECTSHROUD_FOGGED)
					{	//object was not previously fogged but now is fogged.
						//need to record its current state so that it doesn't change
						//while fogged.
						m_ghostObject->snapShot(playerIndex);
					}
				}
			}
		}
		else if( shroudedCells == 0  &&  foggedCells == 0 )
		{	//Record that this object was seen by the player.  This info will be used to show fogged enemy faction buildings.
			m_everSeenByPlayer[playerIndex] = true;
			m_shroudedness[playerIndex] = OBJECTSHROUD_CLEAR;
			if (m_ghostObject && m_shroudednessPrevious[playerIndex] == OBJECTSHROUD_FOGGED)
			{	//object was previously fogged but now is visible so we no longer
				//need a ghost object.
				m_ghostObject->freeSnapShot(playerIndex);
			}
		}
		else
		{	//Record that this object was seen by the player.  This info will be used to show fogged enemy faction buildings.
			m_everSeenByPlayer[playerIndex] = true;
			m_shroudedness[playerIndex] = OBJECTSHROUD_PARTIAL_CLEAR;		// I am at least partially clear otherwise
			if (m_ghostObject && m_shroudednessPrevious[playerIndex] == OBJECTSHROUD_FOGGED)
			{	//object was previously fogged but now is visible so we no longer
				//need a ghost object.
				m_ghostObject->freeSnapShot(playerIndex);
			}
		}
		{
			// fogKeep: which branch decided it (the fogged one also reads relationship and kinds)
			const UnsignedShort bit = (UnsignedShort)(1u << playerIndex);
			const Bool fogBranch = m_coiInUseCount && shroudedCells != m_coiInUseCount && shroudedCells + foggedCells == m_coiInUseCount;
			const Int gs = gxSlot();	// ghost-only: nothing kept
			GxUnitStore &S = TheGxUnitStore;
			if (gs >= 0) { if (fogBranch) S.fogPure[gs] &= ~bit; else S.fogPure[gs] |= bit; }
			if (gs >= 0 && (S.fogKeptMask[gs] & bit))
			{
				extern int gx_verify_mismatch, gx_verify_checked;
				S.fogKeptMask[gs] &= ~bit;
				++gx_verify_checked;
				const Int kept = (Int)(S.fogKept[gs] >> (4 * playerIndex) & 15);
				if (kept != m_shroudedness[playerIndex])
				{
					++gx_verify_mismatch;
					printf("GXVERIFY fogKeep mismatch: object %u player %d kept %d now %d\n", m_object ? m_object->getID() : 0, playerIndex, kept, m_shroudedness[playerIndex]);
				}
				static Int s_gxKeepChecks;
				if (++s_gxKeepChecks % 20000 == 0)
					printf("GXVERIFY fogKeep checked %d (mismatches so far: all checks %d)\n", s_gxKeepChecks, gx_verify_mismatch);
			}
		}
#ifndef DISABLE_INVALID_PREVENTION
 		if (m_coiInUseCount && updateShroudednessPrevious)
		{
#else
 		if (m_coiInUseCount)
		{
#endif
			//only remember the previous state of objects actually on the map.
 			m_shroudednessPrevious[playerIndex] = m_shroudedness[playerIndex];
		}
	}

	return m_shroudedness[playerIndex];
}

//-----------------------------------------------------------------------------
void PartitionData::gxDebugDump() const
{
	CellAndObjectIntersection *coi = m_coiArray;
	printf("GXVERIFY     coi in use %d of %d, slot now %d\n", m_coiInUseCount, m_coiArrayCount, gxSlotOf(this));
	for (Int i = 0; i < m_coiArrayCount; ++i, ++coi)
	{
		PartitionCell *c = coi->getCell();
		if (!c) { printf("GXVERIFY     coi %d: no cell (module %p)\n", i, (void*)coi->getModule()); continue; }
		const Int sl = coi->gxPlayerSlot();
		Bool inList = false;
		for (CellAndObjectIntersection *h = c->gxSlotHead(sl); h; h = h->gxSlotNext()) if (h == coi) inList = true;
		Bool inCell = false;
		for (CellAndObjectIntersection *h = c->getFirstCoiInCell(); h; h = h->getNextCoi()) if (h == coi) inCell = true;
		printf("GXVERIFY     coi %d: cell %d %d slot %d mask %x dense %x inSlotList %d inCell %d\n", i, c->getCellX(), c->getCellY(), sl,
			c->gxPlayerMask(), g_gxCellMasks[c->getCellY() * g_gxCellMaskW + c->getCellX()], inList, inCell);
	}
}

//-----------------------------------------------------------------------------
void PartitionData::gxTeamChanged()
{
	const Int slot = gxSlotOf(this);
	CellAndObjectIntersection *coi = m_coiArray;
	for (Int i = m_coiArrayCount; i > 0; --i, ++coi)
		if (coi->getModule())
			coi->gxSetPlayerSlot(slot);
}

//-----------------------------------------------------------------------------
void PartitionData::removeAllTouchedCells()
{
	CellAndObjectIntersection *coi = m_coiArray;
	for (Int i = m_coiArrayCount; i > 0; --i, ++coi)
	{
		if (coi->getModule())
		{
			DEBUG_ASSERTCRASH(coi->getModule() == this, ("coi uses wrong module"));
			coi->removeAllCoverage();
			DEBUG_ASSERTCRASH(!coi->getModule(), ("coi should no longer be in use"));
			--m_coiInUseCount;
		}
	}
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
}

// -----------------------------------------------------------------------------
void PartitionData::addSubPixToCoverage(PartitionCell *cell)
{
	DEBUG_ASSERTCRASH(m_coiInUseCount < m_coiArrayCount, ("not enough cois allocated for this object"));
	if (cell)
	{
		// see if we already have a coi for this cell.
		CellAndObjectIntersection *coi = m_coiArray;
		CellAndObjectIntersection *coiToUse = nullptr;
		for (Int i = __min(m_coiInUseCount,m_coiArrayCount); i; --i, ++coi)
		{
			if (coi->getCell() == cell)
			{
				coiToUse = coi;
				break;
			}
		}
		DEBUG_ASSERTCRASH(coiToUse != nullptr || m_coiInUseCount < m_coiArrayCount, ("not enough cois allocated for this object"));
		if (coiToUse == nullptr && m_coiInUseCount < m_coiArrayCount)
		{
			// nope, no coi for this cell, allocate a new one
			coiToUse = &m_coiArray[m_coiInUseCount++];
		}
		if (coiToUse)
		{
			coiToUse->addCoverage(cell, this);
		}
	}
}

// -----------------------------------------------------------------------------
void PartitionData::doRectFill(
	Real centerX,
	Real centerY,
	Real halfsizeX,
	Real halfsizeY,
	Real angle
)
{
	Real c = (Real)Cos(angle);
	Real s = (Real)Sin(angle);

	Real actualCellSize = ThePartitionManager->getCellSize();
	Real stepSize = actualCellSize * 0.5f; // in theory, should be getCellSize() exactly, but needs to be smaller to avoid aliasing problems
	Real ydx = s * stepSize;
	Real ydy = -c * stepSize;
	Real xdx = c * stepSize;
	Real xdy = s * stepSize;

	// GS 12-26-02  Oh my.  The step size has been fudged down to ensure that a cell is not jumped over,
	// but then the original step size was used to determine the number of steps.  This error gets
	// more pronounced on bigger buildings.  ie a 100 X 160 registers cells for a 80 x 100.  If you
	// fudge one, you have to fudge the other.
//	Real stepSizeInvTimes2 = 2.0f * ThePartitionManager->getCellSizeInv();
	Real stepSizeInvTimes2 = 2.0f * (1.0f / stepSize);

	/*
		srj fixes subtle bug: need +1 to get all of misaligned geometries accted for.
	*/
//	Int numStepsX = REAL_TO_INT_CEIL(halfsizeX * stepSizeInvTimes2) + 1;
//	Int numStepsY = REAL_TO_INT_CEIL(halfsizeY * stepSizeInvTimes2) + 1;
	Int numStepsX = REAL_TO_INT_CEIL(halfsizeX * stepSizeInvTimes2);
	Int numStepsY = REAL_TO_INT_CEIL(halfsizeY * stepSizeInvTimes2);
	// GS 12-26-02  Oh my part 2.  Now that we are no longer greatly underestimating our COI imprint,
	// we don't need this fudge.  The fudge is not in the calcMaxCOIForShape, so it just results in
	// good COIs getting dropped since we didn't allocate enough to handle this fudge.

	Real tl_x = centerX - halfsizeX*c - halfsizeY*s;
	Real tl_y = centerY + halfsizeY*c - halfsizeX*s;

	for (Int iy = 0; iy < numStepsY; ++iy, tl_x += ydx, tl_y += ydy)
	{
		Real x = tl_x;
		Real y = tl_y;
		for (Int ix = 0; ix < numStepsX; ++ix, x += xdx, y += xdy)
		{
			Int cellx, celly;
			ThePartitionManager->worldToCell(x, y, &cellx, &celly);
			PartitionCell *cell = ThePartitionManager->getCellAt(cellx, celly);	// might be null if off the edge
			if (cell)
			{
				addSubPixToCoverage(cell);
			}
		}
	}

}

// -----------------------------------------------------------------------------
void PartitionData::hLineCircle(Int x1, Int x2, Int y)
{
	for (Int x = x1; x <= x2; ++x)
	{
		PartitionCell* cell = ThePartitionManager->getCellAt(x, y);
		if (cell)
		{
      addSubPixToCoverage(cell);
		}
	}
}

// -----------------------------------------------------------------------------
// Marks all partition cells that intersect a circle of the given center and radius
// as covered by this object using a variation of the midpoint circle algorithm.
void PartitionData::doCircleFill(
	Real centerX,
	Real centerY,
	Real radius
)
{
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("expected no coi in use here"));

	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1)
		cellRadius = 1;

	Int y = cellRadius - 1;
	Int dec = 3 - 2*cellRadius;

#if RETAIL_COMPATIBLE_CRC
	// Cell coverage diverges at radii >= 240 between algorithms.
	Int end = cellRadius - 1;
	Int& endRef = (cellRadius < 240) ? y : end;
	for (Int x = 0; x <= endRef; ++x)
#else
	for (Int x = 0; x <= y; ++x)
#endif
	{
		hLineCircle(cellCenterX - x, cellCenterX + x, cellCenterY + y);
		hLineCircle(cellCenterX - x, cellCenterX + x, cellCenterY - y);
		hLineCircle(cellCenterX - y, cellCenterX + y, cellCenterY + x);
		hLineCircle(cellCenterX - y, cellCenterX + y, cellCenterY - x);

		if (dec >= 0)
		{
			dec += (1 - y) << 2;
			--y;
		}
		dec += (x << 2) + 6;
	}
}

static Bool doesCircleOverlapCell(Real centerX, Real centerY, Real radius, Real cellX, Real cellY, Real cellSize)
{
	Real closestX = std::max(cellX, std::min(centerX, cellX + cellSize));
	Real closestY = std::max(cellY, std::min(centerY, cellY + cellSize));
	Real distX = centerX - closestX;
	Real distY = centerY - closestY;

	return (sqr(distX) + sqr(distY)) < sqr(radius);
}

void PartitionData::doCircleFillPrecise(Real centerX, Real centerY, Real radius)
{
	Int minCellX, minCellY, maxCellX, maxCellY;
	ThePartitionManager->worldToCell(centerX - radius, centerY - radius, &minCellX, &minCellY);
	ThePartitionManager->worldToCell(centerX + radius, centerY + radius, &maxCellX, &maxCellY);

	Real cellSize = ThePartitionManager->getCellSize();

	for (Int x = minCellX; x <= maxCellX; ++x)
	{
		for (Int y = minCellY; y <= maxCellY; ++y)
		{
			Real cellWorldX = x * cellSize;
			Real cellWorldY = y * cellSize;

			if (doesCircleOverlapCell(centerX, centerY, radius, cellWorldX, cellWorldY, cellSize))
			{
				PartitionCell* cell = ThePartitionManager->getCellAt(x, y);
				if (cell)
				{
					addSubPixToCoverage(cell);
				}
			}
		}
	}
}

// -----------------------------------------------------------------------------
void PartitionData::doSmallFill(
	Real centerX,
	Real centerY,
	Real radius
)
{
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("expected no coi in use here"));

	Real halfCellSize = ThePartitionManager->getCellSize() * 0.5f;
	if (radius > halfCellSize)
	{
		DEBUG_CRASH(("object is too large to use a 'small' geometry, truncating size to cellsize"));
		radius = halfCellSize;
	}

	Int cx1, cy1, cx2, cy2;
	ThePartitionManager->worldToCell(centerX - radius, centerY - radius, &cx1, &cy1);
	ThePartitionManager->worldToCell(centerX + radius, centerY + radius, &cx2, &cy2);

	DEBUG_ASSERTCRASH(absInt(cx2-cx1)<=1,("bad cx"));
	DEBUG_ASSERTCRASH(absInt(cy2-cy1)<=1,("bad cy"));

	for (Int x = cx1; x <= cx2; x++)
	{
		for (Int y = cy1; y <= cy2; y++)
		{
			PartitionCell *cell = ThePartitionManager->getCellAt(x, y);
			if (cell)
			{
				m_coiArray[m_coiInUseCount++].addCoverage(cell, this);
			}
		}
	}

	#ifdef INTENSE_DEBUG
	for (int i = 0; i < m_coiInUseCount; i++)
	{
		for (int j = 0; j < i; j++)
		{
			DEBUG_ASSERTCRASH(m_coiArray[i].getCell() != m_coiArray[j].getCell(), ("dup cells"));
		}
	}
	#endif
}

//-----------------------------------------------------------------------------
static UnsignedInt g_gxDeferStamp = 0;	// collideStagger: this frame's stamp (frame + 1), 0 when off
// a deterministic order for two partition datas: object ID (ghosts, with no object, first)
static inline Bool gxBefore(const PartitionData *a, const PartitionData *b)
{
	const Object *oa = a->getObject(), *ob = b->getObject();
	const UnsignedInt ia = oa ? (UnsignedInt)oa->getID() : 0, ib = ob ? (UnsignedInt)ob->getID() : 0;
	return ia < ib;
}

void PartitionData::addPossibleCollisions(PartitionContactList *ctList)
{
// actually, we do occasionally want to detect collisions of dead AIs.
// e.g., dead technicals flying thru the air should check for collisions with
// immobile structures, because otherwise they pass thru them, which looks dopey.
// if preventing dead-ai-collisions is necessary to fix something else, please
// ensure that the above case is cool before signing off. (srj)
#ifdef NOPE_READ_THE_COMMENT
	// dead objects never collide with things...
 	AIUpdateInterface *ai = getObject()->getAIUpdateInterface();
 	if (ai && ai->isAiInDeadState())
	{
		return;
	}
#endif

	//DEBUG_LOG(("adding possible collision for %s",getObject()->getTemplate()->getName().str()));

	// Web port (match rule spatialPairs 8192, switch pairOffer, second word bit 29): a neighbour whose
	// kept place is clearly apart from ours is not offered at all. Changes the game only when a
	// collision reaction earlier in the same frame moves a unit into a pair dropped here.
	extern bool gxMatchRule(unsigned int rule);
	const Bool gxOffer = gxMatchRule(8192 /* GXRULE_SPATIAL_PAIRS */) && !g_gxPairOff;
	const Real gxMx = gxX(), gxMy = gxY(), gxMr = gxR();	// our kept place (store columns; nothing here refreshes it)
	CellAndObjectIntersection *myCoi = m_coiArray;
	for (Int i = m_coiInUseCount; i > 0; --i, ++myCoi)
	{
		PartitionCell *cell = myCoi->getCell();
		if (cell->getCoiCount() < 2)
			continue;

		// switch simdPairs (fourth word bit 17; same game as pairOffer alone): the clearly-apart test for 4
		// neighbours at a time with wasm SIMD, the same f32 expression as below; a lane it keeps runs the
		// scalar test too, so the result is the scalar one
		UnsignedInt gxLanes = ~0u;
		Int gxLane = 0;
#ifdef __wasm_simd128__
		const Bool gxSimd = gxOffer && (GLES3_Page_Switches4() & 131072);
#endif
		for (CellAndObjectIntersection *coi = cell->getFirstCoiInCell(); coi; coi = coi->getNextCoi(), ++gxLane)
		{
#ifdef __wasm_simd128__
			if (gxSimd && (gxLane & 7) == 0)
			{
				// ILP (LOGIC_AGENTS "SIMD step"): 8 neighbours per block, two independent f32x4 chains;
				// a short tail (under 4) is all kept
				gxLanes = ~0u;
				CellAndObjectIntersection *q[8];
				Int n = 0;
				for (CellAndObjectIntersection *c = coi; c && n < 8; c = c->getNextCoi())
					q[n++] = c;
				const v128_t mx = wasm_f32x4_splat(gxMx), my = wasm_f32x4_splat(gxMy), mr = wasm_f32x4_splat(gxMr);
				const v128_t k14 = wasm_f32x4_splat(1.4143f), one = wasm_f32x4_splat(1.0f);
				#define GX_PAIR4(b) ([&]() -> UnsignedInt { \
					const v128_t vx = wasm_f32x4_make(q[b]->gxX(), q[b + 1]->gxX(), q[b + 2]->gxX(), q[b + 3]->gxX()); \
					const v128_t vy = wasm_f32x4_make(q[b]->gxY(), q[b + 1]->gxY(), q[b + 2]->gxY(), q[b + 3]->gxY()); \
					const v128_t vr = wasm_f32x4_make(q[b]->gxR(), q[b + 1]->gxR(), q[b + 2]->gxR(), q[b + 3]->gxR()); \
					const v128_t dx = wasm_f32x4_sub(vx, mx), dy = wasm_f32x4_sub(vy, my); \
					const v128_t lim = wasm_f32x4_add(wasm_f32x4_mul(wasm_f32x4_add(vr, mr), k14), one); \
					const v128_t d2 = wasm_f32x4_add(wasm_f32x4_mul(dx, dx), wasm_f32x4_mul(dy, dy)); \
					return (UnsignedInt)wasm_i32x4_bitmask(wasm_v128_not(wasm_f32x4_gt(d2, wasm_f32x4_mul(lim, lim)))); }())
				if (n == 8)
				{
					const UnsignedInt a = GX_PAIR4(0), b = GX_PAIR4(4);
					gxLanes = a | (b << 4) | ~0xffu;
				}
				else if (n >= 4)
					gxLanes = GX_PAIR4(0) | ~0xfu;
				#undef GX_PAIR4
				gxCount(GXC_PAIRS_SIMD_CULLED, 8 - __builtin_popcount(gxLanes & 0xffu));
			}
			if (gxSimd && !((gxLanes >> (gxLane & 7)) & 1))
			{
				if (GLES3_Page_Switches() & 2097152)
				{
					// verify: the scalar test must turn this neighbour away too
					static unsigned s_checked, s_bad;
					const Real dx = coi->gxX() - gxMx, dy = coi->gxY() - gxMy, lim = (coi->gxR() + gxMr) * 1.4143f + 1.0f;
					++s_checked;
					if (!(dx * dx + dy * dy > lim * lim))
					{
						++s_bad;
						printf("GXVERIFY simdPairs mismatch: kept by the scalar test (d2 %g lim2 %g)\n", dx * dx + dy * dy, lim * lim);
					}
					if (s_checked % 200000 == 0)
						printf("GXVERIFY simdPairs dropped lanes checked %u, mismatches %u\n", s_checked, s_bad);
				}
				continue;
			}
#endif
			if (gxOffer)
			{
				gxCount(GXC_PAIRS_TESTED);
				const Real dx = coi->gxX() - gxMx, dy = coi->gxY() - gxMy, lim = (coi->gxR() + gxMr) * 1.4143f + 1.0f;
				if (dx * dx + dy * dy > lim * lim)
					continue;
			}
			PartitionData *that = coi->getModule();
			if (this != that)
			{
				// collideStagger: a neighbour waiting its turn this frame checks this pair itself on
				// its turn, unless it sorts after us (then we do it now): each pair once per cycle.
				// Sorted by object ID (was by address, which can differ between machines)
				if (g_gxDeferStamp && that->gxDeferStamp() == g_gxDeferStamp && gxBefore(that, this))
					continue;
				ctList->addToContactList(this, that);
			}
		}
	}
}

//-----------------------------------------------------------------------------
Bool PartitionData::collidesWith(const PartitionData *that, CollideLocAndNormal *cinfo) const
{
	const Object *thisObj = this->getObject();
	const Object *thatObj = that->getObject();

	if( thisObj->isKindOf( KINDOF_NO_COLLIDE )  ||  thatObj->isKindOf( KINDOF_NO_COLLIDE ) )
		return FALSE; // A collision extent of zero size is still a point and can collide, but we don't always want to.

	CollideInfo thisInfo(thisObj->getPosition(), thisObj->getGeometryInfo(), thisObj->getOrientation());
	CollideInfo thatInfo(thatObj->getPosition(), thatObj->getGeometryInfo(), thatObj->getOrientation());

	// invariant for all geometries: first do z collision check.
	Real thisTop = thisInfo.position.z + thisInfo.geom.getMaxHeightAbovePosition();
	Real thisBot = thisInfo.position.z - thisInfo.geom.getMaxHeightBelowPosition();
	Real thatTop = thatInfo.position.z + thatInfo.geom.getMaxHeightAbovePosition();
	Real thatBot = thatInfo.position.z - thatInfo.geom.getMaxHeightBelowPosition();
	if (thisTop >= thatBot && thisBot <= thatTop)
	{
		GeometryType thisGeom = thisObj->getGeometryInfo().getGeomType();
		GeometryType thatGeom = thatObj->getGeometryInfo().getGeomType();

		//
		// NOTE: This assumes geometry enumerations that start at GEOMETRY_FIRST AND depends on the
		// order in which they appear in the enum list
		//
		CollideTestProc collideProc = theCollideTestProcs[ (thisGeom - GEOMETRY_FIRST) * GEOMETRY_NUM_TYPES + (thatGeom - GEOMETRY_FIRST) ];
		return (*collideProc)(&thisInfo, &thatInfo, cinfo);
	}
	else
	{
		// no z-intersection -> no collision.
		return false;
	}
}

//-----------------------------------------------------------------------------
/* See if thisObj collides with geom at pos & angle. */
Bool PartitionManager::geomCollidesWithGeom(const Coord3D* pos1,
		const GeometryInfo& geom1,
		Real angle1,
		const Coord3D* pos2,
		const GeometryInfo& geom2,
		Real angle2) const
{
	CollideInfo thisInfo(pos1, geom1, angle1);
	CollideInfo thatInfo(pos2, geom2, angle2);

	// invariant for all geometries: first do z collision check.
	if (thisInfo.position.z + thisInfo.geom.getMaxHeightAbovePosition() >= thatInfo.position.z &&
			thisInfo.position.z <= thatInfo.position.z + thatInfo.geom.getMaxHeightAbovePosition())
	{
		GeometryType thisGeom = geom1.getGeomType();
		GeometryType thatGeom = geom2.getGeomType();

		//
		// NOTE: This assumes geometry enumerations that start at GEOMETRY_FIRST AND depends on the
		// order in which they appear in the enum list
		//
		CollideTestProc collideProc = theCollideTestProcs[ (thisGeom - GEOMETRY_FIRST) * GEOMETRY_NUM_TYPES + (thatGeom - GEOMETRY_FIRST) ];
		CollideLocAndNormal cloc;
		return (*collideProc)(&thisInfo, &thatInfo, &cloc);
	}
	else
	{
		// no z-intersection -> no collision.
		return false;
	}
}

//-----------------------------------------------------------------------------
void PartitionData::updateCellsTouched()
{
	gxRefreshPlace();	// web port: the kept place starts right when the object registers
	GeometryType geom;
	Bool isSmall;
	Coord3D pos;
	Real angle,majorRadius,minorRadius;


	Object *obj = getObject();

	if (obj)
	{
		//we have no object using this PartitionData but we still have a GhostObject so copy its data.
		geom = obj->getGeometryInfo().getGeomType();
		isSmall = obj->getGeometryInfo().getIsSmall();
		pos = *(obj->getPosition());
		angle = obj->getOrientation();
		majorRadius = obj->getGeometryInfo().getMajorRadius();
		minorRadius = obj->getGeometryInfo().getMinorRadius();
	}
	else if (m_ghostObject)
	{
		geom = m_ghostObject->getGeometryType();
		isSmall = m_ghostObject->getGeometrySmall();
		pos = *m_ghostObject->getParentPosition();
		angle = m_ghostObject->getParentAngle();
		majorRadius = m_ghostObject->getGeometryMajorRadius();
		minorRadius = m_ghostObject->getGeometryMinorRadius();
	}
	else
	{
		DEBUG_CRASH(("must be attached to an Object here"));
		return;
	}

	// Web port (switch cellReuse, second switch word bit 8): the cells a fill covers depend only on
	// the values in this key (whole cells for small and round shapes, the exact floats for boxes)
	// and on the size of the COI array. If the key is unchanged, so are the cells, in the same
	// order; taking every COI out of its cell list and adding it again would only move each one
	// to the front of its list (one COI per cell), so that is all the fast path does. The key is
	// kept on both paths so the switch can be flipped mid-game.
	Int key[8];
	{
		Real r = majorRadius;
		key[1] = isSmall ? 1 : (geom == GEOMETRY_BOX ? 3 : 2);
		if (isSmall)
		{
			Real halfCellSize = ThePartitionManager->getCellSize() * 0.5f;
			if (r > halfCellSize)
				r = halfCellSize;
			ThePartitionManager->worldToCell(pos.x - r, pos.y - r, &key[2], &key[3]);
			ThePartitionManager->worldToCell(pos.x + r, pos.y + r, &key[4], &key[5]);
			key[6] = 0;
		}
		else if (geom == GEOMETRY_BOX)
		{
			memcpy(&key[2], &pos.x, 4); memcpy(&key[3], &pos.y, 4); memcpy(&key[4], &majorRadius, 4);
			memcpy(&key[5], &minorRadius, 4); memcpy(&key[6], &angle, 4);
		}
		else
		{
			ThePartitionManager->worldToCell(pos.x, pos.y, &key[2], &key[3]);
			key[4] = ThePartitionManager->worldToCellDist(r);
			key[5] = key[6] = 0;
#if !(RETAIL_COMPATIBLE_CRC || RETAIL_COMPATIBLE_CIRCLE_FILL_ALGORITHM)
			key[1] = 0;	// the precise fill uses the exact position: never reused
#endif
		}
		key[7] = m_coiArrayCount;
		key[0] = (key[1] != 0 && m_coiInUseCount == m_gxFillKey[0]) ? 1 : 0;	// [0]: COIs in use after the last fill
		for (Int i = 1; key[0] && i < 8; ++i)
			if (key[i] != m_gxFillKey[i])
				key[0] = 0;
	}
	const Bool gxVerify = (GLES3_Page_Switches() & 2097152) != 0;
	// verify mode: the fast path runs on odd frames (so the range-search check sees lists it reordered)
	// and the fill check on even ones
	const Bool gxVerifyFast = gxVerify && TheGameLogic && (TheGameLogic->getFrame() & 1);
	const Bool gxSameCells = key[0] != 0;	// the fill makes the same cells as last time (cellReuse's key; verified)
	if (key[0] && (GLES3_Page_Switches2() & 256) && (!gxVerify || gxVerifyFast))
	{
		const Int slot = gxSlotOf(this);
		CellAndObjectIntersection *coi = m_coiArray;
		for (Int i = m_coiInUseCount; i > 0; --i, ++coi)
		{
			if (coi->getPrevCoi())
				coi->gxMoveToFront();
			coi->gxSetPlayerSlot(slot);
		}
	}
	else
	{
	PartitionCell *gxOld[64];
	const Bool gxCheck = gxVerify && key[0] && m_coiInUseCount <= 64;
	if (gxCheck)
		for (Int i = 0; i < m_coiInUseCount; ++i)
			gxOld[i] = m_coiArray[i].getCell();
	const Int gxOldCount = m_coiInUseCount;
	removeAllTouchedCells();
	if (isSmall)
	{
		doSmallFill(pos.x, pos.y, majorRadius);
	}
	else
	{
		switch(geom)
		{
			case GEOMETRY_SPHERE:
			case GEOMETRY_CYLINDER:
			{
#if RETAIL_COMPATIBLE_CRC || RETAIL_COMPATIBLE_CIRCLE_FILL_ALGORITHM
				doCircleFill(pos.x, pos.y, majorRadius);
#else
				// TheSuperHackers @bugfix Stubbjax 29/01/2026 Use precise circle fill to improve
				// collision accuracy, most notably for objects with geometry radii >= 20 and < 40.
				doCircleFillPrecise(pos.x, pos.y, majorRadius);
#endif
				break;
			}

			case GEOMETRY_BOX:
			{
				doRectFill(pos.x, pos.y, majorRadius, minorRadius, angle);
				break;
			}
		};
	}
	if (gxCheck)
	{
		Bool same = gxOldCount == m_coiInUseCount;
		for (Int i = 0; same && i < m_coiInUseCount; ++i)
			same = gxOld[i] == m_coiArray[i].getCell();
		++gx_verify_checked;
		if (!same)
		{
			++gx_verify_mismatch;
			printf("GXVERIFY cellReuse mismatch: kind %d, %d cells vs %d\n", key[1], gxOldCount, m_coiInUseCount);
		}
		if (gx_verify_checked % 20000 == 0)
			printf("GXVERIFY cellReuse checked %d, mismatches %d\n", gx_verify_checked, gx_verify_mismatch);
	}
	}
	key[0] = m_coiInUseCount;
	memcpy(m_gxFillKey, key, sizeof(key));

	Int currentCellIndexX, currentCellIndexY;
	ThePartitionManager->worldToCell( pos.x, pos.y, &currentCellIndexX, &currentCellIndexY );
	const PartitionCell *currentCell = ThePartitionManager->getCellAt( currentCellIndexX, currentCellIndexY );
	if(obj && currentCell != m_lastCell )
	{
		// web port (round 4): the unit store's centre cell (-1 off the map)
		TheGxUnitStore.setCell(obj, currentCell ? currentCellIndexY * ThePartitionManager->getCellCountX() + currentCellIndexX : -1);
		// To not expose PartitionCells, he will think in terms of points.  He will
		// unlook at a point and look at the new point.  We do the rounding and the
		// changing into PartitionCells
		obj->onPartitionCellChange();
		m_lastCell = currentCell;
	}

	// if we have moved, our shroudedness status might be different for all players,
	// so it must all be invalidated.
	if (gxSameCells && (GLES3_Page_Switches4() & 32768))
	{
		// switch fogKeep: same cells, so a status decided by the cells' shroud alone still holds (a cell's
		// shroud change invalidates its objects itself); the rest are invalidated as before
		const Int gs = gxSlot();
		GxUnitStore &S = TheGxUnitStore;
		const UnsignedShort gxPure = gs >= 0 ? S.fogPure[gs] : 0;
		for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
		{
			if ((gxPure >> i & 1) && m_shroudedness[i] != OBJECTSHROUD_INVALID)
			{
				gxCount(GXC_FOG_KEPT);	// tickTiming
				if (!gxVerify)
					continue;
				const ObjectShroudStatus kept = m_shroudedness[i];
				invalidateShroudedStatusForPlayer(i);
				// verify: recompute and compare (unless a real invalidation comes first)
				S.fogKept[gs] = (S.fogKept[gs] & ~(15ull << (4 * i))) | ((unsigned long long)kept << (4 * i));
				S.fogKeptMask[gs] |= (UnsignedShort)(1u << i);
				continue;
			}
			invalidateShroudedStatusForPlayer(i);
		}
	}
	else
		invalidateShroudedStatusForAllPlayers();

#ifdef INTENSE_DEBUG
	for (Int i = 0; i < m_coiInUseCount; i++)
	{
		for (Int j = 0; j < i; j++)
		{
			if (m_coiArray[i].getCell() == m_coiArray[j].getCell())
			{
				DEBUG_CRASH(("dup cells in COI array, this is bad"));
			}
		}
	}
#endif

}

//-----------------------------------------------------------------------------
void PartitionData::invalidateShroudedStatusForPlayer(Int playerIndex)
{
	{
		const Int gs = gxSlot();	// fogKeep verify: a real invalidation, nothing to compare
		if (gs >= 0)
			TheGxUnitStore.fogKeptMask[gs] &= (UnsignedShort)~(1u << playerIndex);
	}
#ifndef DISABLE_INVALID_PREVENTION
	if (m_shroudedness[playerIndex] != OBJECTSHROUD_INVALID && m_shroudedness[playerIndex] != OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID)
#endif
		m_shroudedness[playerIndex] = OBJECTSHROUD_INVALID;
}

//-----------------------------------------------------------------------------
void PartitionData::invalidateShroudedStatusForAllPlayers()
{
	for (Int i = 0; i < MAX_PLAYER_COUNT; i++)
	{
		invalidateShroudedStatusForPlayer(i);
	}
}

#if defined(RTS_DEBUG)
static AsciiString theObjName;
#endif

//-----------------------------------------------------------------------------
Int PartitionData::calcMaxCoiForShape(GeometryType geom, Real majorRadius, Real minorRadius, Bool isSmall)
{
	Int result;


  // THis is commented out, since some cases od big extets labeled small seem to be escaping.
  //M Lorenzen 8/26/03
//	if (isSmall)
//	{
//		#if defined(RTS_DEBUG)
//		Int chk = calcMaxCoiForShape(geom, majorRadius, minorRadius, false);
//		DEBUG_ASSERTCRASH(chk <= 4, ("Small objects should be <= 4 cells, but I calced %s as %d",theObjName.str(),chk));
//		#endif
//		result = 4;
//	}
//	else
	{
		switch(geom)
		{
			case GEOMETRY_SPHERE:
			case GEOMETRY_CYLINDER:
			{
				// note that majorRadius is a radius, not a diameter.
				// this actually allocates a few too many, but that's ok.
				Int cells = ThePartitionManager->worldToCellDist(majorRadius*2) + 1;
				result = cells * cells;
#if !RETAIL_COMPATIBLE_CRC
				break;
#endif
			}
			case GEOMETRY_BOX:
			{
				Real diagonal = (Real)(sqrtf(majorRadius*majorRadius + minorRadius*minorRadius));
				Int cells = ThePartitionManager->worldToCellDist(diagonal*2) + 1;
				result = cells * cells;
				break;
			}
			default:
				return 4;
		};
		static_assert(GEOMETRY_NUM_TYPES == 3, "GEOMETRY_NUM_TYPES has changed");
	}
	if (result < 4)
		result = 4;
	return result;
}

//-----------------------------------------------------------------------------
Int PartitionData::calcMaxCoiForObject()
{
	Object *obj = getObject();
	DEBUG_ASSERTCRASH(obj != nullptr, ("must be attached to an Object here 2"));

	GeometryType geom = obj->getGeometryInfo().getGeomType();
	Real majorRadius = obj->getGeometryInfo().getMajorRadius();
	Real minorRadius = obj->getGeometryInfo().getMinorRadius();
	Bool isSmall = obj->getGeometryInfo().getIsSmall();
#if defined(RTS_DEBUG)
theObjName = obj->getTemplate()->getName();
#endif
	return calcMaxCoiForShape(geom, majorRadius, minorRadius, isSmall);
}

//-----------------------------------------------------------------------------
void PartitionData::makeDirty(Bool needToUpdateCells)
{
	//DEBUG_LOG(("makeDirty for pd %08lx obj %08lx",this,m_object));
	if (!ThePartitionManager->isInListDirtyModules(this))
	{
		if (needToUpdateCells)
			m_dirtyStatus = NEED_CELL_UPDATE_AND_COLLISION_CHECK;
		else if (m_dirtyStatus == NOT_DIRTY)
			m_dirtyStatus = NEED_COLLISION_CHECK;
		ThePartitionManager->prependToDirtyModules(this);
	}
	else if (needToUpdateCells)
	{
		// If I am in the list already, I may be in there for a half update, so if my
		// needs have increased I'll upgrade what I want.
		m_dirtyStatus = NEED_CELL_UPDATE_AND_COLLISION_CHECK;
	}
}

//-----------------------------------------------------------------------------
void PartitionData::allocCoiArray()
{
	DEBUG_ASSERTCRASH(m_coiArrayCount == 0 && m_coiArray == nullptr, ("hmm, coi should probably be null here"));
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
	m_coiArrayCount = calcMaxCoiForObject();
	m_coiArray = MSGNEW("PartitionManager_COI") CellAndObjectIntersection[m_coiArrayCount];	// may throw!
	m_coiInUseCount = 0;
	makeDirty(true);
}

//-----------------------------------------------------------------------------
void PartitionData::freeCoiArray()
{
	delete [] m_coiArray;	// yes, it's OK to call this on null...
	m_coiArray = nullptr;
	m_coiArrayCount = 0;
	m_coiInUseCount = 0;
	makeDirty(true);
}

//-----------------------------------------------------------------------------
void PartitionData::attachToObject(Object* object)
{

	// remember who contains us
	m_object = object;
	if (object->gxStoreSlot() >= 0)
		TheGxUnitStore.queryReset(object->gxStoreSlot());	// web port: a new PartitionData knows no place or fog bits yet

	// we only snapshot things that are immobile and have something to draw.  Don't need ghostobjects for others.
	if (object->isKindOf(KINDOF_IMMOBILE))
	{	const ThingTemplate *tmplate=object->getTemplate();
		const ModuleInfo &drawMod=tmplate->getDrawModuleInfo();
		//Objects with default draw modules usually don't need ghostobjects so skip them to save memory.
		Bool makeGhostObject=TRUE;
		for (Int i=0; i<drawMod.getCount(); i++)
		{
			if (strcmp(drawMod.getNthName(i).str(),"W3DDefaultDraw") == 0)
			{	makeGhostObject=FALSE;
				break;
			}
		}
		if (makeGhostObject)
			m_ghostObject = TheGhostObjectManager->addGhostObject(object, this);
	}

	//DEBUG_LOG(("attach pd for pd %08lx obj %08lx",this,m_object));

	// (re)calc maxCoi and (re)alloc cois
	DEBUG_ASSERTCRASH(m_coiArrayCount == 0 && m_coiArray == nullptr, ("hmm, coi should probably be null here"));
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
	freeCoiArray();
	allocCoiArray();	// may throw!

	if (object)
		object->friend_setPartitionData(this);
	makeDirty(true);
}

//-----------------------------------------------------------------------------
void PartitionData::detachFromObject()
{
	// this is a little hokey... if we are in the midst of processing the contact
	// list, we have to ensure that any potential collisions get removed from that
	// list, since we are about to go bye-bye. ugly, but effective. (srj)
	//
	// note 2: we used to do this in our dtor, but it's too late then, since we require
	// our Object to still be present in order to figure out what to remove.
	// so do it here, since detaching it should nuke it from the obj list anyway. (srj)
	if (TheContactList)
		TheContactList->removeSpecificPartitionData(this);

	if (m_object)
		m_object->friend_setPartitionData(nullptr);
	removeAllTouchedCells();
	freeCoiArray();

	//DEBUG_LOG(("detach pd for pd %08lx obj %08lx",this,m_object));

	// no longer attached to object
	m_object = nullptr;
	TheGhostObjectManager->removeGhostObject(m_ghostObject);
	m_ghostObject = nullptr;
}

//-----------------------------------------------------------------------------
void PartitionData::attachToGhostObject(GhostObject* object)
{

	// remember who contains us
	m_object = nullptr;	//it's only attached to a ghost object, no parent object.
	m_ghostObject = object;

	// (re)calc maxCoi and (re)alloc cois
	DEBUG_ASSERTCRASH(m_coiArrayCount == 0 && m_coiArray == nullptr, ("hmm, coi should probably be null here"));
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
	freeCoiArray();

	m_coiArrayCount = calcMaxCoiForShape(object->getGeometryType(), object->getGeometryMajorRadius(), object->getGeometryMinorRadius(),object->getGeometrySmall());
	m_coiArray = MSGNEW("PartitionManager_COI") CellAndObjectIntersection[m_coiArrayCount];	// may throw!
	m_coiInUseCount = 0;
	makeDirty(true);

	if (m_ghostObject)
		m_ghostObject->updateParentObject(nullptr,this);
}

//-----------------------------------------------------------------------------
void PartitionData::detachFromGhostObject()
{
	// this is a little hokey... if we are in the midst of processing the contact
	// list, we have to ensure that any potential collisions get removed from that
	// list, since we are about to go bye-bye. ugly, but effective. (srj)
	//
	// note 2: we used to do this in our dtor, but it's too late then, since we require
	// our Object to still be present in order to figure out what to remove.
	// so do it here, since detaching it should nuke it from the obj list anyway. (srj)
	if (TheContactList)
		TheContactList->removeSpecificPartitionData(this);

	removeAllTouchedCells();
	freeCoiArray();

	//DEBUG_LOG(("detach pd for pd %08lx obj %08lx",this,m_object));

	// no longer attached to object
	m_object = nullptr;
	m_ghostObject = nullptr;
}

//-----------------------------------------------------------------------------
inline UnsignedInt hash2ints(Int a, Int b)
{
	// do it this way so that [a,b] always hashes to the same value as [b,a].
	// this is unsophisticated but reasonable, since all ObjectIDs will
	// quite likely be well below 65536...
	if (a < b)
	{
		return (a<<16)+b;
	}
	else
	{
		return (b<<16)+a;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
void PartitionContactList::addToContactList( PartitionData *obj, PartitionData *other )
{
	if (obj == other || obj == nullptr || other == nullptr)
		return;

	if (m_gxLean)
	{
		const UnsignedInt pa = (UnsignedInt)(uintptr_t)obj, pb = (UnsignedInt)(uintptr_t)other;
		const unsigned long long key = pa < pb ? ((unsigned long long)pa << 32) | pb : ((unsigned long long)pb << 32) | pa;
		if (s_gxPairs.size() * 2 >= ((size_t)1 << s_gxKeyBits))
			gxGrowLean();
		const UnsignedInt mask = (1u << s_gxKeyBits) - 1;
		UnsignedInt h = (UnsignedInt)((key * 0x9E3779B97F4A7C15ull) >> (64 - s_gxKeyBits));
		Bool repeat = false;
		for (unsigned long long k; (k = s_gxKeys[h]) != 0; h = (h + 1) & mask)
			if (k == key)
			{
				repeat = true;
				break;
			}
		if (!repeat)
		{
			s_gxKeys[h] = key;
			s_gxUsed.push_back(h);
			GxPair np = { obj, other };
			s_gxPairs.push_back(np);
		}
		if (!m_gxVerify)
			return;
	}

	Object* obj_obj = obj->getObject();
	Object* other_obj = other->getObject();
	if (obj_obj == nullptr || other_obj == nullptr)
		return;

	if (m_gxArray && !m_gxLean)
	{
		const UnsignedInt pa = (UnsignedInt)(uintptr_t)obj, pb = (UnsignedInt)(uintptr_t)other;
		const unsigned long long key = pa < pb ? ((unsigned long long)pa << 32) | pb : ((unsigned long long)pb << 32) | pa;
		if (s_gxPairs.size() * 2 >= ((size_t)1 << s_gxSlotBits))
			gxGrow();
		const UnsignedInt mask = (1u << s_gxSlotBits) - 1;
		UnsignedInt h = (UnsignedInt)((key * 0x9E3779B97F4A7C15ull) >> (64 - s_gxSlotBits));
		for (;; h = (h + 1) & mask)
		{
			GxSlot &slot = s_gxSlots[h];
			if (slot.gen != s_gxGen)
			{
				slot.gen = s_gxGen;
				slot.key = key;
				break;
			}
			if (slot.key == key)
			{
				if (!m_gxVerify)
					return;	// already noted
				goto gxOld;
			}
		}
		{
			GxPair np = { obj, other };
			s_gxPairs.push_back(np);
		}
		if (!m_gxVerify)
			return;
	}
gxOld:

	// compute hash index based on object's ids.
	UnsignedInt hashValue = hash2ints(obj_obj->getID(), other_obj->getID());
	PartitionContactListNode** table = m_contactHash;
	if (m_gxHash)
	{
		table = m_gxHash;
		hashValue = (hashValue * 2654435761u) >> (32 - GX_BIG_BITS);	// mixes both ids into 18 bits
	}
	else
		hashValue %= PartitionContactList_SOCKET_COUNT;

	// make sure given hit has not already been recorded
	for (PartitionContactListNode* cd = table[ hashValue ]; cd; cd = cd->m_nextHash )
	{
		if ((cd->m_obj == obj && cd->m_other == other) ||
				(cd->m_obj == other && cd->m_other == obj))
		{
			// already noted
			return;
		}
	}

	// new hit
	PartitionContactListNode *ncd = newInstance(PartitionContactListNode);
	ncd->m_obj = obj;
	ncd->m_other = other;
	ncd->m_hashValue = hashValue;

	// add to hash table
	ncd->m_nextHash = table[ hashValue ];
	table[ hashValue ] = ncd;

	// add to list of contacts for this frame
	ncd->m_next = m_contactList;
	m_contactList = ncd;


#if 0

Int depth = 0;
for (PartitionContactListNode *cd2 = m_contactHash[ hashValue ]; cd2; cd2 = cd2->m_nextHash )
{
	depth++;
}
if (depth > 3)
{
	DEBUG_LOG(("depth is %d for %s %08lx (%d) - %s %08lx (%d)",
		depth,obj_obj->getTemplate()->getName().str(),obj_obj,obj_obj->getID(),
		other_obj->getTemplate()->getName().str(),other_obj,other_obj->getID()
		));

	for (cd2 = m_contactHash[ hashValue ]; cd2; cd2 = cd2->m_nextHash )
	{
		UnsignedInt rawhash = djb2hash2ints(cd2->m_obj->getObject()->getID(), cd2->m_other->getObject()->getID());
		//hashValue %= PartitionContactList_SOCKET_COUNT;


		DEBUG_LOG(("ENTRY: %s %08lx (%d) - %s %08lx (%d) [rawhash %d]",
			cd2->m_obj->getObject()->getTemplate()->getName().str(),cd2->m_obj->getObject(),cd2->m_obj->getObject()->getID(),
			cd2->m_other->getObject()->getTemplate()->getName().str(),cd2->m_other->getObject(),cd2->m_other->getObject()->getID(),
			rawhash));
	}
}

static Real aggtotal = 0;
static Real aggfull = 0;
static Real aggcount = 0;
for (int ii = 0; ii < PartitionContactList_SOCKET_COUNT; ++ii)
{
	if (m_contactHash[ii])
		aggfull += 1.0f;

	for (cd2 = m_contactHash[ ii ]; cd2; cd2 = cd2->m_nextHash )
	{
		aggtotal += 1.0f;
	}
}
aggcount += 1.0f;
DEBUG_ASSERTLOG(((Int)aggcount)%1000!=0,("avg hash depth at %f is %f, fullness %f%%",
aggcount,aggtotal/(aggcount*PartitionContactList_SOCKET_COUNT),(aggfull*100)/(aggcount*PartitionContactList_SOCKET_COUNT)));
#endif


}

//-----------------------------------------------------------------------------
void PartitionContactList::removeSpecificPartitionData(PartitionData* data)
{
	if (m_gxArray)
	{
		for (GxPair &p : s_gxPairs)
			if (p.a == data || p.b == data)
				p.a = p.b = nullptr;
		return;
	}
	for (PartitionContactListNode* cd = m_contactList; cd; cd = cd->m_next)
	{
		if (cd->m_obj == data || cd->m_other == data)
		{
			cd->m_obj = nullptr;
			cd->m_other = nullptr;
		}
	}
}

//-----------------------------------------------------------------------------
PartitionContactListNode** PartitionContactList::s_gxBig = nullptr;

PartitionContactList::GxSlot* PartitionContactList::s_gxSlots = nullptr;
UnsignedInt PartitionContactList::s_gxSlotBits = 0, PartitionContactList::s_gxGen = 0;
std::vector<PartitionContactList::GxPair> PartitionContactList::s_gxPairs;
unsigned long long* PartitionContactList::s_gxKeys = nullptr;
UnsignedInt PartitionContactList::s_gxKeyBits = 0;
std::vector<UnsignedInt> PartitionContactList::s_gxUsed;

// More pairs than half the slots: twice the slots, the current pairs put back.
void PartitionContactList::gxGrowLean()
{
	free(s_gxKeys);
	++s_gxKeyBits;
	s_gxKeys = (unsigned long long*)calloc((size_t)1 << s_gxKeyBits, sizeof(unsigned long long));
	s_gxUsed.clear();
	const UnsignedInt mask = (1u << s_gxKeyBits) - 1;
	for (const GxPair &p : s_gxPairs)
	{
		const UnsignedInt pa = (UnsignedInt)(uintptr_t)p.a, pb = (UnsignedInt)(uintptr_t)p.b;
		const unsigned long long key = pa < pb ? ((unsigned long long)pa << 32) | pb : ((unsigned long long)pb << 32) | pa;
		UnsignedInt h = (UnsignedInt)((key * 0x9E3779B97F4A7C15ull) >> (64 - s_gxKeyBits));
		while (s_gxKeys[h] != 0)
			h = (h + 1) & mask;
		s_gxKeys[h] = key;
		s_gxUsed.push_back(h);
	}
}

// More pairs than half the slots: twice the slots, the current pairs put back.
void PartitionContactList::gxGrow()
{
	free(s_gxSlots);
	++s_gxSlotBits;
	s_gxSlots = (GxSlot*)calloc((size_t)1 << s_gxSlotBits, sizeof(GxSlot));
	s_gxGen = 1;
	const UnsignedInt mask = (1u << s_gxSlotBits) - 1;
	for (const GxPair &p : s_gxPairs)
	{
		// removed pairs (nulled) can't occur while pairs are still being added
		const UnsignedInt pa = (UnsignedInt)(uintptr_t)p.a, pb = (UnsignedInt)(uintptr_t)p.b;
		const unsigned long long key = pa < pb ? ((unsigned long long)pa << 32) | pb : ((unsigned long long)pb << 32) | pa;
		UnsignedInt h = (UnsignedInt)((key * 0x9E3779B97F4A7C15ull) >> (64 - s_gxSlotBits));
		while (s_gxSlots[h].gen == s_gxGen)
			h = (h + 1) & mask;
		s_gxSlots[h].gen = s_gxGen;
		s_gxSlots[h].key = key;
	}
}

void PartitionContactList::resetContactList()
{
	if (m_gxArray)
	{
		s_gxPairs.clear();
		for (UnsignedInt h : s_gxUsed)
			s_gxKeys[h] = 0;
		s_gxUsed.clear();
		if (!m_gxVerify)
			return;
	}
	// remove items from hash table
	PartitionContactListNode* cdnext;
	for (PartitionContactListNode* cd = m_contactList; cd; cd = cdnext)
	{
		cdnext = cd->m_next;
		if (m_gxHash)
			m_gxHash[cd->m_hashValue] = nullptr;
		deleteInstance(cd);
	}

	if (!m_gxHash)
		memset(m_contactHash, 0, sizeof(m_contactHash));
	m_contactList = nullptr;
}

//-----------------------------------------------------------------------------
// One touching pair: the original loop body. a and b are references into the list so a
// collision reaction that removes a partition data (removeSpecificPartitionData) is seen.
void PartitionContactList::gxProcessPair(PartitionData*& a, PartitionData*& b)
{
	if (a == nullptr || b == nullptr)
		return;

	// Web port (switch pairPlace, second word bit 28): two units whose kept places are farther apart
	// than both bounding radii times sqrt 2 plus one unit can't touch (every shape test is a
	// footprint test; the box test treats a circle as its bounding square, whose corner is r * sqrt 2
	// out), so the pair is done without reading either object. Exact: the places follow every move
	// (Object::reactToTransformChange).
	if ((GLES3_Page_Switches2() & 268435456) && !g_gxPairOff)
	{
		const Real dx = a->gxX() - b->gxX(), dy = a->gxY() - b->gxY(), lim = (a->gxR() + b->gxR()) * 1.4143f + 1.0f;
		if (dx * dx + dy * dy > lim * lim)
		{
			if (GLES3_Page_Switches() & 2097152)
			{
				// verify mode: the full test must agree
				Object *o1 = a->getObject(), *o2 = b->getObject();
				CollideLocAndNormal ci;
				++gx_verify_checked;
				if (o1 && o2 && a->friend_collidesWith(b, &ci))
				{
					++gx_verify_mismatch;
					printf("GXVERIFY pair place mismatch: %u and %u touch; kept %.1f,%.1f r%.1f / %.1f,%.1f r%.1f; real %.1f,%.1f r%.1f type %d / %.1f,%.1f r%.1f type %d; %s / %s\n", o1->getID(), o2->getID(),
						a->gxX(), a->gxY(), a->gxR(), b->gxX(), b->gxY(), b->gxR(),
						o1->getPosition()->x, o1->getPosition()->y, o1->getBoundingCircleRadius(), (int)o1->getGeometryInfo().getGeomType(),
						o2->getPosition()->x, o2->getPosition()->y, o2->getBoundingCircleRadius(), (int)o2->getGeometryInfo().getGeomType(),
						o1->getTemplate()->getName().str(), o2->getTemplate()->getName().str());
				}
				if (gx_verify_checked % 50000 == 0)
					printf("GXVERIFY pair place checked %d, mismatches %d (all checks)\n", gx_verify_checked, gx_verify_mismatch);
			}
			return;
		}
	}

	Object* obj = a->getObject();
	Object* other = b->getObject();
	if (obj == nullptr || other == nullptr)
		return;	// only with leanPairs: a pair with a ghost, which the original never listed

	// Web port (switch collisionOrder, page bit 29): the bit test first, then the geometry.
	// Both only decide whether to skip this pair and change nothing, so the order is free.
	const Bool gxStatusFirst = (GLES3_Page_Switches() & 536870912) != 0;
	if( gxStatusFirst && ( obj->getStatusBits().test( OBJECT_STATUS_NO_COLLISIONS ) ||
			other->getStatusBits().test( OBJECT_STATUS_NO_COLLISIONS ) ) )
		return;

	// we know that their partitions overlap; determine if they REALLY collide
	// before proceeding...
	CollideLocAndNormal cinfo;
	if (!a->friend_collidesWith(b, &cinfo))
		return;

	if( !gxStatusFirst && ( obj->getStatusBits().test( OBJECT_STATUS_NO_COLLISIONS ) ||
			other->getStatusBits().test( OBJECT_STATUS_NO_COLLISIONS ) ) )
		return;

	DEBUG_ASSERTCRASH(!(obj->isKindOf(KINDOF_IMMOBILE) && other->isKindOf(KINDOF_IMMOBILE)),
		("we should never have collisions between two immobile things reported"));

	// the onCollide() calls can remove the object(s) from the partition mgr,
	// thus destroying the partitiondata for 'em. go ahead and null these out here
	// so we won't be tempted to use 'em (since they might be bogus).
	a = nullptr;
	b = nullptr;

	obj->onCollide(other, &cinfo.loc, &cinfo.normal);
	flipCoord3D(&cinfo.normal);

 		//Before checking the "other" case, make sure that the previous collision didn't
 		//absorb him. This becomes a conflict for pilots giving veterancy to transports
 		//and pilots entering transports. Both would occur if these isDestroyed checks
 		//were missing.
 		if( !obj->isDestroyed() && !other->isDestroyed() )
 		{
 			other->onCollide(obj, &cinfo.loc, &cinfo.normal);
 		}

	//
	// NOTE: it is VERY IMPORTANT (for performance reasons) to not re-dirty immobile things.
	//
	// NOTE also that we re-get partitiondata from the object, since it might have been
	// removed from the partition system by the onCollide call...
	//
	if (!obj->isDestroyed() && obj->friend_getPartitionData() != nullptr && !obj->isKindOf(KINDOF_IMMOBILE))
	{
//DEBUG_LOG(("%d: re-dirtying collision of %s %08lx with %s %08lx",TheGameLogic->getFrame(),obj->getTemplate()->getName().str(),obj,other->getTemplate()->getName().str(),other));
		obj->friend_getPartitionData()->makeDirty(false);
	}
	if (!other->isDestroyed() && other->friend_getPartitionData() != nullptr && !other->isKindOf(KINDOF_IMMOBILE))
	{
//DEBUG_LOG(("%d: re-dirtying collision of %s %08lx with %s %08lx [other]",TheGameLogic->getFrame(),other->getTemplate()->getName().str(),other,obj->getTemplate()->getName().str(),obj));
		other->friend_getPartitionData()->makeDirty(false);
	}
}

void PartitionContactList::processContactList()
{
	if (m_gxArray)
	{
		if (m_gxVerify)
		{
			size_t k = s_gxPairs.size(), n = 0;
			Bool same = true;
			// leanPairs also lists pairs with a ghost side, which the old list never has
			#define GX_SKIP_GHOSTS while (m_gxLean && k > 0 && (!s_gxPairs[k-1].a->getObject() || !s_gxPairs[k-1].b->getObject())) --k
			for (PartitionContactListNode* cd = m_contactList; cd; cd = cd->m_next, ++n)
			{
				GX_SKIP_GHOSTS;
				if (k == 0 || s_gxPairs[--k].a != cd->m_obj || s_gxPairs[k].b != cd->m_other)
					same = false;
			}
			GX_SKIP_GHOSTS;
			#undef GX_SKIP_GHOSTS
			++gx_verify_checked;
			if (!same || k != 0)
			{
				++gx_verify_mismatch;
				printf("GXVERIFY contact array mismatch: %d pairs vs %d\n", (int)s_gxPairs.size(), (int)n);
			}
			if (gx_verify_checked % 300 == 0)
				printf("GXVERIFY contact array frames checked %d, mismatches %d\n", gx_verify_checked, gx_verify_mismatch);
		}
		// newest first, as the original list was built by putting each new pair at its head
		for (size_t k = s_gxPairs.size(); k-- > 0; )
			gxProcessPair(s_gxPairs[k].a, s_gxPairs[k].b);
		return;
	}
	for (PartitionContactListNode* cd = m_contactList; cd; cd = cd->m_next)
	{
		gxProcessPair(cd->m_obj, cd->m_other);
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Web port (switches collideStagger / collideStagger3, second switch word bits 10 / 11): staggered
// collisions. Moving ground units look for touching pairs only every 2nd (bit 10) or 3rd (bit 11)
// logic frame, by object ID, so collision reactions (pushes, blocked-by, crushing, crates,
// entering buildings) come up to 2 frames late. A unit skipped on a frame stays dirty and is
// checked on its turn. Changes the game: the one gate below. Single player only for now (never
// network games or replays); to be moved onto the host's match rules (gxMatchRule) when merged.
Int gxCollideStaggerEvery()
{
	// match rules (Common/GxMatchRules.h): the host's setting in network games, the recorded one in replays
	extern bool gxMatchRule(unsigned int rule);
	if (!TheGameLogic)
		return 1;
	return gxMatchRule(512 /* GXRULE_COLLIDE_EVERY3 */) ? 3 : gxMatchRule(256 /* GXRULE_COLLIDE_EVERY2 */) ? 2 : 1;
}

// Only units that walk or drive: projectiles, aircraft and anything without AI keep checking
// every frame (a late missile hit or a late landing would be visible).
static Bool gxStaggers(const Object *o)
{
	return o->getAIUpdateInterface() != nullptr && !o->isAboveTerrain()
		&& (o->isKindOf(KINDOF_INFANTRY) || o->isKindOf(KINDOF_VEHICLE)) && !o->isKindOf(KINDOF_PROJECTILE);
}

//-----------------------------------------------------------------------------
PartitionManager::PartitionManager()
{
	m_moduleList = nullptr;
	m_cellSize = m_cellSizeInv = 0.0f;
	m_cellCountX = 0;
	m_cellCountY = 0;
	m_totalCellCount = 0;
	m_cells = nullptr;
	m_worldExtents.lo.zero();
	m_worldExtents.hi.zero();
	m_dirtyModules = nullptr;
	m_updatedSinceLastReset = false;
#ifdef FASTER_GCO
	m_maxGcoRadius = 0;
#endif
}

//-----------------------------------------------------------------------------
PartitionManager::~PartitionManager()
{

	shutdown();

}

//-----------------------------------------------------------------------------
#ifdef PM_CACHE_TERRAIN_HEIGHT
static void calcHeights(const Region3D& world, Real cellSize, Int x, Int y, Real& loZ, Real& hiZ)
{
	DEBUG_ASSERTCRASH(TheTerrainLogic, ("no TheTerrainLogic"));
	Real xbase = world.lo.x + (x * cellSize);
	Real ybase = world.lo.y + (y * cellSize);
	const Real ROUGH_STEP_SIZE = MAP_XY_FACTOR;	// no point in stepping smaller than grid scale
	Real numSteps = ceilf(cellSize / ROUGH_STEP_SIZE);
	Real step = cellSize / numSteps;
	loZ = HUGE_DIST;		// huge positive
	hiZ = -HUGE_DIST;		// huge negative
	for (Real yy = 0; yy <= cellSize; yy += step)
	{
		for (Real xx = 0; xx <= cellSize; xx += step)
		{
			Real h = TheTerrainLogic->getGroundHeight( xbase + xx, ybase + yy );
			if (h < loZ) loZ = h;
			if (h > hiZ) hiZ = h;
		}
	}
}
#endif

//-----------------------------------------------------------------------------
void PartitionManager::init()
{
	m_cellSize = TheGlobalData->m_partitionCellSize;
	if (m_cellSize < 1.0)
		m_cellSize = 1.0;

	m_cellSizeInv = (Real)(1.0 / m_cellSize);

	DEBUG_ASSERTCRASH(m_cells == nullptr, ("double init"));

	if (TheTerrainLogic)
	{
		TheTerrainLogic->getExtent(&m_worldExtents);
		//DEBUG_ASSERTLOG(m_worldExtents.width() > 0 && m_worldExtents.height() > 0, ("TheTerrainLogic must be loaded before ThePartitionManager"));
		// you'd think it wouldn't be legal for terrainlogic to have zero area, but apparently
		// that's what it resets itself to. let's just make the world a simple place and pretend
		// it's nonzero in area, so that we can proceed without crashing.
		if (m_worldExtents.width() < 1.0f)
			m_worldExtents.hi.x = m_worldExtents.lo.x + 1.0f;
		if (m_worldExtents.height() < 1.0f)
			m_worldExtents.hi.y = m_worldExtents.lo.y + 1.0f;
		m_cellCountX = REAL_TO_INT_CEIL(m_worldExtents.width() * m_cellSizeInv);
		m_cellCountY = REAL_TO_INT_CEIL(m_worldExtents.height() * m_cellSizeInv);
		m_totalCellCount = m_cellCountX * m_cellCountY;
		m_cells = MSGNEW("PartitionManager_Cells") PartitionCell[m_totalCellCount];
		delete [] g_gxCellMasks;
		g_gxCellMasks = MSGNEW("PartitionManager_CellMasks") UnsignedInt[m_totalCellCount];
		memset(g_gxCellMasks, 0, m_totalCellCount * sizeof(UnsignedInt));
		g_gxCellMaskW = m_cellCountX;
		g_gxCellMaskH = m_cellCountY;
		g_gxBlockMaskW = (m_cellCountX + (1 << GX_BLOCK_SHIFT) - 1) >> GX_BLOCK_SHIFT;
		{
			const Int blocks = g_gxBlockMaskW * ((m_cellCountY + (1 << GX_BLOCK_SHIFT) - 1) >> GX_BLOCK_SHIFT);
			delete [] g_gxBlockMasks;
			g_gxBlockMasks = MSGNEW("PartitionManager_BlockMasks") UnsignedInt[blocks];
			memset(g_gxBlockMasks, 0, blocks * sizeof(UnsignedInt));
		}
		delete [] g_gxShroudCur;
		delete [] g_gxShroudAct;
		g_gxShroudN = m_totalCellCount;
		g_gxShroudCur = MSGNEW("PartitionManager_ShroudCur") Short[MAX_PLAYER_COUNT * m_totalCellCount];
		g_gxShroudAct = MSGNEW("PartitionManager_ShroudAct") Short[MAX_PLAYER_COUNT * m_totalCellCount];
		for (Int i = 0; i < MAX_PLAYER_COUNT * m_totalCellCount; ++i)
		{
			g_gxShroudCur[i] = 1;	// passive shroud (1, 0), as PartitionCell's constructor
			g_gxShroudAct[i] = 0;
		}
		for (Int x = 0; x < m_cellCountX; x++)
		{
			for (Int y = 0; y < m_cellCountY; y++)
			{
#ifdef PM_CACHE_TERRAIN_HEIGHT
				Real loZ, hiZ;
				calcHeights(m_worldExtents, m_cellSize, x, y, loZ, hiZ);
				getCellAt(x, y)->init(x, y, loZ, hiZ);
#else
				getCellAt(x, y)->init(x, y);
#endif
			}
		}

#ifdef FASTER_GCO
		calcRadiusVec();
#endif

	}
	else
	{
		m_cellSize = m_cellSizeInv = 0.0f;
		m_cellCountX = 0;
		m_cellCountY = 0;
		m_totalCellCount = 0;
		m_cells = nullptr;
		m_worldExtents.lo.zero();
		m_worldExtents.hi.zero();
	}
}

//-----------------------------------------------------------------------------
#ifdef DUMP_PERF_STATS
void PartitionManager::getPMStats(double& gcoTimeThisFrameTotal, double& gcoTimeThisFrameAvg)
{
	Int64 freq64;
	GetPrecisionTimerTicksPerSec(&freq64);

	double gcoTimeInMSecs = (double)s_timeInClosestObjectsThisFrame * 1000.0f / (double)freq64;

	gcoTimeThisFrameTotal = gcoTimeInMSecs;
	gcoTimeThisFrameAvg = gcoTimeInMSecs / (double)s_countInClosestObjectsThisFrame;
}
#endif

//-----------------------------------------------------------------------------
static void gxSnapReset();

void PartitionManager::reset()
{
	gxSnapReset();	// web port (snapPatch): a new game reuses object IDs
	g_gxCellCountsStale = false;	// web port: a new game trusts the per-cell player counts again
#ifdef DUMP_PERF_STATS
	s_countInClosestObjects = 0;
	s_timeInClosestObjects = 0;
	s_countInClosestObjectsThisFrame = 0;
	s_timeInClosestObjectsThisFrame = 0;
	s_gcoPerfFrame = 0xffffffff;
#endif

	resetPendingUndoShroudRevealQueue();

	shutdown();
	//init();
}

//-----------------------------------------------------------------------------
void PartitionManager::shutdown()
{
	m_updatedSinceLastReset = false;
	removeAllDirtyModules();

#ifdef RTS_DEBUG
	// the above *should* remove all the touched cells (via unRegisterObject), but let's check:
	DEBUG_ASSERTCRASH( m_moduleList == nullptr, ("hmm, modules left over"));
	PartitionData *mod, *nextMod;
	for( mod = m_moduleList; mod; mod = nextMod )
	{
		nextMod = mod->getNext();
		DEBUG_ASSERTCRASH(mod->friend_getCoiInUseCount() == 0, ("hmm, coi count mismatch"));
		mod->friend_removeAllTouchedCells();
	}
#endif

#ifdef FASTER_GCO
	m_radiusVec.clear();
#endif

	resetPendingUndoShroudRevealQueue();

	delete [] m_cells;
	m_cells = nullptr;
	delete [] g_gxCellMasks;
	g_gxCellMasks = nullptr;
	g_gxCellMaskW = 0;
	delete [] g_gxBlockMasks;
	g_gxBlockMasks = nullptr;
	g_gxBlockMaskW = 0;
	g_gxCellMaskH = 0;
	delete [] g_gxShroudCur;
	delete [] g_gxShroudAct;
	g_gxShroudCur = g_gxShroudAct = nullptr;
	g_gxShroudN = 0;

	m_cellSize = m_cellSizeInv = 0.0f;
	m_cellCountX = 0;
	m_cellCountY = 0;
	m_totalCellCount = 0;
	m_worldExtents.lo.zero();
	m_worldExtents.hi.zero();
}

//-----------------------------------------------------------------------------
//DECLARE_PERF_TIMER(PartitionManager_update)
//-----------------------------------------------------------------------------
// Web port (round 4, match rule storePairs 65536): the collision broad phase over the unit store.
// Every unit the partition holds is put in a grid by the store's cell index (its centre cell);
// units larger than half a cell are put in every cell under their bounding square instead. Each
// unit that needs a collision check this frame (in object-ID order) is offered every unit within
// reach in that grid: a footprint-sized square for the large ones, footprint plus the largest small
// radius for the small ones, then the same distance cut as pairPlace. The contact list drops
// repeats. Drift: the pairs come in a different order than from the partition's cell lists, and a
// unit is paired by where it is now rather than by the cells it touched when last updated.
static void gxStoreBroadPhase(std::vector<PartitionData*> &seekers, PartitionContactList *ct)
{
	GxUnitStore &st = TheGxUnitStore;
	PartitionManager *pm = ThePartitionManager;
	const Int nx = pm->getCellCountX(), ny = pm->getCellCountY();
	const Real bigR = pm->getCellSize() * 0.5f;
	static std::vector<int> head, next, item;
	head.assign((size_t)nx * ny, -1);
	next.clear(); item.clear();
	Real smallMax = 0.0f;
	for (int s = 0; s < st.size(); ++s)
	{
		Object *o = st.obj[s];
		PartitionData *pd = o ? o->friend_getPartitionData() : nullptr;
		if (!pd || pd->friend_getCoiInUseCount() == 0)
			continue;	// not in the partition's cells (e.g. inside a transport)
		const Real r = st.radius[s];
		const Coord3D p = st.posOf(s);
		Int x0, y0, x1, y1;
		if (r <= bigR)
		{
			if (r > smallMax) smallMax = r;
			const int c = st.cell[s];	// the store's cell index (written by the partition)
			if (c >= 0) { x0 = c % nx; y0 = c / nx; }
			else pm->worldToCell(p.x, p.y, &x0, &y0);
			x1 = x0; y1 = y0;
		}
		else
		{
			const Real e = r * 1.4143f + 1.0f;
			pm->worldToCell(p.x - e, p.y - e, &x0, &y0);
			pm->worldToCell(p.x + e, p.y + e, &x1, &y1);
		}
		x0 = max(x0, 0); y0 = max(y0, 0); x1 = min(x1, nx - 1); y1 = min(y1, ny - 1);
		for (Int cy = y0; cy <= y1; ++cy)
			for (Int cx = x0; cx <= x1; ++cx)
			{
				const int c = cy * nx + cx;
				item.push_back(s); next.push_back(head[c]); head[c] = (int)item.size() - 1;
			}
	}

	std::sort(seekers.begin(), seekers.end(), [](const PartitionData *a, const PartitionData *b) { return gxBefore(a, b); });
	for (PartitionData *d : seekers)
	{
		const Object *o = d->getObject();
		const int s = o ? o->gxStoreSlot() : -1;
		if (s < 0)
			continue;
		const Real r = st.radius[s];
		const Coord3D p = st.posOf(s);
		const Real e = (r + smallMax) * 1.4143f + 1.0f;
		Int x0, y0, x1, y1;
		pm->worldToCell(p.x - e, p.y - e, &x0, &y0);
		pm->worldToCell(p.x + e, p.y + e, &x1, &y1);
		x0 = max(x0, 0); y0 = max(y0, 0); x1 = min(x1, nx - 1); y1 = min(y1, ny - 1);
		for (Int cy = y0; cy <= y1; ++cy)
			for (Int cx = x0; cx <= x1; ++cx)
				for (int k = head[cy * nx + cx]; k >= 0; k = next[k])
				{
					const int t = item[k];
					if (t == s)
						continue;
					const Real dx = st.x[t] - p.x, dy = st.y[t] - p.y, lim = (st.radius[t] + r) * 1.4143f + 1.0f;
					if (dx * dx + dy * dy > lim * lim)
						continue;
					PartitionData *that = st.obj[t]->friend_getPartitionData();
					if (g_gxDeferStamp && that->gxDeferStamp() == g_gxDeferStamp && gxBefore(that, d))
						continue;	// collideStagger, as in addPossibleCollisions
					ct->addToContactList(d, that);
				}
	}
}

void PartitionManager::update()
{
	GxTimeScope gxTU(GXT_PART_UPDATE);	// tickTiming
	//USE_PERF_TIMER(PartitionManager_update)
#ifdef __EMSCRIPTEN__
	// Timing mode (hidden setting gxPairTiming, normal build): frames picked by a fixed hash of the
	// frame number run without pairPlace / pairOffer; both kinds of frame are timed, so machine load
	// hits both alike. Only for measuring: with pairOffer on it changes the game.
	static Int s_gxTiming = 0;
	static double s_gxPairMs[2];
	static Int s_gxPairFrames[2];
	const UnsignedInt gxF = TheGameLogic ? TheGameLogic->getFrame() : 0;
	if (gxF % 60 == 0)
		s_gxTiming = EM_ASM_INT({ var s = typeof window !== 'undefined' && window.GeneralsX && window.GeneralsX.settings; return s && s.gxPairTiming ? 1 : 0; });
	g_gxPairOff = s_gxTiming && ((gxF * 2654435761u) >> 16 & 1);
	const double gxT0 = s_gxTiming ? emscripten_get_now() : 0.0;
	struct GxPairTimer
	{
		double t0;
		~GxPairTimer()
		{
			if (!s_gxTiming)
				return;
			const Int k = g_gxPairOff ? 1 : 0;
			s_gxPairMs[k] += emscripten_get_now() - t0;
			if (++s_gxPairFrames[k] % 300 == 0 && k == 0)
				printf("GXPAIRTIME partition update: features on %.1f ms over %d frames (%.3f ms each), off %.1f ms over %d frames (%.3f ms each)\n",
					s_gxPairMs[0], s_gxPairFrames[0], s_gxPairMs[0] / s_gxPairFrames[0], s_gxPairMs[1], s_gxPairFrames[1], s_gxPairFrames[1] ? s_gxPairMs[1] / s_gxPairFrames[1] : 0.0);
		}
	} gxTimer = { gxT0 };
#endif
	{
#ifdef INTENSE_DEBUG
		Int cc = 0;
#endif
		if (!m_updatedSinceLastReset)
		{
			m_updatedSinceLastReset = true;
		}

		PartitionContactList ctList;
		TheContactList = &ctList;
		const Int gxEvery = gxCollideStaggerEvery();
		const UnsignedInt gxFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;
		static std::vector<ObjectID> s_gxLater;
		s_gxLater.clear();
		static std::vector<PartitionData*> s_gxSeekers;
		s_gxSeekers.clear();
		const Bool gxStorePairs = gxMatchRule(GXRULE_STORE_PAIRS);
		g_gxDeferStamp = 0;
		if (gxEvery > 1)
		{
			// mark this frame's waiting units first, so pair offers can see them
			g_gxDeferStamp = gxFrame + 1;
			for (PartitionData *d = m_dirtyModules; d; d = d->gxNextDirty())
			{
				const Object *o = d->getObject();
				if (o && d->isInNeedOfCollisionCheck() && !o->isKindOf(KINDOF_IMMOBILE)
						&& (o->getID() + gxFrame) % gxEvery != 0 && gxStaggers(o))
					d->gxSetDeferStamp(g_gxDeferStamp);
			}
		}
		while (m_dirtyModules)
		{
#ifdef INTENSE_DEBUG
			++cc;
#endif

			// save it.
			PartitionData *dirty = m_dirtyModules;
			DEBUG_ASSERTCRASH(dirty->getObject() != nullptr || dirty->getGhostObject() != nullptr,
												("must be attached to an Object here %08lx",dirty));

			// get this BEFORE removing from dirty list, since that clears the
			// flag in question.
			Bool updateEm = dirty->isInNeedOfUpdatingCells();
			Bool collideEm = dirty->isInNeedOfCollisionCheck() && dirty->getObject();	//only update collisions if we have object

			// detach it from the dirty list.
			removeFromDirtyModules(dirty);

			if (updateEm)
			{
				{ GxTimeScope gxT(GXT_CELLS); gxCount(GXC_CELL_UPDATES); dirty->friend_updateCellsTouched(); }
			}

			if (collideEm && !dirty->getObject()->isKindOf(KINDOF_IMMOBILE))
			{
				// staggered collisions: a ground unit not on its turn stays dirty for a later frame
				// (its cells are still updated now, so range searches see where it is)
				if (g_gxDeferStamp && dirty->gxDeferStamp() == g_gxDeferStamp)
					s_gxLater.push_back(dirty->getObject()->getID());
				else if (gxStorePairs)
					s_gxSeekers.push_back(dirty);	// storePairs: offered below, from the unit store
				else
					{ GxTimeScope gxT(GXT_PAIRS); dirty->addPossibleCollisions(&ctList); }
			}
		}

		if (gxStorePairs)
			gxStoreBroadPhase(s_gxSeekers, &ctList);
		ctList.processContactList();
		// put the deferred units back on the dirty list (found by ID: a reaction may have removed them)
		for (ObjectID id : s_gxLater)
		{
			Object *o = TheGameLogic->findObjectByID(id);
			if (o && !o->isDestroyed() && o->friend_getPartitionData())
				o->friend_getPartitionData()->makeDirty(false);
		}
#ifdef INTENSE_DEBUG
		DEBUG_ASSERTLOG(cc==0,("updated partition info for %d objects",cc));
#endif
		TheContactList = nullptr;

		processPendingUndoShroudRevealQueue();
	}

#if defined(RTS_DEBUG)
	if (TheGlobalData->m_debugThreatMap)
	{
		if (TheGameLogic->getFrame() % TheGlobalData->m_debugThreatMapTileDuration)
		{
			Int cellCount = m_cellCountX * m_cellCountY;
			for (int i = 0; i < cellCount; ++i)
			{
				UnsignedInt threat = m_cells[i].getThreatValue(rts::getObservedOrLocalPlayer()->getPlayerIndex());
				if (threat > 0)
				{
					Real threatMul = INT_TO_REAL(threat) / TheGlobalData->m_maxDebugThreat;
					if (threatMul > 1.0f)
						threatMul = 1.0f;

					Real size = TheGlobalData->m_partitionCellSize;
					Coord3D pos = { m_cells[i].getCellX() * size,
													m_cells[i].getCellY() * size,
													0 };
					pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
					RGBColor color;
					color.red = threatMul;
					color.blue = 0.0f;
					color.green = 0.0f;

					addIcon(&pos, size, TheGlobalData->m_debugThreatMapTileDuration, color);
				}
			}
		}
	}

	if (TheGlobalData->m_debugCashValueMap)
	{
		if (TheGameLogic->getFrame() % TheGlobalData->m_debugCashValueMapTileDuration)
		{
			Int cellCount = m_cellCountX * m_cellCountY;
			for (int i = 0; i < cellCount; ++i)
			{
				UnsignedInt value = m_cells[i].getCashValue(rts::getObservedOrLocalPlayer()->getPlayerIndex());
				if (value > 0)
				{
					Real valueMul = INT_TO_REAL(value) / TheGlobalData->m_maxDebugValue;
					if (valueMul > 1.0f)
						valueMul = 1.0f;

					Real size = TheGlobalData->m_partitionCellSize;
					Coord3D pos = { m_cells[i].getCellX() * size,
													m_cells[i].getCellY() * size,
													0 };
					pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
					RGBColor color;
					color.red = 0.0f;
					color.blue = 0.0f;
					color.green = valueMul;

					addIcon(&pos, size, TheGlobalData->m_debugCashValueMapTileDuration, color);
				}
			}
		}
	}
#endif // defined(RTS_DEBUG)
}

//------------------------------------------------------------------------------
void gxSnapTouch(const Object *o);

void PartitionManager::registerObject( Object* object )
{
	// sanity
	if( object == nullptr )
		return;
	gxSnapTouch(object);	// web port (snapPatch): its partition data / geometry may change

	// if object is already part of this system get out of here
	if( object->friend_getPartitionData() != nullptr )
	{
		DEBUG_LOG(( "Object '%s' already registered with partition manager",
								object->getTemplate()->getName().str() ));
		return;
	}

	// allocate a new module of partition data
	PartitionData *mod = newInstance( PartitionData );

	// link the module to the list in the partition manager
	mod->setPrev( nullptr );
	mod->setNext( m_moduleList );
	if( m_moduleList )
		m_moduleList->setPrev( mod );
	m_moduleList = mod;

	// add module to object
	mod->attachToObject( object );

}

//------------------------------------------------------------------------------
void PartitionManager::unRegisterObject( Object* object )
{
	// sanity
	if( object == nullptr )
		return;
	gxSnapTouch(object);	// web port (snapPatch)

	// get the partition module
	PartitionData *mod = object->friend_getPartitionData();
	if( mod == nullptr )
		return;

	GhostObject *ghost;

	// need to figure out if any players have a fogged memory of this object.
	// if so, we can't remove it from the shroud system just yet.
	if ((ghost=mod->getGhostObject()) != nullptr && mod->wasSeenByAnyPlayers() < MAX_PLAYER_COUNT)
	{
		if (TheContactList)
			TheContactList->removeSpecificPartitionData(mod);
		object->friend_setPartitionData(nullptr);
		mod->friend_setObject(nullptr);
		//Tell the ghost object that its parent is dead.
		ghost->updateParentObject(nullptr, mod);
		return;
	}

	// detach the module from the object
	mod->detachFromObject();

	// remove module from master list
	PartitionData *next = mod->getNext();
	PartitionData *prev = mod->getPrev();
	if( next )
		next->setPrev( prev );
	if( prev )
		prev->setNext( next );
	else
		m_moduleList = next;

	// delete module
	deleteInstance(mod);

}

//------------------------------------------------------------------------------
void PartitionManager::registerGhostObject( GhostObject* object)
{
	// sanity
	if( object == nullptr )
		return;

	// if object is already part of this system get out of here
	if( object->friend_getPartitionData() != nullptr )
	{
		DEBUG_LOG(( "GhostObject already registered with partition manager"));
		return;
	}

	// allocate a new module of partition data
	PartitionData *mod = newInstance( PartitionData );

	// link the module to the list in the partition manager
	mod->setPrev( nullptr );
	mod->setNext( m_moduleList );
	if( m_moduleList )
		m_moduleList->setPrev( mod );
	m_moduleList = mod;

	// add module to object
	mod->attachToGhostObject( object);
}

//------------------------------------------------------------------------------
void PartitionManager::unRegisterGhostObject( GhostObject* object )
{
	// sanity
	if( object == nullptr )
		return;

	// get the partition module
	PartitionData *mod = object->friend_getPartitionData();
	if( mod == nullptr )
		return;

	// detach the module from the object
	mod->detachFromGhostObject();

	// remove module from master list
	PartitionData *next = mod->getNext();
	PartitionData *prev = mod->getPrev();
	if( next )
		next->setPrev( prev );
	if( prev )
		prev->setNext( next );
	else
		m_moduleList = next;

	// delete module
	deleteInstance(mod);
}

/**
	Reveals the map for the given player, but does not override Shroud generation.  (Script)
*/
void PartitionManager::revealMapForPlayer( Int playerIndex )
{
	// By looking and then stopping on every cell, I clear all Passive Shroud
	// By adding a looker directly I don't hit the Ally logic of the normal look/doShroudReveal
	for (int i = 0; i < m_totalCellCount; ++i)
	{
		m_cells[i].addLooker( playerIndex );
		m_cells[i].removeLooker( playerIndex );
	}
}

/**
	Reveals the map for the given player, AND permanently disables all Shroud generation (Observer Mode).
	*/
void PartitionManager::revealMapForPlayerPermanently( Int playerIndex )
{
	// By skipping the removeLooker, I consider myself as actively looking at everything,
	// so Shroud generation will no longer function
	// By adding a looker directly I don't hit the Ally logic of the normal look/doShroudReveal
	for (int i = 0; i < m_totalCellCount; ++i)
	{
		m_cells[i].addLooker( playerIndex );
	}
}

/**
		Adds a layer of permanent blindness.  Used solely to undo the permanent reveal for debugging
	*/
void PartitionManager::undoRevealMapForPlayerPermanently( Int playerIndex )
{
	//First make sure no lingering looks will leave holes when they aren't wanted.
	processEntirePendingUndoShroudRevealQueue();

	// This will have amusing consequences if done without a preceding revealMapForPlayerPermanently.
	// Everything you own can become shrouded.
	for (int i = 0; i < m_totalCellCount; ++i)
	{
		m_cells[i].removeLooker( playerIndex );
	}
}

/**
	Resets the shroud for the given player with passive shroud (can re-explore).
	*/
void PartitionManager::shroudMapForPlayer( Int playerIndex )
{
	//First make sure no lingering looks will leave holes when they aren't wanted.
	processEntirePendingUndoShroudRevealQueue();

	// By pulsing a blast of shroud like this, we will set everything not actively looked at as Passive Shroud
	for (int i = 0; i < m_totalCellCount; ++i)
	{
		m_cells[i].addShrouder( playerIndex );
		m_cells[i].removeShrouder( playerIndex );
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::refreshShroudForLocalPlayer()
{
	// This is a drawing refresh only, and so is allowed to use the Local Player.
	TheDisplay->clearShroud();
	TheRadar->clearShroud();

	if (m_totalCellCount != 0)
	{
		const Int playerIndex = rts::getObservedOrLocalPlayer()->getPlayerIndex();
		TheRadar->beginSetShroudLevel();

		for (int i = 0; i < m_totalCellCount; ++i)
		{
			const Int x = m_cells[i].getCellX();
			const Int y = m_cells[i].getCellY();
			const CellShroudStatus status = m_cells[i].getShroudStatusForPlayer(playerIndex);
			TheDisplay->setShroudLevel(x, y, status);
			TheRadar->setShroudLevel(x, y, status);
			m_cells[i].invalidateShroudedStatusForAllCois(playerIndex);
		}
		TheRadar->endSetShroudLevel();
	}
}

//-----------------------------------------------------------------------------
CellShroudStatus PartitionManager::getShroudStatusForPlayer(Int playerIndex, Int x, Int y) const
{
	if( playerIndex < 0 )
		return CELLSHROUD_SHROUDED;// Safety.  There are no Negative players, but PlayerIndex is typedef'd to Int, not UnsignedInt

	const PartitionCell* cell = getCellAt(x, y);
	return cell ? cell->getShroudStatusForPlayer(playerIndex) : CELLSHROUD_SHROUDED;
}

//-----------------------------------------------------------------------------
CellShroudStatus PartitionManager::getShroudStatusForPlayer(Int playerIndex, const Coord3D *loc ) const
{
	Int x, y;

	worldToCell( loc->x, loc->y, &x, &y );

	return getShroudStatusForPlayer( playerIndex, x, y );
}


//-----------------------------------------------------------------------------
ObjectShroudStatus PartitionManager::getPropShroudStatusForPlayer(Int playerIndex, const Coord3D *loc ) const
{
	Int x, y;

	worldToCell( loc->x - m_cellSize*0.5f, loc->y - m_cellSize*0.5f, &x, &y );

	CellShroudStatus cellStat = getShroudStatusForPlayer( playerIndex, x, y );
	if (cellStat != getShroudStatusForPlayer( playerIndex, x+1, y )) {
		return OBJECTSHROUD_PARTIAL_CLEAR;
	}
	if (cellStat != getShroudStatusForPlayer( playerIndex, x+1, y+1 )) {
		return OBJECTSHROUD_PARTIAL_CLEAR;
	}
	if (cellStat != getShroudStatusForPlayer( playerIndex, x, y+1 )) {
		return OBJECTSHROUD_PARTIAL_CLEAR;
	}
	if (cellStat == CELLSHROUD_SHROUDED) {
		return OBJECTSHROUD_SHROUDED;
	}
	if (cellStat == CELLSHROUD_CLEAR) {
		return OBJECTSHROUD_CLEAR;
	}
	return OBJECTSHROUD_FOGGED;
}



#ifdef FASTER_GCO
//-----------------------------------------------------------------------------
Int PartitionManager::calcMinRadius(const ICoord2D& cur)
{
	/*
		ok, so if obj "A" is in cell (0,0), and obj "B"
		is in cell (cur.x, cur.y), calc the smallest distance they could be
		from each other, in integral multiples of cellSize. Note that
		each object can be anywhere within a given cell (not just in the center!)
		so even objects in the same cell can still be cellSize*1.414 apart,
		and objects in adjacent cells can be nearly-zero distance apart!
	*/
	Real halfCell = m_cellSize * 0.5;
	Coord3D centerPos[4] =
	{
		{ -halfCell, -halfCell },
		{ halfCell, -halfCell },
		{ -halfCell, halfCell },
		{ halfCell, halfCell }
	};

	Real x = cur.x * m_cellSize;
	Real y = cur.y * m_cellSize;
	Coord3D otherPos[4] =
	{
		{ x - halfCell, y - halfCell },
		{ x + halfCell, y - halfCell },
		{ x - halfCell, y + halfCell },
		{ x + halfCell, y + halfCell }
	};

	/*
		this is ugly and there's probably a faster way, but we only do this once per PM reset,
		so it really shouldn't matter... (I hope)
	*/

	double minDistSqr = 1e12;				// double, not real
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			// double, not real
			double dx = centerPos[i].x - otherPos[j].x;
			double dy = centerPos[i].y - otherPos[j].y;
			double curDistSqr = dx*dx + dy*dy;
			if (minDistSqr > curDistSqr)
				minDistSqr = curDistSqr;
		}
	}

	// double, not real
	double dist = sqrtf(minDistSqr);
	Int minRadius = REAL_TO_INT_CEIL( dist / m_cellSize );

	return minRadius;
}
#endif

#ifdef FASTER_GCO
//-----------------------------------------------------------------------------
void PartitionManager::calcRadiusVec()
{
	Real cellSize = getCellSize();
	Int cx = getCellCountX();
	Int cy = getCellCountY();

	// double, not real
	double dx = (double)cx * (double)cellSize;
	double dy = (double)cy * (double)cellSize;
	double maxPossibleDist = sqrt(dx*dx + dy*dy);

	m_maxGcoRadius = REAL_TO_INT_CEIL(maxPossibleDist / cellSize);

	m_radiusVec.clear();
	m_radiusVec.resize(m_maxGcoRadius+1, OffsetVec());

	ICoord2D cur;
	for (cur.y = -cy+1; cur.y < cy; ++cur.y)
	{
		for (cur.x = -cx+1; cur.x < cx; ++cur.x)
		{
			/*
				m_radiusVec[curRadius] contains a list of the cells (foo) that could
				contain objects that are <= (curRadius * cellSize) distance away from cell (0,0).
			*/
			Int curRadius = calcMinRadius(cur);
			DEBUG_ASSERTCRASH(curRadius <= m_maxGcoRadius, ("expected max of %d but got %d",m_maxGcoRadius,curRadius));
			if (curRadius <= m_maxGcoRadius)
				m_radiusVec[curRadius].push_back(cur);
		}
	}

#if defined(RTS_DEBUG)
	Int total = 0;
	for (Int i = 0; i <= m_maxGcoRadius; ++i)
	{
		total += m_radiusVec[i].size();
		//DEBUG_LOG(("radius %d has %d entries",i,m_radiusVec[i].size()));
	}
	DEBUG_ASSERTCRASH(total == (cx*2-1)*(cy*2-1),("expected %d, got %d",(cx*2-1)*(cy*2-1),total));
#endif

}
#endif

//-----------------------------------------------------------------------------
//DECLARE_PERF_TIMER(getClosestObjects)

// The player slots whose objects could pass filters[0], or all slots (no skipping).
// Slot MAX_PLAYER_COUNT (no player) is in the mask when neutrals pass.
static UnsignedInt gxEnemySlots(PartitionFilter **filters)
{
#ifndef __EMSCRIPTEN__
	return 0xffffffffu;
#endif
	if (!filters || !filters[0] || g_gxCellCountsStale || !(GLES3_Page_Switches() & 268435456))
		return 0xffffffffu;
	Int allow = 1 << ENEMIES;
	const Object *me = filters[0]->gxEnemiesOf();
	// switch searchSlotSkip (second word bit 16): also a relationship filter that lets no allies through
	if (!me && (GLES3_Page_Switches2() & 65536))
		me = filters[0]->gxRelationOf(allow);
	if (!me || (allow & (1 << ALLIES)))
		return 0xffffffffu;	// allies include undetected defectors of any player: no skipping
	const Team *myTeam = me->getTeam();
	const Player *myPlayer = myTeam ? myTeam->getControllingPlayer() : nullptr;
	if (!myPlayer || me->getIsUndetectedDefector() || myTeam->gxHasTeamOverrides() || myPlayer->gxHasTeamOverrides())
		return 0xffffffffu;
	UnsignedInt mask = 0;
	for (Int i = 0; i < ThePlayerList->getPlayerCount() && i < MAX_PLAYER_COUNT; ++i)
	{
		const Player *p = ThePlayerList->getNthPlayer(i);
		const Team *t = p ? p->getDefaultTeam() : nullptr;
		if (!t || p->getPlayerIndex() != i)
			return 0xffffffffu;
		// with no team-level overrides on our side, every team of player i gets this answer
		if (allow & (1 << myTeam->getRelationship(t)))
			mask |= 1u << i;
	}
	if (allow & (1 << NEUTRAL))
		mask |= 1u << MAX_PLAYER_COUNT;	// no team or no player: NEUTRAL
	return mask;
}

// ---------------------------------------------------------------------------------------------------
// Web port (round 4, match rule scanManager 262144, third word bit 12): range searches as a manager.
// - Per-tick lists: at the first search of a tick, every unit in the unit store (GxUnitStore) that is
//   in the partition goes into the cells its circle covers, in store-slot (object-ID) order. Scans
//   then read the store's columns (obj, player, x, y, radius) and open a unit only when it is near.
// - Scan pass: right after, one pass over pass(GXB_SCAN) in object-ID order runs each unit's nearest
//   enemy scan as its AI asked for it last tick (AI::findClosestEnemy without extra filter / priority
//   info), so the AI's own call this tick finds the answer in the cache.
// - Result cache: within a tick, a search with the same searcher (or point), range, distance type,
//   mode and filter classes reuses the first answer (units that died since are left out).
// Changes the game (answers as of the tick's first search): a match rule, default off.
static unsigned long long g_gxSimdCulled = 0;	// lanes the SIMD cull dropped (verify report)
static std::vector<Int> s_gxSnapStart, s_gxSnapSlots;	// per cell: first index into slots; one more at the end
static std::vector<UnsignedInt> s_gxSnapStamp;			// per slot: last search that looked at it
static std::vector<Real> s_gxSnapR;	// per slot: max(bounding circle, bounding sphere) radius, as the COIs' cached place
static UnsignedInt s_gxSnapSearch = 0, s_gxSnapFrame = ~0u, s_gxSnapEpoch = 0;
struct GxScanRequest { UnsignedInt frame; Real range; UnsignedInt qualifiers; };
static std::unordered_map<Int, GxScanRequest> s_gxScanRequests;	// by object ID: the last plain enemy scan asked for
struct GxCachedSearch { ObjectID closest; Real dist; Coord3D vec; std::vector<std::pair<ObjectID, Real> > list; };
// The tick's answers: open addressing over keys, entries reused across ticks (their lists keep their
// capacity), so a warm game allocates nothing per cached search. A stamp per bucket clears it in O(1).
struct GxSearchTable
{
	std::vector<unsigned long long> key;
	std::vector<UnsignedInt> stamp;
	std::vector<Int> at;
	std::vector<GxCachedSearch> ent;
	Int used = 0;
	UnsignedInt now = 1;

	static size_t hashOf(unsigned long long k) { k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33; return (size_t)k; }
	void clear() { used = 0; if (++now == 0) { std::fill(stamp.begin(), stamp.end(), 0u); now = 1; } }
	GxCachedSearch *find(unsigned long long k)
	{
		if (key.empty()) return nullptr;
		const size_t m = key.size() - 1;
		for (size_t i = hashOf(k) & m; stamp[i] == now; i = (i + 1) & m)
			if (key[i] == k) return &ent[at[i]];
		return nullptr;
	}
	GxCachedSearch &get(unsigned long long k)	///< the entry for k, a fresh one if absent
	{
		if (GxCachedSearch *e = find(k)) return *e;
		if ((size_t)(used + 1) * 2 > key.size()) grow();
		const size_t m = key.size() - 1;
		size_t i = hashOf(k) & m;
		while (stamp[i] == now) i = (i + 1) & m;
		key[i] = k; stamp[i] = now; at[i] = used;
		if ((size_t)used == ent.size()) ent.emplace_back();
		return ent[used++];
	}
	void grow()
	{
		const size_t n = key.empty() ? 256 : key.size() * 2;
		std::vector<unsigned long long> k2(n); std::vector<UnsignedInt> s2(n, 0u); std::vector<Int> a2(n);
		for (size_t i = 0; i < key.size(); ++i)
			if (stamp[i] == now)
			{
				size_t j = hashOf(key[i]) & (n - 1);
				while (s2[j] == now) j = (j + 1) & (n - 1);
				k2[j] = key[i]; s2[j] = now; a2[j] = at[i];
			}
		key.swap(k2); stamp.swap(s2); at.swap(a2);
	}
};
static GxSearchTable s_gxSearchCache;
static std::unordered_map<unsigned long long, GxCachedSearch> s_gxSearchMap;	// switch searchTable off: the map as before
static inline bool gxSearchTableOn()	// switch searchTable (sixth word bit 8, on)
{
#ifdef __EMSCRIPTEN__
	extern int GLES3_Page_Switches6();
	return (GLES3_Page_Switches6() & 256) != 0;
#else
	return false;
#endif
}
static UnsignedInt s_gxManagerFrame = ~0u;
static Bool s_gxScanPassDone = false;
static Bool s_gxInScanPass = false;	// the pass's own scans are not requests (else every unit rescans forever)
static UnsignedInt gx_mgr_hits, gx_mgr_misses, gx_mgr_pass, gx_mgr_differ;

/// AI::findClosestEnemy reports each scan here (match rule scanManager), so next tick's pass can run it
void gxScanNoteRequest(const Object *me, Real range, UnsignedInt qualifiers, const AttackPriorityInfo *info, PartitionFilter *optionalFilter)
{
	extern bool gxMatchRule(unsigned int rule);
	if (s_gxInScanPass || !me || !TheGameLogic || !gxMatchRule(262144 /* GXRULE_SCAN_MANAGER */))
		return;
	if (optionalFilter || (info && info != TheScriptEngine->getDefaultAttackInfo()))
		return;	// only the plain scan can be rerun by the pass
	GxScanRequest r = { TheGameLogic->getFrame(), range, qualifiers };
	s_gxScanRequests[me->getID()] = r;
}

static std::vector<Int> s_gxSnapBeg, s_gxSnapEnd;	// per cell: its list in s_gxSnapSlots (the base CSR, or a patched list after it)
static std::vector<Int> s_gxCurBox;	// per slot: the cell box its entries in the lists in use have (batchScan)
static std::vector<Int> s_gxBaseBox, s_gxBaseId;	// per slot at the base build: cell box (x0, y0, x1, y1; x0 = -1 none) and object ID
static Int s_gxBaseN = 0, s_gxBaseLen = 0;			// slots and list entries of the base build
static std::vector<Int> s_gxPatchedCells;			// cells whose lists the last patch replaced
static std::vector<float> s_gxBaseX, s_gxBaseY, s_gxBaseRad;	// per slot at the base build: the store's x, y, radius
static std::vector<char> s_gxTouched;				// per base slot: partition data or bounding sphere may have changed since
static std::vector<Int> s_gxTouchedList;

/// snapPatch: per object ID, whether it has partition data and its bounding sphere radius, as last
/// read (0 = not known). Cleared by gxSnapTouch, the only ways either can change.
static std::vector<signed char> s_gxIdPd;
static std::vector<float> s_gxIdSphere;

// batchScan (gxBatchClosest, below): the seeker being answered and the walk's order table
static const Object *s_gxBatchMe = nullptr;
static Real s_gxBatchRange = 0.0f;
static std::vector<Int> s_gxRingWalk;	// per cell offset (dx + M, dy + M): its position in the walk, -1 never walked
static std::vector<Int> s_gxRingOfWalk;	// per walk position: its ring
static Int s_gxRingM = -1;

static void gxSnapReset()
{
	s_gxIdPd.clear();
	s_gxIdSphere.clear();
	s_gxBaseN = 0;
	s_gxSnapFrame = ~0u;
	s_gxRingM = -1;	// batchScan's walk table: the map's cell radius may differ
}

/// snapPatch: an object's partition registration or bounding sphere changes (registerObject,
/// unRegisterObject, Object::setGeometryInfoZ): the patch recomputes its box even if it did not move
void gxSnapTouch(const Object *o)
{
	const Int id = o ? (Int)o->getID() : 0;
	if (id > 0 && id < (Int)s_gxIdPd.size())
		s_gxIdPd[id] = 0;
	const Int s = o ? o->gxStoreSlot() : -1;
	if (s >= 0 && s < s_gxBaseN && !s_gxTouched[s])
	{
		s_gxTouched[s] = 1;
		s_gxTouchedList.push_back(s);
	}
}


/// one slot's cell box and snapshot radius, as the full build has always computed them
/// (cached: read the partition data and sphere through the per-ID cache)
static inline __attribute__((always_inline)) void gxSnapBox(const GxUnitStore &S, Int s, Int cellCountX, Int cellCountY, Int *box, Bool cached = FALSE)	// straight-line: inlined into the build/patch loops
{
	Object *o = S.obj[s];
	box[0] = -1;
	if (!o)
		return;
	Real sphere;
	const Int id = S.id[s];
	if (cached && id < (Int)s_gxIdPd.size() && s_gxIdPd[id])
	{
		if (s_gxIdPd[id] < 0)
			return;
		sphere = s_gxIdSphere[id];
	}
	else
	{
		const Bool pd = o->friend_getPartitionData() != nullptr;
		sphere = o->getGeometryInfo().getBoundingSphereRadius();
		if (cached)
		{
			if (id >= (Int)s_gxIdPd.size())
			{
				s_gxIdPd.resize(max<size_t>(id + 1, s_gxIdPd.size() * 2), 0);
				s_gxIdSphere.resize(s_gxIdPd.size(), 0.0f);
			}
			s_gxIdPd[id] = pd ? 1 : -1;
			s_gxIdSphere[id] = sphere;
		}
		if (!pd)
			return;
	}
	const Real r = S.radius[s];
	s_gxSnapR[s] = max(r, sphere);	// tall units: the sphere is up to ~3x the circle
	Int x0, y0, x1, y1;
	ThePartitionManager->worldToCell(S.x[s] - r, S.y[s] - r, &x0, &y0);
	ThePartitionManager->worldToCell(S.x[s] + r, S.y[s] + r, &x1, &y1);
	x0 = max(x0, 0); y0 = max(y0, 0); x1 = min(x1, cellCountX - 1); y1 = min(y1, cellCountY - 1);
	if (x0 > x1 || y0 > y1)
		return;
	box[0] = x0; box[1] = y0; box[2] = x1; box[3] = y1;
}

static void gxBuildStoreSnapshot(Int cellCountX, Int cellCountY)
{
	GxTimeScope gxT(GXT_SNAP);	// tickTiming
	gxCount(GXC_SNAP_BUILDS);
	GxUnitStore &S = TheGxUnitStore;
	S.pass(GXB_SCAN);	// compacts the store now if it is going to this frame, so slots stay put after
	const Int n = S.size(), cells = cellCountX * cellCountY;
	s_gxSnapStart.assign(cells + 1, 0);
	if ((Int)s_gxSnapStamp.size() < n)
		s_gxSnapStamp.resize(n, 0);
	s_gxSnapR.resize(n);
	std::vector<Int> &box = s_gxBaseBox;	// per slot: x0, y0, x1, y1 (x0 = -1: not in the partition)
	box.resize(4 * n);
	s_gxBaseId.resize(n);
	const Bool cachedR = (GLES3_Page_Switches4() & 0x80000000u) != 0;	// loop-invariant switch read hoisted
	for (Int s = 0; s < n; ++s)
	{
		s_gxBaseId[s] = S.id[s];
		gxSnapBox(S, s, cellCountX, cellCountY, &box[4 * s], cachedR);
		if (box[4 * s] < 0)
			continue;
		for (Int y = box[4 * s + 1]; y <= box[4 * s + 3]; ++y)
			for (Int x = box[4 * s]; x <= box[4 * s + 2]; ++x)
				++s_gxSnapStart[y * cellCountX + x + 1];
	}
	for (Int c = 0; c < cells; ++c)
		s_gxSnapStart[c + 1] += s_gxSnapStart[c];
	s_gxSnapSlots.resize(s_gxSnapStart[cells]);
	static std::vector<Int> fill;
	fill.assign(s_gxSnapStart.begin(), s_gxSnapStart.end() - 1);
	for (Int s = 0; s < n; ++s)
	{
		if (box[4 * s] < 0)
			continue;
		for (Int y = box[4 * s + 1]; y <= box[4 * s + 3]; ++y)
			for (Int x = box[4 * s]; x <= box[4 * s + 2]; ++x)
				s_gxSnapSlots[fill[y * cellCountX + x]++] = s;
	}
	s_gxSnapBeg.assign(s_gxSnapStart.begin(), s_gxSnapStart.end() - 1);
	s_gxSnapEnd.assign(s_gxSnapStart.begin() + 1, s_gxSnapStart.end());
	s_gxBaseX.assign(S.x.begin(), S.x.begin() + n);
	s_gxBaseY.assign(S.y.begin(), S.y.begin() + n);
	s_gxBaseRad.assign(S.radius.begin(), S.radius.begin() + n);
	s_gxTouched.assign(n, 0);
	s_gxTouchedList.clear();
	s_gxBaseN = n;
	s_gxBaseLen = (Int)s_gxSnapSlots.size();
	s_gxPatchedCells.clear();
	s_gxCurBox.assign(box.begin(), box.begin() + 4 * n);
	s_gxSnapEpoch = S.epoch();
}

/// switch snapPatch (fourth word bit 31; same game): later in the same frame, instead of the full
/// build, recompute every slot's box (as the full build would) and replace only the lists of cells
/// that a slot whose box changed since the base build (moved across a cell edge, new, removed or left
/// the partition) covered then or covers now. Each replaced list is what the full build would make:
/// the cell's slots in ascending order. Returns FALSE when a full build is needed instead.
static Bool gxPatchStoreSnapshot(Int cellCountX, Int cellCountY)
{
	GxTimeScope gxT(GXT_PATCH);	// tickTiming
	GxUnitStore &S = TheGxUnitStore;
	const Int n = S.size();
	if (n < s_gxBaseN)
		return FALSE;
	if ((Int)s_gxSnapStamp.size() < n)
		s_gxSnapStamp.resize(n, 0);
	s_gxSnapR.resize(n);
	static std::vector<Int> cur, changed;
	cur.resize(4 * n);
	changed.clear();
	const Int limit = n / 8 + 64;	// ponytail: past this many changed slots the full build is cheaper
	// a base slot whose id, x, y and radius are bit-equal to the base build's and that nobody touched
	// (registration, sphere) has the same box and radius: only the others are recomputed. The compare
	// runs 8 slots per step in two independent chains (LOGIC_AGENTS "SIMD step").
	for (Int s0 = 0; s0 < n; s0 += 8)
	{
		UnsignedInt diff = 0xffu;	// lane i: slot s0 + i needs its box recomputed
#ifdef __wasm_simd128__
		if (s0 + 8 <= s_gxBaseN)
		{
			#define GX_SAME4(b) ([&]() -> UnsignedInt { \
				const v128_t ex = wasm_i32x4_eq(wasm_v128_load(&S.x[s0 + b]), wasm_v128_load(&s_gxBaseX[s0 + b])); /* bit-equal, NaN too */ \
				const v128_t ey = wasm_i32x4_eq(wasm_v128_load(&S.y[s0 + b]), wasm_v128_load(&s_gxBaseY[s0 + b])); \
				const v128_t er = wasm_i32x4_eq(wasm_v128_load(&S.radius[s0 + b]), wasm_v128_load(&s_gxBaseRad[s0 + b])); \
				const v128_t ei = wasm_i32x4_eq(wasm_v128_load(&S.id[s0 + b]), wasm_v128_load(&s_gxBaseId[s0 + b])); \
				return (UnsignedInt)wasm_i32x4_bitmask(wasm_v128_not(wasm_v128_and(wasm_v128_and(ex, ey), wasm_v128_and(er, ei)))); }())
			const UnsignedInt a = GX_SAME4(0), b = GX_SAME4(4);
			#undef GX_SAME4
			diff = a | (b << 4);
		}
#endif
		const Int sEnd = min(s0 + 8, n);
		for (Int s = s0; s < sEnd; ++s)
		{
			if (s < s_gxBaseN && !((diff >> (s - s0)) & 1) && !s_gxTouched[s])
				continue;	// unchanged: box and radius as at the base build
			Int *c = &cur[4 * s];
			gxSnapBox(S, s, cellCountX, cellCountY, c, TRUE);
			if (s < s_gxBaseN)
			{
				if (S.id[s] != s_gxBaseId[s] && S.id[s] != 0)
					return FALSE;	// a slot holds another object (cannot happen within a frame; be safe)
				const Int *b = &s_gxBaseBox[4 * s];
				if (c[0] == b[0] && (c[0] < 0 || (c[1] == b[1] && c[2] == b[2] && c[3] == b[3])))
					continue;
			}
			else if (c[0] < 0)
				continue;
			changed.push_back(s);
			if ((Int)changed.size() > limit)
				return FALSE;
		}
	}
	gxCount(GXC_SNAP_PATCHES);
	// back to the base lists
	s_gxSnapSlots.resize(s_gxBaseLen);
	for (size_t i = 0; i < s_gxPatchedCells.size(); ++i)
	{
		const Int c = s_gxPatchedCells[i];
		s_gxSnapBeg[c] = s_gxSnapStart[c];
		s_gxSnapEnd[c] = s_gxSnapStart[c + 1];
	}
	s_gxPatchedCells.clear();
	// the cells to redo, each with the changed slots that cover it now (ascending, as changed is)
	static std::vector<Int> mark, adds;	// mark: per cell, 1 + index into s_gxPatchedCells
	static std::vector<std::vector<Int> > addLists;
	mark.resize(cellCountX * cellCountY, 0);
	for (int pass = 0; pass < 2; ++pass)
		for (size_t i = 0; i < changed.size(); ++i)
		{
			const Int s = changed[i];
			const Int *bx = pass == 0 ? (s < s_gxBaseN ? &s_gxBaseBox[4 * s] : nullptr) : &cur[4 * s];
			if (!bx || bx[0] < 0)
				continue;
			for (Int y = bx[1]; y <= bx[3]; ++y)
				for (Int x = bx[0]; x <= bx[2]; ++x)
				{
					const Int c = y * cellCountX + x;
					if (!mark[c])
					{
						s_gxPatchedCells.push_back(c);
						mark[c] = (Int)s_gxPatchedCells.size();
						if ((Int)addLists.size() < mark[c])
							addLists.resize(mark[c]);
						addLists[mark[c] - 1].clear();
					}
					if (pass == 1)
						addLists[mark[c] - 1].push_back(s);
				}
		}
	static std::vector<char> isChanged;
	isChanged.assign(n, 0);
	for (size_t i = 0; i < changed.size(); ++i)
		isChanged[changed[i]] = 1;
	for (size_t i = 0; i < s_gxPatchedCells.size(); ++i)
	{
		const Int c = s_gxPatchedCells[i];
		mark[c] = 0;
		const std::vector<Int> &add = addLists[i];
		// merge: the base list without changed slots, and the changed slots that cover the cell now
		const Int beg = (Int)s_gxSnapSlots.size();
		size_t j = 0;
		for (Int k = s_gxSnapStart[c]; k < s_gxSnapStart[c + 1]; ++k)
		{
			const Int s = s_gxSnapSlots[k];
			if (isChanged[s])
				continue;
			while (j < add.size() && add[j] < s)
				s_gxSnapSlots.push_back(add[j++]);
			s_gxSnapSlots.push_back(s);
		}
		while (j < add.size())
			s_gxSnapSlots.push_back(add[j++]);
		s_gxSnapBeg[c] = beg;
		s_gxSnapEnd[c] = (Int)s_gxSnapSlots.size();
	}
	s_gxCurBox.assign(s_gxBaseBox.begin(), s_gxBaseBox.begin() + 4 * s_gxBaseN);
	s_gxCurBox.resize(4 * n, -1);
	for (size_t i = 0; i < changed.size(); ++i)
		memcpy(&s_gxCurBox[4 * changed[i]], &cur[4 * changed[i]], 4 * sizeof(Int));
	s_gxSnapEpoch = S.epoch();
	return TRUE;
}

/// verify (snapPatch): the lists a full build would make now, compared cell by cell with the ones in use
static void gxCheckStoreSnapshot(Int cellCountX, Int cellCountY)
{
	const GxUnitStore &S = TheGxUnitStore;
	const Int n = S.size(), cells = cellCountX * cellCountY;
	std::vector<Int> start(cells + 1, 0), box(4 * n), slots;
	const std::vector<Real> inUseR = s_gxSnapR;	// the radii in use (gxSnapBox rewrites them uncached)
	for (Int s = 0; s < n; ++s)
	{
		gxSnapBox(S, s, cellCountX, cellCountY, &box[4 * s]);
		if (box[4 * s] >= 0 && memcmp(&s_gxSnapR[s], &inUseR[s], sizeof(Real)))
			printf("GXVERIFY snapPatch mismatch: slot %d radius %.9g, uncached %.9g\n", s, inUseR[s], s_gxSnapR[s]);
		if (box[4 * s] >= 0)
			for (Int y = box[4 * s + 1]; y <= box[4 * s + 3]; ++y)
				for (Int x = box[4 * s]; x <= box[4 * s + 2]; ++x)
					++start[y * cellCountX + x + 1];
	}
	for (Int c = 0; c < cells; ++c)
		start[c + 1] += start[c];
	slots.resize(start[cells]);
	std::vector<Int> fill(start.begin(), start.end() - 1);
	for (Int s = 0; s < n; ++s)
		if (box[4 * s] >= 0)
			for (Int y = box[4 * s + 1]; y <= box[4 * s + 3]; ++y)
				for (Int x = box[4 * s]; x <= box[4 * s + 2]; ++x)
					slots[fill[y * cellCountX + x]++] = s;
	static unsigned s_checked, s_bad;
	++s_checked;
	for (Int c = 0; c < cells; ++c)
	{
		const Int len = start[c + 1] - start[c];
		if (s_gxSnapEnd[c] - s_gxSnapBeg[c] != len || (len && memcmp(&slots[start[c]], &s_gxSnapSlots[s_gxSnapBeg[c]], len * sizeof(Int))))
		{
			++s_bad;
			printf("GXVERIFY snapPatch mismatch: cell %d has %d slots, full build %d\n", c, s_gxSnapEnd[c] - s_gxSnapBeg[c], len);
			break;
		}
	}
	s_gxSnapR = inUseR;
	if (s_checked % 500 == 0)
		printf("GXVERIFY snapPatch snapshots checked %u, mismatches %u\n", s_checked, s_bad);
}

static unsigned long long gxSearchKey(const Object *obj, const Coord3D *pos, Real maxDist, DistanceCalculationType dc,
	PartitionFilter **filters, Bool iter)
{
	unsigned long long h = 1469598103934665603ull;
	#define GXH(v) h = (h ^ (unsigned long long)(v)) * 1099511628211ull
	if (obj)
		GXH(obj->getID());
	else
	{
		UnsignedInt a, b; memcpy(&a, &pos->x, 4); memcpy(&b, &pos->y, 4); GXH(a); GXH(b); GXH(0x9e37u);
	}
	{ UnsignedInt d; memcpy(&d, &maxDist, 4); GXH(d); }
	GXH(dc); GXH(iter ? 1 : 2);
	for (PartitionFilter **f = filters; f && *f; ++f)
		GXH((uintptr_t)typeid(**f).name());
	#undef GXH
	return h;
}

#ifdef FASTER_GCO

// Web port: what a range search may skip (gxClosestImpl's gxFlags). Each is exact: it only
// turns away entries the plain walk would also turn away, before reading them.
enum
{
	GX_GCO_CELLS = 1,		// cells holding no player in the mask (switch enemySearchSkip)
	GX_GCO_ENTRIES = 2,		// entries of players not in the mask (switch searchSlotSkip)
	GX_GCO_DENSE = 4,		// read cell masks from the dense array (switch denseCellMask)
	GX_GCO_CACHE = 8,		// entries farther than the range by their cached place (switch searchCache)
	GX_GCO_BLOCKS = 16,		// read the 8x8 block's mask first (switch blockMask)
	GX_GCO_SNAP = 32		// read this tick's per-cell lists of store slots (match rule scanManager)
};

Int PartitionData::gxSlot() const
{
	return m_object ? m_object->gxStoreSlot() : -1;
}

void PartitionData::gxRefreshPlace()
{
	const Object *o = m_object;
	if (!o)
		return;
	const Coord3D *p = o->getPosition();
	const GeometryInfo &g = o->getGeometryInfo();
	const Real r = max(g.getBoundingCircleRadius(), g.getBoundingSphereRadius());
	const Int s = o->gxStoreSlot();
	if (s >= 0)
	{
		TheGxUnitStore.placeX[s] = p->x;
		TheGxUnitStore.placeY[s] = p->y;
		TheGxUnitStore.placeR[s] = r;
	}
	for (Int i = 0; i < m_coiInUseCount; ++i)
		m_coiArray[i].gxSetPlace(p->x, p->y, r);
}

UnsignedInt g_gxWalks = 0;	// walks started (gxClosestImpl), for batchScan's nesting check
Bool g_gxNoSearchVerify = false;	// set while a caller times its own searches (verify mode)
static UnsignedInt gx_gco_calls;	// verify mode: searches checked, and the time of both runs
static double gx_gco_fastMs, gx_gco_refMs;

Object *PartitionManager::getClosestObjects(
	const Object *obj,
	const Coord3D *pos,
	Real maxDist,
	DistanceCalculationType dc,
	PartitionFilter **filters,
	SimpleObjectIterator *iterArg,	// if nonnull, append ALL satisfactory objects to the iterator (not just the single closest)
	Real *closestDistArg,
	Coord3D *closestVecArg
)
{
	GxTimeScope gxT(GXT_SEARCH);	// tickTiming (Common/GxTime.h)
	gxCount(GXC_SEARCHES);
	UnsignedInt flags = 0;
	UnsignedInt mask = 0xffffffffu;
#ifdef __EMSCRIPTEN__
	const Int sw = GLES3_Page_Switches2();
	if (sw & 524288)
		flags |= GX_GCO_CACHE;
	mask = gxEnemySlots(filters);
	if (mask != 0xffffffffu)
	{
		flags |= GX_GCO_CELLS;
		if (sw & 65536)
			flags |= GX_GCO_ENTRIES;
		if (sw & 262144)
			flags |= GX_GCO_DENSE;
		if (sw & 16777216)
			flags |= GX_GCO_BLOCKS;
	}
	{
		extern bool gxMatchRule(unsigned int rule);
		if (gxMatchRule(262144 /* GXRULE_SCAN_MANAGER */) && TheGameLogic && m_cells)
			return gxManagedSearch(obj, pos, maxDist, dc, filters, iterArg, closestDistArg, closestVecArg, flags, mask);
	}
	// batchScan: the enemy scan gxBatchFindClosestEnemy is running (its own answer path; with scanManager, inside the manager)
	if (obj && obj == s_gxBatchMe && maxDist == s_gxBatchRange && !pos && !iterArg && dc == FROM_BOUNDINGSPHERE_2D && m_cells)
	{
		s_gxBatchMe = nullptr;	// one answer per seeker
		return gxBatchClosest(obj, maxDist, filters, flags, mask, FALSE, closestDistArg, closestVecArg);
	}
	if (!(GLES3_Page_Switches() & 2097152) || g_gxNoSearchVerify)
		return gxClosestImpl(obj, pos, maxDist, dc, filters, iterArg, closestDistArg, closestVecArg, flags, mask, nullptr);
#else
	return gxClosestImpl(obj, pos, maxDist, dc, filters, iterArg, closestDistArg, closestVecArg, flags, mask, nullptr);
#endif
#ifdef __EMSCRIPTEN__

	// Verify mode: the same search again with nothing skipped, the order of the two runs
	// alternating; the objects that passed (in order), the result, its distance and vector must
	// match. Both runs are timed (paired, so machine load hits both alike).
	static std::vector<Object*> traceFast, traceRef;
	traceFast.clear();
	traceRef.clear();
	SimpleObjectIterator *refIter = iterArg ? newInstance(SimpleObjectIterator) : nullptr;
	Real distFast = -1.0f, distRef = -1.0f;
	Coord3D vecFast, vecRef;
	vecFast.zero();
	vecRef.zero();
	Object *fast, *ref;
	const Bool fastFirst = (gx_gco_calls & 1) == 0;
	double t0 = emscripten_get_now();
	if (fastFirst)
		fast = gxClosestImpl(obj, pos, maxDist, dc, filters, iterArg, &distFast, &vecFast, flags, mask, &traceFast);
	double t1 = emscripten_get_now();
	ref = gxClosestImpl(obj, pos, maxDist, dc, filters, refIter, &distRef, &vecRef, 0, 0xffffffffu, &traceRef);
	double t2 = emscripten_get_now();
	if (!fastFirst)
		fast = gxClosestImpl(obj, pos, maxDist, dc, filters, iterArg, &distFast, &vecFast, flags, mask, &traceFast);
	double t3 = emscripten_get_now();
	gx_gco_fastMs += (t1 - t0) + (t3 - t2);
	gx_gco_refMs += t2 - t1;
	++gx_gco_calls;
	++gx_verify_checked;
	Bool same = fast == ref && traceFast == traceRef && memcmp(&distFast, &distRef, sizeof(Real)) == 0
		&& (!fast || memcmp(&vecFast, &vecRef, sizeof(Coord3D)) == 0);	// with no result the vector is unset
	if (refIter)
	{
		Real nf, nr;
		Object *a = iterArg->firstWithNumeric(&nf), *b = refIter->firstWithNumeric(&nr);
		for (; a && b && same; a = iterArg->nextWithNumeric(&nf), b = refIter->nextWithNumeric(&nr))
			same = a == b && memcmp(&nf, &nr, sizeof(Real)) == 0;
		if (a || b)
			same = false;
		iterArg->first();
		deleteInstance(refIter);
	}
	if (!same)
	{
		++gx_verify_mismatch;
		printf("GXVERIFY search mismatch: flags %u, %d/%d passed, result %u/%u\n", flags, (Int)traceFast.size(), (Int)traceRef.size(),
			fast ? fast->getID() : 0, ref ? ref->getID() : 0);
	}
	if (gx_gco_calls % 3000 == 0)
		printf("GXVERIFY searches checked %u, mismatches %d (all checks); fast %.1f ms, plain %.1f ms\n", gx_gco_calls, gx_verify_mismatch,
			gx_gco_fastMs, gx_gco_refMs);
	if (closestDistArg)
		*closestDistArg = distFast;
	if (closestVecArg)
		*closestVecArg = vecFast;
	return fast;
#endif
}

Object *PartitionManager::gxManagedSearch(const Object *obj, const Coord3D *pos, Real maxDist, DistanceCalculationType dc,
	PartitionFilter **filters, SimpleObjectIterator *iterArg, Real *closestDistArg, Coord3D *closestVecArg,
	UnsignedInt flags, UnsignedInt mask)
{
#ifdef __EMSCRIPTEN__
	const Bool verify = (GLES3_Page_Switches() & 2097152) != 0 && !g_gxNoSearchVerify;
	const double t0 = verify ? emscripten_get_now() : 0.0;
	const UnsignedInt frame = TheGameLogic->getFrame();
	// a new tick: forget last tick's answers
	if (frame != s_gxManagerFrame)
	{
		s_gxManagerFrame = frame;
		s_gxSearchCache.clear();
		s_gxSearchMap.clear();
		s_gxScanPassDone = false;
	}
	// this tick's per-cell lists (again if the store moved slots, e.g. a unit was removed)
	if (frame != s_gxSnapFrame || TheGxUnitStore.epoch() != s_gxSnapEpoch)
	{
		if (frame != s_gxSnapFrame || !((UnsignedInt)GLES3_Page_Switches4() & 0x80000000u) || !gxPatchStoreSnapshot(m_cellCountX, m_cellCountY))
			gxBuildStoreSnapshot(m_cellCountX, m_cellCountY);
		if (verify && frame == s_gxSnapFrame && ((UnsignedInt)GLES3_Page_Switches4() & 0x80000000u))
			gxCheckStoreSnapshot(m_cellCountX, m_cellCountY);
		s_gxSnapFrame = frame;
	}
	// the scan pass: every SCAN unit's plain enemy scan from last tick, in object-ID order
	if (!s_gxScanPassDone)
	{
		s_gxScanPassDone = true;
		const std::vector<int> scanners = TheGxUnitStore.pass(GXB_SCAN);	// a copy: the pass may add to the store
		extern bool gxMatchRule(unsigned int rule);
		const Bool gxBatchPass = gxMatchRule(67108864 /* GXRULE_BATCH_SCAN */);
		for (size_t i = 0; i < scanners.size(); ++i)
		{
			Object *o = TheGxUnitStore.obj[scanners[i]];
			if (!o || o->isEffectivelyDead())
				continue;
			std::unordered_map<Int, GxScanRequest>::iterator r = s_gxScanRequests.find(o->getID());
			if (r == s_gxScanRequests.end() || r->second.frame + 1 != frame)
				continue;
			++gx_mgr_pass;
			s_gxInScanPass = true;
			// batchScan (the AI's match rule GXRULE_BATCH_SCAN): the pass's walks take the batch path (same answers)
			s_gxBatchMe = gxBatchPass ? o : nullptr;
			s_gxBatchRange = r->second.range;
			TheAI->findClosestEnemy(o, r->second.range, r->second.qualifiers, nullptr, nullptr);
			s_gxBatchMe = nullptr;
			s_gxInScanPass = false;
		}
	}

	// the result cache
	Object *result = nullptr;
	Real dist = 0.0f;
	Coord3D vec;
	vec.zero();
	const unsigned long long key = gxSearchKey(obj, pos, maxDist, dc, filters, iterArg != nullptr);
	// a search from a point names no searcher, and its filters' state (whose enemies, which laser) is
	// not in the key: never cached. A unit's search is keyed by the unit, which its filters are built from.
	const Bool gxCacheable = obj != nullptr;
	const Bool gxTable = gxSearchTableOn();
	const GxCachedSearch *c = nullptr;
	if (gxCacheable && gxTable)
		c = s_gxSearchCache.find(key);
	else if (gxCacheable)
	{
		std::unordered_map<unsigned long long, GxCachedSearch>::iterator m = s_gxSearchMap.find(key);
		if (m != s_gxSearchMap.end())
			c = &m->second;
	}
	Bool hit = false;
	if (c)
	{
		const GxCachedSearch &cs = *c;
		if (iterArg)
		{
			// the list as the search left it (iterator order); inserting in reverse rebuilds that order
			for (size_t i = cs.list.size(); i-- > 0; )
			{
				Object *o = TheGameLogic->findObjectByID(cs.list[i].first);
				if (o && !o->isDestroyed())
					iterArg->insert(o, cs.list[i].second);
			}
			hit = true;
		}
		else
		{
			result = cs.closest ? TheGameLogic->findObjectByID(cs.closest) : nullptr;
			hit = !cs.closest || (result && !result->isDestroyed());	// a dead answer: search again
			dist = cs.dist;
			vec = cs.vec;
		}
	}
	if (hit)
		++gx_mgr_hits;
	else
	{
		++gx_mgr_misses;
		if (obj && obj == s_gxBatchMe && maxDist == s_gxBatchRange && !pos && !iterArg && dc == FROM_BOUNDINGSPHERE_2D)
		{
			s_gxBatchMe = nullptr;	// batchScan: one answer per seeker, from the tick's lists
			result = gxBatchClosest(obj, maxDist, filters, flags, mask, TRUE, &dist, &vec);
		}
		else
			result = gxClosestImpl(obj, pos, maxDist, dc, filters, iterArg, &dist, &vec, flags | GX_GCO_SNAP, mask, nullptr);
	}
	if (!hit && gxCacheable)
	{
		GxCachedSearch &cs = gxTable ? s_gxSearchCache.get(key) : s_gxSearchMap[key];
		cs.closest = result ? result->getID() : INVALID_ID;
		cs.dist = dist;
		cs.vec = vec;
		cs.list.clear();
		if (iterArg)
		{
			Real num;
			for (Object *o = iterArg->firstWithNumeric(&num); o; o = iterArg->nextWithNumeric(&num))
				cs.list.push_back(std::make_pair(o->getID(), num));
			iterArg->first();
		}
	}
	if (closestDistArg)
		*closestDistArg = dist;
	if (closestVecArg)
		*closestVecArg = vec;

	if (verify)
	{
		// verify mode: time against the plain walk and count answers that differ (the drift)
		const double t1 = emscripten_get_now();
		SimpleObjectIterator *refIter = iterArg ? newInstance(SimpleObjectIterator) : nullptr;
		Real refDist;
		Coord3D refVec;
		Object *ref = gxClosestImpl(obj, pos, maxDist, dc, filters, refIter, &refDist, &refVec, 0, 0xffffffffu, nullptr);
		gx_gco_fastMs += t1 - t0;
		gx_gco_refMs += emscripten_get_now() - t1;
		Bool same = ref == result;
		if (refIter)
		{
			Object *a = iterArg->first(), *b = refIter->first();
			for (; a && b && same; a = iterArg->next(), b = refIter->next())
				same = a == b;
			same = same && !a && !b;
			iterArg->first();
			deleteInstance(refIter);
		}
		++gx_gco_calls;
		if (!same)
			++gx_mgr_differ;
		if (gx_gco_calls % 3000 == 0)
			printf("GXVERIFY scan manager: searches %u, cache hits %u, misses %u, pass scans %u, answers differing from the plain walk %u; manager %.1f ms, plain %.1f ms; simd culled %llu\n",
				gx_gco_calls, gx_mgr_hits, gx_mgr_misses, gx_mgr_pass, gx_mgr_differ, gx_gco_fastMs, gx_gco_refMs, g_gxSimdCulled);
	}
	return result;
#else
	return gxClosestImpl(obj, pos, maxDist, dc, filters, iterArg, closestDistArg, closestVecArg, flags, mask, nullptr);
#endif
}

Object *PartitionManager::gxClosestImpl(const Object *obj, const Coord3D *pos, Real maxDist, DistanceCalculationType dc,
	PartitionFilter **filters, SimpleObjectIterator *iterArg, Real *closestDistArg, Coord3D *closestVecArg,
	UnsignedInt gxFlags, UnsignedInt gxEnemyMask, std::vector<Object*> *trace)
{
	DEBUG_ASSERTCRASH((obj==nullptr) != (pos == nullptr), ("either obj or pos must be null"));

	DistCalcProc distProc = theDistCalcProcs[dc];

	const Coord3D *objPos;
	const Object *objToUse;
	if (pos)
	{
		objPos = pos;
		objToUse = nullptr;
	}
	else
	{
		objPos = obj->getPosition();
		objToUse = obj;
	}
	Int cellCenterX, cellCenterY;
	worldToCell(objPos->x, objPos->y, &cellCenterX, &cellCenterY);

	Object* closestObj = nullptr;
	Real closestDistSqr = maxDist * maxDist;	// if it's not closer than this, we shouldn't consider it anyway...
	Coord3D closestVec;
#if !RETAIL_COMPATIBLE_CRC // TheSuperHackers @info This should be safe to initialize because it is unused, but let us be extra safe for now.
	closestVec.x = maxDist;
	closestVec.y = maxDist;
	closestVec.z = maxDist;
#endif

	Int maxRadius = m_maxGcoRadius;
	if (maxDist < HUGE_DIST)
	{
		// don't go outwards any farther than necessary.
		maxRadius = minInt(m_maxGcoRadius, worldToCellDist(maxDist));
	}
	Int maxRadiusLimit = maxRadius;

	Bool foundAny = false;

	static Int theIterFlag = 1;	// nonzero, thanks
	++theIterFlag;
	++g_gxWalks;	// batchScan: a walk run from inside another one's filters moves the done flags under it
	if (gxFlags & GX_GCO_SNAP)
		++s_gxSnapSearch;

	// Web port (switch searchCache): an entry whose cached centre is farther than the range plus
	// both radii plus one unit can't pass the distance test, whatever the distance type (every
	// type measures at least the 2D centre distance minus the two radii). The unit of slack makes
	// rounding irrelevant. gxLimit follows closestDistSqr.
	const Bool gxCache = (gxFlags & GX_GCO_CACHE) != 0 && maxDist < HUGE_DIST;
	const Bool gxBoundary = dc == FROM_BOUNDINGSPHERE_2D || dc == FROM_BOUNDINGSPHERE_3D;
	Real gxMyR = 0.0f;
	if (gxBoundary && objToUse)
		gxMyR = max(objToUse->getBoundingCircleRadius(), objToUse->getGeometryInfo().getBoundingSphereRadius());
	Real gxLimit = gxCache ? sqrtf(closestDistSqr) + gxMyR + 1.0f : 0.0f;
	const Real px = objPos->x, py = objPos->y;

	/*
		m_radiusVec[curRadius] contains a list of the cells (foo) that could
		contain objects that are <= (curRadius * cellSize) distance away from cell (0,0).
	*/
	for (Int curRadius = 0; curRadius <= maxRadiusLimit; ++curRadius)
	{
		const OffsetVec& offsets = m_radiusVec[curRadius];
		if (offsets.empty())
			continue;
		for (OffsetVec::const_iterator it = offsets.begin(); it != offsets.end(); ++it)
		{
			const Int gxX = cellCenterX + it->x, gxY = cellCenterY + it->y;
			if (gxX < 0 || gxY < 0 || gxX >= m_cellCountX || gxY >= m_cellCountY)
				continue;
			// switch blockMask: a block holding none of the players in the mask holds no such cell;
			// cells are still visited (or skipped) one by one in ring order
			if ((gxFlags & GX_GCO_BLOCKS) && !(g_gxBlockMasks[(gxY >> GX_BLOCK_SHIFT) * g_gxBlockMaskW + (gxX >> GX_BLOCK_SHIFT)] & gxEnemyMask))
			{
				if (trace && (g_gxCellMasks[gxY * m_cellCountX + gxX] & gxEnemyMask))
				{
					++gx_verify_mismatch;	// verify mode: the block said empty, the cell disagrees
					printf("GXVERIFY block mask mismatch at %d,%d\n", gxX, gxY);
				}
				continue;
			}
			if ((gxFlags & GX_GCO_DENSE) && !(g_gxCellMasks[gxY * m_cellCountX + gxX] & gxEnemyMask))
				continue;
			PartitionCell* thisCell = &m_cells[gxY * m_cellCountX + gxX];
			gxCount(GXC_SEARCH_CELLS);
			// Web port (switch enemySearchSkip): when the first filter keeps only objects of some
			// players' (gxEnemySlots), a cell holding nobody of those players can't add anything.
			// Same result and order: those objects would all be rejected by that filter, and the
			// done flags only matter for objects that pass.
			if ((gxFlags & GX_GCO_CELLS) && !(thisCell->gxPlayerMask() & gxEnemyMask))
				continue;

			// switch searchSlotSkip: walk only the lists of the players in the mask, merged back into the
			// cell list's order by sequence (high to low), so entries of other players are never read
			// (same reasoning as the cell skip). Otherwise the whole cell list.
			if (gxFlags & GX_GCO_SNAP)
			{
				// match rule scanManager: this tick's list of store slots for the cell, store columns first
				const GxUnitStore &S = TheGxUnitStore;
				const Int ci = gxY * m_cellCountX + gxX;
				const Int kBeg = s_gxSnapBeg[ci], kEnd = s_gxSnapEnd[ci];
				// switch simdSearch (fourth word bit 13): the distance cull 4 slots at a time from the live
				// x/y/radius columns. Same f32 ops as the scalar test below and the limit only shrinks, so
				// a lane it drops the scalar test would drop too; kept lanes run the unchanged body.
				const Bool gxSimd = gxCache && (GLES3_Page_Switches4() & 8192);
				// switch simdBody (fourth word bit 30): the body's enemy test and the 2D distance (centre or
				// bounding circle) for the same blocks of 8, from the player bits, positions and radii; the
				// scalar body then uses the lane's values (same f32 ops as distCalcProc_*_2D, no FMA)
				const Bool gxBody = gxSimd && (dc == FROM_CENTER_2D || dc == FROM_BOUNDINGSPHERE_2D) && (GLES3_Page_Switches4() & (1 << 30));
				UnsignedInt gxLanes = ~0u, gxEnemyLanes = ~0u, gxHaveD = 0;
				float gxD2[8], gxVx[8], gxVy[8];
				for (Int k = kBeg; k < kEnd; ++k)
				{
					const Int s = s_gxSnapSlots[k];
					gxCount(GXC_SEARCH_ENTRIES);
#ifdef __wasm_simd128__
					if (gxSimd && ((k - kBeg) & 7) == 0)
					{
						gxEnemyLanes = ~0u;
						gxHaveD = 0;
						// ILP (LOGIC_AGENTS "SIMD step"): 8 slots per block, two independent f32x4 chains at
						// the block's limit; a short tail (under 4) is all kept
						gxLanes = ~0u;
						const Int *q = &s_gxSnapSlots[k];
						const v128_t sx = wasm_f32x4_splat(px), sy = wasm_f32x4_splat(py), sl = wasm_f32x4_splat(gxLimit);
						#define GX_SNAP4(b) ([&]() -> UnsignedInt { \
							const v128_t vx = wasm_f32x4_make(S.x[q[b]], S.x[q[b + 1]], S.x[q[b + 2]], S.x[q[b + 3]]); \
							const v128_t vy = wasm_f32x4_make(S.y[q[b]], S.y[q[b + 1]], S.y[q[b + 2]], S.y[q[b + 3]]); \
							const v128_t vr = wasm_f32x4_make(s_gxSnapR[q[b]], s_gxSnapR[q[b + 1]], s_gxSnapR[q[b + 2]], s_gxSnapR[q[b + 3]]); \
							const v128_t dx = wasm_f32x4_sub(vx, sx), dy = wasm_f32x4_sub(vy, sy); \
							const v128_t lim = wasm_f32x4_add(sl, vr); \
							const v128_t d2 = wasm_f32x4_add(wasm_f32x4_mul(dx, dx), wasm_f32x4_mul(dy, dy)); \
							return (UnsignedInt)wasm_i32x4_bitmask(wasm_v128_not(wasm_f32x4_gt(d2, wasm_f32x4_mul(lim, lim)))); }())	/* keep unless "> lim^2", as the scalar test (NaN kept) */
						if (kEnd - k >= 8)
						{
							const UnsignedInt a = GX_SNAP4(0), b = GX_SNAP4(4);
							gxLanes = a | (b << 4) | ~0xffu;
						}
						else if (kEnd - k >= 4)
							gxLanes = GX_SNAP4(0) | ~0xfu;
						#undef GX_SNAP4
						const Int culled = 8 - __builtin_popcount(gxLanes & 0xffu);
						g_gxSimdCulled += culled;
						gxCount(GXC_SIMD_CULLED, culled);
						if (gxBody && kEnd - k >= 8)
						{
							// enemy lanes: the scalar test's bit is 1 << player, or bit MAX_PLAYER_COUNT for none
							const v128_t em = wasm_i32x4_splat((Int)gxEnemyMask), none = wasm_i32x4_splat(1 << MAX_PLAYER_COUNT), z = wasm_i32x4_splat(0);
							const Real ar = (dc == FROM_BOUNDINGSPHERE_2D && objToUse) ? objToUse->getBoundingCircleRadius() : 0.0f;
							const v128_t sar = wasm_f32x4_splat(ar), zf = wasm_f32x4_splat(0.0f), onef = wasm_f32x4_splat(1.0f);
							const Bool bnd = dc == FROM_BOUNDINGSPHERE_2D;
							#define GX_BODY4(b) ([&]() -> UnsignedInt { \
								const v128_t pb = wasm_u32x4_make(S.playerBit[q[b]], S.playerBit[q[b + 1]], S.playerBit[q[b + 2]], S.playerBit[q[b + 3]]); \
								const v128_t bit = wasm_v128_bitselect(none, pb, wasm_i32x4_eq(pb, z)); \
								const UnsignedInt en = (UnsignedInt)wasm_i32x4_bitmask(wasm_i32x4_ne(wasm_v128_and(bit, em), z)); \
								const Coord3D *p0 = S.obj[q[b]] ? S.obj[q[b]]->getPosition() : objPos, *p1 = S.obj[q[b + 1]] ? S.obj[q[b + 1]]->getPosition() : objPos; \
								const Coord3D *p2 = S.obj[q[b + 2]] ? S.obj[q[b + 2]]->getPosition() : objPos, *p3 = S.obj[q[b + 3]] ? S.obj[q[b + 3]]->getPosition() : objPos; \
								const v128_t dx = wasm_f32x4_sub(wasm_f32x4_make(p0->x, p1->x, p2->x, p3->x), wasm_f32x4_splat(objPos->x)); \
								const v128_t dy = wasm_f32x4_sub(wasm_f32x4_make(p0->y, p1->y, p2->y, p3->y), wasm_f32x4_splat(objPos->y)); \
								const v128_t d2 = wasm_f32x4_add(wasm_f32x4_mul(dx, dx), wasm_f32x4_mul(dy, dy)); \
								v128_t outD = d2, f = onef; \
								if (bnd) { \
									/* distCalcProc_BoundaryAndBoundary_2D: totalRad > 0 -> shrink by it, clamped at 0 */ \
									const v128_t tr = wasm_f32x4_add(sar, wasm_f32x4_make(S.radius[q[b]], S.radius[q[b + 1]], S.radius[q[b + 2]], S.radius[q[b + 3]])); \
									const v128_t ad = wasm_f32x4_sqrt(d2), sd = wasm_f32x4_sub(ad, tr); \
									const v128_t le = wasm_f32x4_le(sd, zf), pos = wasm_f32x4_gt(tr, zf); \
									const v128_t fs = wasm_v128_bitselect(zf, wasm_f32x4_div(sd, ad), le); \
									const v128_t ds = wasm_v128_bitselect(zf, wasm_f32x4_mul(sd, sd), le); \
									f = wasm_v128_bitselect(fs, onef, pos); outD = wasm_v128_bitselect(ds, d2, pos); } \
								wasm_v128_store(&gxD2[b], outD); \
								wasm_v128_store(&gxVx[b], wasm_f32x4_mul(dx, f)); wasm_v128_store(&gxVy[b], wasm_f32x4_mul(dy, f)); \
								return en; }())
							const UnsignedInt a = GX_BODY4(0), b = GX_BODY4(4);
							#undef GX_BODY4
							gxEnemyLanes = a | (b << 4) | ~0xffu;
							gxHaveD = 0xffu;
						}
					}
					if (!((gxLanes >> ((k - kBeg) & 7)) & 1))
					{
						if (GLES3_Page_Switches() & 2097152)
						{
							// verify: the scalar cull at the current limit must turn it away too
							static unsigned s_checked, s_bad;
							const Real dx = S.x[s] - px, dy = S.y[s] - py, lim = gxLimit + s_gxSnapR[s];
							++s_checked;
							if (!(dx * dx + dy * dy > lim * lim))
							{
								++s_bad;
								printf("GXVERIFY simdSearch mismatch: kept by the scalar test (d2 %g lim2 %g)\n", dx * dx + dy * dy, lim * lim);
							}
							if (s_checked % 200000 == 0)
								printf("GXVERIFY simdSearch dropped lanes checked %u, mismatches %u\n", s_checked, s_bad);
						}
						continue;
					}
#endif
					Object *thisObj = S.obj[s];
					if (!thisObj || thisObj == obj)
						continue;
					const Int gxL = (k - kBeg) & 7;
					const Int p = S.player[s];
					if (!((gxEnemyLanes >> gxL) & 1))
					{
						if ((GLES3_Page_Switches() & 2097152) && ((1u << ((p >= 0 && p < MAX_PLAYER_COUNT) ? p : MAX_PLAYER_COUNT)) & gxEnemyMask))
							printf("GXVERIFY simdBody mismatch: enemy lane dropped, player %d\n", p);
						continue;
					}
					if (!((1u << ((p >= 0 && p < MAX_PLAYER_COUNT) ? p : MAX_PLAYER_COUNT)) & gxEnemyMask))
						continue;
					if (gxCache)
					{
						// as the COI walk's cached-place test: the larger of the circle and the sphere (combat found
						// "twice the circle" too small for tall units and structures)
						const Real dx = S.x[s] - px, dy = S.y[s] - py, lim = gxLimit + s_gxSnapR[s];
						if (dx * dx + dy * dy > lim * lim)
							continue;
					}
					if (s_gxSnapStamp[s] == s_gxSnapSearch)
						continue;
					s_gxSnapStamp[s] = s_gxSnapSearch;
					if (!thisObj->friend_getPartitionData())
						continue;	// left the partition during the tick (e.g. entered a building)

					Real thisDistSqr;
					Coord3D distVec;
					gxCount(GXC_SEARCH_UNITS);
					if ((gxHaveD >> gxL) & 1)
					{
						thisDistSqr = gxD2[gxL];
						distVec.x = gxVx[gxL]; distVec.y = gxVy[gxL]; distVec.z = 0.0f;
						if (GLES3_Page_Switches() & 2097152)
						{
							static unsigned s_checked, s_bad;
							Real rd; Coord3D rv;
							(*distProc)(objPos, objToUse, thisObj->getPosition(), thisObj, rd, rv, closestDistSqr);
							++s_checked;
							if (memcmp(&rd, &thisDistSqr, 4) || memcmp(&rv.x, &distVec.x, 4) || memcmp(&rv.y, &distVec.y, 4) || memcmp(&rv.z, &distVec.z, 4))
							{
								++s_bad;
								printf("GXVERIFY simdBody mismatch: dc %d d2 %.9g/%.9g vec %.9g,%.9g/%.9g,%.9g\n", (int)dc, rd, thisDistSqr, rv.x, rv.y, distVec.x, distVec.y);
							}
							if (s_checked % 200000 == 0)
								printf("GXVERIFY simdBody distances checked %u, mismatches %u\n", s_checked, s_bad);
						}
						if (!(thisDistSqr < closestDistSqr))
							continue;
					}
					else if (!(*distProc)(objPos, objToUse, thisObj->getPosition(), thisObj, thisDistSqr, distVec, closestDistSqr))
						continue;
					if (!filtersAllow(filters, thisObj))
						continue;
					if (trace)
						trace->push_back(thisObj);
					if (iterArg)
						iterArg->insert(thisObj, thisDistSqr);
					else
					{
						closestObj = thisObj;
						closestDistSqr = thisDistSqr;
						closestVec = distVec;
						if (gxCache)
							gxLimit = sqrtf(closestDistSqr) + gxMyR + 1.0f;
						if (!foundAny)
							maxRadiusLimit = curRadius;
						foundAny = true;
					}
				}
				continue;
			}

			CellAndObjectIntersection *gxHeads[MAX_PLAYER_COUNT + 1];
			Int gxHeadCount = 0;
			const Bool gxSlots = (gxFlags & GX_GCO_ENTRIES) != 0;
			if (gxSlots)
			{
				for (UnsignedInt m = thisCell->gxPlayerMask() & gxEnemyMask; m; m &= m - 1)
				{
					CellAndObjectIntersection *h = thisCell->gxSlotHead(__builtin_ctz(m));
					if (h)
						gxHeads[gxHeadCount++] = h;
				}
			}
			CellAndObjectIntersection *thisCoi = nullptr;
			for (Bool first = true; ; first = false)
			{
				if (gxSlots)
				{
					if (gxHeadCount == 0)
						break;
					Int best = 0;
					for (Int i = 1; i < gxHeadCount; ++i)
						if (gxHeads[i]->gxSeq() > gxHeads[best]->gxSeq())
							best = i;
					thisCoi = gxHeads[best];
					gxHeads[best] = thisCoi->gxSlotNext();
					if (!gxHeads[best])
						gxHeads[best] = gxHeads[--gxHeadCount];
				}
				else
				{
					thisCoi = first ? thisCell->getFirstCoiInCell() : thisCoi->getNextCoi();
					if (!thisCoi)
						break;
				}
				gxCount(GXC_SEARCH_ENTRIES);
				if (gxCache)
				{
					const Real dx = thisCoi->gxX() - px, dy = thisCoi->gxY() - py, lim = gxLimit + thisCoi->gxR();
					if (dx * dx + dy * dy > lim * lim)
						continue;	// every COI of this object says the same: no done flag needed
				}

				PartitionData *thisMod = thisCoi->getModule();
				Object *thisObj = thisMod->getObject();

				// never compare against ourself.
				if (thisObj == obj || thisObj == nullptr)
					continue;

				// since an object can exist in multiple COIs, we use this to avoid processing
				// the same one more than once.
				if (thisMod->friend_getDoneFlag() == theIterFlag)
					continue;
				thisMod->friend_setDoneFlag(theIterFlag);

				Real thisDistSqr;
				Coord3D distVec;
				gxCount(GXC_SEARCH_UNITS);
				if (!(*distProc)(objPos, objToUse, thisObj->getPosition(), thisObj, thisDistSqr, distVec, closestDistSqr))
					continue;

				if (!filtersAllow(filters, thisObj))
					continue;

				if (trace)
					trace->push_back(thisObj);

				// ok, this is within the range, and the filters allow it.
				// add it to the iter, if we have one....
				if (iterArg)
				{
					iterArg->insert(thisObj, thisDistSqr);
				}
				else
				{
					// hey, this is the new closest object! cool.
					// (note that we can't break out now 'cuz we have to finish examining the
					// rest of curRadius)
					closestObj = thisObj;
					closestDistSqr = thisDistSqr;
					closestVec = distVec;
					if (gxCache)
						gxLimit = sqrtf(closestDistSqr) + gxMyR + 1.0f;

					if (!foundAny)
					{
						// if not adding to iterArg, we want to stop once we have the closest object.
						maxRadiusLimit = curRadius;
					}
					foundAny = true;
				}
			}
		}
	}

	if (closestVecArg)
	{
		*closestVecArg = closestVec;
	}
	if (closestDistArg)
	{
		*closestDistArg = (Real)sqrtf(closestDistSqr);
	}
	return closestObj;	// might be null...
}

#else
#error "web port: getClosestObjects is only written for FASTER_GCO"
#endif // FASTER_GCO



// ---------------------------------------------------------------------------------------------------
// Web port (batchScan, fifth word bit 1, the AI's switch): the AI hands its due mood scans over as
// arrays. Each runs through AI::findClosestEnemy as before (same filters, same early outs); its
// getClosestObject comes here instead of the cell walk:
// - one f32x4 pass over the store's contiguous place columns (live position, max(circle, sphere)
//   radius) and player bits keeps the slots that could be enemies within range (two chains, 8 a step);
// - the survivors get the walk's own tests (distProc, filters), and the walk's answer is rebuilt:
//   the walk stops after the ring of the first object passing, then keeps the nearest one seen
//   (strictly nearer replaces; ties go to the earlier visit). So: among the passing objects in the
//   lowest ring any of them is in (by the cells of their COIs), the nearest; an exact distance tie
//   between two objects first seen in the same cell runs the old walk instead.
// With scanManager on the old path is the manager's (tick lists, cache): batchScan stays out of it.
static std::vector<Int> s_gxBatchSurv;
static unsigned s_gxBatchChecked, s_gxBatchBad, s_gxBatchFallbacks, s_gxBatchNested;

Object *PartitionManager::gxBatchClosest(const Object *obj, Real maxDist, PartitionFilter **filters, UnsignedInt gxFlags, UnsignedInt gxMask,
	Bool snap, Real *distOut, Coord3D *vecOut)
{
	const Int M = m_maxGcoRadius, W = 2 * M + 1;
	if (s_gxRingM != M)
	{
		s_gxRingM = M;
		s_gxRingWalk.assign((size_t)W * W, -1);
		s_gxRingOfWalk.clear();
		for (Int r = 0; r <= M && r < (Int)m_radiusVec.size(); ++r)
			for (OffsetVec::const_iterator it = m_radiusVec[r].begin(); it != m_radiusVec[r].end(); ++it)
				if (abs(it->x) <= M && abs(it->y) <= M && s_gxRingWalk[(it->y + M) * W + it->x + M] < 0)
				{
					s_gxRingWalk[(it->y + M) * W + it->x + M] = (Int)s_gxRingOfWalk.size();
					s_gxRingOfWalk.push_back(r);
				}
	}
	const GxUnitStore &S = TheGxUnitStore;
	const Coord3D *objPos = obj->getPosition();
	const Real px = objPos->x, py = objPos->y;
	const Real myR = max(obj->getBoundingCircleRadius(), obj->getGeometryInfo().getBoundingSphereRadius());
	const Real lim0 = sqrtf(maxDist * maxDist) + myR + 1.0f;	// the walk's first gxLimit: 2D centre distance minus both radii, a unit of slack

	// the cull: every store slot, f32x4, two chains, 8 a step: enemy slots (player bit in the mask; none =
	// bit MAX_PLAYER_COUNT) with a place (placeR < 1e5) whose centre is within lim0 + their radius.
	// ponytail: one pass per scan (battle: ~5 us a scan, 0.08 ms/tick). A per-batch x-sorted list cost
	// ~190 us to build and its window cull was no cheaper, so it never paid off; revisit past ~50 scans a batch.
	const UnsignedInt none = 1u << MAX_PLAYER_COUNT;
	Int nSurv = 0;	// straight-line: survivors written straight into a buffer sized up front (no push_back call).
	// ponytail: an all-lanes branchless write measured slower (survivors are rare: 0.067 vs 0.059 ms/tick)
	{
		GxTimeScope gxT(GXT_BATCH_CULL);
		const Int n = S.size();
		if ((Int)s_gxBatchSurv.size() < n + 8)
			s_gxBatchSurv.resize(n + 8);
		Int *__restrict surv = s_gxBatchSurv.data();
		Int s = 0;
#ifdef __wasm_simd128__
		const v128_t sx = wasm_f32x4_splat(px), sy = wasm_f32x4_splat(py), sl = wasm_f32x4_splat(lim0), big = wasm_f32x4_splat(100000.0f);
		const v128_t em = wasm_i32x4_splat((Int)gxMask), vn = wasm_i32x4_splat((Int)none), z = wasm_i32x4_splat(0);
		#define GX_CULL4(b) ([&]() -> UnsignedInt { \
			const v128_t vr = wasm_v128_load(&S.placeR[s + b]), pb = wasm_v128_load(&S.playerBit[s + b]); \
			const v128_t en = wasm_i32x4_ne(wasm_v128_and(wasm_v128_bitselect(vn, pb, wasm_i32x4_eq(pb, z)), em), z); \
			const v128_t dx = wasm_f32x4_sub(wasm_v128_load(&S.placeX[s + b]), sx), dy = wasm_f32x4_sub(wasm_v128_load(&S.placeY[s + b]), sy); \
			const v128_t lim = wasm_f32x4_add(sl, vr), d2 = wasm_f32x4_add(wasm_f32x4_mul(dx, dx), wasm_f32x4_mul(dy, dy)); \
			return (UnsignedInt)wasm_i32x4_bitmask(wasm_v128_and(wasm_v128_and(en, wasm_f32x4_lt(vr, big)), wasm_v128_not(wasm_f32x4_gt(d2, wasm_f32x4_mul(lim, lim))))); }())
		for (; s + 8 <= n; s += 8)
		{
			for (UnsignedInt m = GX_CULL4(0) | (GX_CULL4(4) << 4); m; m &= m - 1)
				surv[nSurv++] = s + __builtin_ctz(m);
		}
		#undef GX_CULL4
#endif
		for (; s < n; ++s)
		{
			const UnsignedInt pb = S.playerBit[s];
			const Real dx = S.placeX[s] - px, dy = S.placeY[s] - py, lim = lim0 + S.placeR[s];
			surv[nSurv] = s;
			nSurv += (((pb ? pb : none) & gxMask) != 0) & (S.placeR[s] < 100000.0f) & !(dx * dx + dy * dy > lim * lim);
		}
	}
	gxCount(GXC_BATCH_SEEKERS);

	// the walk's answer from the survivors
	DistCalcProc distProc = theDistCalcProcs[FROM_BOUNDINGSPHERE_2D];
	Int cx, cy;
	worldToCell(px, py, &cx, &cy);
	const Int maxRadius = maxDist < HUGE_DIST ? minInt(M, worldToCellDist(maxDist)) : M;
	const Real maxDistSqr = maxDist * maxDist;
	Object *best = nullptr;
	Real bestD = 0.0f;
	Int bestRing = 0x7fffffff, bestW = 0x7fffffff, bestSlot = 0x7fffffff;
	Bool tie = false;
	const UnsignedInt walksAtStart = g_gxWalks;
	Coord3D bestVec;
	bestVec.x = bestVec.y = bestVec.z = maxDist;	// as the walk's unset vector
	for (Int i = 0; i < nSurv; ++i)
	{
		const Int sl0 = s_gxBatchSurv[i];
		Object *t = S.obj[sl0];
		if (!t || t == obj)
			continue;
		PartitionData *pd = t->friend_getPartitionData();
		if (!pd)
			continue;
		Int w = 0x7fffffff;	// first visit: the lowest walk position of its cells
		/* the walk's cell skips too (cells whose partition masks hold none of the mask's players) */
		#define GX_CELL_W(cellX, cellY) { const Int dx = (cellX) - cx, dy = (cellY) - cy; \
			if (abs(dx) <= M && abs(dy) <= M) { const Int cw = s_gxRingWalk[(dy + M) * W + dx + M]; \
				const Int ci = (cellY) * m_cellCountX + (cellX); \
				if (cw >= 0 && cw < w && s_gxRingOfWalk[cw] <= maxRadius \
					&& !((gxFlags & GX_GCO_DENSE) && !(g_gxCellMasks[ci] & gxMask)) \
					&& !((gxFlags & GX_GCO_CELLS) && !(m_cells[ci].gxPlayerMask() & gxMask))) w = cw; } }
		if (snap)
		{
			// the tick's lists: the slot's box as they hold it (same cell: lower slot first)
			const Int sl = s_gxBatchSurv[i];
			const Int *b = 4 * sl < (Int)s_gxCurBox.size() ? &s_gxCurBox[4 * sl] : nullptr;
			if (b && b[0] >= 0)
				for (Int y = b[1]; y <= b[3]; ++y)
					for (Int x = b[0]; x <= b[2]; ++x)
						GX_CELL_W(x, y)
		}
		else
			for (Int c = 0, nc = pd->friend_getCoiInUseCount(); c < nc; ++c)
			{
				const PartitionCell *cell = pd->gxCoiCell(c);
				GX_CELL_W(cell->getCellX(), cell->getCellY())
			}
		#undef GX_CELL_W
		if (w == 0x7fffffff)
			continue;	// in no cell the walk reaches
		const Int ring = s_gxRingOfWalk[w];
		if (ring > bestRing)
			continue;	// the walk stops before its ring
		if (snap)
		{
			// the tick lists' walk tests the player column (not the player bit)
			const Int p = S.player[sl0];
			if (!((1u << ((p >= 0 && p < MAX_PLAYER_COUNT) ? p : MAX_PLAYER_COUNT)) & gxMask))
				continue;
		}
		if (snap && (gxFlags & GX_GCO_CACHE) && maxDist < HUGE_DIST)
		{
			// the tick lists' walk culls by the store's x, y and the snapshot radius (not the place):
			// what it turns away at its first limit it never sees
			const Real dx = S.x[sl0] - px, dy = S.y[sl0] - py, lim = lim0 + s_gxSnapR[sl0];
			if (dx * dx + dy * dy > lim * lim)
				continue;
		}
		Real d;
		Coord3D v;
		if (!(*distProc)(objPos, obj, t->getPosition(), t, d, v, maxDistSqr))
			continue;
		if (!filtersAllow(filters, t))
			continue;
		const Int sl = s_gxBatchSurv[i];
		if (ring < bestRing || d < bestD || (d == bestD && (w < bestW || (w == bestW && snap && sl < bestSlot))))
		{
			best = t; bestD = d; bestRing = ring; bestW = w; bestSlot = sl; tie = false;
			bestVec = v;
		}
		else if (d == bestD && w == bestW && !snap)
			tie = true;	// both first seen in one cell: the cell's list order decides (the tick's lists: slot order, as here)
	}
	// a filter ran a search: inside the walk that search would have moved the done flags (stamps)
	// under it, so the walk may skip objects it already flagged. Only the walk itself gives that answer.
	if (g_gxWalks != walksAtStart)
	{
		tie = true;
		++s_gxBatchNested;
	}
	Object *ref = nullptr;
	const Bool verify = (GLES3_Page_Switches() & 2097152) != 0;
	Real refD = 0.0f;
	Coord3D refV;
	refV.zero();
	if (tie || verify)
		ref = gxClosestImpl(obj, nullptr, maxDist, FROM_BOUNDINGSPHERE_2D, filters, nullptr, &refD, &refV, gxFlags | (snap ? GX_GCO_SNAP : 0), gxMask, nullptr);
	if (distOut)
		*distOut = (Real)sqrtf(best ? bestD : maxDistSqr);	// as the walk: from closestDistSqr
	if (vecOut)
		*vecOut = bestVec;
	if (tie)
	{
		++s_gxBatchFallbacks;
		best = ref;
		if (distOut)
			*distOut = refD;
		if (vecOut)
			*vecOut = refV;
	}
	if (verify)
	{
		++s_gxBatchChecked;
		if (best != ref || (best && distOut && memcmp(distOut, &refD, sizeof(Real))))
		{
			++s_gxBatchBad;
			printf("GXVERIFY batchScan mismatch: seeker %u got %u, old search %u (survivors %d, lists %d)\n", obj->getID(),
				best ? best->getID() : 0, ref ? ref->getID() : 0, nSurv, (Int)snap);
			const Int bs = best ? best->gxStoreSlot() : -1;
			if (bs >= 0)
				printf("GXVERIFY batchScan mismatch detail: d2 %.9g max %.9g ring %d w %d; store %.9g,%.9g place %.9g,%.9g pos %.9g,%.9g; snapR %.9g placeR %.9g circle %.9g; box %d,%d,%d,%d\n",
					bestD, maxDistSqr, bestRing, bestW, S.x[bs], S.y[bs], S.placeX[bs], S.placeY[bs], best->getPosition()->x, best->getPosition()->y,
					bs < (Int)s_gxSnapR.size() ? s_gxSnapR[bs] : -1.0f, S.placeR[bs], best->getBoundingCircleRadius(),
					4 * bs < (Int)s_gxCurBox.size() ? s_gxCurBox[4 * bs] : -9, 4 * bs < (Int)s_gxCurBox.size() ? s_gxCurBox[4 * bs + 1] : -9,
					4 * bs < (Int)s_gxCurBox.size() ? s_gxCurBox[4 * bs + 2] : -9, 4 * bs < (Int)s_gxCurBox.size() ? s_gxCurBox[4 * bs + 3] : -9);
			if (bs >= 0 && snap && 4 * bs < (Int)s_gxCurBox.size() && s_gxCurBox[4 * bs] >= 0)
			{
				// the walk's own tests for this slot, cell by cell of its box
				const Int *b = &s_gxCurBox[4 * bs];
				for (Int y = b[1]; y <= b[3]; ++y)
					for (Int x = b[0]; x <= b[2]; ++x)
					{
						const Int ci = y * m_cellCountX + x;
						Int inList = 0;
						for (Int k = s_gxSnapBeg[ci]; k < s_gxSnapEnd[ci]; ++k)
							inList += s_gxSnapSlots[k] == bs;
						printf("GXVERIFY batchScan mismatch cell %d,%d: in list %d, cell mask %x dense %x, flags %x, mask %x\n", x, y, inList,
							m_cells[ci].gxPlayerMask(), g_gxCellMasks ? g_gxCellMasks[ci] : 0u, gxFlags, gxMask);
					}
				for (PartitionFilter **f = filters; f && *f; ++f)
					printf("GXVERIFY batchScan mismatch filter %s: allow now %d\n", typeid(**f).name(), (Int)(*f)->allow(best));
				printf("GXVERIFY batchScan mismatch slot: player %d playerBit %x stamp %u/%u pd %d snapEpoch %u/%u frame %u/%u\n", S.player[bs], S.playerBit[bs],
					bs < (Int)s_gxSnapStamp.size() ? s_gxSnapStamp[bs] : 0u, s_gxSnapSearch, best->friend_getPartitionData() != nullptr,
					s_gxSnapEpoch, TheGxUnitStore.epoch(), s_gxSnapFrame, TheGameLogic->getFrame());
			}
		}
		if (s_gxBatchChecked % 2000 == 0)
			printf("GXVERIFY batchScan checked %u, mismatches %u, tie fallbacks %u (nested searches %u)\n", s_gxBatchChecked, s_gxBatchBad, s_gxBatchFallbacks, s_gxBatchNested);
	}
	return best;
}

void gxBatchFindClosestEnemy(Int n, Object *const *me, const Real *range, const UnsignedInt *qualifiers,
	const AttackPriorityInfo *const *info, Object **out)
{
	for (Int i = 0; i < n; ++i)
	{
		s_gxBatchMe = me[i];
		s_gxBatchRange = range[i];
		out[i] = TheAI->findClosestEnemy(me[i], range[i], qualifiers[i], info[i], nullptr);
		s_gxBatchMe = nullptr;	// unused when the scan stopped early or a cached answer served it
	}
}

//-----------------------------------------------------------------------------
Object *PartitionManager::getClosestObject(
	const Object *obj,
	Real maxDist,
	DistanceCalculationType dc,
	PartitionFilter **filters,
	Real *closestDist,
	Coord3D *closestDistVec
)
{
	return getClosestObjects(obj, nullptr, maxDist, dc, filters, nullptr, closestDist, closestDistVec);
}

//-----------------------------------------------------------------------------
Object *PartitionManager::getClosestObject(
	const Coord3D *pos,
	Real maxDist,
	DistanceCalculationType dc,
	PartitionFilter **filters,
	Real *closestDist,
	Coord3D *closestDistVec
)
{
	return getClosestObjects(nullptr, pos, maxDist, dc, filters, nullptr, closestDist, closestDistVec);
}

//-----------------------------------------------------------------------------
void PartitionManager::getVectorTo(const Object *obj, const Object *otherObj, DistanceCalculationType dc, Coord3D& vec)
{
	DistCalcProc distProc = theDistCalcProcs[dc];
	Real distSqr;
	(*distProc)(obj->getPosition(), obj, otherObj->getPosition(), otherObj, distSqr, vec, HUGE_DIST_SQR);
}

//-----------------------------------------------------------------------------
void PartitionManager::getVectorTo(const Object *obj, const Coord3D *pos, DistanceCalculationType dc, Coord3D& vec)
{
	DistCalcProc distProc = theDistCalcProcs[dc];
	Real distSqr;
	(*distProc)(obj->getPosition(), obj, pos, nullptr, distSqr, vec, HUGE_DIST_SQR);
}

//-----------------------------------------------------------------------------
Real PartitionManager::getDistanceSquared(const Object *obj, const Object *otherObj, DistanceCalculationType dc, Coord3D *vec)
{
	DistCalcProc distProc = theDistCalcProcs[dc];
	Real thisDistSqr;
	Coord3D thisVec;
	(*distProc)(obj->getPosition(), obj, otherObj->getPosition(), otherObj, thisDistSqr, thisVec, HUGE_DIST_SQR);
	if (vec)
		*vec = thisVec;
	return thisDistSqr;
}

//-----------------------------------------------------------------------------
Real PartitionManager::getDistanceSquared(const Object *obj, const Coord3D *pos, DistanceCalculationType dc, Coord3D *vec)
{
	DistCalcProc distProc = theDistCalcProcs[dc];
	Real thisDistSqr;
	Coord3D thisVec;
	(*distProc)(obj->getPosition(), obj, pos, nullptr, thisDistSqr, thisVec, HUGE_DIST_SQR);
	if (vec)
		*vec = thisVec;
	return thisDistSqr;
}

//-----------------------------------------------------------------------------
// Gets the distance if obj were at goalPos.  Used to calculate attack position paths.
Real PartitionManager::getGoalDistanceSquared(const Object *obj, const Coord3D *goalPos, const Object *otherObj, DistanceCalculationType dc, Coord3D *vec)
{
	DistCalcProc distProc = theDistCalcProcs[dc];
	Real thisDistSqr;
	Coord3D thisVec;
	(*distProc)(goalPos, obj, otherObj->getPosition(), otherObj, thisDistSqr, thisVec, HUGE_DIST_SQR);
	if (vec)
		*vec = thisVec;
	return thisDistSqr;
}

//-----------------------------------------------------------------------------
// Gets the distance if obj were at goalPos.  Used to calculate attack position paths.
Real PartitionManager::getGoalDistanceSquared(const Object *obj, const Coord3D *goalPos, const Coord3D *otherPos, DistanceCalculationType dc, Coord3D *vec)
{
	DistCalcProc distProc = theDistCalcProcs[dc];
	Real thisDistSqr;
	Coord3D thisVec;
	(*distProc)(goalPos, obj, otherPos, nullptr, thisDistSqr, thisVec, HUGE_DIST_SQR);
	if (vec)
		*vec = thisVec;
	return thisDistSqr;
}

//-----------------------------------------------------------------------------
Real PartitionManager::getRelativeAngle2D( const Object *obj, const Object *otherObj )
{
	return getRelativeAngle2D(obj, otherObj->getPosition());
}

//-----------------------------------------------------------------------------
Real PartitionManager::getRelativeAngle2D( const Object *obj, const Coord3D *pos )
{
	Coord3D v;

	// compute vector to given position
	Coord3D objPos = *obj->getPosition();
	v.x = pos->x - objPos.x;
	v.y = pos->y - objPos.y;
	v.z = 0.0f;

	Real dist = (Real)sqrtf(sqr(v.x) + sqr(v.y));

	// normalize
	if (dist == 0.0f)
		return 0.0f;


	const Coord3D *dir = obj->getUnitDirectionVector2D();

	Real distInv = 1.0f / dist;
	v.x *= distInv;
	v.y *= distInv;
	v.z *= distInv;

	// dot of two unit vectors is cos of angle
	Real c = dir->x*v.x + dir->y*v.y; // + dir->z*v.z;

	// bound it in case of numerical error
	if (c < -1.0)
		c = -1.0;
	else if (c > 1.0)
		c = 1.0;

	Real value = (Real)ACos( c );

	// Determine sign by checking Z component of dir cross v
	// Note this is assumes 2D, and is identical to dotting the perpendicular of v with dir
	Real perpZ = dir->x * v.y - dir->y * v.x;
	if (perpZ < 0.0f)
		value = -value;

	// note: to make this 3D, 'dir' and 'v' can be normalized and dotted just as they are
	// to test sign, compute N = dir X v, then P = N x dir, then S = P . v, where sign of
	// S is sign of angle - MSB

	return value;
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateObjectsInRange(
	const Object *obj,
	Real maxDist,
	DistanceCalculationType dc,
	PartitionFilter **filters,
	IterOrderType order
)
{
	GxTimeScope gxT(GXT_SEARCH);	// tickTiming: the iterator and its entries count as search allocations
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	getClosestObjects(obj, nullptr, maxDist, dc, filters, iter, nullptr, nullptr);

	iter->sort(order);
	iterHolder.release();
	return iter;
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateObjectsInRange(
	const Coord3D *pos,
	Real maxDist,
	DistanceCalculationType dc,
	PartitionFilter **filters,
	IterOrderType order
)
{
	GxTimeScope gxT(GXT_SEARCH);	// tickTiming: the iterator and its entries count as search allocations
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	getClosestObjects(nullptr, pos, maxDist, dc, filters, iter, nullptr, nullptr);

	iter->sort(order);
	iterHolder.release();
	return iter;
}

//-----------------------------------------------------------------------------
SimpleObjectIterator* PartitionManager::iteratePotentialCollisions(
	const Coord3D* pos,
	const GeometryInfo& geom,
	Real angle,
	Bool use2D
)
{
	Real maxDist = geom.getBoundingSphereRadius();
	maxDist *= 1.1f;	// just a little slop

	GxTimeScope gxT(GXT_SEARCH);	// tickTiming: the iterator and its entries count as search allocations
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	PartitionFilterWouldCollide filter(*pos, geom, angle, true);
	PartitionFilter *filters[] = { &filter, nullptr };

	getClosestObjects(nullptr, pos, maxDist, use2D ? FROM_BOUNDINGSPHERE_2D : FROM_BOUNDINGSPHERE_3D, filters, iter, nullptr, nullptr);

	iterHolder.release();
	return iter;
}

//-----------------------------------------------------------------------------
Bool PartitionManager::isColliding( const Object *a, const Object *b ) const
{
	//Make sure we have objects
  if( !a || !b )
	{
    return false;
	}

	//Get both object's partition data
  const PartitionData* ad = a->friend_getConstPartitionData();
  const PartitionData* bd = b->friend_getConstPartitionData();

	//Make sure we got it
  if( !ad || !bd )
	{
    return false;
	}

	//See if the partition data collides.
	return ad->friend_collidesWith( bd, nullptr );
}

//-----------------------------------------------------------------------------
SimpleObjectIterator *PartitionManager::iterateAllObjects(PartitionFilter **filters)
{
	GxTimeScope gxT(GXT_SEARCH);	// tickTiming: the iterator and its entries count as search allocations
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	// no distance constraints; we're gonna have to process 'em all anyway,
	// so go thru in the fastest way we know how.
	PartitionData *mod, *nextMod;
	for( mod = m_moduleList; mod; mod = nextMod )
	{
		nextMod = mod->getNext();
		Object *obj = mod->getObject();
		if (obj && filtersAllow(filters, obj))
		{
			iter->insert( obj );
		}
	}

	iterHolder.release();
	return iter;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool PartitionManager::tryPosition( const Coord3D *center,
																		Real dist,
																		Real angle,
																		const FindPositionOptions *options,
																		Coord3D *result )
{

	// compute the spot on the terrain we've picked
	Coord3D pos;
	pos.x = dist * Cos( angle ) + center->x;
	pos.y = dist * Sin( angle ) + center->y;

	PathfindLayerEnum layer = LAYER_GROUND;
	if ((options->flags & FPF_USE_HIGHEST_LAYER) != 0)
	{
		pos.z = 99999.0f;
		layer = TheTerrainLogic->getHighestLayerForDestination(&pos);
		pos.z = TheTerrainLogic->getLayerHeight(pos.x, pos.y, layer);
		// ensure we are slightly above the bridge, to account for fudge & sloppy art
		if (layer != LAYER_GROUND)
			pos.z += 1.0f;
	}
	else
	{
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
	}

	if (fabs(pos.z - center->z) > options->maxZDelta)
		return FALSE;

	//
	// we don't usually find positions on cliffs.
	// someday, add bit options for this, like for water.
	//
	if (TheTerrainLogic->isCliffCell(pos.x, pos.y) && layer == LAYER_GROUND)
		return FALSE;

	//
	// srj sez:
	// we don't usually find positions on impassable areas.
	// someday, add bit options for this, like for water.
	//
	{
		Int cellX = REAL_TO_INT_FLOOR(pos.x / PATHFIND_CELL_SIZE);
		Int cellY = REAL_TO_INT_FLOOR(pos.y / PATHFIND_CELL_SIZE);
		PathfindCell* cell = TheAI->pathfinder()->getCell(layer, cellX, cellY);
		if (!cell || cell->getType() == PathfindCell::CELL_IMPASSABLE)
		{
			return FALSE;
		}
		if( BitIsSet(options->flags, FPF_CLEAR_CELLS_ONLY) && cell->getType() != PathfindCell::CELL_CLEAR )
			return FALSE;

	}

	//
	// we don't usually find positions in the water unless we explicitly say that's OK,
	// or if the option is set we can only pick position underwater
	//
	if( BitIsSet( options->flags, FPF_IGNORE_WATER ) == FALSE )
	{
		Bool isUnderwater = TheTerrainLogic->isUnderwater( pos.x, pos.y );

		//
		// if we want water spots only and this is underwater it's no good, otherwise we want
		// the default behavior where underwater spots are invalid
		//
		if( BitIsSet( options->flags, FPF_WATER_ONLY ) && (isUnderwater == FALSE || layer != LAYER_GROUND) )
			return FALSE;
		else if( isUnderwater == TRUE && layer == LAYER_GROUND )
			return FALSE;

	}

	// object checks
	if( BitIsSet( options->flags, FPF_IGNORE_ALL_OBJECTS ) == FALSE )
	{

		//
		// iterate the potential collisions at this location using a
		// very small sphere geometry around the point
		//
		GeometryInfo geometry( GEOMETRY_SPHERE, TRUE, 5.0f, 5.0f, 5.0f );
		ObjectIterator *iter = iteratePotentialCollisions( &pos, geometry, angle, true );
		MemoryPoolObjectHolder hold( iter );
//	Bool overlap = FALSE;

		for( Object *them = iter->first(); them; them = iter->next() )
		{

			// if this is the ignore object, ignore it
			if( them == options->ignoreObject )
				continue;  // continue in the object iterator for loop

			// relationship checks
			if( options->relationshipObject )
			{

				// if this is an ally/neutral unit and we ignore those, do so
				if( BitIsSet( options->flags, FPF_IGNORE_ALLY_OR_NEUTRAL_UNITS ) == TRUE &&
						options->relationshipObject->getRelationship( them ) != ENEMIES &&
						(them->isKindOf( KINDOF_INFANTRY ) || them->isKindOf( KINDOF_VEHICLE )) )
					continue;

				// if this is an ally/neutral structure and we ignore those, do so
				if( BitIsSet( options->flags, FPF_IGNORE_ALLY_OR_NEUTRAL_STRUCTURES ) == TRUE &&
						options->relationshipObject->getRelationship( them ) != ENEMIES &&
						them->isKindOf( KINDOF_STRUCTURE ) )
					continue;

				// if this is an enemy unit and we ignore those, do so
				if( BitIsSet( options->flags, FPF_IGNORE_ENEMY_UNITS ) == TRUE &&
						options->relationshipObject->getRelationship( them ) == ENEMIES &&
						(them->isKindOf( KINDOF_INFANTRY ) || them->isKindOf( KINDOF_VEHICLE )) )
					continue;

				// if this is an enemy structure and we ignore those, do so
				if( BitIsSet( options->flags, FPF_IGNORE_ENEMY_STRUCTURES ) == TRUE &&
						options->relationshipObject->getRelationship( them ) == ENEMIES &&
						them->isKindOf( KINDOF_STRUCTURE ) )
					continue;

			}

			//
			// if we have a source that must path to the destination we will ignore that too
			// NOTE: If needed, we can reorganize the meaning of this method to be
			// more explicit on when to pay attention to any 'source' objects and when not too
			// but for now we're keeping the API very simple for the kinds of queries
			// that we're likely to do for an RTS (CBD)
			//
			if( them == options->sourceToPathToDest )
				continue;  // continue in the object iterator for loop

			// oops, we have overlapped something
			return FALSE;

		}

	}

	//
	// finally ... if sourceToPathDest is valid ... the source object must be able to
	// find a path the the position we have selected.  We want to do this last because
	// a pathfind can sometimes be an relatively expensive operation
	//
	if( options->sourceToPathToDest )
	{
		const AIUpdateInterface *ai = options->sourceToPathToDest->getAIUpdateInterface();

		// check for path existence
		if( ai && TheAI->pathfinder()->clientSafeQuickDoesPathExist( ai->getLocomotorSet(),
																									options->sourceToPathToDest->getPosition(),
																									&pos ) == FALSE )
				return FALSE;

	}

	// save result and return TRUE for position found
	*result = pos;
	return TRUE;

}

//
// the following determines how fast we expand our concentric ring search for the
// find position methods
//
static Real ringSpacing = 5.0f;

//-------------------------------------------------------------------------------------------------
/** This method will attempt to find a legal position from the center position specified,
	* at least minRadis away from it, but no more than maxRadius away.
	*
	* Return TRUE if position is found and that position is returned in 'result'
	* return FALSE no legal position exists or invalid params
	*/
//-------------------------------------------------------------------------------------------------
Bool PartitionManager::findPositionAround( const Coord3D *center,
																					 const FindPositionOptions *options,
																					 Coord3D *result )
{

	// sanity
	if( center == nullptr || result == nullptr || options == nullptr )
		return FALSE;

	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent(&extent);
	// If the goal is off the map, it is a scripted setup, so just
	// use the center.
	if (!extent.isInRegionNoZ(center)) {
		*result = *center;
		return true;
	}
	// sanity, FPF_IGNORE_WATER and FPF_WATER_ONLY are mutually exclusive
	DEBUG_ASSERTCRASH( !(BitIsSet( options->flags, FPF_IGNORE_WATER ) == TRUE &&
										   BitIsSet( options->flags, FPF_WATER_ONLY ) == TRUE),
										 ("PartitionManager::findPositionAround - The options FPF_WATER_ONLY and FPF_IGNORE_WATER are mutually exclusive.  You cannot use them together") );

	// pick a random angle from the center location to start at
	Real startAngle;
	if( options->startAngle == RANDOM_START_ANGLE )
		startAngle = GameLogicRandomValueReal( 0.0f, TWO_PI );
	else
		startAngle = options->startAngle;

	// start the search at the most inner ring (minRadius) and expand to outer most ring (maxRadius)
	for( Real dist = options->minRadius; dist <= options->maxRadius; dist += ringSpacing )
	{

		//
		// given the spacing that we've been using for the angles on the 'idea' inner circle,
		// we will need more points on larger circles to cover more ground
		//
		Real angleSpacing;
		if( dist == options->minRadius )
			angleSpacing = TWO_PI;
		else
			angleSpacing = (ringSpacing / (dist + 1.0f)) * (TWO_PI / 6.0f);  // larger float = more samples

		//
		// on this "ring", try all the angles available to us in a circle ... we'll start at
		// the 'startAngle', then try 'angleSpacing' to the left, then 'angleSpacing' to the
		// right, then '2 angleSpacings' to the left etc., ping ponging around the start angle
		// until we've done them all the way around the circle
		//

		// how many samples will we test
		Int samples = REAL_TO_INT_CEIL( (TWO_PI / angleSpacing) / 2.0f);
		for( Int i = 0; i < samples; ++i )
		{

			// try one "side"
			if( tryPosition( center, dist, startAngle + angleSpacing * i, options, result ) == TRUE )
				return TRUE;

			//
			// try the other "side" ... note for the first point at minRadius this position is
			// the same so we won't test it again in that case
			//
			if( i != 0 )
				if( tryPosition( center, dist, startAngle - angleSpacing * i, options, result ) == TRUE )
					return TRUE;

		}

	}

	// no position was able to be found
	return FALSE;

}

//-----------------------------------------------------------------------------
// Web port (switch fogStamps, fourth word bit 14; same game): sight circles from precomputed stamps.
// A stamp is DiscreteCircle's span list for one cell radius, recorded once at centre (0, 0) in
// drawCircle's order (Bresenham offsets plus the centre, so the same spans as at any centre). A span
// is applied to the dense shroud counters 8 cells at a time with wasm SIMD (kernel: the same
// arithmetic as PartitionCell::addLooker / removeLooker, writing only the span's counters), then the
// cells whose status changed get addLooker's side effects (apply: gxShroudEdge, left to right). The
// side effects read and write no counter, so running them after the span's arithmetic changes nothing.
struct GxSpan { Short x1, x2, dy; };
static std::vector< std::vector<GxSpan> > s_gxStamps;
unsigned long long g_gxFogVerifyChecked = 0, g_gxFogVerifyBad = 0;

static const std::vector<GxSpan> &gxStamp(Int r)
{
	if (r >= (Int)s_gxStamps.size())
		s_gxStamps.resize(r + 1);
	std::vector<GxSpan> &st = s_gxStamps[r];
	if (st.empty())
	{
		DiscreteCircle c(0, 0, r);
		for (VecHorzLine::const_iterator it = c.getEdges().begin(); it != c.getEdges().end(); ++it)
		{
			GxSpan a = { (Short)it->xStart, (Short)it->xEnd, (Short)it->yPos };
			st.push_back(a);
			if (it->yPos != 0)
			{
				GxSpan b = { (Short)it->xStart, (Short)it->xEnd, (Short)-it->yPos };
				st.push_back(b);
			}
		}
	}
	return st;
}

static inline CellShroudStatus gxShroudOf(Int v) { return v == 1 ? CELLSHROUD_SHROUDED : v == 0 ? CELLSHROUD_FOGGED : CELLSHROUD_CLEAR; }

// One span of one player: cells x1..x2 of row y, clipped as hLineAddLooker does.
static void gxSpanLook(Int x1, Int x2, Int y, Int p, Bool add)
{
	PartitionManager *pm = ThePartitionManager;
	const Int W = g_gxCellMaskW;
	if (!W || y < 0 || y >= g_gxShroudN / W || x1 >= W || x2 < 0)
		return;
	if (x1 < 0) x1 = 0;
	if (x2 >= W) x2 = W - 1;
	gxCount(GXC_FOG_CELLS, x2 - x1 + 1);
	Int x = x1;
#ifdef __wasm_simd128__
	Short *cur = g_gxShroudCur + p * g_gxShroudN + y * W;
	const Short *act = g_gxShroudAct + p * g_gxShroudN + y * W;
	const Bool verify = (GLES3_Page_Switches() & 2097152) != 0;
	const v128_t one = wasm_i16x8_splat(1), mone = wasm_i16x8_splat(-1), zero = wasm_i16x8_splat(0), two = wasm_i16x8_splat(2);
	for (; x + 8 <= x2 + 1; x += 8)
	{
		Short before[8];
		if (verify) memcpy(before, cur + x, sizeof(before));
		// kernel: reads cur/act of these 8 cells, writes their cur
		const v128_t o = wasm_v128_load(cur + x);
		v128_t n;
		if (add)
			n = wasm_i16x8_min(wasm_i16x8_sub(o, one), mone);
		else
			n = wasm_v128_bitselect(wasm_i16x8_min(wasm_v128_load(act + x), one), wasm_i16x8_add(o, one), wasm_i16x8_eq(o, mone));
		wasm_v128_store(cur + x, n);
		// status: 2 shrouded (1), 1 fogged (0), 0 clear (anything else)
		const v128_t so = wasm_v128_or(wasm_v128_and(wasm_i16x8_eq(o, one), two), wasm_v128_and(wasm_i16x8_eq(o, zero), one));
		const v128_t sn = wasm_v128_or(wasm_v128_and(wasm_i16x8_eq(n, one), two), wasm_v128_and(wasm_i16x8_eq(n, zero), one));
		// apply: the side effects of the cells whose status changed
		for (UnsignedInt bits = (UnsignedInt)wasm_i16x8_bitmask(wasm_v128_not(wasm_i16x8_eq(so, sn))); bits; bits &= bits - 1)
		{
			const Int l = __builtin_ctz(bits);
			pm->getCellAt(x + l, y)->gxShroudEdge(p, gxShroudOf(cur[x + l]));
		}
		if (verify)
		{
			for (Int l = 0; l < 8; ++l)
			{
				const Int v = before[l];
				const Int want = add ? min(v - 1, -1) : (v == -1 ? min((Int)act[x + l], 1) : v + 1);
				++g_gxFogVerifyChecked;
				if (want != cur[x + l] && g_gxFogVerifyBad++ < 20)
					printf("GXVERIFY fog stamp mismatch: cell %d %d player %d was %d got %d want %d\n", x + l, y, p, v, cur[x + l], want);
			}
			if ((g_gxFogVerifyChecked & 0xfffff) < 8)
				printf("GXVERIFY fog stamps: cells checked %llu, mismatches %llu\n", g_gxFogVerifyChecked, g_gxFogVerifyBad);
		}
	}
	// straight-line tail: addLooker / removeLooker's arithmetic inline; a call only on a status edge
	for (; x <= x2; ++x)
	{
		const Int o = cur[x];
		const Int n = add ? min(o - 1, -1) : (o == -1 ? min((Int)act[x], 1) : o + 1);
		cur[x] = (Short)n;
		if (gxShroudOf(o) != gxShroudOf(n))
			pm->getCellAt(x, y)->gxShroudEdge(p, gxShroudOf(n));
	}
#else
	for (; x <= x2; ++x)
	{
		PartitionCell *cell = pm->getCellAt(x, y);
		if (add) cell->addLooker(p); else cell->removeLooker(p);
	}
#endif
}

static void gxStampLook(Int cx, Int cy, Int r, PlayerMaskType playerMask, Bool add)
{
	const std::vector<GxSpan> &st = gxStamp(r);
	for (Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >= 0; currentIndex--)
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer(currentIndex);
		if (!BitIsSet(playerMask, currentPlayer->getPlayerMask()))
			continue;
		for (size_t k = 0; k < st.size(); ++k)
			gxSpanLook(cx + st[k].x1, cx + st[k].x2, cy + st[k].dy, currentIndex, add);
	}
}

//-----------------------------------------------------------------------------
// This is the main accessor of the shroud system.  At this level, allies are taken
// into consideration as specified by the caller.  Look/Unlook are the ones sending Ally info, as that
// is in Object where Allies make sense.  AddLooker literally just adds a looker for the player you specify.
// This way, Full map reveals and Observer mode active look will not carry over to all
// allies.  They'll use the RevealWholeDamnMap series, which call addLooker directly.
void PartitionManager::doShroudReveal(Real centerX, Real centerY, Real radius, PlayerMaskType playerMask)
{
	GxTimeScope gxT(GXT_FOG_REVEAL);	// tickTiming
	gxCount(GXC_FOG_CIRCLES);
	Int cellCenterX, cellCenterY;
	worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = worldToCellDist(radius);
	if (cellRadius < 1)
		cellRadius = 1;

	if (GLES3_Page_Switches4() & 16384)
	{
		gxStampLook(cellCenterX, cellCenterY, cellRadius, playerMask, TRUE);	// switch fogStamps
		return;
	}

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		// Object's Look is the one who knows about allies.  Anyone can pask a player mask to me and all
		// of those players will have an active looker applied to a bunch of cells
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitIsSet( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineAddLooker, (void*)currentIndex);
		}
	}
}

//-----------------------------------------------------------------------------
// web port (switch iterPool, fifth word bit 29): undone sightings go to a free list and the next queued
// unlook reuses one (reset() gives it the constructor's state), so a moving unit allocates none.
static std::vector<SightingInfo *> s_gxFreeSightings;
static inline bool gxReuseSightings()
{
#ifdef __EMSCRIPTEN__
	extern int GLES3_Page_Switches5();
	return (GLES3_Page_Switches5() & 536870912) != 0;
#else
	return false;
#endif
}

void PartitionManager::processPendingUndoShroudRevealQueue( Bool considerTimestamp )
{
	//Keep going until the front one is in the future.  UndoShroudReveal on each one you process.
	//Again, you know these are in order, because we control the adding of the Constant in-the-queue time.
	UnsignedInt compareTime;
	if(considerTimestamp)
		compareTime = TheGameLogic->getFrame();
	else
		compareTime = UINT_MAX;

	while( !m_pendingUndoShroudReveals.empty() && (m_pendingUndoShroudReveals.front()->m_data < compareTime) )
	{
		SightingInfo *thisInfo = m_pendingUndoShroudReveals.front();

		undoShroudReveal( thisInfo->m_where.x, thisInfo->m_where.y, thisInfo->m_howFar, thisInfo->m_forWhom );

		if (gxReuseSightings())
			s_gxFreeSightings.push_back(thisInfo);
		else
			deleteInstance(thisInfo);
		m_pendingUndoShroudReveals.pop();
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::processEntirePendingUndoShroudRevealQueue()
{
	// Something major is about to happen, so we need to rush through our queue and get everything in order.
	// Examples are map resize, full map undo-reveal, stuff like that.

	// Don't like duplicating code, so route to original function.
	processPendingUndoShroudRevealQueue(FALSE);
}

//-----------------------------------------------------------------------------
void PartitionManager::resetPendingUndoShroudRevealQueue()
{
	while( !m_pendingUndoShroudReveals.empty() )
	{
		SightingInfo *thisInfo = m_pendingUndoShroudReveals.front();
		deleteInstance(thisInfo);
		m_pendingUndoShroudReveals.pop();
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoShroudReveal(Real centerX, Real centerY, Real radius, PlayerMaskType playerMask)
{
	GxTimeScope gxT(GXT_FOG_REVEAL);	// tickTiming
	gxCount(GXC_FOG_CIRCLES);
	Int cellCenterX, cellCenterY;
	worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = worldToCellDist(radius);
	if (cellRadius < 1)
		cellRadius = 1;

	if (GLES3_Page_Switches4() & 16384)
	{
		gxStampLook(cellCenterX, cellCenterY, cellRadius, playerMask, FALSE);	// switch fogStamps
		return;
	}

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitIsSet( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineRemoveLooker, (void*)currentIndex);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::queueUndoShroudReveal(Real centerX, Real centerY, Real radius, PlayerMaskType playerMask)
{
	UnsignedInt now = TheGameLogic->getFrame();
	SightingInfo *newInfo;
	if (!s_gxFreeSightings.empty() && gxReuseSightings())
	{
		newInfo = s_gxFreeSightings.back();
		s_gxFreeSightings.pop_back();
		newInfo->reset();
	}
	else
		newInfo = newInstance(SightingInfo);

	newInfo->m_where.x = centerX;
	newInfo->m_where.y = centerY;
	newInfo->m_howFar = radius;
	newInfo->m_forWhom = playerMask;
	newInfo->m_data = now + TheGlobalData->m_unlookPersistDuration;

	m_pendingUndoShroudReveals.push(newInfo);
}

//-----------------------------------------------------------------------------
void PartitionManager::doShroudCover(Real centerX, Real centerY, Real radius, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = worldToCellDist(radius);
	if (cellRadius < 1)
		cellRadius = 1;

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		// Object's Shroud is the one who knows about allies.  Anyone can pask a player mask to me and all
		// of those players will have an active shrouder applied to a bunch of cells
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitIsSet( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineAddShrouder, (void*)currentIndex);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoShroudCover(Real centerX, Real centerY, Real radius, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = worldToCellDist(radius);
	if (cellRadius < 1)
		cellRadius = 1;

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitIsSet( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineRemoveShrouder, (void*)currentIndex);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::doThreatAffect( Real centerX, Real centerY, Real radius, UnsignedInt threatVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = worldToCellDist(radius);
	if (cellRadius < 1)
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = threatVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitIsSet( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineAddThreat, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoThreatAffect( Real centerX, Real centerY, Real radius, UnsignedInt threatVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = worldToCellDist(radius);
	if (cellRadius < 1)
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = threatVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitIsSet( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineRemoveThreat, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::doValueAffect( Real centerX, Real centerY, Real radius, UnsignedInt valueVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = worldToCellDist(radius);
	if (cellRadius < 1)
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = valueVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitIsSet( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineAddValue, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoValueAffect( Real centerX, Real centerY, Real radius, UnsignedInt valueVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = worldToCellDist(radius);
	if (cellRadius < 1)
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = valueVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitIsSet( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineRemoveValue, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::getCellCenterPos(Int x, Int y, Real& xx, Real& yy)
{
	DEBUG_ASSERTCRASH(x >= 0 && y >= 0, ("hmm, invalid cell"));
	Real half = m_cellSize*0.5f;
	xx = m_worldExtents.lo.x + (x * m_cellSize) + half;
	yy = m_worldExtents.lo.y + (y * m_cellSize) + half;
}

//-----------------------------------------------------------------------------
#ifdef PM_CACHE_TERRAIN_HEIGHT

	struct TerrainExtremeData
	{
		Real* minZ;
		Real* maxZ;
		Coord2D* minZPos;
		Coord2D* maxZPos;
		Bool isValid;
	};

static Int checkTerrainExtreme(PartitionCell* cell, void* userData)
{
	TerrainExtremeData* data = (TerrainExtremeData*)userData;

	data->isValid = true;

	Real tmp;

	if (data->minZ)
	{
		tmp = cell->getLoTerrain();
		if (tmp < *data->minZ )
		{
			*data->minZ = tmp;
			if (data->minZPos)
			{
				cell->getCellCenterPos(data->minZPos->x, data->minZPos->y);
			}
		}
	}

	if (data->maxZ)
	{
		tmp = cell->getHiTerrain();
		if (tmp > *data->maxZ )
		{
			*data->maxZ = tmp;
			if (data->maxZPos)
			{
				cell->getCellCenterPos(data->maxZPos->x, data->maxZPos->y);
			}
		}
	}

	return 0;	// zero to continue
}
#endif

//-----------------------------------------------------------------------------
#ifdef PM_CACHE_TERRAIN_HEIGHT
Bool PartitionManager::estimateTerrainExtremesAlongLine(const Coord3D& pos, const Coord3D& posOther, Real* minZ, Real* maxZ, Coord2D* minZPos, Coord2D* maxZPos)
{
	TerrainExtremeData data;
	data.minZ = minZ;
	data.maxZ = maxZ;
	data.minZPos = minZPos;
	data.maxZPos = maxZPos;
	data.isValid = false;
	if (minZ) *minZ = HUGE_DIST;
	if (maxZ) *maxZ = -HUGE_DIST;
	iterateCellsAlongLine(pos, posOther, checkTerrainExtreme, &data);
	return data.isValid;
}
#endif

//-----------------------------------------------------------------------------
// Uses Bresenham line algorithm from www.gamedev.net.
Int PartitionManager::iterateCellsAlongLine(const Coord3D& pos, const Coord3D& posOther, CellAlongLineProc proc, void* userData)
{
	ICoord2D start, end, delta;
	Int x, y;
	Int xinc1, xinc2;
	Int yinc1, yinc2;
	Int den, num, numadd;
	Int numpixels;

	worldToCell(pos.x, pos.y, &start.x, &start.y);
	worldToCell(posOther.x, posOther.y, &end.x, &end.y);

	delta.x = abs(end.x - start.x);			// The difference between the x's
	delta.y = abs(end.y - start.y);			// The difference between the y's
	x = start.x;												// Start x off at the first pixel
	y = start.y;												// Start y off at the first pixel

	if (end.x >= start.x)								// The x-values are increasing
	{
		xinc1 = 1;
		xinc2 = 1;
	}
	else																// The x-values are decreasing
	{
		xinc1 = -1;
		xinc2 = -1;
	}

	if (end.y >= start.y)               // The y-values are increasing
	{
		yinc1 = 1;
		yinc2 = 1;
	}
	else																// The y-values are decreasing
	{
		yinc1 = -1;
		yinc2 = -1;
	}
	Bool checkY = true;
	if (delta.x >= delta.y)							// There is at least one x-value for every y-value
	{
		xinc1 = 0;												// Don't change the x when numerator >= denominator
		yinc2 = 0;												// Don't change the y for every iteration
		den = delta.x;
		num = delta.x / 2;
		numadd = delta.y;
		numpixels = delta.x;							// There are more x-values than y-values
	}
	else																// There is at least one y-value for every x-value
	{
		checkY = false;
		xinc2 = 0;												// Don't change the x for every iteration
		yinc1 = 0;												// Don't change the y when numerator >= denominator
		den = delta.y;
		num = delta.y / 2;
		numadd = delta.x;
		numpixels = delta.y;							// There are more y-values than x-values
	}

	for (Int curpixel = 0; curpixel <= numpixels; curpixel++)
	{
		PartitionCell* cell = getCellAt(x, y);	// might be null if off the edge
		DEBUG_ASSERTCRASH(cell != nullptr, ("off the map"));
		if (cell)
		{
			Int ret = (*proc)(cell, userData);
			if (ret != 0)
				return ret;	// bail early
		}

		num += numadd;										// Increase the numerator by the top of the fraction
		if (num >= den)										// Check if numerator >= denominator
		{
			num -= den;											// Calculate the new numerator value
			x += xinc1;											// Change the x as appropriate
			y += yinc1;											// Change the y as appropriate
		}
		x += xinc2;												// Change the x as appropriate
		y += yinc2;												// Change the y as appropriate
	}

	return 0;
}

//-----------------------------------------------------------------------------
Int PartitionManager::iterateCellsBreadthFirst(const Coord3D *pos, CellBreadthFirstProc proc, void *userData)
{
	// starting at pos, iterate the cells in the following manner:
	// left, up, right, down
	// using Breadth-first search.
	// Call proc on each cell, and if it returns non-zero, then return the cell index
	// -1 means error, but we should add a define later for this.

	Int cellX, cellY;
	worldToCell(pos->x, pos->y, &cellX, &cellY);

	// Note, bool. not Bool, cause bool will cause this to be a bitfield.
	std::vector<bool> bitField;
	Int cellCount = m_cellCountX * m_cellCountY;
	bitField.resize(cellCount);
	// 0 means not done, 1 means done.

	for (Int i = 0; i < cellCount; ++i) {
		bitField[i] = false;
	}

	std::queue<PartitionCell *> cellQ;

	// mark the starting cell done
	bitField[cellY * m_cellCountX + cellX] = true;
	cellQ.push(&m_cells[cellY * m_cellCountX + cellX]);

	Int curX = cellX;
	Int curY = cellY;
	while (!cellQ.empty()) {
		// first, add the neighbors
		// left
		if (curX - 1 >= 0) {
			if (!bitField[curY * m_cellCountX + curX - 1]) {
				bitField[curY * m_cellCountX + curX - 1] = true;
				cellQ.push(&m_cells[curY * m_cellCountX + curX - 1]);
			}
		}

		// top
		if (curY - 1 >= 0) {
			if (!bitField[(curY - 1) * m_cellCountX + curX]) {
				bitField[(curY - 1) * m_cellCountX + curX] = true;
				cellQ.push(&m_cells[(curY - 1) * m_cellCountX + curX]);
			}
		}

		// right
		if (curX + 1 < m_cellCountX) {
			if (!bitField[curY * m_cellCountX + curX + 1]) {
				bitField[curY * m_cellCountX + curX + 1] = true;
				cellQ.push(&m_cells[curY * m_cellCountX + curX + 1]);
			}
		}

		// bottom
		if (curY + 1 > 0) {
			if (!bitField[(curY + 1) * m_cellCountX + curX]) {
				bitField[(curY + 1) * m_cellCountX + curX] = true;
				cellQ.push(&m_cells[(curY + 1) * m_cellCountX + curX]);
			}
		}

		// now process the current top.
		PartitionCell *curCell = cellQ.front();
		cellQ.pop();
		curX = curCell->getCellX();
		curY = curCell->getCellY();

		if ((proc)(curCell, userData) != 0)
			return (curCell->getCellY() * m_cellCountX + curCell->getCellX());
	}

	return -1;
}

//-----------------------------------------------------------------------------
static Real calcDist2D(Real x1, Real y1, Real x2, Real y2)
{
	return sqrtf(sqr(x1-x2) + sqr(y1-y2));
}

//-----------------------------------------------------------------------------
Bool PartitionManager::isClearLineOfSightTerrain(const Object* obj, const Coord3D& objPos, const Object* other, const Coord3D& otherPos)
{
	Coord3D pos, posOther;

	if (obj)
	{
		pos = *obj->getPosition();
		// note that we want to measure from the top of the collision
		// shape, not the bottom! (most objects have eyes a lot closer
		// to their head than their feet. if we have really odd critters
		// with eye-feet, we'll need to change this assumption.)
		pos.z += obj->getGeometryInfo().getMaxHeightAbovePosition();
	}
	else
	{
		pos = objPos;
	}

	if (other)
	{
		posOther = *other->getPosition();
		// note that we want to measure from the top of the collision
		// shape, not the bottom! (most objects have eyes a lot closer
		// to their head than their feet. if we have really odd critters
		// with eye-feet, we'll need to change this assumption.)
		posOther.z += other->getGeometryInfo().getMaxHeightAbovePosition();
	}
	else
	{
		posOther = otherPos;
	}

#ifdef NO_BAD_AND_INACCURATE
/*
	This looks tempting, and in theory would be better than the below code; however, it
	is not (yet) accurate enough for our targeting needs. what we really need to do is add a Bresenham
	line iterator to the TerrainLogic itself, which would probably suffice. Until then, this code is
	left in as an example of what NOT to do. (srj)
*/
	Real maxZ;
	Coord2D maxZPos;
	Bool valid = estimateTerrainExtremesAlongLine(pos, posOther, nullptr, &maxZ, nullptr, &maxZPos);
	DEBUG_ASSERTCRASH(valid, ("this should never happen unless both positions are off-map"));
	if (!valid)
		return true;

	Real terrainAtHighPoint = maxZ;

	Real totalDist2D = calcDist2D(pos.x, pos.y, posOther.x, posOther.y);
	Real terrainDist2D = calcDist2D(pos.x, pos.y, maxZPos.x, maxZPos.y);

	const Real TINY_DIST = 0.01f;
	Real percent = (totalDist2D > TINY_DIST) ? (terrainDist2D / totalDist2D) : 0.0f;
	if (percent < 0.0f) percent = 0.0f;
	if (percent > 1.0f) percent = 1.0f;

	Real lineOfSightAtHighPoint = pos.z + (posOther.z - pos.z) * percent;

	// if terrainAtHighPoint > lineOfSightAtHighPoint, we can't see.
	// add a little fudge to account for slop.
	const Real LOS_FUDGE = 0.5f;
	if (terrainAtHighPoint > lineOfSightAtHighPoint + LOS_FUDGE)
	{
		//DEBUG_LOG(("isClearLineOfSightTerrain fails"));
		return false;
	}

	return true;

#else
	return TheTerrainLogic->isClearLineOfSight(pos, posOther);
#endif
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PartitionManager::crc( Xfer *xfer )
{

	for (Int i=0; i<m_totalCellCount; ++i)
	{
		m_cells[i].crc(xfer);
	}

}

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info:
	* 1: Initial version
	* 2: m_pendingUndoShroudReveals stores Unlooks waiting to happen.
	*/
// ------------------------------------------------------------------------------------------------
void PartitionManager::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// cell size information
	Real cellSize = m_cellSize;
	xfer->xferReal( &cellSize );

	//
	// cell size should match what we have allocated in the partition manager, if it doesn't
	// we need to increment the version here and edit this code to be able to load
	// the old cell size if changed after we release
	//
	if( cellSize != m_cellSize )
	{

		DEBUG_CRASH(( "Partition cell size has changed, this save game file is invalid" ));
		throw SC_INVALID_DATA;

	}

	// cell count
	Int totalCellCount = m_totalCellCount;
	xfer->xferInt( &totalCellCount );

	// total cell count better match what we have allocated for cells in the partition manager
	if( totalCellCount != m_totalCellCount )
	{

		DEBUG_CRASH(( "Partition total cell count mismatch %d, should be %d",
									totalCellCount, m_totalCellCount ));
		throw SC_INVALID_DATA;

	}

	// xfer each cell information
	PartitionCell *cell;
	for( Int i = 0; i < totalCellCount; ++i )
	{

		// get this partition cell
		cell = &m_cells[ i ];

		// xfer the data for this cell
		xfer->xferSnapshot( cell );

	}

	// when loading tell the partition manager to rethink and refresh all shroud information
	if( xfer->getXferMode() == XFER_LOAD )
	{

		// tell partition manager to re-evaluate shroud things when next asked
		m_updatedSinceLastReset = FALSE;

		// refresh the shroud for the local player which will update the radar and everything
		refreshShroudForLocalPlayer();

	}

	if( version >= 2 )
	{
		Int queueSize = m_pendingUndoShroudReveals.size();
		xfer->xferInt(&queueSize);
		// This xfer thing is still cool.  On save, I just wrote the number of elements.  On load, the first line
		// above was a decoy, and the second found out the size.

		if(xfer->getXferMode() == XFER_LOAD)
		{
			// have to remove this assert, because during load there is a setTeam call for each guy on a sub-team, and that results
			// in a queued unlook, so we actually have stuff in here at the start.  I am fairly certain that setTeam should wait
			// until loadPostProcess, but I ain't gonna change it now.
//			DEBUG_ASSERTCRASH(m_pendingUndoShroudReveals.empty(), ("At load, we appear to not be in a reset state.") );

			// I have to split this up though, since on Load I need to make new instances.
			for( Int infoIndex = 0; infoIndex < queueSize; infoIndex++ )
			{
				SightingInfo *newInfo = newInstance(SightingInfo);
				xfer->xferSnapshot(newInfo);
				m_pendingUndoShroudReveals.push(newInfo);
			}
		}
		else
		{
			// And on Save, I need to loop through, but not destroy anything, so I pop-push n times, cycling through the queue once
			// (No random access on queues, and speed not a concern in saveload)
			for( Int infoIndex = 0; infoIndex < queueSize; infoIndex++ )
			{
				SightingInfo *saveInfo = m_pendingUndoShroudReveals.front();
				xfer->xferSnapshot(saveInfo);
				m_pendingUndoShroudReveals.pop();
				m_pendingUndoShroudReveals.push(saveInfo);
			}
		}

	}
	else
	{
		// Version 1 save games will just not have any SightingInfos in the queue to be undone.
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PartitionManager::loadPostProcess()
{

}

//-----------------------------------------------------------------------------
Real PartitionManager::getGroundOrStructureHeight(Real posx, Real posy)
{
	// get the terrain height
	Real terrainHeightHere = TheTerrainLogic->getGroundHeight( posx, posy );

	// scan all objects in the radius of our extent and find the tallest height among them
	PartitionFilterAcceptByKindOf filter1( MAKE_KINDOF_MASK( KINDOF_STRUCTURE ), KINDOFMASK_NONE );
	PartitionFilter *filters[] = { &filter1, nullptr };
  Coord3D pos;
  pos.x = posx;
  pos.y = posy;
  pos.z = terrainHeightHere;
	const Real RANGE = 1.0f;
	ObjectIterator *iter = iterateObjectsInRange( &pos, RANGE, FROM_BOUNDINGSPHERE_2D, filters );
	MemoryPoolObjectHolder hold( iter );

	Real tallestHeight = 0.0f;
	Real thisHeight;
	for( Object* obj = iter->first(); obj; obj = iter->next() )
	{
		// store the height of the tallest object under us
		thisHeight = obj->getGeometryInfo().getMaxHeightAbovePosition();
		if( thisHeight > tallestHeight )
			tallestHeight = thisHeight;

	}

  return terrainHeightHere + tallestHeight;
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::getMostValuableLocation( Int playerIndex, UnsignedInt whichPlayerTypes, ValueOrThreat valType, Coord3D *outLocation )
{
	if (!outLocation)
		return;

	PlayerMaskType playerMask = ThePlayerList->getPlayersWithRelationship(playerIndex, whichPlayerTypes);
	if (playerMask == 0)
		return;

	Int cellCount = m_cellCountX * m_cellCountY;

	PlayerMaskType allPlayerMasks[MAX_PLAYER_COUNT] = { 0 };
	Int totalPlayerCount = ThePlayerList->getPlayerCount();

	Int i;
	for (i = 0; i < totalPlayerCount; ++i) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (!player) {
			continue;
		}

		allPlayerMasks[i] = player->getPlayerMask();
	}

	Int greatestValueCell = -1;
	Int maxCellValue = -1;
	for (i = 0; i < cellCount; ++i) {
		Int cellValue = 0;

		for (Int player = 0; player < MAX_PLAYER_COUNT; ++player) {
			if (BitIsSet(allPlayerMasks[player], playerMask)) {
				if (valType == VOT_CashValue) {
					cellValue += m_cells[i].getCashValue(player);
				} else {
					cellValue += m_cells[i].getThreatValue(player);
				}
			}
		}

		if (cellValue > maxCellValue) {
			maxCellValue = cellValue;
			greatestValueCell = i;
		}
	}

	if (greatestValueCell == -1 || maxCellValue == -1) {
		DEBUG_CRASH(("PartitionManager::getMostValuableLocation: jkmcd"));
		return;
	}

	outLocation->set(m_cells[greatestValueCell].getCellX() * TheGlobalData->m_partitionCellSize,
									 m_cells[greatestValueCell].getCellY() * TheGlobalData->m_partitionCellSize,
									 0
									);
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::getNearestGroupWithValue( Int playerIndex, UnsignedInt whichPlayerTypes, ValueOrThreat valType,
															 const Coord3D *sourceLocation, Int valueRequired, Bool greaterThan, Coord3D *outLocation )
{
	if (!(sourceLocation && outLocation))
		return;

	PlayerMaskType playerMask = ThePlayerList->getPlayersWithRelationship(playerIndex, whichPlayerTypes);
	if (playerMask == 0)
		return;

	PlayerMaskType allPlayerMasks[MAX_PLAYER_COUNT] = { 0 };
	Int totalPlayerCount = ThePlayerList->getPlayerCount();

	Int i;
	for (i = 0; i < totalPlayerCount; ++i) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (!player) {
			continue;
		}

		allPlayerMasks[i] = player->getPlayerMask();
	}

	CellValueProcParms parms;
	parms.valueRequired = valueRequired;
	parms.greaterThan = valueRequired;
	parms.valueType = valType;
	parms.allowedPlayersMasks = playerMask;
	for (i = 0; i < MAX_PLAYER_COUNT; ++i)
		parms.allPlayersMask[i] = allPlayerMasks[i];

	Int nearestGreat = iterateCellsBreadthFirst(sourceLocation, cellValueProc, &parms);
	if (nearestGreat != -1) {
		(*outLocation).x = m_cells[nearestGreat].getCellX() * TheGlobalData->m_partitionCellSize;
		(*outLocation).y = m_cells[nearestGreat].getCellY() * TheGlobalData->m_partitionCellSize;
		(*outLocation).z = 0;
	}

	// all done
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::storeFoggedCells(ShroudStatusStoreRestore &outPartitionStore, Bool storeToFog) const
{
	Int i, j, p;

	if (storeToFog) {
		// This is the first pass
		outPartitionStore.m_cellsWide = m_cellCountX;

		// Resize all of our arrays.
		for (p = 0; p < MAX_PLAYER_COUNT; ++p) {
			outPartitionStore.m_foggedOrRevealed[p].resize(m_totalCellCount);
			for (i = 0; i < m_totalCellCount; ++i) {
				outPartitionStore.m_foggedOrRevealed[p][i] = STORE_DONTTOUCH;
			}
		}
	}

	for (p = 0; p < MAX_PLAYER_COUNT; ++p) {
		if (outPartitionStore.m_foggedOrRevealed[p].size() != m_totalCellCount) {
			DEBUG_CRASH(("PartitionManager::storeFoggedCells: jkmcd - x36872"));
			continue;
		}

		for (j = 0; j < m_cellCountY; ++j) {
			for (i = 0; i < m_cellCountX; ++i) {
				UnsignedByte &byteToWrite = outPartitionStore.m_foggedOrRevealed[p][j * m_cellCountX + i];

				if (storeToFog && m_cells[j * m_cellCountX + i].getShroudStatusForPlayer(p) == CELLSHROUD_FOGGED) {
					byteToWrite = STORE_FOG;
				}

				if (!storeToFog && m_cells[j * m_cellCountX + i].getShroudStatusForPlayer(p) == CELLSHROUD_CLEAR) {
					byteToWrite = STORE_PERMANENTLY_REVEALED;
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::restoreFoggedCells(const ShroudStatusStoreRestore &inPartitionStore, Bool restoreToFog)
{
	Int i, j, p;

	Int storeWidth = inPartitionStore.m_cellsWide;
	Int storeHeight = inPartitionStore.m_foggedOrRevealed[0].size() / storeWidth;

	for (p = 0; p < MAX_PLAYER_COUNT; ++p) {
		for (j = 0; j < storeHeight; ++j) {
			if (j >= m_cellCountY) {
				// All done.
				return;
			}

			for (i = 0; i < storeWidth; ++i) {
				if (i >= m_cellCountX) {
					// Skip to the next line. This info will be thrown away.
					break;
				}

				UnsignedByte byteToRestore = inPartitionStore.m_foggedOrRevealed[p][j * storeWidth + i];
				if (byteToRestore == STORE_DONTTOUCH) {
					continue;
				}

				if (byteToRestore == STORE_FOG && restoreToFog) {
					// restore the fog status of this cell
					m_cells[j * m_cellCountX + i].addLooker(p);
					m_cells[j * m_cellCountX + i].removeLooker(p);
				}

				if (byteToRestore == STORE_PERMANENTLY_REVEALED && !restoreToFog) {
					// Add an extra looker.
					m_cells[j * m_cellCountX + i].addLooker(p);
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionFilterRejectBuildings::PartitionFilterRejectBuildings(const Object *o) :
	m_self(o),
	m_acquireEnemies(false)
{
	// if I am a computer-controlled opponent, auto-acquire enemy buildings
	if (m_self->getControllingPlayer()->getPlayerType() == PLAYER_COMPUTER)
	{
		m_acquireEnemies = true;
	}
}

//-----------------------------------------------------------------------------
Bool PartitionFilterRejectBuildings::allow( Object *other )
{
	// this filter allows all non-buildings
	// um, no, it clearly doesn't any more.
	// didn't the person adding the code below read the comment?
	if (!other->isKindOf( KINDOF_STRUCTURE ))
		return true;

	const Player* myPlayer = m_self->getControllingPlayer();
	if (!myPlayer)
		return false;

	// Get the controlling team of other.
	ContainModuleInterface* contain = other->getContain();
	const Player* otherPlayer = contain ? contain->getApparentControllingPlayer(myPlayer) : nullptr;

	if (!otherPlayer)
		otherPlayer = other->getControllingPlayer();

	if (!otherPlayer)
		return false;

	Relationship relationship = myPlayer->getRelationship(otherPlayer->getDefaultTeam());
	if (relationship != ENEMIES)
		return false;

	// if I am a computer-controlled opponent, auto-acquire enemy buildings (if we can see them!)
	if (m_acquireEnemies)
		return true;

	if (other->isKindOf( KINDOF_FS_BASE_DEFENSE))
	{
		// Don't reject base defenses.
		return true;
	}

	if (other->getContain() != nullptr && other->isAbleToAttack())
	{
		// Don't reject garrisoned buildings that can attack
		return true;
  }

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterFreeOfFog::allow( Object *other )
{
	return other->getShroudedStatus(m_comparisonIndex) == OBJECTSHROUD_CLEAR;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterInsignificantBuildings::allow( Object *other )
{
	if (other->isStructure()) {
		if (other->isNonFactionStructure() && !m_allowInsignificant) {
			ContainModuleInterface *cmi = other->getContain();
			if (cmi) {
				if (!cmi->isGarrisonable() || cmi->getContainCount() == 0) {
					return false;
				}
			}
		}
		return true;

	} else {
		if (m_allowNonBuildings) {
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------

Bool PartitionFilterRepulsor::allow( Object *other )
{
	if (other == m_self)
	{
		// don't repulse yourself. :)
		return false;
	}

	// If it's flagged, it's a repulsor.
	if (other->testStatus(OBJECT_STATUS_REPULSOR))
	{
		return	true;
	}

	if (other->isEffectivelyDead())
		return false; // no dead enemies.

	Relationship r = m_self->getRelationship(other);
	if (r != ENEMIES)
	{
		return false; // only enemies auto repulse.
	}

	if (other->isKindOf( KINDOF_STRUCTURE ))
	{
		// always pay attention to buildings that can attack
		if (other->isAbleToAttack())
			return true;
		return false;
	}

	if ( other->isKindOf( KINDOF_INERT ))
		return false;


	if ( ! other->isAbleToAttack() )
	{
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

Bool PartitionFilterIrregularArea::allow( Object *other )
{

	return PointInsideArea2D(other->getPosition(), m_area, m_numPointsInArea);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPolygonTrigger::allow( Object *other )
{
	ICoord3D iPos;
	iPos.x = other->getPosition()->x;
	iPos.y = other->getPosition()->y;
	iPos.z = 0; // Trigger areas compare on xy only.
	return m_trigger->pointInTrigger(iPos);
}

//-----------------------------------------------------------------------------

Bool PartitionFilterPlayer::allow( Object *other )
{
	return ((m_player == other->getControllingPlayer()) == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPlayerAffiliation::allow( Object *other )
{
	Relationship rel = m_player->getRelationship(other->getTeam());
	switch (rel)
	{
		case ENEMIES:
			if (m_affiliation & ALLOW_ENEMIES) {
				return m_match;
			}
			break;

		case NEUTRAL:
			if (m_affiliation & ALLOW_NEUTRAL) {
				return m_match;
			}
			break;

		case ALLIES:
			if (m_affiliation & ALLOW_ALLIES) {
				return m_match;
			}
			break;
	}

	if (other->getControllingPlayer() == m_player) {
		return m_match;
	}

	return !m_match;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterThing::allow( Object *other )
{
	return (m_tThing->isEquivalentTo(other->getTemplate()) == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterGarrisonable::allow( Object *other )
{
	ContainModuleInterface *cmi = other->getContain();
	if (!cmi) {
		return !m_match;
	}

	return (cmi->isGarrisonable() == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterGarrisonableByPlayer::allow( Object *other )
{
	return TheActionManager->canPlayerGarrison(m_player, other, m_commandSource) == m_match;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterUnmannedObject::allow( Object *other )
{
	return (other->isDisabledByType( DISABLED_UNMANNED ) == m_match);
}

//-----------------------------------------------------------------------------
Bool PartitionFilterValidCommandButtonTarget::allow( Object *other )
{
	return (m_commandButton->isValidToUseOn(m_source, other, nullptr, m_commandSource) == m_match);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterIsFlying::allow(Object *objOther)
{
	return objOther->isUsingAirborneLocomotor();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionFilterWouldCollide::PartitionFilterWouldCollide(const Coord3D& pos, const GeometryInfo& geom, Real angle, Bool desired) :
  m_position(pos),
	m_geom(geom),
  m_angle(angle),
  m_desiredCollisionResult(desired)
{
}

//-----------------------------------------------------------------------------

Bool PartitionFilterWouldCollide::allow(Object *objOther)
{
	CollideInfo thisInfo(&m_position, m_geom, m_angle);
	CollideInfo thatInfo(objOther->getPosition(), objOther->getGeometryInfo(), objOther->getOrientation());

  Bool doesCollide;

	// invariant for all geometries: first do z collision check.
	if (thisInfo.position.z + thisInfo.geom.getMaxHeightAbovePosition() >= thatInfo.position.z &&
			thisInfo.position.z <= thatInfo.position.z + thatInfo.geom.getMaxHeightAbovePosition())
	{
		GeometryType thisGeom = m_geom.getGeomType();
		GeometryType thatGeom = objOther->getGeometryInfo().getGeomType();

		//
		// NOTE: This assumes geometry enumerations that start at GEOMETRY_FIRST AND depends on the
		// order in which they appear in the enum list
		//
		CollideTestProc collideProc = theCollideTestProcs[ (thisGeom - GEOMETRY_FIRST) * GEOMETRY_NUM_TYPES + (thatGeom - GEOMETRY_FIRST) ];
		CollideLocAndNormal cinfo;
		doesCollide = (*collideProc)(&thisInfo, &thatInfo, &cinfo);
	}
	else
	{
		// no z-intersection -> no collision.
		doesCollide = false;
	}

  return doesCollide == m_desiredCollisionResult;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterSamePlayer::allow(Object *objOther)
{
	if (m_player == objOther->getControllingPlayer())
		return true;

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterRelationship::allow(Object *objOther)
{
	Relationship r = m_obj->getRelationship(objOther);

	if ((m_flags & (1<<r)) != 0)
		return true;

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterAcceptOnTeam::PartitionFilterAcceptOnTeam(const Team *team) : m_team(team)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterAcceptOnTeam::allow(Object *objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	return (objOther->getTeam() == m_team);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterAcceptOnSquad::PartitionFilterAcceptOnSquad(const Squad *squad) : m_squad(squad)
{
}

//-----------------------------------------------------------------------------

Bool PartitionFilterAcceptOnSquad::allow(Object *objOther)
{
	return (m_squad && m_squad->isOnSquad(objOther) && !objOther->isEffectivelyDead());
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterSameMapStatus::allow(Object* objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	return objOther->isOffMap() == m_obj->isOffMap();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterOnMap::allow(Object* objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	return !objOther->isOffMap();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Bool PartitionFilterAlive::allow( Object *objOther )
{
	return !objOther->isEffectivelyDead();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterRejectBehind::PartitionFilterRejectBehind( Object *obj )
{
	m_obj = obj;
}

//-----------------------------------------------------------------------------

Bool PartitionFilterRejectBehind::allow( Object *other )
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)

//const Coord3D *pos = m_obj->getPosition();
//const Coord3D *dir = m_obj->getUnitDirectionVector2D();
	Vector3 dir = m_obj->getTransformMatrix()->Get_X_Vector();
	dir.Normalize();
//const Coord3D *otherPos = other->getPosition();

	Coord3D v;
	ThePartitionManager->getVectorTo( m_obj, other, FROM_CENTER_3D, v );

	Real dot = dir.X * v.x + dir.Y * v.y + dir.Z * v.z;

	if (dot > 0.0f)
		return true;

	return false;
}



//-----------------------------------------------------------------------------
PartitionFilterLineOfSight::PartitionFilterLineOfSight(const Object *obj)
{
	m_obj = obj;
}

//-----------------------------------------------------------------------------
Bool PartitionFilterLineOfSight::allow(Object *objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)

	if (!ThePartitionManager->isClearLineOfSightTerrain(m_obj, *m_obj->getPosition(), objOther, *objOther->getPosition()))
		return false;

	if (TheAI && TheAI->pathfinder()->isViewBlockedByObstacle(m_obj, objOther))
		return false;

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionFilterPossibleToAttack::PartitionFilterPossibleToAttack(AbleToAttackType t, const Object *obj, CommandSourceType commandSource) :
	m_attackType(t),
	m_obj(obj),
	m_commandSource(commandSource)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPossibleToAttack::allow(Object *objOther)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)

	// we should have already filtered out isAbleToAttack!
	DEBUG_ASSERTCRASH(m_obj->isAbleToAttack(), ("if the object is unable to attack at all, you should filter that out ahead of time!"));

	CanAttackResult result = m_obj->getAbleToAttackSpecificObject( m_attackType, objOther, m_commandSource );
	if( result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
	{
		return TRUE;
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterPossibleToEnter::PartitionFilterPossibleToEnter(const Object *obj, CommandSourceType commandSource) :
	m_obj(obj),
	m_commandSource(commandSource)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPossibleToEnter::allow(Object *objOther)
{
	if (!objOther || !m_obj)
		return FALSE;

	if( TheActionManager->canEnterObject( m_obj, objOther, m_commandSource, DONT_CHECK_CAPACITY ) )
		return TRUE;
	else
		return FALSE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
PartitionFilterPossibleToHijack::PartitionFilterPossibleToHijack(const Object *obj, CommandSourceType commandSource) :
	m_obj(obj),
	m_commandSource(commandSource)
{
}

//-----------------------------------------------------------------------------
Bool PartitionFilterPossibleToHijack::allow(Object *objOther)
{
	if (!objOther || !m_obj)
		return FALSE;

	if( TheActionManager->canHijackVehicle(m_obj, objOther, m_commandSource) )
		return TRUE;
	else
		return FALSE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
PartitionFilterLastAttackedBy::PartitionFilterLastAttackedBy(Object *obj)
{
	if (obj && obj->getBodyModule()) {
		m_lastAttackedBy = obj->getBodyModule()->getLastDamageInfo()->in.m_sourceID;
	} else {
		m_lastAttackedBy = INVALID_ID;
	}
}


//-----------------------------------------------------------------------------

Bool PartitionFilterLastAttackedBy::allow(Object *other)
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)
	if (other->getID() == m_lastAttackedBy)
		return true;

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterAcceptByObjectStatus::allow(Object *objOther)
{
	ObjectStatusMaskType status = objOther->getStatusBits();
	return status.testForAll( m_mustBeSet ) && status.testForNone( m_mustBeClear );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterRejectByObjectStatus::allow(Object *objOther)
{
	ObjectStatusMaskType status = objOther->getStatusBits();

	return !( status.testForAll( m_mustBeSet ) && status.testForNone( m_mustBeClear ) );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterAcceptByKindOf::allow(Object *objOther)
{
	return objOther->isKindOfMulti(m_mustBeSet, m_mustBeClear);
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterRejectByKindOf::allow(Object *objOther)
{
	return !objOther->isKindOfMulti(m_mustBeSet, m_mustBeClear);
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
Bool PartitionFilterStealthedAndUndetected::allow( Object *objOther )
{
	// objOther is guaranteed to be non-null, so we don't need to check (srj)

	Bool stealthed = objOther->testStatus( OBJECT_STATUS_STEALTHED );
	Bool detected = objOther->testStatus( OBJECT_STATUS_DETECTED );
	Bool disguised = objOther->testStatus( OBJECT_STATUS_DISGUISED );

	if( stealthed && !detected )
	{
		if( !objOther->isKindOf( KINDOF_DISGUISER ) )
		{
			//We have a stealthed & undetected object.
			return m_allow;
		}
		else if( disguised )
		{
			//Exception case -- bomb trucks can't be considered stealthed units when they are disguised as the enemy.

      StealthUpdate *update = objOther->getStealth();

			if( update && update->isDisguised() )
			{
				Player *ourPlayer = m_obj->getControllingPlayer();
				Player *otherPlayer = ThePlayerList->getNthPlayer( update->getDisguisedPlayerIndex() );
				if( ourPlayer && otherPlayer )
				{
					if( ourPlayer->getRelationship( otherPlayer->getDefaultTeam() ) == ENEMIES )
					{
						//Our stealthed & undetected object is disguised as a unit perceived to be our enemy,
						//therefore it's not considered to be stealthed!
						return !m_allow;
					}
					//Our object is disguised as a "friendly" object, therefore it's considered to be stealthed.
					return m_allow;
				}
			}
			//Our bomb truck is not disguised therefore it's not stealthed.
			return !m_allow;
		}
	}
	else
	{
		//This handles neutral containers that hold stealth units. This specifically fixes a bug where hunt scripts would ignore
		//this case -- units would acquire the building Jarmen Kell occupied even though it was not stealth detected.
		const ContainModuleInterface* contain = objOther->getContain();
		if( contain && !contain->getContainedItemsList()->empty() )
		{
			const Player* victimApparentController = contain->getApparentControllingPlayer( m_obj->getControllingPlayer() );
			//Check if it's stealthed!
			if( contain->getStealthUnitsContained() == contain->getContainCount() )
			{
				//Check if the first object inside is detected (if one is detected, all are detected).
				ContainedItemsList::const_iterator it = contain->getContainedItemsList()->begin();
				Object *member = (*it);
				if( member && !(*it)->getStatusBits().test( OBJECT_STATUS_DETECTED ) )
				{
					//Finally check the relationship!
					if( victimApparentController && m_obj->getTeam()->getRelationship( victimApparentController->getDefaultTeam() ) == ENEMIES )
					{
						//Our object is a neutral building garrisoned by enemy units we can't see, therefore it is stealthed.
						return m_allow;
					}
				}
			}
		}
	}

	//The unit is not stealthed!
	return !m_allow;
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
static int cellValueProc(PartitionCell* cell, void* userData)
{
	CellValueProcParms *parms = (CellValueProcParms*) userData;

	UnsignedInt val = 0;
	for (Int i = 0; i < MAX_PLAYER_COUNT; ++i) {
		if (BitIsSet(parms->allowedPlayersMasks, parms->allPlayersMask[i])) {
			if (parms->valueType == VOT_CashValue) {
				val += cell->getCashValue(i);
			} else {
				val += cell->getThreatValue(i);
			}
		}
	}

	if ((val > parms->valueRequired && parms->greaterThan) ||
			(val < parms->valueRequired && !parms->greaterThan)) {
		return 1;
	}

	return 0;
}

// -----------------------------------------------------------------------------
void hLineAddLooker(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;
	gxCount(GXC_FOG_CELLS, min(x2, ThePartitionManager->m_cellCountX - 1) - max(x1, 0) + 1);	// tickTiming

	Int playerIndex = (Int)(playerIndexVoid);

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->addLooker(playerIndex);
	}
}

// -----------------------------------------------------------------------------
void hLineRemoveLooker(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;
	gxCount(GXC_FOG_CELLS, min(x2, ThePartitionManager->m_cellCountX - 1) - max(x1, 0) + 1);	// tickTiming

	Int playerIndex = (Int)(playerIndexVoid);

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->removeLooker(playerIndex);
	}
}

// -----------------------------------------------------------------------------
void hLineAddShrouder(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	Int playerIndex = (Int)(playerIndexVoid);

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->addShrouder( playerIndex );
	}
}

// -----------------------------------------------------------------------------
void hLineRemoveShrouder(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	Int playerIndex = (Int)(playerIndexVoid);

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->removeShrouder( playerIndex );
	}
}

// -----------------------------------------------------------------------------
void hLineAddThreat(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = (ThreatValueParms*)threatValueParms;

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = sqrt( pow(x - parms->xCenter, 2) + pow(y - parms->yCenter, 2) );
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f)
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;

		cell->addThreatValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// -----------------------------------------------------------------------------
void hLineRemoveThreat(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = (ThreatValueParms*)threatValueParms;

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = sqrt( pow(x - parms->xCenter, 2) + pow(y - parms->yCenter, 2) );
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f)
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;

		cell->removeThreatValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// -----------------------------------------------------------------------------
void hLineAddValue(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = (ThreatValueParms*)threatValueParms;

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = sqrt( pow(x - parms->xCenter, 2) + pow(y - parms->yCenter, 2) );
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f)
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;

		cell->addCashValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// -----------------------------------------------------------------------------
void hLineRemoveValue(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = (ThreatValueParms*)threatValueParms;

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = sqrt( pow(x - parms->xCenter, 2) + pow(y - parms->yCenter, 2) );
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f)
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;

		cell->removeCashValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SightingInfo::SightingInfo()
{
	reset();
}

// ------------------------------------------------------------------------------------------------
/** Resetting sighting info */
// ------------------------------------------------------------------------------------------------
void SightingInfo::reset()
{
	m_where.zero();
	m_howFar = 0.0f;
	m_forWhom = 0;
	m_data = 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool SightingInfo::isInvalid() const
{
	return m_howFar == 0.0f;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SightingInfo::crc( Xfer *xfer )
{

}

// ------------------------------------------------------------------------------------------------
/** Xfer Method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SightingInfo::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// where
	xfer->xferCoord3D( &m_where );

	// how far
	xfer->xferReal( &m_howFar );

	// for whom
	xfer->xferUser( &m_forWhom, sizeof( PlayerMaskType ) );

	// how much
	xfer->xferUnsignedInt( &m_data );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SightingInfo::loadPostProcess()
{

}

// ------------------------------------------------------------------------------------------------
SightingInfo::~SightingInfo()
{

}

