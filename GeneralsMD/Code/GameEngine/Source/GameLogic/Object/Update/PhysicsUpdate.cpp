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

// PhysicsBehavior.cpp
// Simple rigid body physics
// Author: Michael S. Booth, November 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

// please talk to MDC (x36804) before taking this out
#define NO_DEBUG_CRC

#include "Common/PerfTimer.h"
#include "Common/GxTime.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/CrushDie.h"		// for CrushEnum
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/LogicRandomValue.h"

const Real DEFAULT_MASS = 1.0f;

const Real DEFAULT_SHOCK_YAW = 0.05f;
const Real DEFAULT_SHOCK_PITCH = 0.025f;
const Real DEFAULT_SHOCK_ROLL = 0.025f;

const Real DEFAULT_FORWARD_FRICTION = 0.15f;
const Real DEFAULT_LATERAL_FRICTION = 0.15f;
const Real DEFAULT_Z_FRICTION = 0.8f;
const Real DEFAULT_AERO_FRICTION = 0.0f;

// Air friction used to be completely separate and unclamped, so this constant must be split.
// The fact it defaults to 0 shows it.
const Real MIN_AERO_FRICTION = 0.00f;
const Real MIN_NON_AERO_FRICTION = 0.01f;
const Real MAX_FRICTION = 0.99f;

const Real STUN_RELIEF_EPSILON = 0.5f;

#include "Common/CRCDebug.h"
#include "Common/GxMatchRules.h"

// web port: performance switches (gles3_wrapper.cpp) and verify counters (GameLogic.cpp)
#ifdef __EMSCRIPTEN__
extern int GLES3_Page_Switches();
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif
extern int GLES3_Page_Switches2();
extern int GLES3_Page_Switches4();
extern int gx_verify_mismatch, gx_verify_checked;
#else
static int GLES3_Page_Switches() { return 0; }
static int GLES3_Page_Switches2() { return 0; }
static int GLES3_Page_Switches4() { return 0; }
static int gx_verify_mismatch, gx_verify_checked;
#endif

const Int MOTIVE_FRAMES = LOGICFRAMES_PER_SECOND / 3;

#define SLEEPY_PHYSICS


//-------------------------------------------------------------------------------------------------
static Real angleBetweenVectors(const Coord3D& inCurDir, const Coord3D& inGoalDir)
{
	Vector3 curDir;
	curDir.X = inCurDir.x;
	curDir.Y = inCurDir.y;
	curDir.Z = inCurDir.z;
	curDir.Normalize();

	Vector3 goalDir;
	goalDir.X = inGoalDir.x;
	goalDir.Y = inGoalDir.y;
	goalDir.Z = inGoalDir.z;
	goalDir.Normalize();

	// dot of two unit vectors is cos of angle between them.
	Real cosine = Vector3::Dot_Product(curDir, goalDir);

	// bound it in case of numerical error
	Real angleBetween = (Real)ACos(clamp(-1.0f, cosine, 1.0f));

	return angleBetween;
}

//-------------------------------------------------------------------------------------------------
static Real heightToSpeed(Real height)
{
	// don't bother trying to remember how far we've fallen; instead,
	// back-calc it from our speed & gravity... v = sqrt(2*g*h)
	return sqrt(fabs(2.0f * TheGlobalData->m_gravity * height));
}

//-------------------------------------------------------------------------------------------------
PhysicsBehaviorModuleData::PhysicsBehaviorModuleData()
{
	m_mass = DEFAULT_MASS;
	m_shockResistance = 0.0f;
	m_shockMaxYaw = DEFAULT_SHOCK_YAW;
	m_shockMaxPitch = DEFAULT_SHOCK_PITCH;
	m_shockMaxRoll = DEFAULT_SHOCK_ROLL;

	m_forwardFriction = DEFAULT_FORWARD_FRICTION;
	m_lateralFriction = DEFAULT_LATERAL_FRICTION;
	m_ZFriction = DEFAULT_Z_FRICTION;
	m_aerodynamicFriction = DEFAULT_AERO_FRICTION;
	m_centerOfMassOffset = 0.0f;
	m_allowBouncing = false;
	m_allowCollideForce = true;
	m_killWhenRestingOnGround = false;
	m_minFallSpeedForDamage = heightToSpeed(40.0f);
	m_fallHeightDamageFactor = 1.0f;	// was 10. now is 1.
	/*
		thru some bizarre editing mishap, we have been double-apply pitch/roll/yaw rates
		to objects for, well, a long time, it looks like. I have corrected that problem
		in the name of efficiency, but to maintain the same visual appearance without having
		to edit every freaking INI in the world at this point, I am just multiplying
		all the results by a factor so that the effect is the same (but with less execution time).
		I have put this factor into INI in the unlikely event we ever need to change it,
		but defaulting it to 2 is, in fact, the right thing for now... (srj)
	*/
	m_pitchRollYawFactor = 2.0f;
	m_vehicleCrashesIntoBuildingWeaponTemplate = TheWeaponStore->findWeaponTemplate("VehicleCrashesIntoBuildingWeapon");
	m_vehicleCrashesIntoNonBuildingWeaponTemplate = TheWeaponStore->findWeaponTemplate("VehicleCrashesIntoNonBuildingWeapon");

}

//-------------------------------------------------------------------------------------------------
static void parseHeightToSpeed( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	// don't bother trying to remember how far we've fallen; instead,
	// back-calc it from our speed & gravity... v = sqrt(2*g*h)
	Real height = INI::scanReal(ini->getNextToken());
	*(Real *)store = heightToSpeed(height);
}

//-------------------------------------------------------------------------------------------------
static void parseFrictionPerSec( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	Real fricPerSec = INI::scanReal(ini->getNextToken());
	Real fricPerFrame = fricPerSec * SECONDS_PER_LOGICFRAME_REAL;
	*(Real *)store = fricPerFrame;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void PhysicsBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  UpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "Mass",								INI::parsePositiveNonZeroReal,		nullptr, offsetof( PhysicsBehaviorModuleData, m_mass ) },

		{ "ShockResistance",		INI::parsePositiveNonZeroReal,		nullptr, offsetof( PhysicsBehaviorModuleData, m_shockResistance ) },
		{ "ShockMaxYaw",				INI::parsePositiveNonZeroReal,		nullptr, offsetof( PhysicsBehaviorModuleData, m_shockMaxYaw ) },
		{ "ShockMaxPitch",			INI::parsePositiveNonZeroReal,		nullptr, offsetof( PhysicsBehaviorModuleData, m_shockMaxPitch ) },
		{ "ShockMaxRoll",				INI::parsePositiveNonZeroReal,		nullptr, offsetof( PhysicsBehaviorModuleData, m_shockMaxRoll ) },

		{ "ForwardFriction",			parseFrictionPerSec,		nullptr, offsetof( PhysicsBehaviorModuleData, m_forwardFriction ) },
		{ "LateralFriction",			parseFrictionPerSec,		nullptr, offsetof( PhysicsBehaviorModuleData, m_lateralFriction ) },
		{ "ZFriction",						parseFrictionPerSec,		nullptr, offsetof( PhysicsBehaviorModuleData, m_ZFriction ) },
		{ "AerodynamicFriction",	parseFrictionPerSec,		nullptr, offsetof( PhysicsBehaviorModuleData, m_aerodynamicFriction ) },

		{ "CenterOfMassOffset",	INI::parseReal,		nullptr, offsetof( PhysicsBehaviorModuleData, m_centerOfMassOffset ) },
		{ "AllowBouncing",			INI::parseBool,		nullptr, offsetof( PhysicsBehaviorModuleData, m_allowBouncing ) },
		{ "AllowCollideForce",	INI::parseBool,		nullptr, offsetof( PhysicsBehaviorModuleData, m_allowCollideForce ) },
		{ "KillWhenRestingOnGround", INI::parseBool, nullptr, offsetof( PhysicsBehaviorModuleData, m_killWhenRestingOnGround) },

		{ "MinFallHeightForDamage",			parseHeightToSpeed,		nullptr, offsetof( PhysicsBehaviorModuleData, m_minFallSpeedForDamage) },
		{ "FallHeightDamageFactor",			INI::parseReal,		nullptr, offsetof( PhysicsBehaviorModuleData, m_fallHeightDamageFactor) },
		{ "PitchRollYawFactor",			INI::parseReal,		nullptr, offsetof( PhysicsBehaviorModuleData, m_pitchRollYawFactor) },

		{ "VehicleCrashesIntoBuildingWeaponTemplate", INI::parseWeaponTemplate, nullptr, offsetof(PhysicsBehaviorModuleData, m_vehicleCrashesIntoBuildingWeaponTemplate) },
		{ "VehicleCrashesIntoNonBuildingWeaponTemplate", INI::parseWeaponTemplate, nullptr, offsetof(PhysicsBehaviorModuleData, m_vehicleCrashesIntoNonBuildingWeaponTemplate) },

		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

const Real INVALID_VEL_MAG = -1.0f;

//-------------------------------------------------------------------------------------------------
// web port (round 4): the current velocity lives in the unit store
GxVec3Ref PhysicsBehavior::gxVel() const
{
	const Int slot = getObject()->gxStoreSlot();
	if (slot < 0)
	{
		static Real s_none[3];	// object not in the store (never for a live object)
		s_none[0] = s_none[1] = s_none[2] = 0.0f;
		return GxVec3Ref(s_none[0], s_none[1], s_none[2]);
	}
	return TheGxUnitStore.velRef(slot);
}

GxVec3Ref PhysicsBehavior::gxAccel() const
{
	const Int slot = getObject()->gxStoreSlot();
	if (slot < 0)
	{
		static Real s_none[3];
		s_none[0] = s_none[1] = s_none[2] = 0.0f;
		return GxVec3Ref(s_none[0], s_none[1], s_none[2]);
	}
	return TheGxUnitStore.accelRef(slot);
}

const Coord3D *PhysicsBehavior::getVelocity() const
{
	m_gxPhysicsBehavior->m_gxVelOut = gxVel();	// web port (round 5): copied out of the store's vx/vy/vz columns
	return &m_gxPhysicsBehavior->m_gxVelOut;
}

static UnsignedInt s_gxBulkFrame = ~0u;	// web port (match rule physBulk): the frame whose pass ran

PhysicsBehavior::PhysicsBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	gxAccel().zero();
	m_gxPhysicsBehavior->m_prevAccel = gxAccel();
	gxVel().zero();
	m_gxPhysicsBehavior->m_velMag = 0.0f;
	m_gxPhysicsBehavior->m_yawRate = 0.0f;
	m_gxPhysicsBehavior->m_rollRate = 0.0f;
	m_gxPhysicsBehavior->m_pitchRate = 0.0f;
	m_gxPhysicsBehavior->m_mass = getPhysicsBehaviorModuleData()->m_mass;
	m_gxPhysicsBehavior->m_motiveForceExpires = 0;

	m_gxPhysicsBehavior->m_flags = 0;
	m_gxPhysicsBehavior->m_extraBounciness = 0.0f;
	m_gxPhysicsBehavior->m_extraFriction = 0.0f;

	m_gxPhysicsBehavior->m_currentOverlap = INVALID_ID;
	m_gxPhysicsBehavior->m_previousOverlap = INVALID_ID;
	m_gxPhysicsBehavior->m_lastCollidee = INVALID_ID;

	m_gxPhysicsBehavior->m_ignoreCollisionsWith = INVALID_ID;

	setAllowBouncing(getPhysicsBehaviorModuleData()->m_allowBouncing);
	setAllowCollideForce(getPhysicsBehaviorModuleData()->m_allowCollideForce);

	m_gxPhysicsBehavior->m_pui = nullptr;
	m_gxPhysicsBehavior->m_bounceSound = nullptr;

	m_gxPhysicsBehavior->m_gxPreFrame = ~0u;
	m_gxPhysicsBehavior->m_gxSleep = UPDATE_SLEEP_NONE;

#ifdef SLEEPY_PHYSICS
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
#endif
}

//-------------------------------------------------------------------------------------------------
static ProjectileUpdateInterface* getPui(Object* obj)
{
	if (!obj->isKindOf(KINDOF_PROJECTILE))
		return nullptr;

	ProjectileUpdateInterface* objPui = nullptr;
	for (BehaviorModule** u = obj->getBehaviorModules(); *u; ++u)
	{
		if ((objPui = (*u)->getProjectileUpdateInterface()) != nullptr)
			return objPui;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::onObjectCreated()
{
	m_gxPhysicsBehavior->m_pui = getPui(getObject());
}

//-------------------------------------------------------------------------------------------------
PhysicsBehavior::~PhysicsBehavior()
{
	deleteInstance(m_gxPhysicsBehavior->m_bounceSound);
	m_gxPhysicsBehavior->m_bounceSound = nullptr;
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::setIgnoreCollisionsWith(const Object* obj)
{
	m_gxPhysicsBehavior->m_ignoreCollisionsWith = obj ? obj->getID() : INVALID_ID;
}

//-------------------------------------------------------------------------------------------------
Bool PhysicsBehavior::isIgnoringCollisionsWith(ObjectID id) const
{
	return id != INVALID_ID && id == m_gxPhysicsBehavior->m_ignoreCollisionsWith;
}

//-------------------------------------------------------------------------------------------------
Real PhysicsBehavior::getAerodynamicFriction() const
{
	Real f = getPhysicsBehaviorModuleData()->m_aerodynamicFriction + m_gxPhysicsBehavior->m_extraFriction;
	if (f < MIN_AERO_FRICTION) f = MIN_AERO_FRICTION;
	if (f > MAX_FRICTION) f = MAX_FRICTION;
	return f;
}

//-------------------------------------------------------------------------------------------------
Real PhysicsBehavior::getForwardFriction() const
{
	Real f = getPhysicsBehaviorModuleData()->m_forwardFriction + m_gxPhysicsBehavior->m_extraFriction;
	if (f < MIN_NON_AERO_FRICTION) f = MIN_NON_AERO_FRICTION;
	if (f > MAX_FRICTION) f = MAX_FRICTION;
	return f;
}

//-------------------------------------------------------------------------------------------------
Real PhysicsBehavior::getLateralFriction() const
{
	Real f = getPhysicsBehaviorModuleData()->m_lateralFriction + m_gxPhysicsBehavior->m_extraFriction;
	if (f < MIN_NON_AERO_FRICTION) f = MIN_NON_AERO_FRICTION;
	if (f > MAX_FRICTION) f = MAX_FRICTION;
	return f;
}

//-------------------------------------------------------------------------------------------------
Real PhysicsBehavior::getZFriction() const
{
	Real f = getPhysicsBehaviorModuleData()->m_ZFriction + m_gxPhysicsBehavior->m_extraFriction;
	if (f < MIN_NON_AERO_FRICTION) f = MIN_NON_AERO_FRICTION;
	if (f > MAX_FRICTION) f = MAX_FRICTION;
	return f;
}

//-------------------------------------------------------------------------------------------------
/**
 * Apply a force at the object's CG
 */
void PhysicsBehavior::applyForce( const Coord3D *force )
{
// TheSuperHackers @info helmutbuhler 06/05/2025 This debug mutates the code to become CRC incompatible
#if defined(RTS_DEBUG) || !RETAIL_COMPATIBLE_CRC
	DEBUG_ASSERTCRASH(!(_isnan(force->x) || _isnan(force->y) || _isnan(force->z)), ("PhysicsBehavior::applyForce force NAN!"));
#endif
	if (_isnan(force->x) || _isnan(force->y) || _isnan(force->z)) {
		return;
	}
	// F = ma  -->  a = F/m  (divide force by mass)
	Real mass = getMass();
	Coord3D modForce = *force;
	if (isMotive())
	{
		const Coord3D *dir = getObject()->getUnitDirectionVector2D();
		// Only accept the lateral acceleration.
		Real lateralDot = force->x * (-dir->y) + force->y * dir->x;
		modForce.x = lateralDot * -dir->y;
		modForce.y = lateralDot * dir->x;
	}

	Real massInv = 1.0f / mass;
	gxAccel().x += modForce.x * massInv;
	gxAccel().y += modForce.y * massInv;
	gxAccel().z += modForce.z * massInv;

	//DEBUG_ASSERTCRASH(!(_isnan(gxAccel().x) || _isnan(gxAccel().y) || _isnan(gxAccel().z)), ("PhysicsBehavior::applyForce accel NAN!"));
	//DEBUG_ASSERTCRASH(!(_isnan(gxVel().x) || _isnan(gxVel().y) || _isnan(gxVel().z)), ("PhysicsBehavior::applyForce vel NAN!"));
	//DEBUG_ASSERTCRASH(fabs(force->z) < 3, ("unlikely z-force"));
#ifdef SLEEPY_PHYSICS
	if (getFlag(IS_IN_UPDATE))
	{
		// we're applying a force from inside our own update (probably for a bounce or friction).
		// just do nothing, since update will calculate the correct sleep behavior at the end.
	}
	else
	{
		// when a force is applied by an external module, we must wake up, even if the force is zero.
		setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
	}
#endif
}

//-------------------------------------------------------------------------------------------------
/**
 * Apply a shocwave force at the object's CG
 */
void PhysicsBehavior::applyShock( const Coord3D *force )
{
	Coord3D resistedForce = *force;
	resistedForce.scale( 1.0f - min( 1.0f, max( 0.0f, getPhysicsBehaviorModuleData()->m_shockResistance ) ) );

	// Apply the processed shock force to the object
	applyForce(&resistedForce);
}

//-------------------------------------------------------------------------------------------------
/**
 * Apply a random rotation at the object's CG
 */
void PhysicsBehavior::applyRandomRotation()
{
	// Ignore any pitch, roll & yaw rotation if behavior is stick to ground
	if (getFlag(STICK_TO_GROUND))	return;

	// Set bounce to true for a while until the unit is complete bouncing
	setAllowBouncing(true);

	Real randomModifier;

	randomModifier = GameLogicRandomValue(-1.0f, 1.0f);
	m_gxPhysicsBehavior->m_yawRate += getPhysicsBehaviorModuleData()->m_shockMaxYaw * randomModifier;

	randomModifier = GameLogicRandomValue(-1.0f, 1.0f);
	m_gxPhysicsBehavior->m_pitchRate += getPhysicsBehaviorModuleData()->m_shockMaxPitch * randomModifier;

	randomModifier = GameLogicRandomValue(-1.0f, 1.0f);
	m_gxPhysicsBehavior->m_rollRate += getPhysicsBehaviorModuleData()->m_shockMaxRoll * randomModifier;

#ifdef SLEEPY_PHYSICS
	if (getFlag(IS_IN_UPDATE))
	{
		// we're applying a force from inside our own update (probably for a bounce or friction).
		// just do nothing, since update will calculate the correct sleep behavior at the end.
	}
	else
	{
		// when a force is applied by an external module, we must wake up, even if the force is zero.
		setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
	}
#endif
}

//-------------------------------------------------------------------------------------------------
Bool PhysicsBehavior::isMotive() const
{
	return m_gxPhysicsBehavior->m_motiveForceExpires > TheGameLogic->getFrame();
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::applyMotiveForce( const Coord3D *force )
{
	m_gxPhysicsBehavior->m_motiveForceExpires = 0; // make it accept this force unquestioningly :)
	applyForce(force);
	m_gxPhysicsBehavior->m_motiveForceExpires = TheGameLogic->getFrame() + MOTIVE_FRAMES;
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::resetDynamicPhysics()
{
	gxAccel().zero();
	m_gxPhysicsBehavior->m_prevAccel.zero();
	gxVel().zero();
	m_gxPhysicsBehavior->m_velMag = 0.0f;
	m_gxPhysicsBehavior->m_turning = TURN_NONE;
	m_gxPhysicsBehavior->m_yawRate = 0;
	m_gxPhysicsBehavior->m_rollRate = 0;
	m_gxPhysicsBehavior->m_pitchRate = 0;
	setFlag(HAS_PITCHROLLYAW, false);
#ifdef SLEEPY_PHYSICS
	DEBUG_ASSERTCRASH(!getFlag(IS_IN_UPDATE), ("hmm, should not happen, may not work"));
	setWakeFrame(getObject(), calcSleepTime());
#endif
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::applyGravitationalForces()
{
	gxAccel().z += TheGlobalData->m_gravity;
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::applyFrictionalForces()
{
	//Are we a plane that is taxiing on a deck with a height offset?
	Bool deckTaxiing = getObject()->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET )
										 && getObject()->getAI()
										 && getObject()->getAI()->getCurLocomotorSetType() == LOCOMOTORSET_TAXIING;

	if (getFlag(APPLY_FRICTION2D_WHEN_AIRBORNE) || !getObject()->isSignificantlyAboveTerrain() || deckTaxiing )
	{
		applyYPRDamping(1.0f - DEFAULT_LATERAL_FRICTION);

		if (gxVel().x || gxVel().y)
		{
			const Coord3D *dir = getObject()->getUnitDirectionVector2D();
			Real mass = getMass();

			Real lateralDot = gxVel().x * (-dir->y) + gxVel().y * dir->x;
			Real lateralVel_x = lateralDot * -dir->y;
			Real lateralVel_y = lateralDot * dir->x;

			Real lf = mass * getLateralFriction();

			Coord3D accel;
			accel.x = -(lf * lateralVel_x);
			accel.y = -(lf * lateralVel_y);
			accel.z = 0.0f;

			if (!isMotive())
			{
				Real forwardDot = gxVel().x * dir->x + gxVel().y * dir->y;
				Real forwardVel_x = forwardDot * dir->x;
				Real forwardVel_y = forwardDot * dir->y;
				Real ff = mass * getForwardFriction();
				accel.x += -(ff * forwardVel_x);
				accel.y += -(ff * forwardVel_y);
			}
			applyForce(&accel);
		}
	}
	else
	{
		Real aerodynamics = -getAerodynamicFriction();	// negated!

		// Air resistance is proportional to velocity in the opposite direction
		gxAccel().x += gxVel().x * aerodynamics;
		gxAccel().y += gxVel().y * aerodynamics;
		gxAccel().z += gxVel().z * aerodynamics;

		applyYPRDamping(1.0f + aerodynamics);	// since aero is negated, this results in 1.0-getAerodynamicFriction()
	}
}


//-------------------------------------------------------------------------------------------------
Bool PhysicsBehavior::handleBounce(Real oldZ, Real newZ, Real groundZ, Coord3D* bounceForce)
{
	if (getFlag(ALLOW_BOUNCE) && newZ <= groundZ)
	{
		const Real MIN_STIFF = 0.01f;
		const Real MAX_STIFF = 0.99f;
		Real stiffness = TheGlobalData->m_groundStiffness;
		if (stiffness < MIN_STIFF) stiffness = MIN_STIFF;
		if (stiffness > MAX_STIFF) stiffness = MAX_STIFF;

		Real desiredAccelZ = 0.0f;
		Real vz = getVelocity()->z;
		if (oldZ > groundZ && vz < 0.0f)
		{
			desiredAccelZ = fabs(vz) * stiffness;
		}

		bounceForce->x = 0.0f;
		bounceForce->y = 0.0f;
		bounceForce->z = getMass() * desiredAccelZ;

		const Real DAMPING = 0.7f;
		applyYPRDamping(DAMPING);

		if (vz < 0.0f)
		{
			Vector3 zvec = getObject()->getTransformMatrix()->Get_Z_Vector();
			const Real rollAngle = (zvec.Z > 0) ? 0 : PI;
			// don't flip both pitch and roll... we'll "flip" twice.
			const Real pitchAngle = 0;
			Real yawAngle = getObject()->getTransformMatrix()->Get_Z_Rotation();
			setAngles(yawAngle, pitchAngle, rollAngle);
		}

		if(bounceForce->z > 0.0f)
		{
			testStunnedUnitForDestruction();
			return true;
		}
		else
		{
			setAllowBouncing(m_gxPhysicsBehavior->m_originalAllowBounce);
			return false;
		}
	}
	else
	{
		bounceForce->zero();
		return false;
	}
}

//-------------------------------------------------------------------------------------------------
inline Bool isVerySmall3D(const Coord3D& v)
{
	const Real THRESH = 0.01f;
	return (fabs(v.x) < THRESH && fabs(v.y) < THRESH && fabs(v.z) < THRESH);
}

//-------------------------------------------------------------------------------------------------
inline Bool isZero3D(const Coord3D& v)
{
	return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f;
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::setPitchRate(Real pitch)
{
	m_gxPhysicsBehavior->m_pitchRate = pitch;
	setFlag(HAS_PITCHROLLYAW, (m_gxPhysicsBehavior->m_pitchRate != 0.0f || m_gxPhysicsBehavior->m_rollRate != 0.0f || m_gxPhysicsBehavior->m_yawRate != 0.0f));
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::setRollRate(Real roll)
{
	m_gxPhysicsBehavior->m_rollRate = roll;
	setFlag(HAS_PITCHROLLYAW, (m_gxPhysicsBehavior->m_pitchRate != 0.0f || m_gxPhysicsBehavior->m_rollRate != 0.0f || m_gxPhysicsBehavior->m_yawRate != 0.0f));
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::setYawRate(Real yaw)
{
	m_gxPhysicsBehavior->m_yawRate = yaw;
	setFlag(HAS_PITCHROLLYAW, (m_gxPhysicsBehavior->m_pitchRate != 0.0f || m_gxPhysicsBehavior->m_rollRate != 0.0f || m_gxPhysicsBehavior->m_yawRate != 0.0f));
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::applyYPRDamping(Real factor)
{
	m_gxPhysicsBehavior->m_pitchRate *= factor;
	m_gxPhysicsBehavior->m_rollRate *= factor;
	m_gxPhysicsBehavior->m_yawRate *= factor;
	setFlag(HAS_PITCHROLLYAW, (m_gxPhysicsBehavior->m_pitchRate != 0.0f || m_gxPhysicsBehavior->m_rollRate != 0.0f || m_gxPhysicsBehavior->m_yawRate != 0.0f));
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::setBounceSound(const AudioEventRTS* bounceSound)
{
	if (bounceSound)
	{
		if (m_gxPhysicsBehavior->m_bounceSound == nullptr)
			m_gxPhysicsBehavior->m_bounceSound = newInstance(DynamicAudioEventRTS);

		m_gxPhysicsBehavior->m_bounceSound->m_event = *bounceSound;
	}
	else
	{
		deleteInstance(m_gxPhysicsBehavior->m_bounceSound);
		m_gxPhysicsBehavior->m_bounceSound = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/**
 * Basic rigid body physics using an Euler integrator.
 * @todo Currently, only translations are integrated. Rotations should also be integrated. (MSB)
 */

DECLARE_PERF_TIMER(PhysicsBehavior)
void PhysicsBehavior::gxIntegrate()
{
	applyGravitationalForces();
	applyFrictionalForces();

	// integrate acceleration into velocity
	gxVel().x += gxAccel().x;
	gxVel().y += gxAccel().y;
	gxVel().z += gxAccel().z;

	// when vel gets tiny, just clamp to zero
	const Real THRESH = 0.001f;
	if (fabsf(gxVel().x) < THRESH) gxVel().x = 0.0f;
	if (fabsf(gxVel().y) < THRESH) gxVel().y = 0.0f;
	if (fabsf(gxVel().z) < THRESH) gxVel().z = 0.0f;

	m_gxPhysicsBehavior->m_velMag = INVALID_VEL_MAG;
}

// web port (match rule physBulk = physics pass): at the first physics update of a frame, run the
// whole physics step of every unit whose physics is due this frame, over the unit store's PHYS list
// in object-ID order (positions and velocities in the store). Each unit's own call from the update
// heap then only hands back the sleep time its step returned. Units woken during the phase (not due
// when the pass ran) step on their own call as before. The drift: units step in ID order instead of
// the update heap's order.
void PhysicsBehavior::gxBulkPass(UnsignedInt now)
{
	static std::vector<int> s_list;	// a copy: the step can create objects, which may rebuild pass lists
	s_list = TheGxUnitStore.pass(GXB_PHYS);
	// web port (round 5, switch physSimd, fourth word bit 0; rides on physBulk): the forces of every
	// due unit first, then velocity += acceleration (and the tiny-velocity clamp) for all of them in
	// one 4-units-per-instruction pass over the store columns, then each unit's remaining step.
	// Exact (same float ops); the drift beyond physBulk's: a force one unit's step applies to a unit
	// that steps later in the frame now counts from the next frame.
	const Bool simd = (GLES3_Page_Switches4() & 1) != 0;
	if (simd)
	{
		GxUnitStore &S = TheGxUnitStore;
		static std::vector<UnsignedInt> s_due;
		s_due.assign(((size_t)S.size() + 3) & ~(size_t)3, 0u);
		for (size_t i = 0; i < s_list.size(); ++i)
		{
			Object *o = S.obj[s_list[i]];
			PhysicsBehavior *pb = o ? o->getPhysics() : nullptr;
			if (!pb || pb->friend_getNextCallFrame() != now || pb->m_gxPhysicsBehavior->m_gxPreFrame == now || o->isDisabledByType(DISABLED_HELD))
				continue;
			pb->m_gxPhysicsBehavior->m_prevAccel = pb->gxAccel();
			pb->applyGravitationalForces();
			pb->applyFrictionalForces();
			pb->m_gxPhysicsBehavior->m_velMag = INVALID_VEL_MAG;
			pb->m_gxPhysicsBehavior->m_gxIntegrated = now;
			s_due[s_list[i]] = ~0u;
		}
		gxIntegrateColumns(s_due.data(), 0, S.size());	// kernel over all rows (a job system can split the range)
	}
	for (size_t i = 0; i < s_list.size(); ++i)
	{
		Object *o = TheGxUnitStore.obj[s_list[i]];
		PhysicsBehavior *pb = o ? o->getPhysics() : nullptr;
		if (!pb || pb->friend_getNextCallFrame() != now || pb->m_gxPhysicsBehavior->m_gxPreFrame == now)
			continue;
		pb->m_gxPhysicsBehavior->m_gxPreFrame = now;
		pb->m_gxPhysicsBehavior->m_gxSleep = pb->gxStep();
	}
}

// web port (round 5): the physics integration kernel. For rows [begin, end) whose due[] is all ones:
// v += a, |v| < 0.001 -> 0, a = 0 (wasm simd128, 4 rows per instruction; same float ops).
// Reads: due[], store vx/vy/vz, ax/ay/az. Writes: vx/vy/vz, ax/ay/az of rows in [begin, end) only.
// No allocation, events, RNG or other globals: safe on a worker thread for disjoint ranges.
// (The forces before it and the rest of each unit's step after it are the main-thread parts.)
// One row, scalar (head and tail of the SIMD loop): the same float ops as the SIMD lanes.
static inline void gxIntegrateRow(float *__restrict v, float *__restrict a, Int i, UnsignedInt d)
{
	const float THRESH = 0.001f;
	const float nv0 = v[i] + a[i];
	const float nv = fabsf(nv0) < THRESH ? 0.0f : nv0;
	v[i] = d ? nv : v[i];
	a[i] = d ? 0.0f : a[i];
}

__attribute__((noinline)) void PhysicsBehavior::gxIntegrateColumns(const UnsignedInt *__restrict due, Int begin, Int end)
{
	// straight-line kernel: column pointers in __restrict locals, no branch in the loop (rows not due
	// keep their values through bitselect), two blocks of 4 rows per step as independent chains
	GxUnitStore &S = TheGxUnitStore;
	float *__restrict vx = S.vx.data(), *__restrict vy = S.vy.data(), *__restrict vz = S.vz.data();
	float *__restrict ax = S.ax.data(), *__restrict ay = S.ay.data(), *__restrict az = S.az.data();
	const Int n = end;
	Int i = begin;
	for (; i < n && (i & 3); ++i)	// to a multiple of 4
	{
		gxIntegrateRow(vx, ax, i, due[i]); gxIntegrateRow(vy, ay, i, due[i]); gxIntegrateRow(vz, az, i, due[i]);
	}
#ifdef __wasm_simd128__
	const v128_t th = wasm_f32x4_splat(0.001f), zero = wasm_f32x4_splat(0.0f);
#define GX_INTEGRATE4(V, A, M, J) \
	{ \
		const v128_t vv = wasm_v128_load(V + (J)), aa = wasm_v128_load(A + (J)); \
		v128_t nv = wasm_f32x4_add(vv, aa); \
		nv = wasm_v128_andnot(nv, wasm_f32x4_lt(wasm_f32x4_abs(nv), th));	/* tiny -> 0 */ \
		wasm_v128_store(V + (J), wasm_v128_bitselect(nv, vv, M)); \
		wasm_v128_store(A + (J), wasm_v128_bitselect(zero, aa, M)); \
	}
	for (; i + 8 <= n; i += 8)
	{
		const v128_t m0 = wasm_v128_load(due + i), m1 = wasm_v128_load(due + i + 4);
		GX_INTEGRATE4(vx, ax, m0, i) GX_INTEGRATE4(vx, ax, m1, i + 4)
		GX_INTEGRATE4(vy, ay, m0, i) GX_INTEGRATE4(vy, ay, m1, i + 4)
		GX_INTEGRATE4(vz, az, m0, i) GX_INTEGRATE4(vz, az, m1, i + 4)
	}
	for (; i + 4 <= n; i += 4)
	{
		const v128_t m0 = wasm_v128_load(due + i);
		GX_INTEGRATE4(vx, ax, m0, i) GX_INTEGRATE4(vy, ay, m0, i) GX_INTEGRATE4(vz, az, m0, i)
	}
#undef GX_INTEGRATE4
#endif
	for (; i < n; ++i)
	{
		gxIntegrateRow(vx, ax, i, due[i]); gxIntegrateRow(vy, ay, i, due[i]); gxIntegrateRow(vz, az, i, due[i]);
	}
}

UpdateSleepTime PhysicsBehavior::update()
{
	GxTimeScope gxT(GXT_PHYSICS);	// tickTiming (Common/GxTime.h)
	gxCount(GXC_PHYS_UNITS);
	if (gxMatchRule(GXRULE_PHYS_BULK))
	{
		const UnsignedInt now = TheGameLogic->getFrame();
		if (now != s_gxBulkFrame)
		{
			s_gxBulkFrame = now;
			gxBulkPass(now);
		}
		if (m_gxPhysicsBehavior->m_gxPreFrame == now)
			return m_gxPhysicsBehavior->m_gxSleep;
	}
	return gxStep();
}

// web port (measuring only, tickTiming; phase 13 part B): the physics step by section. One "GXPHYSK"
// line every 900 frames: ms since the start in the step before the transform, the transform, and after.
static double s_gxPhysMs[3]; static unsigned long long s_gxPhysN = 0; static UnsignedInt s_gxPhysPrinted = 0;
static inline double gxPhysNow() { return gxTimingOn() ? emscripten_get_now() : 0.0; }

UpdateSleepTime PhysicsBehavior::gxStep()
{
	USE_PERF_TIMER(PhysicsBehavior)
	const double gxT0 = gxPhysNow(); double gxT1 = gxT0, gxT2 = gxT0;
	if (gxTimingOn()) {
		const UnsignedInt f = TheGameLogic->getFrame();
		if (f % 900 == 0 && f != s_gxPhysPrinted) { s_gxPhysPrinted = f; printf("GXPHYSK f%u: steps %llu, before transform %.0f ms, transform %.0f ms, after %.0f ms\n", f, s_gxPhysN, s_gxPhysMs[0], s_gxPhysMs[1], s_gxPhysMs[2]); }
		++s_gxPhysN;
	}

	Object*														obj = getObject();
	const PhysicsBehaviorModuleData*	d = getPhysicsBehaviorModuleData();
	Bool															airborneAtStart = obj->isAboveTerrain();
	Real															activeVelZ = 0;
	Coord3D														bounceForce;
	Bool															gotBounceForce = false;

	DEBUG_ASSERTCRASH(!getFlag(IS_IN_UPDATE), ("impossible"));
	setFlag(IS_IN_UPDATE, true);

	if (!getFlag(UPDATE_EVER_RUN))
	{
		// set the flag so that we don't get bogus "collisions" on the first frame.
		setFlag(WAS_AIRBORNE_LAST_FRAME, airborneAtStart);
	}

	Coord3D prevPos = *obj->getPosition();
	const Bool gxPreIntegrated = m_gxPhysicsBehavior->m_gxIntegrated == TheGameLogic->getFrame();	// physSimd did forces + integration
	if (!gxPreIntegrated)
		m_gxPhysicsBehavior->m_prevAccel = gxAccel();

	// web port (switch physReuse, second word bit 26; same game): reuse this step's ground height
	const Bool gxVerify = (GLES3_Page_Switches() & 2097152) != 0;
	const Bool physReuse = (GLES3_Page_Switches2() & 67108864) != 0;

	if (!obj->isDisabledByType(DISABLED_HELD))
	{
		Matrix3D mtx = *obj->getTransformMatrix();

		if (!gxPreIntegrated)
			gxIntegrate();

		Real oldPosZ = mtx.Get_Z_Translation();

		// integrate velocity into position
		if (obj->testStatus(OBJECT_STATUS_BRAKING))
		{
			// Don't update position if the locomotor is braking.
			if (!obj->isKindOf(KINDOF_PROJECTILE))
			{
				// Things other than projectiles don't cheat in z.  jba.
				mtx.Adjust_Z_Translation(gxVel().z);
			}
		}
		else
		{
			mtx.Adjust_X_Translation(gxVel().x);
			mtx.Adjust_Y_Translation(gxVel().y);
			mtx.Adjust_Z_Translation(gxVel().z);
		}

		if (_isnan(mtx.Get_X_Translation()) || _isnan(mtx.Get_Y_Translation()) ||
			_isnan(mtx.Get_Z_Translation())) {
			DEBUG_CRASH(("Object position is NAN, deleting."));
			TheGameLogic->destroyObject(obj);
		}

		// Check when to clear the stunned status
		if (getIsStunned())
		{
			if ( (fabs(gxVel().x) < STUN_RELIEF_EPSILON &&
				    fabs(gxVel().y) < STUN_RELIEF_EPSILON &&
				    fabs(gxVel().z) < STUN_RELIEF_EPSILON)
          ||
           obj->isSignificantlyAboveTerrain() == FALSE )
			{
				setStunned(false);
				getObject()->clearModelConditionState(MODELCONDITION_STUNNED);
			}

    }

		if (getFlag(HAS_PITCHROLLYAW))
		{

			/*
				You may be tempted to do something like this:

					Real rollAngle = -mtx.Get_X_Rotation();
					Real pitchAngle = mtx.Get_Y_Rotation();
					Real yawAngle = mtx.Get_Z_Rotation();
					// do stuff to angles, then rebuild the mtx with 'em

				You must resist this temptation, because your code will be wrong!

				The problem is that you can't use these calls to later reconstruct
				the matrix... because doing such a thing is highly order-dependent,
				and furthermore, you'd have to use Euler angles (Not the Get_?_Rotation
				calls) to be able to reconstruct 'em, and that's too slow to do for
				every object every frame.

				The one exception is that it is OK to use Get_Z_Rotation() to get
				the yaw angle.
			*/

			// only update the position if we are not HELD
			// (otherwise, slowdeath sinking into ground won't work)
			Real yawRateToUse = m_gxPhysicsBehavior->m_yawRate * d->m_pitchRollYawFactor;
			Real pitchRateToUse = m_gxPhysicsBehavior->m_pitchRate * d->m_pitchRollYawFactor;
			Real rollRateToUse = m_gxPhysicsBehavior->m_rollRate * d->m_pitchRollYawFactor;

			// With a center of mass listing, pitchRate needs to dampen towards straight down/straight up
			Real offset = getCenterOfMassOffset();

			// Magnitude sets initial rate, here we care about sign
			if (offset != 0.0f)
			{
				Vector3 xvec = mtx.Get_X_Vector();
				Real xy = sqrtf(sqr(xvec.X) + sqr(xvec.Y));
				Real pitchAngle = atan2(xvec.Z, xy);
				Real remainingAngle = (offset > 0) ? ((PI/2) - pitchAngle) : (-(PI/2) + pitchAngle);
				Real s = Sin(remainingAngle);
				pitchRateToUse *= s;
			}

			// update rotation
			/// @todo Rotation should use torques, and integrate just like forces (MSB)
			// note, we DON'T want to Pre-rotate (either inplace or not),
			// since we want to add our mods to the existing matrix.
			mtx.Rotate_X(rollRateToUse);
			mtx.Rotate_Y(pitchRateToUse);
			mtx.Rotate_Z(yawRateToUse);
		}

		// do not allow object to pass through the ground
		Real groundZ = TheTerrainLogic->getLayerHeight(mtx.Get_X_Translation(), mtx.Get_Y_Translation(), obj->getLayer());
		const Real layerZ = groundZ;	// web port (physReuse): Object::calculateHeightAboveTerrain() at this x, y and layer
		if( obj->getStatusBits().test( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
		{
			groundZ += obj->getCarrierDeckHeight();
		}
		gotBounceForce = handleBounce(oldPosZ, mtx.Get_Z_Translation(), groundZ, &bounceForce);

		// remember our z-vel prior to doing ground-slam adjustment
		activeVelZ = gxVel().z;
		if (mtx.Get_Z_Translation() <= groundZ)
		{
			// Note - when vehicles are going down a slope, they will maintain a small negative
			// z velocity as they go down.  So don't slam it to 0 if they aren't slamming into the
			// ground.
			Real dz = groundZ - mtx.Get_Z_Translation();  // Our excess z velocity.
			gxVel().z += dz;							// Remove the excess z velocity.
			if (gxVel().z > 0.0f)
				gxVel().z = 0.0f;

			m_gxPhysicsBehavior->m_velMag = INVALID_VEL_MAG;

			mtx.Set_Z_Translation(groundZ);

			// this flag is ALWAYS cleared once we hit the ground.
			setFlag(ALLOW_TO_FALL, false);

			// When a stunned object hits the ground the first time, change it's model state from stunned flailing to just stunned.
			if (getFlag(IS_STUNNED))
			{
				obj->clearModelConditionState(MODELCONDITION_STUNNED_FLAILING);
				obj->setModelConditionState(MODELCONDITION_STUNNED);
			}
		}
		else if (mtx.Get_Z_Translation() > groundZ)
		{
			if (getFlag(IS_IN_FREEFALL))
			{
				obj->setDisabled(DISABLED_FREEFALL);
				obj->setModelConditionState(MODELCONDITION_FREEFALL);
			}
			else if (getFlag(STICK_TO_GROUND) && !getFlag(ALLOW_TO_FALL))
			{
				mtx.Set_Z_Translation(groundZ);
			}
		}

		gxT1 = gxPhysNow();
		if (gotBounceForce)
		{
			// Right the object after the bounce since the pitch and roll may have been affected
			Real yawAngle = getObject()->getTransformMatrix()->Get_Z_Rotation();
			setAngles(yawAngle, 0.0f, 0.0f);

			// Set the translation of the after bounce matrix to the one calculated above
			Matrix3D afterBounceMatrix = *getObject()->getTransformMatrix();
			afterBounceMatrix.Set_Translation(mtx.Get_Translation());

			// Set the result of the after bounce matrix as the object's final matrix
			obj->setTransformMatrix(&afterBounceMatrix);
		}
		else
		{
			obj->setTransformMatrix(&mtx);
		}

		gxT2 = gxPhysNow();
		// physReuse: the airborne check below would look up the same ground height again
		const Coord3D *p = obj->getPosition();
		if (physReuse && p->x == mtx.Get_X_Translation() && p->y == mtx.Get_Y_Translation())
		{
			const Real h = p->z - layerZ;
			if (gxVerify)
			{
				static Int s_checked = 0, s_bad = 0;
				++gx_verify_checked;
				if (h != p->z - TheTerrainLogic->getLayerHeight(p->x, p->y, obj->getLayer())) { ++gx_verify_mismatch; ++s_bad; printf("GXVERIFY physReuse height mismatch: object %u\n", obj->getID()); }
				if (++s_checked % 20000 == 0) printf("GXVERIFY physReuse checked %d, mismatches %d\n", s_checked, s_bad);
			}
			else
				obj->gxSetHeightAboveTerrain(h);
		}
	}

	// reset the acceleration for accumulation next frame (physSimd: zeroed by the column pass; forces
	// applied since then belong to the next frame)
	if (!gxPreIntegrated)
		gxAccel().zero();

	// clear overlap object, which will be set by PhysicsCollide later
	m_gxPhysicsBehavior->m_previousOverlap = m_gxPhysicsBehavior->m_currentOverlap;
	m_gxPhysicsBehavior->m_currentOverlap = INVALID_ID;

	if (gotBounceForce && getFlag(ALLOW_BOUNCE))
	{
		applyForce(&bounceForce);
	}

	Bool airborneAtEnd = obj->isAboveTerrain();

	// it's not good enough to check for airborne being different between
	// the start and end of this func... we have to compare since last frame,
	// since (if we're held by a parachute, for instance) we might have been
	// moved by other bits of code!
	if (getFlag(WAS_AIRBORNE_LAST_FRAME) && !airborneAtEnd && !getFlag(IMMUNE_TO_FALLING_DAMAGE))
	{
		doBounceSound(prevPos);

		// the normal always points straight down, though we could
		// get the alignWithTerrain() normal if it proves interesting
		Coord3D normal;
		normal.x = normal.y = 0.0f;
		normal.z = -1.0f;
		obj->onCollide(nullptr, obj->getPosition(), &normal);

		//
		// don't bother trying to remember how far we've fallen; instead,
		// we back-calc it from our speed & gravity... v = sqrt(2*g*h).
		// (note that m_minFallSpeedForDamage is always POSITIVE.)
		//
		// also note: since projectiles are immune to falling damage, don't
		// even bother doing this check here.
		//
		Real netSpeed = -activeVelZ - d->m_minFallSpeedForDamage;

		if (netSpeed > 0.0f && m_gxPhysicsBehavior->m_pui == nullptr)
		{
			// only apply force if it's a pretty steep fall, so that things
			// going down hills don't injure themselves (unless the hill is really steep)
			const Real MIN_ANGLE_TAN = 3.0f;	//	roughly 71 degrees
			const Real TINY_DELTA = 0.01f;
			if ((fabs(gxVel().x) <= TINY_DELTA || fabs(activeVelZ / gxVel().x) >= MIN_ANGLE_TAN) &&
				(fabs(gxVel().y) <= TINY_DELTA || fabs(activeVelZ / gxVel().y) >= MIN_ANGLE_TAN))
			{
				Real damageAmt = netSpeed * getMass() * d->m_fallHeightDamageFactor;

				DamageInfo damageInfo;
				damageInfo.in.m_damageType = DAMAGE_FALLING;
				damageInfo.in.m_deathType = DEATH_SPLATTED;
				damageInfo.in.m_sourceID = obj->getID();
				damageInfo.in.m_amount = damageAmt;
        damageInfo.in.m_shockWaveAmount = 0.0f;
				obj->attemptDamage( &damageInfo );
				//DEBUG_LOG(("Dealing %f (%f %f) points of falling damage to %s!",damageAmt,damageInfo.out.m_actualDamageDealt, damageInfo.out.m_actualDamageClipped,obj->getTemplate()->getName().str()));

				// if this killed us, add SPLATTED to get a cool death.
				if (obj->isEffectivelyDead())
				{
					obj->setModelConditionState(MODELCONDITION_SPLATTED);
				}
			}
		}
	}

	if (!airborneAtEnd)
	{
		// just in case.
		setFlag(IS_IN_FREEFALL, false);
		if (obj->isDisabledByType(DISABLED_FREEFALL))
			obj->clearDisabled(DISABLED_FREEFALL);
		obj->clearModelConditionState(MODELCONDITION_FREEFALL);
	}


	// If we are effectively dead, we shouldn't recall kill.
	if (d->m_killWhenRestingOnGround && !airborneAtEnd && isVerySmall3D(gxVel()))
	{
		if( !obj->isKindOf( KINDOF_DRONE ) || obj->isEffectivelyDead() || obj->isDisabledByType( DISABLED_UNMANNED ) )
		{
			//Must be one of the following cases in order to splat:
			//1) Not a drone
			//2) Dead drone
			//3) Unmanned drone
			obj->kill();
		}
	}

	setFlag(UPDATE_EVER_RUN, true);
	setFlag(WAS_AIRBORNE_LAST_FRAME, airborneAtEnd);

	setFlag(IS_IN_UPDATE, false);
	if (gxTimingOn()) { const double t3 = emscripten_get_now(); s_gxPhysMs[0] += gxT1 - gxT0; s_gxPhysMs[1] += gxT2 - gxT1; s_gxPhysMs[2] += t3 - gxT2; }
	return calcSleepTime();
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime PhysicsBehavior::calcSleepTime() const
{
#ifdef SLEEPY_PHYSICS
	if (isZero3D(gxVel())
			&& isZero3D(gxAccel())
			&& !getFlag(HAS_PITCHROLLYAW)
			&& !isMotive()
			&& (getObject()->getLayer() == LAYER_GROUND && !getObject()->isAboveTerrain())
			&& getCurrentOverlap() == INVALID_ID
			&& getPreviousOverlap() == INVALID_ID
			&& getFlag(UPDATE_EVER_RUN))
	{
		return UPDATE_SLEEP_FOREVER;
	}
	else
#endif
	{
		return UPDATE_SLEEP_NONE;
	}
}

//-------------------------------------------------------------------------------------------------
Real PhysicsBehavior::getVelocityMagnitude() const
{
	if (m_gxPhysicsBehavior->m_velMag == INVALID_VEL_MAG)
	{
		m_gxPhysicsBehavior->m_velMag = (Real)sqrtf( sqr(gxVel().x) + sqr(gxVel().y) + sqr(gxVel().z) );
	}
	return m_gxPhysicsBehavior->m_velMag;
}

//-------------------------------------------------------------------------------------------------
/**
 * Return the current velocity magnitude in the forward direction.
 * If velocity is opposite facing vector, the returned value will be negative.
 */
Real PhysicsBehavior::getForwardSpeed2D() const
{
	const Coord3D *dir = getObject()->getUnitDirectionVector2D();

	Real vx = gxVel().x * dir->x;
	Real vy = gxVel().y * dir->y;

	Real dot = vx + vy;

	Real speedSquared = vx*vx + vy*vy;
//	DEBUG_ASSERTCRASH( speedSquared != 0, ("zero speedSquared will overflow sqrtf()!") );// lorenzen... sanity check

	Real speed = (Real)sqrtf( speedSquared );

	if (dot >= 0.0f)
		return speed;

	return -speed;
}

//-------------------------------------------------------------------------------------------------
/**
 * Return the current velocity magnitude in the forward direction.
 * If velocity is opposite facing vector, the returned value will be negative.
 */
Real PhysicsBehavior::getForwardSpeed3D() const
{
	Vector3 dir = getObject()->getTransformMatrix()->Get_X_Vector();

	Real vx = gxVel().x * dir.X;
	Real vy = gxVel().y * dir.Y;
	Real vz = gxVel().z * dir.Z;

	Real dot = vx + vy + vz;

	Real speed = (Real)sqrtf( vx*vx + vy*vy + vz*vz );

	if (dot >= 0.0f)
		return speed;

	return -speed;
}

//-------------------------------------------------------------------------------------------------
Bool PhysicsBehavior::isCurrentlyOverlapped(Object *obj) const
{
	return obj != nullptr && obj->getID() == m_gxPhysicsBehavior->m_currentOverlap;
}

//-------------------------------------------------------------------------------------------------
Bool PhysicsBehavior::wasPreviouslyOverlapped(Object *obj) const
{
	return obj != nullptr && obj->getID() == m_gxPhysicsBehavior->m_previousOverlap;
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::scrubVelocityZ( Real desiredVelocity )
{
	if (fabs(desiredVelocity) < 0.001f)
	{
		gxVel().z = 0;
	}
	else
	{
		if ((desiredVelocity < 0 && gxVel().z < desiredVelocity) || (desiredVelocity > 0 && gxVel().z > desiredVelocity))
		{
			gxVel().z = desiredVelocity;
		}
	}
	m_gxPhysicsBehavior->m_velMag = INVALID_VEL_MAG;
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::scrubVelocity2D( Real desiredVelocity )
{
	if (desiredVelocity < 0.001f)
	{
		gxVel().x = 0;
		gxVel().y = 0;
	}
	else
	{
		Real curVelocity = sqrtf(gxVel().x*gxVel().x + gxVel().y*gxVel().y);
		if (desiredVelocity > curVelocity)
		{
			return;
		}
		desiredVelocity /= curVelocity;
		gxVel().x *= desiredVelocity;
		gxVel().y *= desiredVelocity;
	}
	m_gxPhysicsBehavior->m_velMag = INVALID_VEL_MAG;
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::addOverlap(Object *obj)
{
	if (obj && !isCurrentlyOverlapped(obj))
	{
		m_gxPhysicsBehavior->m_currentOverlap = obj->getID();
	}
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::transferVelocityTo(PhysicsBehavior* that) const
{
	if (that != nullptr)
	{
		{ const Coord3D v = gxVel(); that->gxVel().add(&v); }
		that->m_gxPhysicsBehavior->m_velMag = INVALID_VEL_MAG;
	}
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::addVelocityTo( const Coord3D *vel)
{
	if (vel != nullptr)
		gxVel().add( vel );
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::setAngles( Real yaw, Real pitch, Real roll )
{
	const Coord3D* pos = getObject()->getPosition();

	Matrix3D xfrm;
	xfrm.Make_Identity();
	xfrm.Translate( pos->x, pos->y, pos->z );
	// here we DO want to use in-place-etc, cuz we're not adding to any existing rot/etc
	xfrm.In_Place_Pre_Rotate_X( -roll );
	xfrm.In_Place_Pre_Rotate_Y( pitch );
	xfrm.In_Place_Pre_Rotate_Z( yaw );
	getObject()->setTransformMatrix( &xfrm );
}

//-------------------------------------------------------------------------------------------------
Real PhysicsBehavior::getMass() const
{
	Real mass = m_gxPhysicsBehavior->m_mass;
	ContainModuleInterface* contain = getObject()->getContain();
	if (contain)
		mass += contain->getContainedItemsMass();
	return mass;
}

//-----------------------------------------------------------------------------
inline Real calcDistSqr(const Coord3D& a, const Coord3D& b)
{
	return sqr(a.x - b.x) + sqr(a.y - b.y) + sqr(a.z - b.z);
}

//-------------------------------------------------------------------------------------------------
void PhysicsBehavior::doBounceSound(const Coord3D& prevPos)
{
	if (!m_gxPhysicsBehavior->m_bounceSound)
		return;

	const Real NORMAL_VEL_Z	= 0.25f;
	const Real NORMAL_MASS	= 50.0f;

	// get the per-unit sound for the collision which was stuffed in on Object creation.
	AudioEventRTS collisionSound = m_gxPhysicsBehavior->m_bounceSound->m_event;

//Real vel = fabs(getVelocity()->z);
// can't use velocity, because it's already been updated this frame, and will be zero... (srj)
	Real vel = fabs(prevPos.z - getObject()->getPosition()->z);

	Real mass = fabs(getMass());
	if (vel > NORMAL_VEL_Z) {
		vel = NORMAL_VEL_Z;
	}

	if (mass > NORMAL_MASS) {
		mass = NORMAL_MASS;
	}

	if (vel < 0) {
		vel = 0;
	}

	if (mass < 0) {
		mass = 0;
	}

#ifdef FIX_AUDIO
	Real volAdjust = NormalizeToRange(MuLaw(vel, NORMAL_VEL_Z, 500), -1, 1, 0.25, 1.0);
	volAdjust *= NormalizeToRange(MuLaw(mass, NORMAL_MASS, 500), -1, 1, 0.25, 1.0);
	collisionSound.setVolume(volAdjust);
#endif
	collisionSound.setObjectID(getObject()->getID());
	TheAudio->addAudioEvent(&collisionSound);
}

//-------------------------------------------------------------------------------------------------
/**
 * Resolve the collision between getObject() and other by computing
 * the amount the objects have overlapd, and applying proportional forces to
 * push them apart.
 * Note that this call only applies forces to our object, not to "other".  Since the
 * forces should be equal and opposite, this could be optimized.
 * @todo Make this work properly for non-cylindrical objects (MSB)
 * @todo Physics collision resolution is 2D - should it be 3D? (MSB)
 */
//DECLARE_PERF_TIMER(PhysicsBehavioronCollide)
void PhysicsBehavior::onCollide( Object *other, const Coord3D *loc, const Coord3D *normal )
{
	//USE_PERF_TIMER(PhysicsBehavioronCollide)
	if (m_gxPhysicsBehavior->m_pui != nullptr)
	{
		// projectiles always get a chance to handle their own collisions, and not go thru here
		if (m_gxPhysicsBehavior->m_pui->projectileHandleCollision(other))
			return;
	}

	Object *obj = getObject();

	Object* objContainedBy = obj->getContainedBy();

	// Note that other == null means "collide with ground"
	if (other == nullptr)
	{
		// if we are in a container, tell the container we collided with the ground.
		// (handy for parachutes.)
		if (objContainedBy)
		{
			objContainedBy->onCollide(other, loc, normal);
		}
		return;
	}

	// if we are containing this object, ignore collisions with it.
	Object* otherContainedBy = other->getContainedBy();
	if (otherContainedBy == obj || objContainedBy == other)
	{
		return;
	}

	// if we're both on parachutes, we never collide with each other.
	/// @todo srj -- ugh, an awful hack for paradrop problems. fix someday.
	if (obj->testStatus(OBJECT_STATUS_PARACHUTING) && other->testStatus(OBJECT_STATUS_PARACHUTING))
	{
		return;
	}

	// ignore collisions with our "ignore" thingy, if any (and vice versa)
	AIUpdateInterface* ai = obj->getAIUpdateInterface();
	if (ai != nullptr  && ai->getIgnoredObstacleID() == other->getID())
	{
/// @todo srj -- what the hell is this code doing here? ack!
		//Before we return, check for a very special case of an infantry colliding with an unmanned vehicle.
		//If this is the case, it'll become its new pilot!
		if( obj->isKindOf( KINDOF_INFANTRY ) && other->isDisabledByType( DISABLED_UNMANNED ) )
		{
			//This is in fact the case, and we are doing it here because it applies to all infantry in any unmanned vehicle.
			//This could be done via a special/new module, but doing it here doesn't require a new module update to every infantry.
			other->clearDisabled( DISABLED_UNMANNED );

			//We need to be able to test whether an object on a team has been captured, so set here that this object
			//was captured.
			other->setCaptured(true);

			other->defect( obj->getTeam(), 0 );
			//other->setTeam( obj->getTeam() );

			//In order to make things easier for the designers, we are going to transfer the name
			//of the infantry to the vehicle... so the designer can control the vehicle with their scripts.
			TheScriptEngine->transferObjectName( obj->getName(), other );

			TheGameLogic->destroyObject( obj );
		}
		return;
	}

	AIUpdateInterface* aiOther = other->getAIUpdateInterface();
	if (aiOther != nullptr  && aiOther->getIgnoredObstacleID() == obj->getID())
	{
		return;
	}

	if (isIgnoringCollisionsWith(other->getID()))
	{
		return;
	}

	Bool immobile = obj->isKindOf( KINDOF_IMMOBILE );
	Bool otherImmobile = other->isKindOf( KINDOF_IMMOBILE );

	PhysicsBehavior* otherPhysics = other->getPhysics();
	if (otherPhysics)
	{
		if (otherPhysics->isIgnoringCollisionsWith(obj->getID()))
		{
			return;
		}
	}
	else
	{
		// if the object we have collided with has no physics, it is insubstantial
		// (exception: if it's immobile.)
		if (!otherImmobile)
		{
			return;
		}
	}


	if (checkForOverlapCollision(other))
	{
		// we should overlap them, rather than bounce them, so punt here.
		// (note that we do this prior to checking for Physics update,
		// because Overlap doesn't require 'other' to have Physics...)
		return;
	}

	Real mass = getMass();
	if (immobile)
		mass = 999999.0f;

	if (ai)
	{
		// AI objects move under their own initiative, not by getting bounced.  jba.
		// unless they are dead & colliding with something immobile. (srj)
		// or parachuting and colliding with something immobile. (srj)
		if (!((obj->isEffectivelyDead() || obj->testStatus(OBJECT_STATUS_PARACHUTING)) && otherImmobile))
		{
			Bool doForce = ai->processCollision(this, other);
			if (!doForce)
				return;
		}

	}

	Coord3D usCenter, themCenter;
	obj->getGeometryInfo().getCenterPosition(*obj->getPosition(), usCenter);
	other->getGeometryInfo().getCenterPosition(*other->getPosition(), themCenter);

	Coord3D delta;
	delta.x = themCenter.x - usCenter.x;
	delta.y = themCenter.y - usCenter.y;
	delta.z = themCenter.z - usCenter.z;

	Real distSqr, usRadius, themRadius;
	if (obj->isAboveTerrain())
	{
		// do 3d testing.
		usRadius = obj->getGeometryInfo().getBoundingSphereRadius();
		themRadius = other->getGeometryInfo().getBoundingSphereRadius();
		distSqr = sqr(delta.x) + sqr(delta.y) + sqr(delta.z);
	}
	else
	{
		// do 2d testing.
		usRadius = obj->getBoundingCircleRadius();
		themRadius = other->getBoundingCircleRadius();
		distSqr = sqr(delta.x) + sqr(delta.y);
		delta.z = 0;
	}

	if (distSqr > sqr(usRadius + themRadius))
	{
		// We don't overlap at all.  How did we get here?
		return;
	}


	m_gxPhysicsBehavior->m_lastCollidee = other->getID();

	Real dist = sqrtf(distSqr);
	Real overlap = usRadius + themRadius - dist;

	// if objects are coincident, dist is zero, so force would be infinite -- clearly
	// not what we want. so just cap it here for now. (srj)
	if (dist < 1.0f)
		dist = 1.0f;

	if (getAllowCollideForce())
	{
		Real factor;
		Coord3D force;
		if (otherImmobile && !obj->isDestroyed())
		{
			if (obj->testStatus(OBJECT_STATUS_PARACHUTING))
			{
				// don't let us intersect buildings. cheat. applying a force won't work
				// cuz we are usually braking. jam it.
				Object* objToBounce = obj;
				while (objToBounce->getContainedBy() != nullptr)
					objToBounce = objToBounce->getContainedBy();

				Real bounceOutDist = usRadius * 0.1f;
				Coord3D tmp = *objToBounce->getPosition();
				tmp.x -= bounceOutDist * delta.x / dist;
				tmp.y -= bounceOutDist * delta.y / dist;
				objToBounce->setPosition(&tmp);

				objToBounce->getPhysics()->scrubVelocity2D(0);
				return;
			}

			// if other is immobile, we must apply enough force to at least stop our motion,
			// and preferably negate it, otherwise we will pass thru the object. so cheat,
			// and act like we're bouncing off the ground.

			const Real MIN_STIFF = 0.01f;
			const Real MAX_STIFF = 0.99f;
			Real stiffness = TheGlobalData->m_structureStiffness;
			if (stiffness < MIN_STIFF) stiffness = MIN_STIFF;
			if (stiffness > MAX_STIFF) stiffness = MAX_STIFF;
			// huh huh, he said "stiff"

			Real mag = getVelocityMagnitude();
			const Real MINBOUNCESPEED = 1.0f/(LOGICFRAMES_PER_SECOND*5.0f);
			if (mag < MINBOUNCESPEED)
				mag = MINBOUNCESPEED;
			factor = -mag * getMass() * stiffness;

			// if we are moving down, we may want to blow ourselves into smithereens....
			if (delta.z < 0.0f &&
					obj->getPosition()->z >= TheGlobalData->m_defaultStructureRubbleHeight)
			{
				if (other->isKindOf(KINDOF_STRUCTURE))
				{
					// fall into a building. if a vehicle, blow up. then destroy ourself (not die), regardless.
					if (obj->isKindOf(KINDOF_VEHICLE))
					{
						TheWeaponStore->createAndFireTempWeapon(getPhysicsBehaviorModuleData()->m_vehicleCrashesIntoBuildingWeaponTemplate, obj, obj->getPosition());
					}
					TheGameLogic->destroyObject(obj);
					return;
				}
				else
				{
					// fall into a nonbuilding -- whatever. if we're a vehicle, quietly do a little damage.
					if (obj->isKindOf(KINDOF_VEHICLE))
					{
						TheWeaponStore->createAndFireTempWeapon(getPhysicsBehaviorModuleData()->m_vehicleCrashesIntoNonBuildingWeaponTemplate, obj, obj->getPosition());
					}
				}
			}

			// nuke the velocity. why? very simple: we want to ignore the previous vel in favor of
			// this. in theory, we could be clever and calculate the right force to apply to achieve this,
			// but then if we were still colliding next frame, we'd get a sudden 'acceleration' of bounce
			// that would look freakish. so cheat.
			gxVel().x = 0;
			gxVel().y = 0;
			gxVel().z = 0;
			m_gxPhysicsBehavior->m_velMag = INVALID_VEL_MAG;
		}
		else
		{
			if (overlap > 5.0f)
				overlap = 5.0f;
			factor = -overlap;
		}

		force.x = factor * delta.x / dist;
		force.y = factor * delta.y / dist;
		force.z = factor * delta.z / dist;	// will be zero for 2d case.
		DEBUG_ASSERTCRASH(!(_isnan(force.x) || _isnan(force.y) || _isnan(force.z)), ("PhysicsBehavior::onCollide force NAN!"));

		applyForce( &force );
	}
}

//-------------------------------------------------------------------------------------------------
static Bool perpsLogicallyEqual( Real perpOne, Real perpTwo )
{
	// Equality with a wiggle fudge.
  const Real PERP_RANGE = 0.15f;
	return fabs( perpOne - perpTwo ) <= PERP_RANGE;
}

//-------------------------------------------------------------------------------------------------
/** Do the collision
 *  Cases:
 *  * The crusheeOther is !OVERLAPPABLE or !CRUSHABLE.								Result: return false
 *  * The crusherMe is !CRUSHER.																			Result: return false
 *  * crusher is CRUSHER, crushee is CRUSHABLE but allied.						Result: return false
 *  * crusher is CRUSHER, crushee is OVERLAPPABLE but not CRUSHABLE.  Result: return false
 *  * crushee has no PhysicsBehavior.																	Result: return false
 *  * crusher is CRUSHER, crushee is CRUSHABLE, but is too hard.			Result: return false
 *  * crusher is CRUSHER, crushee is CRUSHABLE, hardness ok.					Result: crush, return true
 *
 * Return true if we want to skip having physics push us apart // LORENZEN
 */
//-------------------------------------------------------------------------------------------------
Bool PhysicsBehavior::checkForOverlapCollision(Object *other)
{
	//This is the most Supreme Truth... that unless I am moving right now, I may not crush anything!
	if ( isVerySmall3D( *getVelocity() ) )
		return false;


  Object* crusherMe = getObject();
  Object* crusheeOther = other;

	//Determine if we can crush the other object.
	Bool selfCrushingOther = crusherMe->canCrushOrSquish( crusheeOther, TEST_CRUSH_ONLY );
	Bool selfBeingCrushed = crusheeOther->canCrushOrSquish( crusherMe, TEST_CRUSH_ONLY );

	if( selfCrushingOther && selfBeingCrushed )
	{
		//Is it possible to crush and be crushed at the same time?
		DEBUG_CRASH( ("%s (Crusher:%d, Crushable:%d) is attempting to crush %s (Crusher:%d, Crushable:%d) but it is reciprocating -- shouldn't be possible!",
			crusherMe->getTemplate()->getName().str(), crusherMe->getCrusherLevel(), crusherMe->getCrushableLevel(),
			crusheeOther->getTemplate()->getName().str(), crusheeOther->getCrusherLevel(), crusheeOther->getCrushableLevel() ) );
		return false;
	}

	// if we are being crushed, then skip all this and return true;
	// this allows us to NOT react in the normal way and just be passive to the overlap...
	if( selfBeingCrushed )
	{
		return true;
	}

	// grab physics modules if there
	PhysicsBehavior *crusherPhysics = this;
	if( crusherPhysics == nullptr )
	{
		return false;
	}

	if( !selfCrushingOther )
	{
		return false;
	}

	// ok, add this to our list-of-overlapped-things.
	crusherPhysics->addOverlap(crusheeOther);
	if (!crusherPhysics->wasPreviouslyOverlapped(crusheeOther))
	{
		DamageInfo damageInfo;
		damageInfo.in.m_damageType = DAMAGE_CRUSH;
		damageInfo.in.m_deathType = DEATH_CRUSHED;
		damageInfo.in.m_sourceID = crusherMe->getID();
		damageInfo.in.m_amount = 0.0f;	// yes, that's right -- we don't want to do damage, just to trigger the minor DamageFX, if any
		crusheeOther->attemptDamage( &damageInfo );
	}

  const Coord3D *crusheePos = crusheeOther->getPosition();
  const Coord3D *crusherPos = crusherMe->getPosition();

	BodyModuleInterface* crusheeBody = crusheeOther->getBodyModule();
	Bool frontCrushed = crusheeBody->getFrontCrushed();
	Bool backCrushed = crusheeBody->getBackCrushed();
	if( !(frontCrushed && backCrushed) )
	{
		Bool crushIt = FALSE;

		const Coord3D *dir = crusherMe->getUnitDirectionVector2D();
		const Coord3D *crusheeDir = crusheeOther->getUnitDirectionVector2D();
		Real crushPointOffsetDistance = crusheeOther->getGeometryInfo().getMajorRadius() / 2;

		Coord3D crushPointOffset;
		crushPointOffset.x = crusheeDir->x * crushPointOffsetDistance;
		crushPointOffset.y = crusheeDir->y * crushPointOffsetDistance;
		crushPointOffset.z = 0;

		Coord3D comparisonCoord;
		Real dx, dy;

		/// @todo GS To account for different sized crushers, this should be redone as a box or circle test, not a point
		// First decide which crush point has the shortest perp to our direction ray.
//		Real bestPerp = 9999;
		CrushEnum crushTarget = NO_CRUSH;
		if( frontCrushed || backCrushed )
		{
			// Degenerate case; there is only one point to consider.
			if( frontCrushed )
				crushTarget = BACK_END_CRUSH;
			else
				crushTarget = FRONT_END_CRUSH;
		}
		else
		{
			// Else, there are three points.
			Real frontPerpLength, backPerpLength, centerPerpLength;
			Coord3D frontVector, backVector, centerVector;
			{
				comparisonCoord = *crusheePos;
				comparisonCoord.x += crushPointOffset.x;
				comparisonCoord.y += crushPointOffset.y;
				frontVector = comparisonCoord;
				frontVector.x -= crusherPos->x;
				frontVector.y -= crusherPos->y; //vector from me to the front crush point
				frontVector.z = 0;

				Real rayLength = frontVector.x * dir->x + frontVector.y * dir->y;
				Coord3D dirVector;
				dirVector.x = rayLength * dir->x;
				dirVector.y = rayLength * dir->y; //vector from me to point of perp along direction ray
				dirVector.z = 0;

				Coord3D perpVector;
				perpVector.x = dirVector.x - frontVector.x;
				perpVector.y = dirVector.y - frontVector.y; //vector from the front point perp to my direction
				perpVector.z = 0;

				frontPerpLength = perpVector.length();
			}
			{
				comparisonCoord = *crusheePos;
				comparisonCoord.x -= crushPointOffset.x;
				comparisonCoord.y -= crushPointOffset.y;
				backVector = comparisonCoord;
				backVector.x -= crusherPos->x;
				backVector.y -= crusherPos->y; //vector from me to the front crush point
				backVector.z = 0;

				Real rayLength = backVector.x * dir->x + backVector.y * dir->y;
				Coord3D dirVector;
				dirVector.x = rayLength * dir->x;
				dirVector.y = rayLength * dir->y; //vector from me to point of perp along direction ray
				dirVector.z = 0;

				Coord3D perpVector;
				perpVector.x = dirVector.x - backVector.x;
				perpVector.y = dirVector.y - backVector.y; //vector from the front point perp to my direction
				perpVector.z = 0;

				backPerpLength = perpVector.length();
			}
			{
				comparisonCoord = *crusheePos;
				centerVector = comparisonCoord;
				centerVector.x -= crusherPos->x;
				centerVector.y -= crusherPos->y; //vector from me to the front crush point
				centerVector.z = 0;

				Real rayLength = centerVector.x * dir->x + centerVector.y * dir->y;
				Coord3D dirVector;
				dirVector.x = rayLength * dir->x;
				dirVector.y = rayLength * dir->y; //vector from me to point of perp along direction ray
				dirVector.z = 0;

				Coord3D perpVector;
				perpVector.x = dirVector.x - centerVector.x;
				perpVector.y = dirVector.y - centerVector.y; //vector from the front point perp to my direction
				perpVector.z = 0;

				centerPerpLength = perpVector.length();
			}

			// Now find the shortest.  Use the straightline distance to crush point as tie breaker
			if( (frontPerpLength <= centerPerpLength)  && (frontPerpLength <= backPerpLength) )
			{
				if( perpsLogicallyEqual(frontPerpLength, centerPerpLength)
					|| perpsLogicallyEqual(frontPerpLength, backPerpLength)
					)
				{
					Real frontVectorLength = frontVector.length();
					if( perpsLogicallyEqual(frontPerpLength, centerPerpLength) )
					{
						Real centerVectorLength = centerVector.length();
						if( frontVectorLength < centerVectorLength )
							crushTarget = FRONT_END_CRUSH;
						else
							crushTarget = TOTAL_CRUSH;
					}
					else if( perpsLogicallyEqual(frontPerpLength, backPerpLength) )
					{
						Real backVectorLength = backVector.length();
						if( frontVectorLength < backVectorLength )
							crushTarget = FRONT_END_CRUSH;
						else
							crushTarget = BACK_END_CRUSH;
					}
				}
				else
				{
					crushTarget = FRONT_END_CRUSH;
				}
			}
			else if( (backPerpLength <= centerPerpLength)  && (backPerpLength <= frontPerpLength) )
			{
				if( perpsLogicallyEqual(backPerpLength, centerPerpLength)
					|| perpsLogicallyEqual(backPerpLength, frontPerpLength)
					)
				{
					Real backVectorLength = backVector.length();
					if( perpsLogicallyEqual(backPerpLength, centerPerpLength) )
					{
						Real centerVectorLength = centerVector.length();
						if( backVectorLength < centerVectorLength )
							crushTarget = BACK_END_CRUSH;
						else
							crushTarget = TOTAL_CRUSH;
					}
					else if( perpsLogicallyEqual(backPerpLength, frontPerpLength) )
					{
						Real frontVectorLength = frontVector.length();
						if( backVectorLength < frontVectorLength )
							crushTarget = BACK_END_CRUSH;
						else
							crushTarget = FRONT_END_CRUSH;
					}
				}
				else
				{
					crushTarget = BACK_END_CRUSH;
				}
			}
			else // centerperp is shortest
			{
				if( perpsLogicallyEqual(centerPerpLength, backPerpLength)
					|| perpsLogicallyEqual(centerPerpLength, frontPerpLength)
					)
				{
					Real centerVectorLength = centerVector.length();
					if( perpsLogicallyEqual(centerPerpLength, frontPerpLength) )
					{
						Real frontVectorLength = frontVector.length();
						if( centerVectorLength < frontVectorLength )
							crushTarget = TOTAL_CRUSH;
						else
							crushTarget = FRONT_END_CRUSH;
					}
					else if( perpsLogicallyEqual(centerPerpLength, backPerpLength) )
					{
						Real backVectorLength = backVector.length();
						if( centerVectorLength < backVectorLength )
							crushTarget = TOTAL_CRUSH;
						else
							crushTarget = BACK_END_CRUSH;
					}
				}
				else
				{
					crushTarget = TOTAL_CRUSH;
				}
			}
		}

		Real distanceTooFarSquared = 2.25 * crushPointOffsetDistance * crushPointOffsetDistance;
		// And just because there is only one crush point left doesn't
		// mean we automatically get it. (1.5x)^2 means if we are outside the crushed
		//front point, we will not auto get the back point (only 3/8 car spread)

		// Then ask ourselves if we have passed the correct crush point (dot < 0).
		if( crushTarget == TOTAL_CRUSH )
		{
			// Check the middle crush point
			comparisonCoord = *crusheePos; //copy so can move to each crush point

			dx = comparisonCoord.x - crusherPos->x;
			dy = comparisonCoord.y - crusherPos->y;

			Real dot = dir->x * dx + dir->y * dy;
			Real distanceSquared = (dx * dx) + (dy * dy);

			if( (dot < 0)  &&  (distanceSquared < distanceTooFarSquared) )
			{
				// Past target point, but not too far in distance or angle.
				crushIt = TRUE;
			}
		}
		else if( crushTarget == FRONT_END_CRUSH )
		{
			// Check the front point.
			comparisonCoord = *crusheePos;
			comparisonCoord.x += crushPointOffset.x;
			comparisonCoord.y += crushPointOffset.y;

			dx = comparisonCoord.x - crusherPos->x;
			dy = comparisonCoord.y - crusherPos->y;

			Real dot = dir->x * dx + dir->y * dy;
			Real distanceSquared = (dx * dx) + (dy * dy);

			if( (dot < 0)  &&  (distanceSquared < distanceTooFarSquared) )
			{
				// Past target point, but not too far in distance or angle.
				crushIt = TRUE;
			}
		}
		else if( crushTarget == BACK_END_CRUSH )
		{
			// Check back point
			comparisonCoord = *crusheePos;
			comparisonCoord.x -= crushPointOffset.x;
			comparisonCoord.y -= crushPointOffset.y;

			dx = comparisonCoord.x - crusherPos->x;
			dy = comparisonCoord.y - crusherPos->y;

			Real dot = dir->x * dx + dir->y * dy;
			Real distanceSquared = (dx * dx) + (dy * dy);

			if( (dot < 0)  &&  (distanceSquared < distanceTooFarSquared) )
			{
				// Past target point, but not too far in distance or angle.
				crushIt = TRUE;
			}
		}

		if( crushIt )
		{
			// do a boat load of crush damage, and the onDie will handle cases like crushed car object
			DamageInfo damageInfo;
			damageInfo.in.m_damageType = DAMAGE_CRUSH;
			damageInfo.in.m_deathType = DEATH_CRUSHED;
			damageInfo.in.m_sourceID = crusherMe->getID();
			damageInfo.in.m_amount = HUGE_DAMAGE_AMOUNT;			// make sure they die
			crusheeOther->attemptDamage( &damageInfo );
		}

	}

	return true;
}

// ------------------------------------------------------------------------------------------------
/** Test whether unit needs to die because of being on illegal cell, upside down, outside legal bounds **/
// ------------------------------------------------------------------------------------------------
void PhysicsBehavior::testStunnedUnitForDestruction()
{
	// Only do test if unit is stunned
	if (!getFlag(IS_STUNNED))
		return;

	// Grab the object
	Object *obj = getObject();
	const Coord3D *pos = obj->getPosition();

	// If a stunned object is upside down when it hits the ground, kill it
	if(obj->getTransformMatrix()->Get_Z_Vector().Z < 0.0f)
	{
		obj->kill();
		return;
	}

	// Check if unit has exited playable area. If so, kill it
  if (obj->isOffMap())
	{
     obj->kill();
		 return;
  }

	// Check for being in cells that the unit has no locomotor for
	AIUpdateInterface *aiInt = obj->getAI();
	if (!aiInt) return;

	// Check for object being stuck on cliffs. If so kill it
	if (TheTerrainLogic->isCliffCell(pos->x, pos->y) && !aiInt->hasLocomotorForSurface(LOCOMOTORSURFACE_CLIFF))
	{
		obj->kill();
		return;
	}

	// Check for object being stuck on water. If so kill it
	if (TheTerrainLogic->isUnderwater(pos->x, pos->y) && !aiInt->hasLocomotorForSurface(LOCOMOTORSURFACE_WATER))
	{
		obj->kill();
		return;
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PhysicsBehavior::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void PhysicsBehavior::xfer( Xfer *xfer )
{

	// version
	const XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// yaw rate
	xfer->xferReal( &m_gxPhysicsBehavior->m_yawRate );

	// roll rate
	xfer->xferReal( &m_gxPhysicsBehavior->m_rollRate );

	// pitch rate
	xfer->xferReal( &m_gxPhysicsBehavior->m_pitchRate );

	// we dont' need to mess with sound stuff
	// m_gxPhysicsBehavior->m_bounceSound <---- do nothing with this

	// accel
	{ Coord3D a = gxAccel(); xfer->xferCoord3D( &a ); gxAccel() = a; }

	// prev accel
	xfer->xferCoord3D( &m_gxPhysicsBehavior->m_prevAccel );

	// velocity
	{ Coord3D v = gxVel(); xfer->xferCoord3D( &v ); gxVel() = v; }

	// prevPos (now defunct)
	if (version < 2)
	{
		Coord3D tmp;
		tmp.zero();
		xfer->xferCoord3D( &tmp );
	}

	// turning
	xfer->xferUser( &m_gxPhysicsBehavior->m_turning, sizeof( PhysicsTurningType ) );

	// ignore collisions with
	xfer->xferObjectID( &m_gxPhysicsBehavior->m_ignoreCollisionsWith );

	// flags
	xfer->xferInt( &m_gxPhysicsBehavior->m_flags );

	// mass
	xfer->xferReal( &m_gxPhysicsBehavior->m_mass );

	// current overlap
	xfer->xferObjectID( &m_gxPhysicsBehavior->m_currentOverlap );

	// previous overlap
	xfer->xferObjectID( &m_gxPhysicsBehavior->m_previousOverlap );

	// motive force applied
	xfer->xferUnsignedInt( &m_gxPhysicsBehavior->m_motiveForceExpires );

	// extra bounciness
	xfer->xferReal( &m_gxPhysicsBehavior->m_extraBounciness );

	// extra friction
	xfer->xferReal( &m_gxPhysicsBehavior->m_extraFriction );

	// we don't need to save/load this, it is acquired on object creation
	// m_gxPhysicsBehavior->m_pui  <---- do nothing with this

	// mag of current vel
	xfer->xferReal( &m_gxPhysicsBehavior->m_velMag );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PhysicsBehavior::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
