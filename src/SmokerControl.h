#pragma once

/*
    SmokerControl.h - Global data structures for the Smoker Control project

    This header defines three main data structures used throughout the project:
    - SmokerData: sensor readings and state information
    - SmokerConfig: tunable parameters
    - UIState: user interface/input state

    These structures are declared as extern here. Their actual instances
    are defined in SmokerControl.cpp (see that file for initialization).

    Usage in other modules:
        #include "SmokerControl.h"
        // Access sensor data
        float temp = smokerData.filteredSmokeChamberTemp;
        // Access config
        float threshold = smokerConfig.minAutoRestartTemp;
        // Access UI state
        if (uiData.web_start || uiData.btn_start) { ... }
*/

// ============================================================================
// Data Structures
// ============================================================================

constexpr int MAX_RECIPE_STEPS = 10;
constexpr int MAX_RECIPES = 5;

struct RecipeStep {
    char name[24];                  // step name (null-terminated)
    bool enabled;                   // whether the step is enabled
    float startTempSetpoint;        // start temperature setpoint (F)
    float endTempSetpoint;          // end temperature setpoint (F)
    float startSmokeSetpoint;       // start smoke setpoint (arbitrary units)
    float endSmokeSetpoint;         // end smoke setpoint (arbitrary units)
    unsigned long stepDurationMs;   // step duration in milliseconds
    float meatProbeExitTemp;        // meat probe exit temperature (F)

};

struct Recipe {
    char name[32];                  // recipe name
    RecipeStep steps[MAX_RECIPE_STEPS];
    int stepCount;              // number of valid steps in this recipe
    bool enabled;                   // whether this recipe is enabled
};

struct ActuatorControl {
    enum class Mode 
    {
        Off = 0,
        On = 1,
        Auto = 2,
    };
    Mode mode;              // operating mode (Off/On/Auto)
    float dutyCycle;        // duty cycle for PWM in auto mode (0-100%)
    float frequency;        // frequency for PWM in auto mode (Hz)
};

struct SmokerData {
    float filteredSmokeChamberTemp;
    float filteredFirePotTemp;
    float setpoint;                 // current temperature setpoint (F)
    float smokesetpoint;            // current smoke setpoint (%)
    int recipeStepIndex;
    int selectedRecipeIndex;
    ActuatorControl igniter;        // igniter control (mode, duty cycle, frequency)
    ActuatorControl auger;          // auger control (mode, duty cycle, frequency)
    ActuatorControl fan;            // fan control (mode, duty cycle, frequency)
};
    
struct SmokerConfig {
    float minAutoRestartTemp;        // temp threshold to auto-restart if cold
    float minIdleTemp;               // idle temperature to maintain after startup
    float firePotBurningTemp;        // firebox temp considered "burning"
    unsigned long startupFillTime;   // how long to run auger to fill (ms)
    unsigned long igniterPreheatTime;// igniter preheat duration (ms)
    unsigned long stabilizeTime;     // time to stabilize at idle temp (ms)
    Recipe recipeData[MAX_RECIPES];
    int recipeCount;                 // number of loaded recipes (<= MAX_RECIPES)
};

struct UserInputs {
    bool startup;
    bool automode;
    bool manualmode;
    bool shutdown;
};

// ============================================================================
// Global Instances (extern - defined in SmokerControl.cpp)
// ============================================================================

extern SmokerData smokerData;
extern SmokerConfig smokerConfig;
extern UserInputs uiData;