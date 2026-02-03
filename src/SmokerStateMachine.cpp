#include "SmokerStateMachine.h"

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
            smokerData.auger = SmokerData::Mode::Off;
            smokerData.fan = SmokerData::Mode::Off;
            smokerData.igniter = SmokerData::Mode::Off;
        }

        // during

        // exit (placeholder)
        if (smokerData.filteredSmokeChamberTemp < smokerConfig.minAutoRestartTemp)
        {
            RequestStateTransition(State::Startup_WaitForStart);
        }
        else if (smokerData.filteredSmokeChamberTemp >= smokerConfig.minAutoRestartTemp)
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
            smokerData.auger = SmokerData::Mode::Off;
            smokerData.fan = SmokerData::Mode::Off;
            smokerData.igniter = SmokerData::Mode::Off;
        }

        // during

        // exit (placeholder)
        if (uiData.web_start || uiData.btn_start)
        {
            RequestStateTransition(State::Startup_FillFirePot);
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup before leaving WaitForStart
        }
        break;

    case State::Startup_FillFirePot:
        // entry
        if (firstEntry)
        {
            smokerData.auger = SmokerData::Mode::On;
            smokerData.fan = SmokerData::Mode::Off;
            smokerData.igniter = SmokerData::Mode::Off;
        }

        // exit (placeholder)
        if (stateTimer >= smokerConfig.startupFillTime) // 3 minutes to fill
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
            smokerData.auger = SmokerData::Mode::On;
            smokerData.fan = SmokerData::Mode::Off;
            smokerData.igniter = SmokerData::Mode::On;
        }

        // exit (placeholder)
        if (stateTimer >= smokerConfig.igniterPreheatTime) // 1 minute preheat
        {
            RequestStateTransition(State::Startup_PuffFan);
        }
        else if(smokerData.filteredFireBoxTemp >= smokerConfig.fireBoxBurningTemp) // 200 degrees F
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
            smokerData.auger = SmokerData::Mode::On;
            smokerData.fan = SmokerData::Mode::Auto;
            smokerData.igniter = SmokerData::Mode::On;
        }

        // exit (placeholder)
        if (smokerData.filteredFireBoxTemp >= smokerConfig.fireBoxBurningTemp || 
            smokerData.filteredSmokeChamberTemp >= smokerConfig.minAutoRestartTemp)
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
            smokerData.setpoint = smokerConfig.minIdleTemp; // 175 degrees F

            smokerData.auger = SmokerData::Mode::Auto;
            smokerData.fan = SmokerData::Mode::Auto;
            smokerData.igniter = SmokerData::Mode::Off;    
            idleTempReached = false;   
        }

        if(smokerData.filteredSmokeChamberTemp >= smokerConfig.minIdleTemp)
        {
            idleTempReached = true;
        }
        else
        {
            idleTempReached = false;
            stateTimer = 0; //reset timer if temp drops below setpoint
        }

        // exit (placeholder)
        if (stateTimer >= smokerConfig.stabilizeTime && idleTempReached) // 1 minute to stabilize
        {
            RequestStateTransition(State::Auto_LoadRecipe);
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
            smokerData.recipeStepIndex = 0;
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
            smokerData.auger = SmokerData::Mode::Auto;
            smokerData.fan = SmokerData::Mode::Auto;
            smokerData.igniter = SmokerData::Mode::Off; 

            smokerData.setpoint = smokerConfig.recipeData[smokerData.selectedRecipeIndex].steps[smokerData.recipeStepIndex].startTempSetpoint;
            smokerData.smokesetpoint = smokerConfig.recipeData[smokerData.selectedRecipeIndex].steps[smokerData.recipeStepIndex].startSmokeSetpoint;
        }

        // during

        // exit (placeholder)
        if (stateTimer >= smokerConfig.recipeData[smokerData.selectedRecipeIndex].steps[smokerData.recipeStepIndex].stepDurationMs ||
            false) // add meat probe logic later
        {
            RequestStateTransition(State::Auto_NextStep);
        }

        // exit cleanup
        if (transitionRequested)
        {
            smokerData.setpoint = smokerConfig.recipeData[smokerData.selectedRecipeIndex].steps[smokerData.recipeStepIndex].endTempSetpoint;
            smokerData.smokesetpoint = smokerConfig.recipeData[smokerData.selectedRecipeIndex].steps[smokerData.recipeStepIndex].endSmokeSetpoint;

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
        if (smokerData.recipeStepIndex < (MAX_RECIPE_STEPS -1))
        {
            smokerData.recipeStepIndex++;
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
        if (uiData.web_shutdown || uiData.btn_shutdown)
        {
            RequestStateTransition(State::Shutdown_Cool);
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup after recipe end
        }
        break;

    case State::Shutdown_Cool:
        // entry
        if (firstEntry)
        {
            // start cool-down sequence
        }

        // during

        // exit (placeholder)
        if (/* condition to move to next state */ false)
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
            // turn all actuators off
        }

        // during

        // exit (placeholder)
        if (/* condition to move to next state */ false)
        {
            RequestStateTransition(State::Manual_Run);
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup after shutdown
        }
        break;

    case State::Manual_Run:
        // entry
        if (firstEntry)
        {
            // manual control enabled
        }

        // during

        // exit (placeholder)
        if (/* condition to move to next state */ false)
        {
            RequestStateTransition(State::InitialConditions);
        }

        // exit cleanup
        if (transitionRequested)
        {
            // cleanup before leaving manual mode
        }
        break;

    }

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