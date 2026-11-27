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

// TurretAI.h
// Author: Steven Johnson, April 2002
// Simple turret behavior

#pragma once

#include "Common/StateMachine.h"
#include "Common/GxComponentPool.h"	// web port (round 5)
#include "Common/GameMemory.h"

const Real DEFAULT_TURN_RATE = 0.01f;
const Real DEFAULT_PITCH_RATE = 0.01f;

/**
 * The TurretAI state IDs.
 * Each of these constants will be associated with an instance of a State class
 * in a given StateMachine.
 */
enum TurretStateType CPP_11(: Int)
{
	TURRETAI_IDLE,
	TURRETAI_IDLESCAN,
	TURRETAI_AIM,															///< aim turret at GoalObject
	TURRETAI_FIRE,														///< fire turret at GoalObject
	TURRETAI_RECENTER,												///< rotate turret back to default position
	TURRETAI_HOLD,														///< hold turret position for a bit before recenter

	NUM_TURRETAI_STATES
};

// FORWARD DECLARATIONS ///////////////////////////////////////////////////////////////////////////
class Weapon;
class TurretAI;

//-----------------------------------------------------------------------------------------------------------
/**
 * The AI state machine.  This is used by AIUpdate to implement all of the
 * commands in the AICommandInterface.
 */
struct GxTurretStateMachineRow;
class TurretStateMachine : public StateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( TurretStateMachine, "TurretStateMachine" );

public:
	/**
	 * The implementation of this constructor defines the states
	 * used by this machine.
	 */
	TurretStateMachine( TurretAI* tai, Object* owner, AsciiString name );

	TurretAI* getTurretAI() const;

	virtual void clear();
	virtual StateReturnType resetToDefaultState();
	virtual StateReturnType setState( StateID newStateID );

private:
	GxRow<struct GxTurretStateMachineRow> m_gxTurretStateMachineRow;	///< web port (round 5): this unit's row in TheGxTurretStateMachines
	friend struct GxTurretStateMachineRow;
	inline struct GxTurretStateMachineRow &gxRow() const;

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

// Web port (round 5, units are pure data): TurretStateMachine's per-unit state, one row per unit in TheGxTurretStateMachines;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxTurretStateMachineRow
{
	TurretAI* turretAI;
};


inline GxTurretStateMachineRow &TurretStateMachine::gxRow() const { return const_cast<GxTurretStateMachineRow &>(*m_gxTurretStateMachineRow); }
inline TurretAI* TurretStateMachine::getTurretAI() const
{ return gxRow().turretAI; }


//-----------------------------------------------------------------------------------------------------------
class TurretState : public State
{
	MEMORY_POOL_GLUE_ABC(TurretState)
protected:
	TurretState( TurretStateMachine* machine, AsciiString name ) : State( machine, name) { }
	TurretAI* getTurretAI() { return ((TurretStateMachine*)getMachine())->getTurretAI(); }
};
EMPTY_DTOR(TurretState)

//-----------------------------------------------------------------------------------------------------------
struct GxTurretAIIdleStateRow;
class TurretAIIdleState : public TurretState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TurretAIIdleState, "TurretAIIdleState")
public:
	TurretAIIdleState( TurretStateMachine* machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual StateReturnType update();

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	void resetIdleScan();
	GxRow<struct GxTurretAIIdleStateRow> m_gxTurretAIIdleStateRow;	///< web port (round 5): this unit's row in TheGxTurretAIIdleStates
	friend struct GxTurretAIIdleStateRow;
	inline struct GxTurretAIIdleStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): TurretAIIdleState's per-unit state, one row per unit in TheGxTurretAIIdleStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxTurretAIIdleStateRow
{
	UnsignedInt nextIdleScan;
};


inline GxTurretAIIdleStateRow &TurretAIIdleState::gxRow() const { return const_cast<GxTurretAIIdleStateRow &>(*m_gxTurretAIIdleStateRow); }

//-----------------------------------------------------------------------------------------------------------
struct GxTurretAIIdleScanStateRow;
class TurretAIIdleScanState : public TurretState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TurretAIIdleScanState, "TurretAIIdleScanState")
public:
	TurretAIIdleScanState( TurretStateMachine* machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
private:
	GxRow<struct GxTurretAIIdleScanStateRow> m_gxTurretAIIdleScanStateRow;	///< web port (round 5): this unit's row in TheGxTurretAIIdleScanStates
	friend struct GxTurretAIIdleScanStateRow;
	inline struct GxTurretAIIdleScanStateRow &gxRow() const;
};

// Web port (round 5, units are pure data): TurretAIIdleScanState's per-unit state, one row per unit in TheGxTurretAIIdleScanStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxTurretAIIdleScanStateRow
{
	Real desiredAngle;
};


inline GxTurretAIIdleScanStateRow &TurretAIIdleScanState::gxRow() const { return const_cast<GxTurretAIIdleScanStateRow &>(*m_gxTurretAIIdleScanStateRow); }

//-----------------------------------------------------------------------------------------------------------
/**
 * Aim a turret at GoalObject
 */
class TurretAIAimTurretState : public TurretState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TurretAIAimTurretState, "TurretAIAimTurretState")
private:
#ifdef INTER_TURRET_DELAY
	UnsignedInt m_extraDelay;
#endif
public:
	TurretAIAimTurretState( TurretStateMachine* machine ) : TurretState( machine, "TurretAIAimTurretState" )
	{

	}
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	// snapshot interface	STUBBED - no member vars to save. jba.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
};
EMPTY_DTOR(TurretAIAimTurretState)

//-----------------------------------------------------------------------------------------------------------
/**
 * Rotate a turret back to its default position
 */
class TurretAIRecenterTurretState : public TurretState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TurretAIRecenterTurretState, "TurretAIRecenterTurretState")
public:
	TurretAIRecenterTurretState( TurretStateMachine* machine ) : TurretState( machine, "TurretAIRecenterTurretState" ) { }
	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	// snapshot interface	STUBBED - no member vars to save. jba.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
};
EMPTY_DTOR(TurretAIRecenterTurretState)

//-----------------------------------------------------------------------------------------------------------
/**
 * Hold turret position for a bit before recenter
 */
struct GxTurretAIHoldTurretStateRow;
class TurretAIHoldTurretState : public TurretState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TurretAIHoldTurretState, "TurretAIHoldTurretState")
private:
	GxRow<struct GxTurretAIHoldTurretStateRow> m_gxTurretAIHoldTurretStateRow;	///< web port (round 5): this unit's row in TheGxTurretAIHoldTurretStates
	friend struct GxTurretAIHoldTurretStateRow;
	inline struct GxTurretAIHoldTurretStateRow &gxRow() const;
public:
	TurretAIHoldTurretState( TurretStateMachine* machine );	///< web port (round 5): defined in the .cpp

	virtual StateReturnType onEnter();
	virtual void onExit( StateExitType status );
	virtual StateReturnType update();
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();
};

// Web port (round 5, units are pure data): TurretAIHoldTurretState's per-unit state, one row per unit in TheGxTurretAIHoldTurretStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxTurretAIHoldTurretStateRow
{
	UnsignedInt timestamp;
};


inline GxTurretAIHoldTurretStateRow &TurretAIHoldTurretState::gxRow() const { return const_cast<GxTurretAIHoldTurretStateRow &>(*m_gxTurretAIHoldTurretStateRow); }

//-------------------------------------------------------------------------------------------------
class TurretAIData : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TurretAIData, "TurretAIData")
public:
	Real						m_turnRate;
	Real						m_pitchRate;
	Real						m_naturalTurretAngle;
	Real						m_naturalTurretPitch;
	Real						m_turretFireAngleSweep[WEAPONSLOT_COUNT];	///< if nonzero, sweep within +/- this angle range while firing
	Real						m_turretSweepSpeedModifier[WEAPONSLOT_COUNT];	///< While sweeping, change your speed by this
	Real						m_firePitch;						///< if nonzero, I am on target at this fixed pitch, not when pointing at target
	Real						m_minPitch;							///< dependent on allowspitch. defaults to 0 (horizontal). The lowest pitch allowed (negative to allow pointing down of a high turret)
	Real						m_groundUnitPitch;			///< dependent on allowspitch. defaults to 0 (horizontal). The lowest pitch allowed when firing at ground units to give the weapon an arc.  jba
	UnsignedInt			m_turretWeaponSlots;		///< which WeaponSlots are controlled by this turret
#ifdef INTER_TURRET_DELAY
	UnsignedInt			m_interTurretDelay;			///< special-case for multiturret battleships
#endif
	Real						m_minIdleScanAngle;			///< max angle the turret can turn while idling
	Real						m_maxIdleScanAngle;			///< max angle the turret can turn while idling
	UnsignedInt			m_minIdleScanInterval;	///< min interval between idle scans
	UnsignedInt			m_maxIdleScanInterval;	///< max interval between idle scans
	UnsignedInt			m_recenterTime;					///< time to wait before recentering turret
	Bool						m_initiallyDisabled;		///< manually controlled and disabled.
	Bool						m_firesWhileTurning;    ///< so the firing state does not instantly expire the turning state
	Bool						m_isAllowsPitch;				///< This type of turret can pitch up and down as well as spin

	TurretAIData();
	static void buildFieldParse(MultiIniFieldParse& p);

	static void parseTurretSweep(INI* ini, void *instance, void *store, const void* userData);
	static void parseTurretSweepSpeed(INI* ini, void *instance, void *store, const void* userData);
};
EMPTY_DTOR(TurretAIData)

//-----------------------------------------------------------------------------------------------------
enum TurretTargetType CPP_11(: Int)
{
	TARGET_NONE,
	TARGET_OBJECT,
	TARGET_POSITION
};

//-----------------------------------------------------------------------------------------------------
/**
 * Turret behavior implementation.
 */
struct GxTurretAIRow;
class TurretAI : public MemoryPoolObject, public Snapshot, public NotifyWeaponFiredInterface
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( TurretAI, "TurretAI" )

public:
	TurretAI(Object* owner, const TurretAIData* data, WhichTurretType tur);
	// virtual destructor prototype provided by memory pool declaration

	Real getTurretAngle() const;
	Real getTurretPitch() const;
	Real getMinPitch() const;
	Bool isAllowsPitch() const;
	Real getTurnRate() const;
	Real getNaturalTurretAngle() const;
	Real getPitchRate() const;
	Real getFirePitch() const;
	Real getGroundUnitPitch() const;
	Real getNaturalTurretPitch() const;
	Real getTurretFireAngleSweepForWeaponSlot( WeaponSlotType slot ) const;
	Real getTurretSweepSpeedModifierForWeaponSlot( WeaponSlotType slot ) const;
	Real getMinIdleScanAngle() const;
	Real getMaxIdleScanAngle() const;
	UnsignedInt getMinIdleScanInterval() const;
	UnsignedInt getMaxIdleScanInterval() const;
	UnsignedInt getRecenterTime() const;
	Object* getOwner();
	const Object* getOwner() const;

	Bool isOwnersCurWeaponOnTurret() const;
	Bool isWeaponSlotOnTurret(WeaponSlotType wslot) const;
	Bool isAttackingObject() const { return m_target == TARGET_OBJECT; }
	Bool isForceAttacking() const;

	// this will cause the turret to continuously track the given victim.
	// passing null will allow the turret to revert back to "normal" position.
	void setTurretTargetObject(Object* o, Bool forceAttacking);
	void setTurretTargetPosition(const Coord3D* pos);
	void recenterTurret();
	Bool isTurretInNaturalPosition() const;

	void setTurretEnabled( Bool enabled );
	Bool isTurretEnabled() const;

	/**
		return true iff the turret is trying to aim at the victim, BUT not yet pointing in
		the right dir.
	*/
	Bool isTryingToAimAtTarget(const Object* victim) const;

	UpdateSleepTime updateTurretAI();			///< implement this module's behavior

	virtual void notifyFired();
	virtual void notifyNewVictimChosen(Object* victim);
	virtual const Coord3D* getOriginalVictimPos() const { return nullptr; }	// yes, we return nullptr here
	virtual Bool isWeaponSlotOkToFire(WeaponSlotType wslot) const;

	// these are only for use by the state machines... don't call them otherwise, please
	Bool friend_turnTowardsAngle(Real desiredAngle, Real rateModifier, Real relThresh);
	Bool friend_turnTowardsPitch(Real pitch, Real rateModifier);
	Bool friend_getPositiveSweep() const;
	void friend_setPositiveSweep(Bool b);
	Bool friend_isSweepEnabled() const;
	WhichTurretType friend_getWhichTurret() const;
#ifdef INTER_TURRET_DELAY
	UnsignedInt friend_getInterTurretDelay();
#endif
	Bool friend_isAnyWeaponInRangeOf(const Object* o) const;
	TurretTargetType friend_getTurretTarget( Object*& obj, Coord3D& pos, Bool clearDeadTargets = TRUE ) const;
	Bool friend_getTargetWasSetByIdleMood() const { return m_targetWasSetByIdleMood; }
	const Team* friend_getVictimInitialTeam() const;
	void friend_checkForIdleMoodTarget();
	UnsignedInt friend_getNextIdleMoodTargetFrame() const;
	void friend_notifyStateMachineChanged();

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

private:


	void startRotOrPitchSound();					///< start turret rotation sound
	void stopRotOrPitchSound();						///< stop turret rotation sound
	void removeSelfAsTargeter();

#ifdef INTER_TURRET_DELAY
	void getOtherTurretWeaponInfo(Int& numSelf, Int& numSelfReloading, Int& numSelfReady, Int& numOther, Int& numOtherReloading, Int& numOtherReady) const;
#endif

	GxRow<struct GxTurretAIRow> m_gxTurretAIRow;	///< web port (round 5): this unit's row in TheGxTurretAIs
	friend struct GxTurretAIRow;
	inline struct GxTurretAIRow &gxRow() const;
	mutable TurretTargetType	m_target;

	mutable Bool						m_targetWasSetByIdleMood : 1;

};

// Web port (round 5, units are pure data): TurretAI's per-unit state, one row per unit in TheGxTurretAIs;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxTurretAIRow
{
	const TurretAIData* data;
	WhichTurretType whichTurret;
	Object* owner;
	TurretStateMachine* turretStateMachine;
	Real angle;
	Real pitch;
	AudioEventRTS turretRotOrPitchSound;
	UnsignedInt enableSweepUntil;
	Team* victimInitialTeam;
	UnsignedInt continuousFireExpirationFrame;
	UnsignedInt sleepUntil;
	Bool playRotSound;
	Bool playPitchSound;
	Bool positiveSweep;
	Bool didFire;
	Bool enabled;
	Bool firesWhileTurning;
	Bool isForceAttacking;
};


inline GxTurretAIRow &TurretAI::gxRow() const { return const_cast<GxTurretAIRow &>(*m_gxTurretAIRow); }
inline Real TurretAI::getTurretAngle() const
{ return gxRow().angle; }
inline Real TurretAI::getTurretPitch() const
{ return gxRow().pitch; }
inline Real TurretAI::getMinPitch() const
{ return gxRow().data->m_minPitch; }
inline Bool TurretAI::isAllowsPitch() const
{ return gxRow().data->m_isAllowsPitch; }
inline Real TurretAI::getTurnRate() const
{ return gxRow().data->m_turnRate; }
inline Real TurretAI::getNaturalTurretAngle() const
{ return gxRow().data->m_naturalTurretAngle; }
inline Real TurretAI::getPitchRate() const
{ return gxRow().data->m_pitchRate; }
inline Real TurretAI::getFirePitch() const
{ return gxRow().data->m_firePitch; }
inline Real TurretAI::getGroundUnitPitch() const
{ return gxRow().data->m_groundUnitPitch; }
inline Real TurretAI::getNaturalTurretPitch() const
{ return gxRow().data->m_naturalTurretPitch;	}
inline Real TurretAI::getMinIdleScanAngle() const
{ return gxRow().data->m_minIdleScanAngle;	}
inline Real TurretAI::getMaxIdleScanAngle() const
{ return gxRow().data->m_maxIdleScanAngle;	}
inline UnsignedInt TurretAI::getMinIdleScanInterval() const
{ return gxRow().data->m_minIdleScanInterval;	}
inline UnsignedInt TurretAI::getMaxIdleScanInterval() const
{ return gxRow().data->m_maxIdleScanInterval;	}
inline UnsignedInt TurretAI::getRecenterTime() const
{ return gxRow().data->m_recenterTime;	}
inline Object* TurretAI::getOwner()
{ return gxRow().owner; }
inline const Object* TurretAI::getOwner() const
{ return gxRow().owner; }
inline Bool TurretAI::isForceAttacking() const
{ return gxRow().isForceAttacking; }
inline Bool TurretAI::isTurretEnabled() const
{ return gxRow().enabled; }
inline Bool TurretAI::friend_getPositiveSweep() const
{ return gxRow().positiveSweep; }
inline void TurretAI::friend_setPositiveSweep(Bool b)
{ gxRow().positiveSweep = b; }
inline WhichTurretType TurretAI::friend_getWhichTurret() const
{ return gxRow().whichTurret; }
inline const Team* TurretAI::friend_getVictimInitialTeam() const
{ return gxRow().victimInitialTeam; }

