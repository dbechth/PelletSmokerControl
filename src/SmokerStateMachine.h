#pragma once
#include "SmokerControl.h"

class SmokerStateMachine
{
public:
    enum class State
    {
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
        Auto_Run,
        Shutdown_Cool,
        Shutdown_AllOff,
        Manual_Run
    };

    SmokerStateMachine();
    void Run(unsigned long taskRateMs);
    State GetActiveState() const;
    const char *GetStateName(State state);
    void RequestStateTransition(State state);
    void ForceStateTransition(State state);

private:
    State activeState;
    bool firstEntry;
    unsigned long stateTimer;
    bool resetTimer;
    bool transitionRequested;
    State requestedState;

    void ProcessButtonInputs();
};
