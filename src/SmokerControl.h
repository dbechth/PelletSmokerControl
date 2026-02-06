#pragma once

// Global data structures for the Smoker Control project

const int MAX_RECIPE_STEPS = 10;
const int MAX_RECIPES = 5;

struct RecipeStep {
    char name[24];
    bool enabled;
    float startTempSetpoint;
    float endTempSetpoint;
    float startSmokeSetpoint;
    float endSmokeSetpoint;
    unsigned long stepDurationMs;
    float meatProbeExitTemp;
};

struct Recipe {
    char name[32];
    RecipeStep steps[MAX_RECIPE_STEPS];
    int stepCount;
    bool enabled;
};

struct IgniterControl {
    enum class Mode { Off = 0, On = 1 };
    Mode mode;
};

struct AugerControl {
    enum class Mode { Off = 0, On = 1, Auto = 2, Manual = 3, Mass = 4 };
    Mode mode;
    float dutyCycle_Manual;
    float frequency_Manual;
    float Mass;
};

struct FanControl {
    enum class Mode { Off = 0, On = 1, Auto = 2, Manual = 3 };
    Mode mode;
    float dutyCycle_Manual;
    float frequency_Manual;
};

struct SmokerData {
    float filteredSmokeChamberTemp;
    float filteredFirePotTemp;
    IgniterControl igniter;
    AugerControl auger;
    FanControl fan;
};
    
struct SmokerConfig {

    struct OperatingParams {
        float setpoint;
        float smokesetpoint;
        char activeState[32]; // Alias for current state machine state
    };

    struct TunableParams {
        float minAutoRestartTemp;
        float minIdleTemp;
        float firePotBurningTemp;
        unsigned long startupFillTime;
        unsigned long igniterPreheatTime;
        unsigned long stabilizeTime;
    };

    struct RecipeState {
        int recipeStepIndex;
        int selectedRecipeIndex;
        Recipe recipeData[MAX_RECIPES];
    };

    OperatingParams operating;
    TunableParams tunable;
    RecipeState recipe;
};

struct UserInputs {
	bool btn_Startup;
	bool btn_Auto;
	bool btn_Shutdown;
	bool btn_Manual;
};

extern SmokerData smokerData;
extern SmokerConfig smokerConfig;
extern UserInputs uiData;

bool SaveConfigToSPIFFS(const SmokerConfig& config);
bool LoadConfigFromSPIFFS(SmokerConfig& config);