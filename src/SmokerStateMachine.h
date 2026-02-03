
#pragma once
#include "SmokerControl.h"
/*
    Startup.h - Smoker state machine

    README / Usage
    ----------------
    This header declares the `SmokerStateMachine` class which encapsulates
    the state machine used by the smoker controller. The class keeps
    all state-tracking variables private so multiple state machines can
    coexist without symbol collisions.

    Basic usage:

        // instantiate (e.g. as a global or member)
        SmokerStateMachine smoker;

        // in your periodic task / main loop (pass ms since last call):
        smoker.Run(taskRateMs);

        // request a queued transition (applied after current state's logic):
        smoker.RequestStateTransition(SmokerStateMachine::State::Startup_FillFirePot);

        // force an immediate transition (pre-empts current state's logic):
        smoker.ForceStateTransition(SmokerStateMachine::State::Manual_Run);

    Design notes
    -------------
    - States are declared as `enum class State` to keep the namespace
        contained within the class.
    - `RequestStateTransition()` queues a transition which is applied
        exactly once at the end of `Run()`. This prevents having to check
        for state changes both before and after the state's logic.
    - `ForceStateTransition()` applies a state change immediately and
        should be used only when pre-emption is required.
    - The implementation assumes a single-threaded (non-preemptive)
        embedded environment. If you request transitions from an ISR or
        another context, consider making the transition flags `volatile`
        and protecting access with a critical section.

    API
    ---
    - `SmokerStateMachine()` : constructor
    - `void Run(unsigned long taskRateMs)` : advance the state machine
        by one tick (taskRateMs in milliseconds)
    - `State GetActiveState() const` : query current state
    - `void RequestStateTransition(State state)` : queue transition
    - `void ForceStateTransition(State state)` : immediate transition

*/

class SmokerStateMachine
{
public:
        // States used by this startup state machine
        enum class State {
                InitialConditions,
                Startup_WaitForStart,
                Startup_FillFirePot,
                Startup_IgniterOn,
                Startup_PuffFan,
                Startup_Stabilize,
                Auto_LoadRecipe,
                Auto_NextStep,
                Auto_RunStep,
                Auto_EndRecipe,
                Shutdown_Cool,
                Shutdown_AllOff,
                Manual_Run
        };

        // Constructor: initializes internal state
        SmokerStateMachine();

        // Run the state machine. `taskRateMs` is the caller's period in ms.
        void Run(unsigned long taskRateMs);

        // Query or request a state change. The internal state remains private.
        State GetActiveState() const;

        // Request a state transition; the request is queued and applied once
        // after the current state's logic has been evaluated in `Run()`.
        void RequestStateTransition(State state);

        // Force an immediate state transition (applies the change now).
        void ForceStateTransition(State state);

private:
        State activeState;
        bool firstEntry;
        unsigned long stateTimer;
        bool resetTimer;

        // Transition request storage. `RequestStateTransition()` sets these
        // and `Run()` applies them once at the end of state processing.
        bool transitionRequested;
        State requestedState;
};
