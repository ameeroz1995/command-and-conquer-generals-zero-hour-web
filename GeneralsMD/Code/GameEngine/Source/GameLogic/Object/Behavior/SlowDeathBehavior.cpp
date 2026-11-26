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

// FILE: SlowDeathBehavior.cpp ///////////////////////////////////////////////////////////////////////
// Author:
// Desc:
///////////////////////////////////////////////////////////////////////////////////////////////////


// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#define DEFINE_SLOWDEATHPHASE_NAMES
#include "Common/GameLOD.h"
#include "Common/INI.h"
#include "Common/RandomValue.h"
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/SlowDeathBehavior.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"

// Web port (round 5, units are pure data): SlowDeathBehavior's per-unit state is a row of TheGxSlowDeathBehaviors;
// GXROW_SLOWDEATHBEHAVIOR is this unit's row (the old m_ members), so logic and xfer/CRC order are unchanged.
#define GXROW_SLOWDEATHBEHAVIOR (const_cast<GxSlowDeathBehaviorRow &>(*m_gxSlowDeathBehaviorRow))



const Real BEGIN_MIDPOINT_RATIO = 0.35f;
const Real END_MIDPOINT_RATIO = 0.65f;

//-------------------------------------------------------------------------------------------------
SlowDeathBehaviorModuleData::SlowDeathBehaviorModuleData()
{
	m_sinkRate = 0;
	m_probabilityModifier = 10;
	m_modifierBonusPerOverkillPercent = 0;
	m_sinkDelay = 0;
	m_sinkDelayVariance = 0;
	m_destructionDelay = 0;
	m_destructionDelayVariance = 0;
	m_destructionAltitude = -10;
	m_maskOfLoadedEffects = 0; //assume no ocl, fx, or weapons.
	m_flingForce = 0;
	m_flingForceVariance = 0;
	m_flingPitch = 0;
	m_flingPitchVariance = 0;
	// redundant.
	//m_fx.clear();
	//m_ocls.clear();
	//m_weapons.clear();
}

//-------------------------------------------------------------------------------------------------
static void parseFX( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	SlowDeathBehaviorModuleData* self = (SlowDeathBehaviorModuleData*)instance;
	SlowDeathPhaseType sdphase = (SlowDeathPhaseType)INI::scanIndexList(ini->getNextToken(), TheSlowDeathPhaseNames);
	for (const char* token = ini->getNextToken(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		const FXList *fxl = TheFXListStore->findFXList((token));	// could be null! this is OK!
		self->m_fx[sdphase].push_back(fxl);
		if (fxl)
			self->m_maskOfLoadedEffects |= SlowDeathBehaviorModuleData::HAS_FX;
	}
}

//-------------------------------------------------------------------------------------------------
static void parseOCL( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	SlowDeathBehaviorModuleData* self = (SlowDeathBehaviorModuleData*)instance;
	SlowDeathPhaseType sdphase = (SlowDeathPhaseType)INI::scanIndexList(ini->getNextToken(), TheSlowDeathPhaseNames);
	for (const char* token = ini->getNextToken(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList(token);	// could be null! this is OK!
		self->m_ocls[sdphase].push_back(ocl);
		if (ocl)
			self->m_maskOfLoadedEffects |= SlowDeathBehaviorModuleData::HAS_OCL;
	}
}

//-------------------------------------------------------------------------------------------------
static void parseWeapon( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	SlowDeathBehaviorModuleData* self = (SlowDeathBehaviorModuleData*)instance;
	SlowDeathPhaseType sdphase = (SlowDeathPhaseType)INI::scanIndexList(ini->getNextToken(), TheSlowDeathPhaseNames);
	for (const char* token = ini->getNextToken(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		const WeaponTemplate *wt = TheWeaponStore->findWeaponTemplate(token);	// could be null! this is OK!
		self->m_weapons[sdphase].push_back(wt);
		if (wt)
			self->m_maskOfLoadedEffects |= SlowDeathBehaviorModuleData::HAS_WEAPON;
	}
}

//-------------------------------------------------------------------------------------------------
/*static*/ void SlowDeathBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  UpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "SinkRate",													INI::parseVelocityReal,						nullptr, offsetof( SlowDeathBehaviorModuleData, m_sinkRate ) },
		{ "ProbabilityModifier",							INI::parseInt,										nullptr, offsetof( SlowDeathBehaviorModuleData, m_probabilityModifier ) },
		{ "ModifierBonusPerOverkillPercent",	INI::parsePercentToReal,					nullptr, offsetof( SlowDeathBehaviorModuleData, m_modifierBonusPerOverkillPercent ) },
		{ "SinkDelay",												INI::parseDurationUnsignedInt,		nullptr, offsetof( SlowDeathBehaviorModuleData, m_sinkDelay ) },
		{ "SinkDelayVariance",								INI::parseDurationUnsignedInt,		nullptr, offsetof( SlowDeathBehaviorModuleData, m_sinkDelayVariance ) },
		{ "DestructionDelay",									INI::parseDurationUnsignedInt,		nullptr, offsetof( SlowDeathBehaviorModuleData, m_destructionDelay ) },
		{ "DestructionDelayVariance",					INI::parseDurationUnsignedInt,		nullptr, offsetof( SlowDeathBehaviorModuleData, m_destructionDelayVariance ) },
		{ "DestructionAltitude",							INI::parseReal,										nullptr, offsetof( SlowDeathBehaviorModuleData, m_destructionAltitude ) },
		{ "FX",																parseFX,													nullptr, 0 },
		{ "OCL",															parseOCL,													nullptr, 0 },
		{ "Weapon",														parseWeapon,											nullptr, 0 },
		{ "FlingForce",												INI::parseReal,										nullptr, offsetof( SlowDeathBehaviorModuleData, m_flingForce) },
		{ "FlingForceVariance",								INI::parseReal,										nullptr, offsetof( SlowDeathBehaviorModuleData, m_flingForceVariance) },
		{ "FlingPitch",												INI::parseAngleReal,							nullptr, offsetof( SlowDeathBehaviorModuleData, m_flingPitch) },
		{ "FlingPitchVariance",								INI::parseAngleReal,							nullptr, offsetof( SlowDeathBehaviorModuleData, m_flingPitchVariance) },
		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
	p.add(DieMuxData::getFieldParse(), offsetof( SlowDeathBehaviorModuleData, m_dieMuxData ));
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SlowDeathBehavior::SlowDeathBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	static Bool gxRegistered = FALSE;
	if (!gxRegistered)
	{
		gxRegistered = TRUE;
		GameLogic::gxRegisterKindKernel(NAMEKEY("SlowDeathBehavior"), gxKernel, gxApply);
	}

	GXROW_SLOWDEATHBEHAVIOR.flags = 0;
	GXROW_SLOWDEATHBEHAVIOR.sinkFrame = 0;
	GXROW_SLOWDEATHBEHAVIOR.midpointFrame = 0;
	GXROW_SLOWDEATHBEHAVIOR.destructionFrame = 0;
	GXROW_SLOWDEATHBEHAVIOR.acceleratedTimeScale = 1.0f;

	if (getSlowDeathBehaviorModuleData()->m_probabilityModifier < 1)
	{
		DEBUG_CRASH(("ProbabilityModifier must be >= 1."));
		throw INI_INVALID_DATA;
	}

	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SlowDeathBehavior::~SlowDeathBehavior()
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Int SlowDeathBehavior::getProbabilityModifier( const DamageInfo *damageInfo ) const
{
	// Calculating how far past dead we were allows us to pick more spectacular deaths when
	// severely killed, and more sedate ones when only slightly killed.
	// eg ( 200 hp max, had 10 left, took 50 damage, 40 overkill, (40/200) * 100 = 20 overkill %)
	Int overkillDamage = damageInfo->out.m_actualDamageDealt - damageInfo->out.m_actualDamageClipped;
	Real overkillPercent = (float)overkillDamage / (float)getObject()->getBodyModule()->getMaxHealth();
	Int overkillModifier = overkillPercent * getSlowDeathBehaviorModuleData()->m_modifierBonusPerOverkillPercent;

	return max( getSlowDeathBehaviorModuleData()->m_probabilityModifier + overkillModifier, 1 );
}

//-------------------------------------------------------------------------------------------------
static void calcRandomForce(Real minMag, Real maxMag, Real minPitch, Real maxPitch, Coord3D& force)
{
	Real angle = GameLogicRandomValueReal(-PI, PI);
	Real pitch = GameLogicRandomValueReal(minPitch, maxPitch);
	Real mag = GameLogicRandomValueReal(minMag, maxMag);

	Matrix3D mtx(1);
	mtx.Scale(mag);
	mtx.Rotate_Z(angle);
	mtx.Rotate_Y(-pitch);

	Vector3 v = mtx.Get_X_Vector();

	force.x = v.X;
	force.y = v.Y;
	force.z = v.Z;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SlowDeathBehavior::beginSlowDeath(const DamageInfo *damageInfo)
{
	if (!isSlowDeathActivated())
	{
		const SlowDeathBehaviorModuleData* d = getSlowDeathBehaviorModuleData();
		Object* obj = getObject();

		if (d->m_sinkRate && obj->isKindOf(KINDOF_INFANTRY))
		{

			Drawable *draw = getObject()->getDrawable();
			if ( draw )
			{
				// this object sinks slowly after it dies so don't draw a
				// floating shadow decal on the ground above it.
				obj->getDrawable()->setShadowsEnabled(false);
				draw->setTerrainDecalFadeTarget( 0.0f, -0.2f );
			}



		}


		// Ask game detail manager if we need to speedup all deaths to improve performance
		Real timeScale = TheGameLODManager->getSlowDeathScale();
		GXROW_SLOWDEATHBEHAVIOR.acceleratedTimeScale = 1.0f;	// assume normal death speed.

		if (timeScale == 0.0f && !d->hasNonLodEffects())
		{
			// Deaths happen instantly so just delete the object and return
			TheGameLogic->destroyObject(obj);
			return;
		}
		else
		{
			// timescale is some non-zero value so we may need to speed up death
			if( getObject()->isKindOf( KINDOF_HULK ) && TheGameLogic->getHulkMaxLifetimeOverride() != -1 )
			{
				//Scripts don't want hulks around, so start sinking immediately!
				GXROW_SLOWDEATHBEHAVIOR.sinkFrame = 1;
				GXROW_SLOWDEATHBEHAVIOR.midpointFrame = (LOGICFRAMES_PER_SECOND/2) + 1;
				GXROW_SLOWDEATHBEHAVIOR.destructionFrame = LOGICFRAMES_PER_SECOND + 1;
				GXROW_SLOWDEATHBEHAVIOR.acceleratedTimeScale = 1.0f;
			}
			else
			{
				GXROW_SLOWDEATHBEHAVIOR.sinkFrame = timeScale * (d->m_sinkDelay + GameLogicRandomValue(0, d->m_sinkDelayVariance));
				GXROW_SLOWDEATHBEHAVIOR.destructionFrame = timeScale * (d->m_destructionDelay + GameLogicRandomValue(0, d->m_destructionDelayVariance));
				GXROW_SLOWDEATHBEHAVIOR.midpointFrame = GameLogicRandomValue( BEGIN_MIDPOINT_RATIO * GXROW_SLOWDEATHBEHAVIOR.destructionFrame, END_MIDPOINT_RATIO * GXROW_SLOWDEATHBEHAVIOR.destructionFrame );
				GXROW_SLOWDEATHBEHAVIOR.acceleratedTimeScale = timeScale;
			}
		}

		UnsignedInt now = TheGameLogic->getFrame();

		if (d->m_flingForce > 0)
		{


			//Just in case this is a stingersoldier or other HELD object, lets set them free so they will fly
			// with their own physics during slow death
			if( obj->isDisabledByType( DISABLED_HELD ) )
			{
				static NameKeyType key_SlavedUpdate = NAMEKEY( "SlavedUpdate" );
				SlavedUpdate* slave = (SlavedUpdate*)obj->findUpdateModule( key_SlavedUpdate );
				if( slave )
				{
					slave->onSlaverDie( nullptr );
				}
			}

			PhysicsBehavior* physics = obj->getPhysics();
			if (physics)
			{
				// make sure we are at least a bit above the ground
				const Real MIN_ALTITUDE = 1.0f;
				Real altitude = obj->getHeightAboveTerrain();
				if (altitude < MIN_ALTITUDE)
				{
					Coord3D pos = *obj->getPosition();
					pos.z += MIN_ALTITUDE;
					obj->setPosition(&pos);
				}

				Coord3D force;
				calcRandomForce(d->m_flingForce, d->m_flingForce + d->m_flingForceVariance,
												d->m_flingPitch, d->m_flingPitch + d->m_flingPitchVariance, force);
				physics->setAllowToFall(true);
				physics->applyForce(&force);
				physics->setExtraBounciness(-1.0);					// we don't want this guy to bounce at all
				physics->setExtraFriction(-3 * SECONDS_PER_LOGICFRAME_REAL);							// reduce his ground friction a bit
				physics->setAllowBouncing(true);
				Real orientation = atan2(force.y, force.x);
				physics->setAngles(orientation, 0, 0);
				obj->getDrawable()->setModelConditionState(MODELCONDITION_EXPLODED_FLAILING);
				GXROW_SLOWDEATHBEHAVIOR.flags |= (1<<FLUNG_INTO_AIR);
			}
			setWakeFrame(obj, UPDATE_SLEEP_NONE);
		}
		else
		{
			// we don't need to wake up immediately, but only when the first of these
			// counters wants to trigger....
			Int whenToWakeTime = GXROW_SLOWDEATHBEHAVIOR.sinkFrame;
			if (whenToWakeTime > GXROW_SLOWDEATHBEHAVIOR.destructionFrame)
				whenToWakeTime = GXROW_SLOWDEATHBEHAVIOR.destructionFrame;
			if (whenToWakeTime > GXROW_SLOWDEATHBEHAVIOR.midpointFrame)
				whenToWakeTime = GXROW_SLOWDEATHBEHAVIOR.midpointFrame;
			setWakeFrame(obj, UPDATE_SLEEP(whenToWakeTime));
		}
		GXROW_SLOWDEATHBEHAVIOR.sinkFrame += now;
		GXROW_SLOWDEATHBEHAVIOR.destructionFrame += now;
		GXROW_SLOWDEATHBEHAVIOR.midpointFrame += now;

		GXROW_SLOWDEATHBEHAVIOR.flags |= (1<<SLOW_DEATH_ACTIVATED);

		doPhaseStuff(SDPHASE_INITIAL);

	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SlowDeathBehavior::doPhaseStuff(SlowDeathPhaseType sdphase)
{
	const SlowDeathBehaviorModuleData* d = getSlowDeathBehaviorModuleData();
	Int idx, listSize;

	if (!d->m_maskOfLoadedEffects)
		return;	//has no ocl, fx, or weapons.

	listSize = d->m_fx[sdphase].size();
	if (listSize > 0)
	{
		idx = GameLogicRandomValue(0, listSize-1);
		const FXListVec& v = d->m_fx[sdphase];
		DEBUG_ASSERTCRASH(idx>=0&&idx<v.size(),("bad idx"));
		const FXList* fxl = v[idx];
		FXList::doFXObj(fxl, getObject(), nullptr);
	}

	listSize = d->m_ocls[sdphase].size();
	if (listSize > 0)
	{
		idx = GameLogicRandomValue(0, listSize-1);
		const OCLVec& v = d->m_ocls[sdphase];
		DEBUG_ASSERTCRASH(idx>=0&&idx<v.size(),("bad idx"));
		const ObjectCreationList* ocl = v[idx];
		ObjectCreationList::create(ocl, getObject(), nullptr);
	}

	listSize = d->m_weapons[sdphase].size();
	if (listSize > 0)
	{
		idx = GameLogicRandomValue(0, listSize-1);
		const WeaponTemplateVec& v = d->m_weapons[sdphase];
		DEBUG_ASSERTCRASH(idx>=0&&idx<v.size(),("bad idx"));
		const WeaponTemplate* wt = v[idx];
		if (wt)
		{
			TheWeaponStore->createAndFireTempWeapon(wt, getObject(), getObject()->getPosition());
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// Round 5 addendum: the per-frame work is split into gxStep (the kernel part: reads and writes only this
// unit's row and its module data, and returns what is due) and gxApplyStep (everything that touches the
// object or the world). update() runs both; under the per-kind hook (match rule updatePasses) the kind's
// kernel runs gxStep for every dying unit first and apply runs gxApplyStep in object-ID order, so the
// result is the same as the units' own turns (nothing another unit does in its turn reads or writes these rows).

// verify mode (page switch bit 21): at each unit's apply, its row must be as the kernel left it (nobody
// touched it in between), so computing it in the unit's own turn would have given the same result.
static void gxSlowDeathVerify(const char *what, Bool same)
{
#ifdef __EMSCRIPTEN__
	extern int gx_verify_mismatch, gx_verify_checked;
	++gx_verify_checked;
	if (!same) ++gx_verify_mismatch;
	static int n = 0;
	if (!same || (++n % 20000) == 0)
		printf("GXVERIFY kindKernels %s: %s, checked %d, mismatches %d\n", what, same ? "ok" : "MISMATCH", gx_verify_checked, gx_verify_mismatch);
#endif
}
static Real s_gxTimeScale = 1.0f;	// verify mode: the scale the kernel used
extern int GLES3_Page_Switches();
enum { GXSD_INSTANT = 1, GXSD_FLUNG = 2, GXSD_SINK = 4, GXSD_MIDPOINT = 8, GXSD_FINAL = 16 };
UnsignedInt SlowDeathBehavior::gxStep( UnsignedInt now, Real timeScale )
{
	return gxStepRow(GXROW_SLOWDEATHBEHAVIOR, getSlowDeathBehaviorModuleData(), now, timeScale);
}
UnsignedInt SlowDeathBehavior::gxStepRow( GxSlowDeathBehaviorRow &r, const SlowDeathBehaviorModuleData *d, UnsignedInt now, Real timeScale )
{

	// Check if we have normal time scale but LODManager is requesting acceleration
	if (timeScale != 1.0f && r.acceleratedTimeScale == 1.0f && !d->hasNonLodEffects())
	{
		// speed of deaths has been increased since beginning of death
		// so adjust it to current levels.
		if (timeScale == 0)
			return GXSD_INSTANT;	// instant death

		r.sinkFrame = (Real)r.sinkFrame * timeScale;
		r.midpointFrame = (Real)r.midpointFrame * timeScale;
		r.destructionFrame = (Real)r.destructionFrame * timeScale;
		r.acceleratedTimeScale = timeScale;
	}

	UnsignedInt due = 0;
	if ((r.flags & (1<<FLUNG_INTO_AIR)) != 0 && (r.flags & (1<<BOUNCED)) == 0)
	{
		++r.sinkFrame;
		++r.midpointFrame;
		++r.destructionFrame;
		due |= GXSD_FLUNG;
	}
	if (now >= r.sinkFrame && d->m_sinkRate > 0.0f)
		due |= GXSD_SINK;
	if (now >= r.midpointFrame && (r.flags & (1<<MIDPOINT_EXECUTED)) == 0)
		due |= GXSD_MIDPOINT;
	if (now >= r.destructionFrame)
		due |= GXSD_FINAL;
	return due;
}

UpdateSleepTime SlowDeathBehavior::gxApplyStep( UnsignedInt due )
{
	const SlowDeathBehaviorModuleData* d = getSlowDeathBehaviorModuleData();
	Object* obj = getObject();

	if (due & GXSD_INSTANT)
	{
		TheGameLogic->destroyObject(obj);
		return UPDATE_SLEEP_NONE;
	}

	if (due & GXSD_FLUNG)
	{
		if (!obj->isAboveTerrain())
		{
			obj->clearAndSetModelConditionFlags(MAKE_MODELCONDITION_MASK(MODELCONDITION_EXPLODED_FLAILING),
																					MAKE_MODELCONDITION_MASK(MODELCONDITION_EXPLODED_BOUNCING));
			GXROW_SLOWDEATHBEHAVIOR.flags |= (1<<BOUNCED);
		}

		// Here we want to make sure we die if we collide with a tree on the way down
		PhysicsBehavior *phys = obj->getPhysics();
		if ( phys )
		{
			ObjectID treeID = phys->getLastCollidee();
			Object *tree = TheGameLogic->findObjectByID( treeID );
			if ( tree )
			{
				if (tree->isKindOf( KINDOF_SHRUBBERY ) )
				{
					obj->setDisabled( DISABLED_HELD );
					obj->clearModelConditionFlags( MAKE_MODELCONDITION_MASK(MODELCONDITION_EXPLODED_FLAILING) );
					obj->clearModelConditionFlags( MAKE_MODELCONDITION_MASK(MODELCONDITION_EXPLODED_BOUNCING) );
					obj->setModelConditionFlags(   MAKE_MODELCONDITION_MASK(MODELCONDITION_PARACHUTING) ); //looks like he is snagged in a tree
					obj->setPositionZ( obj->getPosition()->z - (d->m_sinkRate * 50.0f) );// make him sink faster
					if ( !obj->isAboveTerrain() )
						TheGameLogic->destroyObject(obj);

				}
			}
		}
	}

	if (due & GXSD_SINK)
	{
		// disable Physics (if any) so that we can control the sink...
		obj->setDisabled( DISABLED_HELD );
		Coord3D pos = *obj->getPosition();
		pos.z -= d->m_sinkRate / GXROW_SLOWDEATHBEHAVIOR.acceleratedTimeScale;
		obj->setPosition( &pos );
	}

	if (due & GXSD_MIDPOINT)
	{
		doPhaseStuff(SDPHASE_MIDPOINT);
		GXROW_SLOWDEATHBEHAVIOR.flags |= (1<<MIDPOINT_EXECUTED);
	}

	if (due & GXSD_FINAL)
	{
		doPhaseStuff(SDPHASE_FINAL);
		TheGameLogic->destroyObject(obj);
	}

	return UPDATE_SLEEP_NONE;
}

// SIMD step (switch kernelSimd, fourth word bit 9; same game): with no LOD time scale (the scale branch
// cannot fire) the step is integer compares and adds, done 4 rows per instruction, branchless; rows are
// gathered from and scattered back to the pool. Bit-exact with gxStepRow (verify mode checks it).
#ifdef __EMSCRIPTEN__
#include <wasm_simd128.h>
extern int GLES3_Page_Switches4();
void SlowDeathBehavior::gxKernelSimd( UpdateModulePtr *mods, Int begin, Int end, UnsignedInt now )
{
	const v128_t nowv = wasm_i32x4_splat((int)now);
	const v128_t fFlung = wasm_i32x4_splat(1 << FLUNG_INTO_AIR), fBounced = wasm_i32x4_splat(1 << BOUNCED);
	const v128_t fMid = wasm_i32x4_splat(1 << MIDPOINT_EXECUTED), zero = wasm_i32x4_splat(0);
	const v128_t bFlung = wasm_i32x4_splat(GXSD_FLUNG), bSink = wasm_i32x4_splat(GXSD_SINK);
	const v128_t bMid = wasm_i32x4_splat(GXSD_MIDPOINT), bFinal = wasm_i32x4_splat(GXSD_FINAL);
	Int k = begin;
	for (; k + 4 <= end; k += 4)
	{
		GxSlowDeathBehaviorRow *r0 = &static_cast<SlowDeathBehavior *>(mods[k])->gxRow();
		GxSlowDeathBehaviorRow *r1 = &static_cast<SlowDeathBehavior *>(mods[k + 1])->gxRow();
		GxSlowDeathBehaviorRow *r2 = &static_cast<SlowDeathBehavior *>(mods[k + 2])->gxRow();
		GxSlowDeathBehaviorRow *r3 = &static_cast<SlowDeathBehavior *>(mods[k + 3])->gxRow();
		const SlowDeathBehaviorModuleData *d0 = static_cast<SlowDeathBehavior *>(mods[k])->getSlowDeathBehaviorModuleData();
		const SlowDeathBehaviorModuleData *d1 = static_cast<SlowDeathBehavior *>(mods[k + 1])->getSlowDeathBehaviorModuleData();
		const SlowDeathBehaviorModuleData *d2 = static_cast<SlowDeathBehavior *>(mods[k + 2])->getSlowDeathBehaviorModuleData();
		const SlowDeathBehaviorModuleData *d3 = static_cast<SlowDeathBehavior *>(mods[k + 3])->getSlowDeathBehaviorModuleData();
		v128_t sink = wasm_i32x4_make(r0->sinkFrame, r1->sinkFrame, r2->sinkFrame, r3->sinkFrame);
		v128_t mid = wasm_i32x4_make(r0->midpointFrame, r1->midpointFrame, r2->midpointFrame, r3->midpointFrame);
		v128_t dst = wasm_i32x4_make(r0->destructionFrame, r1->destructionFrame, r2->destructionFrame, r3->destructionFrame);
		const v128_t fl = wasm_i32x4_make(r0->flags, r1->flags, r2->flags, r3->flags);
		const v128_t sinkPos = wasm_i32x4_make(d0->m_sinkRate > 0.0f ? -1 : 0, d1->m_sinkRate > 0.0f ? -1 : 0,
			d2->m_sinkRate > 0.0f ? -1 : 0, d3->m_sinkRate > 0.0f ? -1 : 0);
		// flung and not yet bounced: all three frames move one later (mask is -1, so subtracting adds 1)
		const v128_t flung = wasm_v128_andnot(wasm_i32x4_ne(wasm_v128_and(fl, fFlung), zero),
			wasm_i32x4_ne(wasm_v128_and(fl, fBounced), zero));
		sink = wasm_i32x4_sub(sink, flung);
		mid = wasm_i32x4_sub(mid, flung);
		dst = wasm_i32x4_sub(dst, flung);
		const v128_t sinkDue = wasm_v128_and(wasm_u32x4_ge(nowv, sink), sinkPos);
		const v128_t midDue = wasm_v128_and(wasm_u32x4_ge(nowv, mid), wasm_i32x4_eq(wasm_v128_and(fl, fMid), zero));
		const v128_t finDue = wasm_u32x4_ge(nowv, dst);
		const v128_t due = wasm_v128_or(wasm_v128_or(wasm_v128_and(flung, bFlung), wasm_v128_and(sinkDue, bSink)),
			wasm_v128_or(wasm_v128_and(midDue, bMid), wasm_v128_and(finDue, bFinal)));
#define GXSD_PUT(i, r) \
		r->sinkFrame = wasm_i32x4_extract_lane(sink, i); r->midpointFrame = wasm_i32x4_extract_lane(mid, i); \
		r->destructionFrame = wasm_i32x4_extract_lane(dst, i); r->gxDue = wasm_i32x4_extract_lane(due, i); r->gxKernelFrame = now;
		GXSD_PUT(0, r0) GXSD_PUT(1, r1) GXSD_PUT(2, r2) GXSD_PUT(3, r3)
#undef GXSD_PUT
	}
	for (; k < end; ++k)
	{
		SlowDeathBehavior *m = static_cast<SlowDeathBehavior *>(mods[k]);
		GxSlowDeathBehaviorRow &r = m->gxRow();
		r.gxDue = gxStepRow(r, m->getSlowDeathBehaviorModuleData(), now, 1.0f);
		r.gxKernelFrame = now;
	}
}
#endif

void SlowDeathBehavior::gxKernel( UpdateModulePtr *mods, Int begin, Int end )
{
	const UnsignedInt now = TheGameLogic->getFrame();	// read only: fixed for the frame
	const Real timeScale = TheGameLODManager->getSlowDeathScale();
	const Bool gxVerify = (GLES3_Page_Switches() & 2097152) != 0;
	s_gxTimeScale = timeScale;
#ifdef __EMSCRIPTEN__
	if (timeScale == 1.0f && (GLES3_Page_Switches4() & 512))	// kernelSimd
	{
		static std::vector<GxSlowDeathBehaviorRow> before;
		if (gxVerify)
		{
			before.clear();
			for (Int k = begin; k < end; ++k) before.push_back(static_cast<SlowDeathBehavior *>(mods[k])->gxRow());
		}
		gxKernelSimd(mods, begin, end, now);
		if (gxVerify)
		{
			// the scalar step on the rows as they were must give the same rows and results
			for (Int k = begin; k < end; ++k)
			{
				SlowDeathBehavior *m = static_cast<SlowDeathBehavior *>(mods[k]);
				GxSlowDeathBehaviorRow r = before[k - begin];
				const UnsignedInt due = gxStepRow(r, m->getSlowDeathBehaviorModuleData(), now, timeScale);
				const GxSlowDeathBehaviorRow &v = m->gxRow();
				gxSlowDeathVerify("slowDeathSimd", due == v.gxDue && r.sinkFrame == v.sinkFrame
					&& r.midpointFrame == v.midpointFrame && r.destructionFrame == v.destructionFrame);
			}
			for (Int k = begin; k < end; ++k)	// the snapshot the apply check compares with
			{
				GxSlowDeathBehaviorRow &r = static_cast<SlowDeathBehavior *>(mods[k])->gxRow();
				r.gxSnap[0] = r.sinkFrame; r.gxSnap[1] = r.midpointFrame; r.gxSnap[2] = r.destructionFrame;
				memcpy(&r.gxSnap[3], &r.acceleratedTimeScale, 4); r.gxSnap[4] = r.flags;
			}
		}
		return;
	}
#endif
	for (Int k = begin; k < end; ++k)
	{
		SlowDeathBehavior *m = static_cast<SlowDeathBehavior *>(mods[k]);
		GxSlowDeathBehaviorRow &r = m->gxRow();
		r.gxDue = m->gxStep(now, timeScale);
		r.gxKernelFrame = now;
		if (gxVerify)
		{
			r.gxSnap[0] = r.sinkFrame; r.gxSnap[1] = r.midpointFrame; r.gxSnap[2] = r.destructionFrame;
			memcpy(&r.gxSnap[3], &r.acceleratedTimeScale, 4); r.gxSnap[4] = r.flags;
		}
	}
}
void SlowDeathBehavior::gxApply( UpdateModulePtr *mods, Int count )
{
	const Bool gxVerify = (GLES3_Page_Switches() & 2097152) != 0;
	for (Int k = 0; k < count; ++k)
	{
		SlowDeathBehavior *m = static_cast<SlowDeathBehavior *>(mods[k]);
		const GxSlowDeathBehaviorRow &r = m->gxRow();
		if (gxVerify)
		{
			UnsignedInt cur[5] = { r.sinkFrame, r.midpointFrame, r.destructionFrame, 0, r.flags };
			memcpy(&cur[3], &r.acceleratedTimeScale, 4);
			gxSlowDeathVerify("slowDeath", memcmp(cur, r.gxSnap, sizeof(cur)) == 0
				&& r.gxKernelFrame == TheGameLogic->getFrame() && s_gxTimeScale == TheGameLODManager->getSlowDeathScale());
		}
		m->gxApplyStep(r.gxDue);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime SlowDeathBehavior::update()
{
	//DEBUG_LOG(("updating SlowDeathBehavior %08lx",this));
	DEBUG_ASSERTCRASH(isSlowDeathActivated(), ("hmm, this should not be possible"));

	const UnsignedInt now = TheGameLogic->getFrame();
	if (GXROW_SLOWDEATHBEHAVIOR.gxKernelFrame == now && now != 0)
		return UPDATE_SLEEP_NONE;	// this frame's turn was taken by the kind kernel + apply
	return gxApplyStep(gxStep(now, TheGameLODManager->getSlowDeathScale()));
}

//-------------------------------------------------------------------------------------------------
void SlowDeathBehavior::onDie( const DamageInfo *damageInfo )
{
	Object *obj = getObject();

	if (!isDieApplicable(damageInfo))
		return;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai)
	{
		// has another AI already handled us. (hopefully another SlowDeathBehavior)
		if (ai->isAiInDeadState())
			return;
		ai->markAsDead();
	}

	// deselect this unit for all players.
	TheGameLogic->deselectObject(obj, PLAYERMASK_ALL, TRUE);

	Int total = 0;
	BehaviorModule** update = obj->getBehaviorModules();
	for (; *update; ++update)
	{
		SlowDeathBehaviorInterface* sdu = (*update)->getSlowDeathBehaviorInterface();
		if (sdu != nullptr && sdu->isDieApplicable(damageInfo))
		{
			total += sdu->getProbabilityModifier( damageInfo );
		}
	}
	DEBUG_ASSERTCRASH(total > 0, ("Hmm, this is wrong"));


	// this returns a value from 1...total, inclusive
	Int roll = GameLogicRandomValue(1, total);

	for (/* UpdateModuleInterface** */ update = obj->getBehaviorModules(); *update; ++update)
	{
		SlowDeathBehaviorInterface* sdu = (*update)->getSlowDeathBehaviorInterface();
		if (sdu != nullptr && sdu->isDieApplicable(damageInfo))
		{
			roll -= sdu->getProbabilityModifier( damageInfo );
			if (roll <= 0)
			{
				sdu->beginSlowDeath(damageInfo);
				return;
			}
		}
	}

	DEBUG_CRASH(("We should never get here"));
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SlowDeathBehavior::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SlowDeathBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// sink frame
	xfer->xferUnsignedInt( &GXROW_SLOWDEATHBEHAVIOR.sinkFrame );

	// midpoint frame
	xfer->xferUnsignedInt( &GXROW_SLOWDEATHBEHAVIOR.midpointFrame );

	// destruction frame
	xfer->xferUnsignedInt( &GXROW_SLOWDEATHBEHAVIOR.destructionFrame );

	// accelerated time scale
	xfer->xferReal( &GXROW_SLOWDEATHBEHAVIOR.acceleratedTimeScale );

	// flags
	xfer->xferUnsignedInt( &GXROW_SLOWDEATHBEHAVIOR.flags );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SlowDeathBehavior::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
