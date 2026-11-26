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

// StateMachine.h
// Finite state machine encapsulation
// Author: Michael S. Booth, January 2002

#pragma once

#include "Common/GameMemory.h"
#include "Common/GxComponentPool.h"	// web port (round 5)
#include "Common/GameType.h"
#include "Common/ModelState.h"
#include "Common/Snapshot.h"
#include "Common/Xfer.h"

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

class State;
class StateMachine;
class Object;

//#undef STATE_MACHINE_DEBUG
#if defined(RTS_DEBUG)
	#define STATE_MACHINE_DEBUG
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

enum { MACHINE_DONE_STATE_ID = 999998, INVALID_STATE_ID = 999999 };
typedef UnsignedInt StateID;									///< used to denote individual states
typedef Bool (*StateTransFuncPtr)( State *state, void* userData );

/**
 * State return codes
 */
enum StateReturnType CPP_11(: Int)
{
	// note that all positive values are reserved for STATE_SLEEP!

	STATE_CONTINUE	= 0,						///< stay in this state (only for update method)
	STATE_SUCCESS		= -1,						///< state finished successfully, go to next state
	STATE_FAILURE		= -2,						///< state finished abnormally, go to next state
};

#define STATE_SLEEP(numFrames)				((StateReturnType)(numFrames))
#define IS_STATE_SLEEP(ret)						((Int)(ret) > 0)
#define GET_STATE_SLEEP_FRAMES(ret)		((UnsignedInt)(ret))

// (we use 0x3fffffff so that we can add offsets and not overflow...
//		at 30fps that's around ~414 days!)
#define STATE_SLEEP_FOREVER		STATE_SLEEP(0x3fffffff)

// this is mainly useful for states that enclose other state machines...
// where, even if the enclosed machine is sleeping, the encloser still needs
// to run every frame.
inline StateReturnType CONVERT_SLEEP_TO_CONTINUE(StateReturnType s)
{
	return IS_STATE_SLEEP(s) ? STATE_CONTINUE : s;
}

// this is mainly useful for states that enclose other state machines...
// where the encloser and enclosee both might sleep. we need to choose the min
// sleep time that satisfies both.
inline StateReturnType MIN_SLEEP(UnsignedInt encloserSleep, StateReturnType encloseeResult)
{
	if (IS_STATE_SLEEP(encloseeResult))
	{
		UnsignedInt encloseeSleep = GET_STATE_SLEEP_FRAMES(encloseeResult);
		return STATE_SLEEP(min(encloserSleep, encloseeSleep));
	}
	else
	{
		// if enclosee needs to stay awake, we better do so, regardless
		return encloseeResult;
	}
}


/**
 * Special argument for onCondition. It means when the given condition
 * becomes true, the state machine will exit and return the given status.
 */
enum
{
	EXIT_MACHINE_WITH_SUCCESS = 9998,
	EXIT_MACHINE_WITH_FAILURE = 9999
};

/**
 * Parameters for onExit().
 */
enum StateExitType CPP_11(: Int)
{
	EXIT_NORMAL,							///< state exited due to normal state transitioning
	EXIT_RESET								///< state exited due to state machine reset
};

struct StateConditionInfo
{
	StateTransFuncPtr	test;
	StateID						toStateID;
	void*							userData;

	StateConditionInfo(StateTransFuncPtr t, StateID id, void* ud) : test(t), toStateID(id), userData(ud) { }
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
/**
 * An abstraction of a machine's "state".
 */
struct GxStateRow;
class State : public MemoryPoolObject, public Snapshot
{
	MEMORY_POOL_GLUE_ABC( State )						///< this abstract class needs memory pool hooks
public:
	State( StateMachine *machine, AsciiString name);
	// already defined by MPO.
	//virtual ~State() { }

	virtual StateReturnType onEnter() { return STATE_CONTINUE; }	///< executed once when entering state
	virtual void onExit( StateExitType status ) { }											///< executed once when leaving state
	virtual StateReturnType update() = 0;	///< implements this state's behavior, decides when to change state

	virtual Bool isIdle() const { return false; }
	virtual Bool isAttack() const { return false; }
	virtual Bool isGuardIdle() const { return FALSE; }
	//Definition of busy -- when explicitly in the busy state. Moving or attacking is not considered busy!
	virtual Bool isBusy() const { return false; }
	virtual Bool gxInnerIdle() const { return TRUE; }	///< web port (batchScan): an inner machine (attack move / follow) is idle, so update() makes the routine mood scan

	StateMachine* getMachine();		///< return the machine this state is part of
	StateID getID() const;			///< get this state's id

	Object* getMachineOwner();
	const Object* getMachineOwner() const;
	Object* getMachineGoalObject();
	const Object* getMachineGoalObject() const;
	const Coord3D* getMachineGoalPosition() const;

#ifdef STATE_MACHINE_DEBUG
	virtual AsciiString getName() const;
	std::vector<StateID> *getTransitions();
#endif

	// for internal use by the StateMachine class ---------------------------------------------------------
	void friend_setID( StateID id );			///< define this state's id (for use only by StateMachine class)
	void friend_onSuccess( StateID toStateID );	///< define which state to move to after successful completion
	void friend_onFailure( StateID toStateID );	///< define which state to move to after failure
	void friend_onCondition( StateTransFuncPtr test, StateID toStateID, void* userData, const char* description = nullptr );	///< define when to change state
	StateReturnType friend_checkForTransitions( StateReturnType status );	///< given a return code, handle state transitions
	StateReturnType friend_checkForSleepTransitions( StateReturnType status );	///< given a return code, handle state transitions

protected:

#ifdef STATE_MACHINE_DEBUG
	inline void setName(AsciiString n) { gxRow().name = n; }
#endif

protected:
	// snapshot interface	 - pure virtual here.
	// Essentially all the member data gets set up on creation and shouldn't change.
	// So none of it needs to be saved, and it nicely forces all user states to
	// remember to implement crc, xfer & loadPostProcess.  jba
	virtual void crc( Xfer *xfer )=0;
	virtual void xfer( Xfer *xfer )=0;
	virtual void loadPostProcess()=0;

private:

	struct TransitionInfo
	{
		StateTransFuncPtr		test;											///< the condition evaluation function
		StateID							toStateID;								///< the state to transition to
		void*								userData;									///< data passed to transFuncPtr.
#ifdef STATE_MACHINE_DEBUG
		const char*					description;							///< description (for debugging purposes)
#endif

		TransitionInfo(StateTransFuncPtr t, StateID id, void* ud, const char* desc) :
			test(t),
			toStateID(id),
			userData(ud)
#ifdef STATE_MACHINE_DEBUG
			, description(desc)
#endif
		{ }
	};

	GxRow<struct GxStateRow> m_gxStateRow;	///< web port (round 5): this unit's row in TheGxStates
	friend struct GxStateRow;
	inline struct GxStateRow &gxRow() const;

protected:
#ifdef STATE_MACHINE_DEBUG
#endif
};

// Web port (round 5, units are pure data): State's per-unit state, one row per unit in TheGxStates;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxStateRow
{
	StateID ID;
	StateID successStateID;
	StateID failureStateID;
	std::vector<State::TransitionInfo> transitions;
	StateMachine* machine;
	AsciiString name;
};


inline GxStateRow &State::gxRow() const { return const_cast<GxStateRow &>(*m_gxStateRow); }
inline StateMachine* State::getMachine()
{ return gxRow().machine; }
inline StateID State::getID() const
{ return gxRow().ID; }
#if defined(STATE_MACHINE_DEBUG)
inline AsciiString State::getName() const
{return gxRow().name;}
#endif
inline void State::friend_setID( StateID id )
{ gxRow().ID = id; }
inline void State::friend_onSuccess( StateID toStateID )
{ gxRow().successStateID = toStateID; }
inline void State::friend_onFailure( StateID toStateID )
{ gxRow().failureStateID = toStateID; }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

/**
 * A finite state machine.
 */
struct GxStateMachineRow;
class StateMachine : public MemoryPoolObject, public Snapshot
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( StateMachine, "StateMachinePool" );

public:
	/**
	 * All of the states used by this machine should be
	 * instantiated and defined via defineState() in the
	 * machine's constructor.
	 */
	StateMachine( Object *owner, AsciiString name );
	// virtual destructor defined by MemoryPool

	virtual StateReturnType updateStateMachine();				///< run one step of the machine

	virtual void clear();																///< clear the machine's internals to a known, initialized state

	virtual StateReturnType resetToDefaultState();			///< clear the machine's internals and set to the default state

	/** must be called to jump the StateMachine into the default state... this may fail
		(return a failure code), so it can't be done via the ctor. you MUST call this before
		using the state machine. */
	virtual StateReturnType initDefaultState();

	virtual StateReturnType setState( StateID newStateID );			///< change the current state of the machine (which may cause further state changes, due to onEnter)

	void Add_Ref() const;
	void Release_Ref() const;
	UnsignedByte Num_Refs() const;

	StateID getCurrentStateID() const;	///< return the id of the current state of the machine
	Bool isInIdleState() const;	// stateless things are considered 'idle'
	Bool isInAttackState() const;	// stateless things are considered 'idle'
	inline Bool gxInnerIdle() const;	///< web port (batchScan): the current state's gxInnerIdle()
	Bool isInForceAttackState() const;	// stateless things are considered 'idle'
	Bool isInGuardIdleState() const; // stateless things aren't guard idle.

	//Definition of busy -- when explicitly in the busy state. Moving or attacking is not considered busy!
	Bool isInBusyState() const;	// stateless things are not considered 'busy'

	// no, this is now deprecated. you should strive to avoid having to get the current state.
	// try to make do with getCurrentStateID() or isInIdleState() instead. (srj)
	// State *getCurrentState() { return gxRow().currentState; }	///< return the current state of the machine

	/**
	 * Lock/unlock this state machine.
	 * If a machine is locked, it cannot be reset, or given external setStates(), etc.
	 */
	void lock(const char* msg);

	void unlock();

	Bool isLocked() const;

	/**
	 * Get the object that "owns" this machine.
	 * There need not be an object with a machine, but it is so common
	 * that it is useful to have this method in the generic state machine.
	 */
	Object * getOwner();
	const Object * getOwner() const;

	// common parameters for state machines
	void setGoalObject( const Object *obj );
	Object *getGoalObject();
	const Object *getGoalObject() const;
	void setGoalPosition( const Coord3D *pos );
	const Coord3D * getGoalPosition() const;
	Bool isGoalObjectDestroyed() const;  ///< Returns true if we had a goal object, but it has been destroyed.

	virtual void halt(); ///< Stops the state machine & disables it in preparation for deleting it.

	//
	// The following methods are for internal use by the State class
	//
	StateReturnType internalSetState( StateID newStateID );	///< for internal use only - change the current state of the machine

#if defined(RTS_DEBUG)
	UnsignedInt peekSleepTill() const;
#endif

#ifdef STATE_MACHINE_DEBUG
	Bool getWantsDebugOutput() const;
	void setDebugOutput( Bool output );
	void setName( AsciiString name);
	inline AsciiString getName() const {return gxRow().name;}
	virtual AsciiString getCurrentStateName() const;
#else
	Bool getWantsDebugOutput() const { return false; }
	AsciiString getCurrentStateName() const { return AsciiString::TheEmptyString;}
#endif

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess();

protected:

	/**
	 * Given a unique (to this machine) integer ID representing a state, and an instance
	 * of that state, the machine records this as a possible state, and
	 * internally maps the given integer ID to the state instance.
	 * These state id's are used to change the machine's state via setState().
	 */
	void defineState( StateID id, State *state,
										StateID successID,
										StateID failureID,
										const StateConditionInfo* conditions = nullptr);

	State* internalGetState( StateID id );

private:

	void internalClear();
	void internalSetGoalObject( const Object *obj );
	void internalSetGoalPosition( const Coord3D *pos);


	void gxNoteCurrentState();	///< web port: refresh the cached current-state ID / idleness (after every change)
public:
	inline StateID gxCachedStateID() const;	///< web port: getCurrentStateID(), kept in the row
	inline UnsignedInt gxSleepTill() const;	///< web port: frame the machine sleeps until (0 = awake)
	inline Bool gxCachedIdle() const;	///< web port: isInIdleState(), kept in the row
protected:
	GxRow<struct GxStateMachineRow> m_gxStateMachineRow;	///< web port (round 5): this unit's row in TheGxStateMachines
	friend struct GxStateMachineRow;
	inline struct GxStateMachineRow &gxRow() const;






#ifdef STATE_MACHINE_DEBUG
#endif
};

// Web port (round 5, units are pure data): StateMachine's per-unit state, one row per unit in TheGxStateMachines;
// the module is a facade over its row (members read and write it through macros in its .cpp).
struct GxStateMachineRow
{
	std::map<StateID, State *> stateMap;
	Object* owner;
	UnsignedInt sleepTill;
	StateID defaultStateID;
	State* currentState;
	ObjectID goalObjectID;
	Coord3D goalPosition;
	Bool locked;
	Bool defaultStateInited;
	RefCountValue<UnsignedByte> refCount;
	Bool debugOutput;
	AsciiString name;
	const char* lockedby;
	StateID gxCurID = INVALID_STATE_ID;	///< cached currentState->getID() (switch dataBehaviors)
	Bool gxCurIdle = true;	///< cached currentState->isIdle() (a state's isIdle() is fixed per state class)
};


inline GxStateMachineRow &StateMachine::gxRow() const { return const_cast<GxStateMachineRow &>(*m_gxStateMachineRow); }
inline StateID StateMachine::gxCachedStateID() const { return gxRow().gxCurID; }
inline UnsignedInt StateMachine::gxSleepTill() const { return gxRow().sleepTill; }
inline Bool StateMachine::gxCachedIdle() const { return gxRow().gxCurIdle; }
inline void StateMachine::Add_Ref() const
{ gxRow().refCount.Add_Ref(); }
inline void StateMachine::Release_Ref() const
{ gxRow().refCount.Release_Ref(MemoryPoolObject::deleteInstanceInternal, this); }
inline UnsignedByte StateMachine::Num_Refs() const
{ return gxRow().refCount.Num_Refs(); }
inline StateID StateMachine::getCurrentStateID() const
{ return gxRow().currentState ? gxRow().currentState->getID() : INVALID_STATE_ID; }
inline Bool StateMachine::isInIdleState() const
{ return gxRow().currentState ? gxRow().currentState->isIdle() : true; }
inline Bool StateMachine::gxInnerIdle() const
{ return gxRow().currentState ? gxRow().currentState->gxInnerIdle() : TRUE; }
inline Bool StateMachine::isInAttackState() const
{ return gxRow().currentState ? gxRow().currentState->isAttack() : true; }
inline Bool StateMachine::isInForceAttackState() const
{ return gxRow().currentState ? gxRow().currentState->isIdle() : true; }
inline Bool StateMachine::isInGuardIdleState() const
{ return gxRow().currentState ? gxRow().currentState->isGuardIdle() : FALSE; }
inline Bool StateMachine::isInBusyState() const
{ return gxRow().currentState ? gxRow().currentState->isBusy() : false; }
inline void StateMachine::lock(const char* msg)
{
		gxRow().locked = true;
#ifdef STATE_MACHINE_DEBUG
		gxRow().lockedby = msg;
#endif
	}
inline void StateMachine::unlock()
{
		gxRow().locked = false;
#ifdef STATE_MACHINE_DEBUG
		gxRow().lockedby = nullptr;
#endif
	}
inline Bool StateMachine::isLocked() const
{ return gxRow().locked; }
inline Object * StateMachine::getOwner()
{ return gxRow().owner; }
inline const Object * StateMachine::getOwner() const
{ return gxRow().owner; }
inline const Coord3D * StateMachine::getGoalPosition() const
{ return &gxRow().goalPosition; }
#if (defined(RTS_DEBUG))
inline UnsignedInt StateMachine::peekSleepTill() const
{ return gxRow().sleepTill; }
#endif
#if defined(STATE_MACHINE_DEBUG)
inline void StateMachine::setDebugOutput( Bool output )
{ gxRow().debugOutput = output; }
#endif
#if defined(STATE_MACHINE_DEBUG)
inline void StateMachine::setName( AsciiString name)
{gxRow().name = name;}
#endif
#if defined(STATE_MACHINE_DEBUG)
inline AsciiString StateMachine::getCurrentStateName() const
{ return gxRow().currentState ? gxRow().currentState->getName() : AsciiString::TheEmptyString;}
#endif


//-----------------------------------------------------------------------------

inline Object* State::getMachineOwner() { return gxRow().machine->getOwner(); }
inline const Object* State::getMachineOwner() const { return gxRow().machine->getOwner(); }

inline Object* State::getMachineGoalObject() { return gxRow().machine->getGoalObject(); }		///< return the machine this state is part of
inline const Object* State::getMachineGoalObject() const { return gxRow().machine->getGoalObject(); }		///< return the machine this state is part of

inline const Coord3D* State::getMachineGoalPosition() const { return gxRow().machine->getGoalPosition(); }		///< return the machine this state is part of

//-----------------------------------------------------------------------------------------------------------
/**
	A utility state that immediately succeeds.
*/
class SuccessState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(SuccessState, "SuccessState")
public:
	SuccessState( StateMachine *machine ) : State( machine, "SuccessState") { }
	virtual StateReturnType onEnter() { return STATE_SUCCESS; }
	virtual StateReturnType update() { return STATE_SUCCESS; }
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};

};
EMPTY_DTOR(SuccessState)

//-----------------------------------------------------------------------------------------------------------
/**
	A utility state that immediately fails.
*/
class FailureState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(FailureState, "FailureState")
public:
	FailureState( StateMachine *machine ) : State( machine, "FailureState") { }
	virtual StateReturnType onEnter() { return STATE_FAILURE; }
	virtual StateReturnType update() { return STATE_FAILURE; }
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
};
EMPTY_DTOR(FailureState)


//-----------------------------------------------------------------------------------------------------------
/**
	A utility state that never exits (except due to conditions).
*/
class ContinueState :  public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(ContinueState, "ContinueState")
public:
	ContinueState( StateMachine *machine ) : State( machine, "ContinueState" ) { }
	virtual StateReturnType onEnter() { return STATE_CONTINUE; }
	virtual StateReturnType update() { return STATE_CONTINUE; }
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
};
EMPTY_DTOR(ContinueState)


//-----------------------------------------------------------------------------------------------------------
/**
	A utility state that sleeps forever.
*/
class SleepState :  public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(SleepState, "SleepState")
public:
	SleepState( StateMachine *machine ) : State( machine, "SleepState" ) { }
	virtual StateReturnType onEnter() { return STATE_SLEEP_FOREVER; }
	virtual StateReturnType update() { return STATE_SLEEP_FOREVER; }
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
};
EMPTY_DTOR(SleepState)


//-----------------------------------------------------------------------------------------------------------
// TheSuperHackers @info Misappropriates deleteInstance to call Release_Ref to keep the StateMachine fix small.
// @todo Replace calls to deleteInstance with RefCountPtr<StateMachine> when so appropriate.
inline void deleteInstance(StateMachine* machine)
{
	if (machine != nullptr)
		machine->Release_Ref();
}
