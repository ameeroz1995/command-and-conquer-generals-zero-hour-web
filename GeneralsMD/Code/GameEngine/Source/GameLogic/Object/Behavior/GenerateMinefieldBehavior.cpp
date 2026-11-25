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

// FILE: GenerateMinefieldBehavior.cpp ///////////////////////////////////////////////////////////////////////
// Author:
// Desc:
///////////////////////////////////////////////////////////////////////////////////////////////////


// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "Common/GxUnitStore.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/UpdateModule.h"
#define DEFINE_SLOWDEATHPHASE_NAMES

#include "Common/GlobalData.h"
#include "Common/Thing.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/GenerateMinefieldBehavior.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"


//-------------------------------------------------------------------------------------------------
GenerateMinefieldBehaviorModuleData::GenerateMinefieldBehaviorModuleData()
{
	m_mineName.clear();
	m_mineNameUpgraded.clear();
	m_mineUpgradeTrigger.clear();

	m_genFX = nullptr;
	m_distanceAroundObject = TheGlobalData->m_standardMinefieldDistance;
	m_minesPerSquareFoot = TheGlobalData->m_standardMinefieldDensity;
	m_onDeath = false;
	m_borderOnly = true;
	m_alwaysCircular = false;
	m_upgradable = false;
	m_smartBorder = false;
	m_smartBorderSkipInterior = true;
	m_randomJitter = 0.0f;
	m_skipIfThisMuchUnderStructure = 0.33f;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void GenerateMinefieldBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{

	static const FieldParse dataFieldParse[] =
	{
		{ "MineName", INI::parseAsciiString,	nullptr, offsetof( GenerateMinefieldBehaviorModuleData, m_mineName ) },
		{ "UpgradedMineName", INI::parseAsciiString,	nullptr, offsetof( GenerateMinefieldBehaviorModuleData, m_mineNameUpgraded ) },
		{ "UpgradedTriggeredBy", INI::parseAsciiString,	nullptr, offsetof( GenerateMinefieldBehaviorModuleData, m_mineUpgradeTrigger ) },
		{ "GenerationFX", INI::parseFXList,	nullptr, offsetof( GenerateMinefieldBehaviorModuleData, m_genFX ) },
		{ "DistanceAroundObject", INI::parseReal, nullptr, offsetof( GenerateMinefieldBehaviorModuleData, m_distanceAroundObject ) },
		{ "MinesPerSquareFoot", INI::parseReal, nullptr, offsetof( GenerateMinefieldBehaviorModuleData, m_minesPerSquareFoot ) },
		{ "GenerateOnlyOnDeath", INI::parseBool, nullptr, offsetof(GenerateMinefieldBehaviorModuleData, m_onDeath) },
		{ "BorderOnly", INI::parseBool, nullptr, offsetof(GenerateMinefieldBehaviorModuleData, m_borderOnly) },
		{ "SmartBorder", INI::parseBool, nullptr, offsetof(GenerateMinefieldBehaviorModuleData, m_smartBorder) },
		{ "SmartBorderSkipInterior", INI::parseBool, nullptr, offsetof(GenerateMinefieldBehaviorModuleData, m_smartBorderSkipInterior) },
		{ "AlwaysCircular", INI::parseBool, nullptr, offsetof(GenerateMinefieldBehaviorModuleData, m_alwaysCircular) },
		{ "Upgradable", INI::parseBool, nullptr, offsetof(GenerateMinefieldBehaviorModuleData, m_upgradable) },
		{ "RandomJitter", INI::parsePercentToReal, nullptr, offsetof(GenerateMinefieldBehaviorModuleData, m_randomJitter) },
		{ "SkipIfThisMuchUnderStructure", INI::parsePercentToReal, nullptr, offsetof(GenerateMinefieldBehaviorModuleData, m_skipIfThisMuchUnderStructure) },
		{ nullptr, nullptr, nullptr, 0 }
	};

  BehaviorModuleData::buildFieldParse(p);
  p.add(dataFieldParse);
  p.add(UpgradeMuxData::getFieldParse(), offsetof( GenerateMinefieldBehaviorModuleData, m_upgradeMuxData ));
}

//-------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Web port (round 5, units are pure data): the per-unit state is a row of TheGxMinefields; the old
// member names below read and write this unit's row, so the logic and xfer/CRC order are unchanged.
// ------------------------------------------------------------------------------------------------
GxComponentTable<GxMinefieldRow> TheGxMinefields;
#define m_target		(TheGxMinefields.at(m_gxRow).target)
#define m_hasTarget		(TheGxMinefields.at(m_gxRow).hasTarget)
#define m_generated		(TheGxMinefields.at(m_gxRow).generated)
#define m_upgraded		(TheGxMinefields.at(m_gxRow).upgraded)
#define m_mineList		(TheGxMinefields.at(m_gxRow).mineList)

// ------------------------------------------------------------------------------------------------
// Web port (round 5 addendum): the minefield system as kernel + apply, on store behaviour bit 56
// (set for upgradable minefields until they are upgraded). Under the updatePasses rule the kernel and
// apply replace the module's per-frame poll; without it the module's update() polls as before.
// kernel reads: store obj[], the object's completed-upgrade mask, row.generated/upgraded;
//        writes: row.gxReplace only.
// apply (main thread, object-ID order): replaces the mines of the flagged rows.
// ------------------------------------------------------------------------------------------------
static const Int GXB_AI_MINEFIELD = 56;
static std::vector<GxMinefieldRow *> s_gxMinefieldById;	// object ID -> row (rebuilt if an ID moved)
static const UpgradeTemplate *gxEmpMinesUpgrade();
static const UpgradeMaskType *s_gxEmpMask = nullptr;	// set by apply on the main thread, read by the kernel

static Bool gxMinefieldRule()
{
	extern bool gxMatchRule(unsigned int rule);
	return gxMatchRule(131072);
}
void gxMinefieldNote(const Object *obj, GxMinefieldRow *row)
{
	const Int id = (Int)obj->getID();
	if (id >= (Int)s_gxMinefieldById.size()) s_gxMinefieldById.resize(id + 1, nullptr);
	s_gxMinefieldById[id] = row;
	TheGxUnitStore.setMask(obj, GXB_BIT(GXB_AI_MINEFIELD));
}
void gxMinefieldForget(const Object *obj, GxMinefieldRow *row)
{
	const Int id = (Int)obj->getID();
	if (id < (Int)s_gxMinefieldById.size() && s_gxMinefieldById[id] == row) s_gxMinefieldById[id] = nullptr;
	TheGxUnitStore.setMask(obj, 0, GXB_BIT(GXB_AI_MINEFIELD));
}
static GxMinefieldRow *gxMinefieldRowOf(Object *obj)
{
	const Int id = (Int)obj->getID();
	GxMinefieldRow *r = id < (Int)s_gxMinefieldById.size() ? s_gxMinefieldById[id] : nullptr;
	if (r && r->facade && r->owner == obj) return r;
	return nullptr;	// (after a load the map is refilled by apply, see below)
}
static void gxMinefieldKernel(const Int *slots, Int begin, Int end)
{
	if (!s_gxEmpMask) return;
	for (Int k = begin; k < end; ++k)
	{
		Object *obj = TheGxUnitStore.obj[slots[k]];
		if (!obj) continue;
		GxMinefieldRow *r = gxMinefieldRowOf(obj);
		if (!r) continue;
		r->gxReplace = r->generated && !r->upgraded && obj->getObjectCompletedUpgradeMask().testForAny(*s_gxEmpMask);
	}
}
static void gxMinefieldApply(const Int *slots, Int count)
{
	if (!gxMinefieldRule()) { s_gxEmpMask = nullptr; return; }
	const UpgradeTemplate *t = gxEmpMinesUpgrade();
	const Bool firstFrame = (s_gxEmpMask == nullptr);
	s_gxEmpMask = t ? &t->getUpgradeMask() : nullptr;
	if (firstFrame)
	{
		// the rule just came on (or a load): refill the ID map from the rows and run the kernel now
		TheGxMinefields.forEach([](const Object *o, GxMinefieldRow &r) { if (r.facade) gxMinefieldNote(o, &r); });
		gxMinefieldKernel(slots, 0, count);
	}
	for (Int k = 0; k < count; ++k)
	{
		Object *obj = TheGxUnitStore.obj[slots[k]];
		if (!obj) continue;
		GxMinefieldRow *r = gxMinefieldRowOf(obj);
		if (r && r->gxReplace)
		{
			r->gxReplace = false;
			r->facade->gxReplaceMines();
		}
	}
}
static void gxMinefieldRegister()
{
	static Bool done = false;
	if (done) return;
	done = true;
	GameLogic::gxRegisterKernel(GXB_AI_MINEFIELD, PHASE_NORMAL, gxMinefieldKernel, gxMinefieldApply);
}

//-------------------------------------------------------------------------------------------------
GenerateMinefieldBehavior::GenerateMinefieldBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_gxRow = TheGxMinefields.alloc( getObject() );	// a fresh row: target zero, nothing generated or upgraded, no mines
	m_gxRow->facade = this;
	m_gxRow->owner = getObject();
	gxMinefieldRegister();
	if (getGenerateMinefieldBehaviorModuleData()->m_upgradable)
		gxMinefieldNote(getObject(), m_gxRow);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
GenerateMinefieldBehavior::~GenerateMinefieldBehavior()
{
	gxMinefieldForget(getObject(), m_gxRow);
	TheGxMinefields.release( m_gxRow );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::upgradeImplementation()
{
	placeMines();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::onDie( const DamageInfo *damageInfo )
{
	const GenerateMinefieldBehaviorModuleData* d = getGenerateMinefieldBehaviorModuleData();

	if (d->m_onDeath)
	{
		placeMines();
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::setMinefieldTarget(const Coord3D* pos)
{
	if (pos)
	{
		m_hasTarget = true;
		m_target = *pos;
	}
	else
	{
		m_hasTarget = false;
		m_target.zero();
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const Coord3D* GenerateMinefieldBehavior::getMinefieldTarget() const
{
	return m_hasTarget ? &m_target : getObject()->getPosition();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
static Bool isAnythingTooClose2D(const std::vector<Object*>& v, const Coord3D& pos, Real minDistSqr)
{
	for (std::vector<Object*>::const_iterator it = v.begin(); it != v.end(); ++it)
	{
		const Coord3D* p = (*it)->getPosition();
		Real distSqr = sqr(p->x - pos.x) + sqr(p->y - pos.y);
		if (distSqr < minDistSqr)
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
static void offsetBySmallRandomAmount(Coord3D& pt, Real maxAmt)
{
	pt.x += GameLogicRandomValueReal(-maxAmt, maxAmt);
	pt.y += GameLogicRandomValueReal(-maxAmt, maxAmt);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Object* GenerateMinefieldBehavior::placeMineAt(const Coord3D& pt, const ThingTemplate* mineTemplate, Team* team, const Object* producer)
{
	Coord3D tmp = pt;
	tmp.z = 99999.0f;
	PathfindLayerEnum layer = TheTerrainLogic->getHighestLayerForDestination(&tmp);

	if (layer == LAYER_GROUND && TheTerrainLogic->isUnderwater(pt.x, pt.y))
		return nullptr;

	if (layer == LAYER_GROUND && TheTerrainLogic->isCliffCell(pt.x, pt.y))
		return nullptr;

	Real orient = GameLogicRandomValueReal(-PI, PI);

	// if the mine will be "mostly" under a structure, don't place it.
	// for now, "mostly" means "central third of radius would overlap"
	const GenerateMinefieldBehaviorModuleData* d = getGenerateMinefieldBehaviorModuleData();
	GeometryInfo geom = mineTemplate->getTemplateGeometryInfo();
	Real mineRadius = mineTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
	geom.expandFootprint(mineRadius * -(1.0f - d->m_skipIfThisMuchUnderStructure));
	ObjectIterator *iter = ThePartitionManager->iteratePotentialCollisions( &pt, geom, orient );
	MemoryPoolObjectHolder hold(iter);
	for (Object* them = iter->first(); them; them = iter->next())
	{
		if (them->isKindOf(KINDOF_STRUCTURE))
			return nullptr;
	}

	Object* mine = TheThingFactory->newObject(mineTemplate, team);
	mine->setPosition(&pt);
	mine->setOrientation(orient);
	mine->setProducer(producer);

	for (BehaviorModule** bmi = mine->getBehaviorModules(); *bmi; ++bmi)
	{
		LandMineInterface* lmi = (*bmi)->getLandMineInterface();
		if (lmi)
		{
			lmi->setScootParms(*producer->getPosition(), pt);
			break;
		}
	}

	// Keep track of the mines
	if (mine && d->m_upgradable)
	{
		m_mineList.push_back(mine->getID());
	}

	return mine;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::placeMinesAlongLine(const Coord3D& posStart, const Coord3D& posEnd, const ThingTemplate* mineTemplate, Bool skipOneAtStart)
{
	const Object* obj = getObject();
	const GenerateMinefieldBehaviorModuleData* d = getGenerateMinefieldBehaviorModuleData();
	Team* team = obj->getControllingPlayer()->getDefaultTeam();

	Real dx = posEnd.x - posStart.x;
	Real dy = posEnd.y - posStart.y;
	Real len = sqrt(sqr(dx) + sqr(dy));
	Real mineRadius = mineTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
	Real mineDiameter = mineRadius * 2.0f;
	Real mineJitter = mineRadius*d->m_randomJitter;
	Int numMines = REAL_TO_INT_CEIL(len / mineDiameter);
	if (numMines < 1)
		numMines = 1;
	Real inc = len/numMines;
	for (Real place = skipOneAtStart ? inc : 0; place <= len; place += inc)
	{
		Coord3D pt;
		pt.x = posStart.x + place * dx / len;
		pt.y = posStart.y + place * dy / len;
		pt.z = TheTerrainLogic->getGroundHeight( pt.x, pt.y );
		offsetBySmallRandomAmount(pt, mineJitter);
		placeMineAt(pt, mineTemplate, team, obj);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
static void makeCorner(const Coord3D& pos, Real majorRadius, Real minorRadius, const Matrix3D& mtx, Coord3D& corner)
{
	Vector3 tmp;
	tmp.X = majorRadius;
	tmp.Y = minorRadius;
	tmp.Z = 0;
	Matrix3D::Transform_Vector(mtx, tmp, &tmp);
	corner.x = tmp.X;
	corner.y = tmp.Y;
	corner.z = tmp.Z;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::placeMinesAroundRect(const Coord3D& pos, Real majorRadius, Real minorRadius, const ThingTemplate* mineTemplate)
{
	const Object* obj = getObject();
	const Matrix3D* mtx = obj->getTransformMatrix();

	Coord3D pt[4];
	makeCorner(pos,  majorRadius,  minorRadius, *mtx, pt[0]);
	makeCorner(pos, -majorRadius,  minorRadius, *mtx, pt[1]);
	makeCorner(pos, -majorRadius, -minorRadius, *mtx, pt[2]);
	makeCorner(pos,  majorRadius, -minorRadius, *mtx, pt[3]);

	placeMinesAlongLine(pt[0], pt[1], mineTemplate, true);
	placeMinesAlongLine(pt[1], pt[2], mineTemplate, true);
	placeMinesAlongLine(pt[2], pt[3], mineTemplate, true);
	placeMinesAlongLine(pt[3], pt[0], mineTemplate, true);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::placeMinesAroundCircle(const Coord3D& pos, Real radius, const ThingTemplate* mineTemplate)
{
	const Object* obj = getObject();
	const GenerateMinefieldBehaviorModuleData* d = getGenerateMinefieldBehaviorModuleData();
	Team* team = obj->getControllingPlayer()->getDefaultTeam();

	Real circum = 2.0f * PI * radius;
	Real mineRadius = mineTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
	Real mineDiameter = mineRadius * 2.0f;
	Real mineJitter = mineRadius*d->m_randomJitter;
	Int numMines = REAL_TO_INT_CEIL(circum / mineDiameter);
	if (numMines < 1)
		numMines = 1;
	Real angleInc = (2*PI)/numMines;
	Real angleLim = (2*PI) - angleInc*0.5f;
	for (Real angle = 0; angle < angleLim; angle += angleInc)
	{
		Coord3D pt;
		pt.x = pos.x + radius * Cos(angle);
		pt.y = pos.y + radius * Sin(angle);
		pt.z = TheTerrainLogic->getGroundHeight( pt.x, pt.y );
		offsetBySmallRandomAmount(pt, mineJitter);
		placeMineAt(pt, mineTemplate, team, obj);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::placeMinesInFootprint(const GeometryInfo& geom, const ThingTemplate* mineTemplate)
{
	const Object* obj = getObject();
	const GenerateMinefieldBehaviorModuleData* d = getGenerateMinefieldBehaviorModuleData();
	Team* team = obj->getControllingPlayer()->getDefaultTeam();

	Real area = geom.getFootprintArea();
	Int numMines = REAL_TO_INT_CEIL(d->m_minesPerSquareFoot * area);
	if (numMines < 1)
		numMines = 1;

	const Coord3D* target = getMinefieldTarget();
	std::vector<Object*> minesCreatedSoFar;
	Real minDistSqr = sqr(mineTemplate->getTemplateGeometryInfo().getBoundingCircleRadius() * 2.0f);
	for (int i = 0; i < numMines; ++i)
	{
		Coord3D pt;
		Int maxRetry = 100;
		do
		{
			geom.makeRandomOffsetWithinFootprint(pt);
			pt.x += target->x;
			pt.y += target->y;
			pt.z += target->z;
			--maxRetry;
		} while (isAnythingTooClose2D(minesCreatedSoFar, pt, minDistSqr) && maxRetry > 0);
		DEBUG_ASSERTCRASH(maxRetry>0,("ran out of retries %f",minDistSqr));

		if (getObject()->getGeometryInfo().isPointInFootprint(*target, pt))
			continue;

		Object* mine = placeMineAt(pt, mineTemplate, team, obj);	// can return null.
		if (mine)
			minesCreatedSoFar.push_back(mine);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::placeMines()
{
	if (m_generated)
		return;

	m_generated = true;

	const Object* obj = getObject();
	const GenerateMinefieldBehaviorModuleData* d = getGenerateMinefieldBehaviorModuleData();
	const ThingTemplate* mineTemplate = nullptr;

	if (m_upgraded)
		mineTemplate = TheThingFactory->findTemplate(d->m_mineNameUpgraded);
	else
		mineTemplate = TheThingFactory->findTemplate(d->m_mineName);

	if (!mineTemplate)
	{
		DEBUG_CRASH(("mine %s not found",d->m_mineName.str()));
		return;
	}

	const Coord3D* target = getMinefieldTarget();
	if (d->m_smartBorder)
	{
		GeometryInfo geom = obj->getGeometryInfo();

		if (!d->m_smartBorderSkipInterior)
		{
			geom = mineTemplate->getTemplateGeometryInfo();
			placeMineAt(*target, mineTemplate, obj->getControllingPlayer()->getDefaultTeam(), obj);
		}

		if (d->m_alwaysCircular)
			geom.set(GEOMETRY_CYLINDER, false, 1, geom.getBoundingCircleRadius(), geom.getBoundingCircleRadius());

		Real mineRadius = mineTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
		Real mineDiameter = mineRadius * 2.0f;
		geom.expandFootprint(mineRadius);
		do
		{
			if (geom.getGeomType() == GEOMETRY_BOX && !d->m_alwaysCircular)
			{
				placeMinesAroundRect(*target, geom.getMajorRadius(), geom.getMinorRadius(), mineTemplate);
			}
			else
			{
				placeMinesAroundCircle(*target, geom.getMajorRadius(), mineTemplate);
			}
			geom.expandFootprint(mineDiameter);
		} while (geom.getBoundingCircleRadius() < d->m_distanceAroundObject);
	}
	else if (d->m_borderOnly)
	{
		GeometryInfo geom = obj->getGeometryInfo();
		geom.expandFootprint(d->m_distanceAroundObject);

		if (geom.getGeomType() == GEOMETRY_BOX && !d->m_alwaysCircular)
		{
			placeMinesAroundRect(*target, geom.getMajorRadius(), geom.getMinorRadius(), mineTemplate);
		}
		else
		{
			placeMinesAroundCircle(*target, geom.getMajorRadius(), mineTemplate);
		}
	}
	else
	{
		GeometryInfo geom = obj->getGeometryInfo();
		geom.expandFootprint(d->m_distanceAroundObject);

		if (d->m_alwaysCircular)
			geom.set(GEOMETRY_CYLINDER, false, 1, geom.getBoundingCircleRadius(), geom.getBoundingCircleRadius());

		placeMinesInFootprint(geom, mineTemplate);
	}

	FXList::doFXObj(d->m_genFX, obj);

}

// ------------------------------------------------------------------------------------------------

// The minefield system: one step for one unit's row. Its only per-frame work is testing whether the
// EMP-mines upgrade has completed; the upgrade is looked up by name once instead of every frame
// (fourth switch word bit 6, same game: the upgrade list is fixed once the INI is loaded).
static const UpgradeTemplate *gxEmpMinesUpgrade()
{
	extern int GLES3_Page_Switches4();
	if (!(GLES3_Page_Switches4() & 64))
		return TheUpgradeCenter->findUpgrade( "Upgrade_ChinaEMPMines" );
	static const UpgradeCenter *s_center = nullptr;
	static const UpgradeTemplate *s_upgrade = nullptr;
	if (s_center != TheUpgradeCenter)
	{
		s_center = TheUpgradeCenter;
		s_upgrade = TheUpgradeCenter->findUpgrade( "Upgrade_ChinaEMPMines" );
	}
	extern int GLES3_Page_Switches();
	if (GLES3_Page_Switches() & 2097152)	// verify mode: the cached template is the one the lookup finds
	{
		extern int gx_verify_mismatch, gx_verify_checked;
		++gx_verify_checked;
		if (s_upgrade != TheUpgradeCenter->findUpgrade( "Upgrade_ChinaEMPMines" ))
		{
			++gx_verify_mismatch;
			printf("GXVERIFY minefield upgrade cache mismatch\n");
		}
	}
	return s_upgrade;
}

void GenerateMinefieldBehavior::gxReplaceMines()
{
	m_upgraded = TRUE;
	gxMinefieldForget(getObject(), m_gxRow);	// upgraded: no longer polled
	// Remove all old mine objects if present
	for (std::list<ObjectID>::iterator it = m_mineList.begin(); it != m_mineList.end(); ++it)
	{
		Object *obj = TheGameLogic->findObjectByID(*it);
		if (obj)
			TheGameLogic->destroyObject(obj);
	}
	m_mineList.clear();
	// Place new mines down (Replace old ones that we removed
	m_generated = false;
	placeMines();
}

UpdateSleepTime GenerateMinefieldBehavior::update()
{
	// Test to see if we need to replace the current mines with upgraded ones
	if (!m_upgraded && getGenerateMinefieldBehaviorModuleData()->m_upgradable)
	{
		if (gxMinefieldRule())
			return UPDATE_SLEEP_NONE;	// round 5: the minefield system's kernel + apply do the poll and the replacement
		if (m_generated)
		{
			// Upgraded minefield to next level for China Player
			const UpgradeTemplate *upgradeTemplate = gxEmpMinesUpgrade();

			if (upgradeTemplate)
			{
				const UpgradeMaskType& upgradeMask = upgradeTemplate->getUpgradeMask();
				const UpgradeMaskType& objMask = getObject()->getObjectCompletedUpgradeMask();
				if (objMask.testForAny(upgradeMask))
				{
					m_upgraded = TRUE;

					// Remove all old mine objects if present
					for (std::list<ObjectID>::iterator it = m_mineList.begin(); it != m_mineList.end(); ++it)
					{
						ObjectID objID = *it;
						Object *obj = TheGameLogic->findObjectByID(objID);
						if (obj)
						{
							TheGameLogic->destroyObject(obj);
						}
					}
					m_mineList.clear();

					// Place new mines down (Replace old ones that we removed
					m_generated = false;
					placeMines();
				}
			}
		}

		return UPDATE_SLEEP_NONE;
	}

	return UPDATE_SLEEP_FOREVER;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::crc( Xfer *xfer )
{

	// extend base class
	BehaviorModule::crc( xfer );

	// extend base class
	UpgradeMux::upgradeMuxCRC( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// base class
	BehaviorModule::xfer( xfer );

	// mux "base class"
	UpgradeMux::upgradeMuxXfer( xfer );

	// generated
	xfer->xferBool( &m_generated );
	xfer->xferBool( &m_hasTarget );
	xfer->xferBool( &m_upgraded );

	xfer->xferCoord3D( &m_target );

		// spaces info count and objectID data
	UnsignedByte spacesCount = m_mineList.size();
	xfer->xferUnsignedByte( &spacesCount );
	if( xfer->getXferMode() == XFER_SAVE )
	{
		// save all elements
		std::list<ObjectID>::iterator it;
		for( it = m_mineList.begin(); it != m_mineList.end(); ++it )
		{
			// object in this space
			xfer->xferObjectID( &(*it) );
		}

	}
	else if( xfer->getXferMode() == XFER_LOAD )
	{
		ObjectID objectID;
		m_mineList.clear();

		// read all elements
		std::list<ObjectID>::iterator it;
		it = m_mineList.begin();
		for(int i = 0; i < spacesCount; ++i )
		{
			// read object id
			xfer->xferObjectID( &objectID );

			m_mineList.push_back(objectID);
		}
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void GenerateMinefieldBehavior::loadPostProcess()
{

	// extend base class
	BehaviorModule::loadPostProcess();

	// extend base class
	UpgradeMux::upgradeMuxLoadPostProcess();

}
