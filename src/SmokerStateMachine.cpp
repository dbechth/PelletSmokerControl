#include "SmokerStateMachine.h"
#include <cstring>

bool idleTempReached = false;   

// Implementation of the SmokerStateMachine class
SmokerStateMachine::SmokerStateMachine()
        : activeState(State::InitialConditions),
            firstEntry(true),
            stateTimer(0),
            resetTimer(false),
            transitionRequested(false),
            requestedState(State::InitialConditions)
{
}

void SmokerStateMachine::ProcessButtonInputs()
{
    // Check all button inputs and request state transitions accordingly
    if (uiData.btn_Startup)
    {
        RequestStateTransition(State::InitialConditions);
        uiData.btn_Startup = false;
    }
    else if (uiData.btn_Auto)
    {
        RequestStateTransition(State::Auto_Run);
        uiData.btn_Auto = false;
    }
    else if (uiData.btn_Shutdown)
    {
        RequestStateTransition(State::Shutdown_Cool);
        uiData.btn_Shutdown = false;
    }
    else if (uiData.btn_Manual)
    {
        RequestStateTransition(State::Manual_Run);
        uiData.btn_Manual = false;
    }
}

const char* SmokerStateMachine::GetStateName(State state)
{
    switch (state)
    {
    case State::InitialConditions: return "Check for Hot Start";
    case State::Startup_WaitForStart: return "Waiting for Start";
    case State::Startup_FillFirePot: return "Filling";
    case State::Startup_IgniterOn: return "Heating";
    case State::Startup_PuffFan: return "Kindling Burn";
    case State::Startup_Stabilize: return "Stabilizing Burn";
    case State::Auto_LoadRecipe: return "Loading Recipe";
    case State::Auto_NextStep: return "Loading Next Step";
    case State::Auto_RunStep: return "Running Recipe";
    case State::Auto_EndRecipe: return "Recipe Complete";
    case State::Auto_Run: return "Running";
    case State::Shutdown_Cool: return "Cooldown";
    case State::Shutdown_AllOff: return "Off";
    case State::Manual_Run: return "Manual Mode";
    default: return "Unknown";
    }
}

void SmokerStateMachine::Run(unsigned long taskRateMs)
{
    if (firstEntry)
    {
        stateTimer = 0;
    }
    else
    {
        stateTimer += taskRateMs;
    }

    switch (activeState)
    {
    case State::InitialConditions:
        // entry
        if (firstEntry)
        {
            smokerData.auger.mode = AugerControl::Mode::Off;
            smokerData.fan.mode = FanControl::Mode::Off;
            smokerData.igniter.mode = IgniterControl::Mode::Off;
        }

        // during

        // exit (placeholder)
        if (smokerData.filteredSmokeChamberTemp < smokerConfig.tunable.minAutoRestartTemp)
        {
            RequestStateTransition(State::Startup_FillFirePot);
        }
        else if (smokerData.filteredSmokeChamberTemp >= smokerConfig.tunable.minAutoRestartTemp)
        {
            RequestStateTransition(State::Startup_IgniterOn);        
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup before leaving InitialConditions
        }
        break;

    case State::Startup_WaitForStart:
        // entry
        if (firstEntry)
        {
            smokerData.auger.mode = AugerControl::Mode::Off;
            smokerData.fan.mode = FanControl::Mode::Off;
            smokerData.igniter.mode = IgniterControl::Mode::Off;
        }

        // during

        // exit (placeholder)
        // Button handling moved to centralized ProcessButtonInputs()

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup before leaving Startup_WaitForStart
        }
        break;

    case State::Startup_FillFirePot:
        // entry
        if (firstEntry)
        {
            smokerData.auger.mode = AugerControl::Mode::On;
            smokerData.fan.mode = FanControl::Mode::Off;
            smokerData.igniter.mode = IgniterControl::Mode::Off;
        }

        // exit (placeholder)
        if (stateTimer >= smokerConfig.tunable.startupFillTime) // 3 minutes to fill
        {
            RequestStateTransition(State::Startup_IgniterOn);
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup fill actions
        }
        break;

    case State::Startup_IgniterOn:
        // entry
        if (firstEntry)
        {
            smokerData.auger.mode = AugerControl::Mode::On;
            smokerData.fan.mode = FanControl::Mode::Off;
            smokerData.igniter.mode = IgniterControl::Mode::On;
        }

        // exit (placeholder)
        if (stateTimer >= smokerConfig.tunable.igniterPreheatTime) // 1 minute preheat
        {
            RequestStateTransition(State::Startup_PuffFan);
        }
        else if(smokerData.filteredFirePotTemp >= smokerConfig.tunable.firePotBurningTemp) // 200 degrees F
        {
            RequestStateTransition(State::Startup_Stabilize);
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup ignition activities
        }
        break;

    case State::Startup_PuffFan:
        // entry
        if (firstEntry)
        {
            smokerData.auger.mode = AugerControl::Mode::On;
            smokerData.fan.mode = FanControl::Mode::Auto;
            smokerData.igniter.mode = IgniterControl::Mode::On;
        }

        // exit (placeholder)
        if (smokerData.filteredFirePotTemp >= smokerConfig.tunable.firePotBurningTemp || 
            smokerData.filteredSmokeChamberTemp >= smokerConfig.tunable.minAutoRestartTemp)
        {
            RequestStateTransition(State::Startup_Stabilize);
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup puff fan activities
        }
        break;

    case State::Startup_Stabilize:
        // entry

        if (firstEntry)
        {
            smokerConfig.operating.setpoint = smokerConfig.tunable.minIdleTemp; // 175 degrees F

            smokerData.auger.mode = AugerControl::Mode::Auto;
            smokerData.fan.mode = FanControl::Mode::Auto;
            smokerData.igniter.mode = IgniterControl::Mode::Off;    
            idleTempReached = false;   
        }

        if(smokerData.filteredSmokeChamberTemp >= smokerConfig.tunable.minIdleTemp)
        {
            idleTempReached = true;
        }
        else
        {
            idleTempReached = false;
            stateTimer = 0; //reset timer if temp drops below setpoint
        }

        // exit (placeholder)
        if (stateTimer >= smokerConfig.tunable.stabilizeTime && idleTempReached) // 1 minute to stabilize
        {
            if (smokerConfig.recipe.selectedRecipeIndex < 0)
            {
                RequestStateTransition(State::Auto_Run);
            }
            else
            {
                RequestStateTransition(State::Auto_LoadRecipe);
            }
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup stabilization activities
        }
        break;

    case State::Auto_LoadRecipe:
        // entry
        if (firstEntry)
        {
            smokerConfig.recipe.recipeStepIndex = 0;
        }

        // during

        // exit (placeholder)
        if (true)
        {
            RequestStateTransition(State::Auto_NextStep);
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup after loading recipe
        }
        break;

    case State::Auto_RunStep:
        // entry
        if (firstEntry)
        {
            smokerData.auger.mode = AugerControl::Mode::Auto;
            smokerData.fan.mode = FanControl::Mode::Auto;
            smokerData.igniter.mode = IgniterControl::Mode::Off; 

            smokerConfig.operating.setpoint = smokerConfig.recipe.recipeData[smokerConfig.recipe.selectedRecipeIndex].steps[smokerConfig.recipe.recipeStepIndex].startTempSetpoint;
            smokerConfig.operating.smokesetpoint = smokerConfig.recipe.recipeData[smokerConfig.recipe.selectedRecipeIndex].steps[smokerConfig.recipe.recipeStepIndex].startSmokeSetpoint;
        }

        // during

        // exit (placeholder)
        if (stateTimer >= smokerConfig.recipe.recipeData[smokerConfig.recipe.selectedRecipeIndex].steps[smokerConfig.recipe.recipeStepIndex].stepDurationMs ||
            false) // add meat probe logic later
        {
            RequestStateTransition(State::Auto_NextStep);
        }

        // exit cleanup
        if (transitionRequested)
        {
            smokerConfig.operating.setpoint = smokerConfig.recipe.recipeData[smokerConfig.recipe.selectedRecipeIndex].steps[smokerConfig.recipe.recipeStepIndex].endTempSetpoint;
            smokerConfig.operating.smokesetpoint = smokerConfig.recipe.recipeData[smokerConfig.recipe.selectedRecipeIndex].steps[smokerConfig.recipe.recipeStepIndex].endSmokeSetpoint;

            // cleanup after running step
        }
        break;

    case State::Auto_NextStep:
        // entry
        if (firstEntry)
        {
            
        }

        // during

        // exit (placeholder)
        if (smokerConfig.recipe.recipeStepIndex < (MAX_RECIPE_STEPS -1))
        {
            smokerConfig.recipe.recipeStepIndex++;
            RequestStateTransition(State::Auto_RunStep);
        }
        else
        {
            RequestStateTransition(State::Auto_EndRecipe);
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup before running step
        }
        break;

    case State::Auto_EndRecipe:
        // entry
        if (firstEntry)
        {
            // finalize recipe
        }

        // during

        // exit (placeholder)
        // Button handling moved to centralized ProcessButtonInputs()

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup before leaving Auto_EndRecipe
        }
        break;

        case State::Auto_Run:
        // entry
        if (firstEntry)
        {
            smokerData.auger.mode = AugerControl::Mode::Auto;
            smokerData.fan.mode = FanControl::Mode::Auto;
            smokerData.igniter.mode = IgniterControl::Mode::Off; 
        }

        // during
        // consider adding logic to detect flameout here!!!
        break;

    case State::Shutdown_Cool:
        // entry
        if (firstEntry)
        {
            smokerData.auger.mode = AugerControl::Mode::Off;
            smokerData.fan.mode = FanControl::Mode::On;
            smokerData.igniter.mode = IgniterControl::Mode::Off; 
        }

        // during

        // exit (placeholder)
        if (smokerData.filteredSmokeChamberTemp <= smokerConfig.tunable.minAutoRestartTemp || smokerData.filteredFirePotTemp <= smokerConfig.tunable.firePotBurningTemp)
        {
            RequestStateTransition(State::Shutdown_AllOff);
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup cooling actions
        }
        break;

    case State::Shutdown_AllOff:
        // entry
        if (firstEntry)
        {
            smokerData.auger.mode = AugerControl::Mode::Off;
            smokerData.fan.mode = FanControl::Mode::Off;
            smokerData.igniter.mode = IgniterControl::Mode::Off; 
        }

        // during
        break;

    case State::Manual_Run:
        // entry
        if (firstEntry)
        {
            smokerData.auger.mode = AugerControl::Mode::Manual;
            smokerData.fan.mode = FanControl::Mode::Manual;
            smokerData.igniter.mode = IgniterControl::Mode::Off; 
        }

        // during
        break;
    }

    // Process button inputs after state processing to ensure UI calls happen on the same cycle
    ProcessButtonInputs();

    // Apply any requested transition once, after state processing.
    if (transitionRequested)
    {
        firstEntry = true;
        activeState = requestedState;
        resetTimer = true;
        transitionRequested = false;
    }
    else
    {
        firstEntry = false;
    }

    // Update the active state in operating params
    strncpy(smokerConfig.operating.activeState, GetStateName(activeState), sizeof(smokerConfig.operating.activeState) - 1);
    smokerConfig.operating.activeState[sizeof(smokerConfig.operating.activeState) - 1] = '\0'; // Ensure null termination
}

SmokerStateMachine::State SmokerStateMachine::GetActiveState() const
{
    return activeState;
}

void SmokerStateMachine::RequestStateTransition(SmokerStateMachine::State state)
{
    // Ignore requests that would not change the state.
    if (state == activeState) {
        return;
    }

    // If the same request is already queued, do nothing.
    if (transitionRequested && requestedState == state) {
        return;
    }

    requestedState = state;
    transitionRequested = true;
}

void SmokerStateMachine::ForceStateTransition(SmokerStateMachine::State state)
{
    // Immediately apply the transition, bypassing the queued-request mechanism.
    activeState = state;
    requestedState = state;
    transitionRequested = false;
    firstEntry = true;
    resetTimer = true;
} 