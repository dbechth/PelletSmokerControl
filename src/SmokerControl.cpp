#include <EEPROM.h>
#include "AC2.h"
#include <WiFiClient.h>
#include <SPI.h>
#include <Wire.h>
#include <Servo.h>
#include "max6675.h"
#include "SmokerControl.h"
#include "SmokerOutputs.h"
#include "SmokerStateMachine.h"

SmokerData smokerData = {
	.filteredSmokeChamberTemp = 0.0f,
	.filteredFirePotTemp = 0.0f,
	.setpoint = 0.0f,
	.smokesetpoint = 0.0f,
	.recipeStepIndex = 0,
	.selectedRecipeIndex = -1,
	.igniter = SmokerData::Mode::Off,
	.auger = SmokerData::Mode::Off,
	.fan = SmokerData::Mode::Off};

SmokerConfig smokerConfig = {
	.minAutoRestartTemp = 100.0f,
	.minIdleTemp = 175.0f,
	.firePotBurningTemp = -200.0f,
	.startupFillTime = 180000UL,   // 3 minutes
	.igniterPreheatTime = 60000UL, // 1 minute
	.stabilizeTime = 60000UL,	   // 1 minute
	.recipeCount = 0};

UserInputs uiData = {
	.startup = false,
	.automode = false,
	.manualmode = false,
	.shutdown = false};

constexpr auto ControllerName = "PelletSmoker32";
int thermoDO = 21;
int thermoCLK = 19;
int thermoCS = 5;
int thermoCS2 = 18;
// int pinInletDamper = D1;
// int pinOutletDamper = D2;

int augerPin = 32;	 // Relay 1
int fanPin = 33;	 // Relay 2
int igniterPin = 25; // Relay 3
int sparePin = 26;	 // Relay 4

constexpr int On = HIGH;
constexpr int Off = LOW;

MAX6675 smokechamberthermocouple(thermoCLK, thermoCS2, thermoDO);
MAX6675 fireboxthermocouple(thermoCLK, thermoCS, thermoDO);
static float smokechamberTemperature = 0;
static float firepotTemperature = 0;

// WiFi
char ssid[] = "Bulldog";
char pass[] = "6412108682";
char APssid[] = "DBBSmoker";

// State Machine
SmokerStateMachine smokerStateMachine;
static unsigned long lastStateUpdateTime = 0;
const unsigned long STATE_UPDATE_PERIOD_MS = 100; // Update state machine every 100ms

bool augerPWM(int percent, float periodSeconds)
{
	static unsigned long cycleStartTime = 0;
	static bool cycleInitialized = false;

	if (!cycleInitialized)
	{
		cycleStartTime = millis();
		cycleInitialized = true;
	}

	unsigned long periodMillis = (unsigned long)(periodSeconds * 1000.0);
	unsigned long onTimeMillis = (unsigned long)((percent / 100.0) * periodMillis);
	unsigned long elapsedTime = millis() - cycleStartTime;

	// Reset cycle if period has elapsed
	if (elapsedTime >= periodMillis)
	{
		cycleStartTime = millis();
		elapsedTime = 0;
	}

	// Return true if within the on-time window, false otherwise
	return (elapsedTime < onTimeMillis);
}

// 1D Lookup Table with Linear Interpolation
// Returns interpolated output value based on input
// Parameters:
//   inputValue: The x-axis value to look up
//   inputArray: Array of input (x) values in ascending order
//   outputArray: Array of output (y) values corresponding to inputArray
//   arraySize: Number of elements in both arrays
// Returns: Interpolated output value
float lookupTableInterpolate(float inputValue, const float *inputArray, const float *outputArray, int arraySize)
{
	// Handle edge cases
	if (arraySize <= 0)
		return 0.0;
	if (arraySize == 1)
		return outputArray[0];

	// If input is below minimum, return first output value
	if (inputValue <= inputArray[0])
	{
		return outputArray[0];
	}

	// If input is above maximum, return last output value
	if (inputValue >= inputArray[arraySize - 1])
	{
		return outputArray[arraySize - 1];
	}

	// Find the two points to interpolate between
	for (int i = 0; i < arraySize - 1; i++)
	{
		if (inputValue >= inputArray[i] && inputValue <= inputArray[i + 1])
		{
			// Linear interpolation formula: y = y1 + (x - x1) * (y2 - y1) / (x2 - x1)
			float x1 = inputArray[i];
			float x2 = inputArray[i + 1];
			float y1 = outputArray[i];
			float y2 = outputArray[i + 1];

			float interpolatedValue = y1 + (inputValue - x1) * (y2 - y1) / (x2 - x1);
			return interpolatedValue;
		}
	}

	// Should never reach here if arrays are valid
	return outputArray[arraySize - 1];
}

// Initialize recipe defaults (clears names, disables steps/recipes)
static void initRecipeDefaults()
{
	smokerConfig.recipeCount = 0;
	for (int r = 0; r < MAX_RECIPES; ++r)
	{
		smokerConfig.recipeData[r].name[0] = '\0';
		smokerConfig.recipeData[r].stepCount = 0;
		smokerConfig.recipeData[r].enabled = false;
		for (int s = 0; s < MAX_RECIPE_STEPS; ++s)
		{
			smokerConfig.recipeData[r].steps[s].name[0] = '\0';
			smokerConfig.recipeData[r].steps[s].startTempSetpoint = 0.0f;
			smokerConfig.recipeData[r].steps[s].endTempSetpoint = 0.0f;
			smokerConfig.recipeData[r].steps[s].startSmokeSetpoint = 0.0f;
			smokerConfig.recipeData[r].steps[s].endSmokeSetpoint = 0.0f;
			smokerConfig.recipeData[r].steps[s].stepDurationMs = 0;
			smokerConfig.recipeData[r].steps[s].meatProbeExitTemp = 0.0f;
			smokerConfig.recipeData[r].steps[s].enabled = false;
		}
	}
}

void setup()
{
	pinMode(igniterPin, OUTPUT);
	pinMode(fanPin, OUTPUT);
	pinMode(augerPin, OUTPUT);

	Serial.begin(115200);

	WiFi.hostname(ControllerName);
	WiFi.mode(WIFI_AP_STA);
	WiFi.begin(ssid, pass);
	WiFi.softAP(APssid);

	AC2.init(ControllerName, WiFi.localIP(), IPADDR_BROADCAST, 4020, 100);

	// initialize recipe data structures to safe defaults
	initRecipeDefaults();

	smokerData.filteredSmokeChamberTemp = smokechamberthermocouple.readFahrenheit();

	// Initialize state machine timing
	lastStateUpdateTime = millis();
}

void loop()
{

	AC2.task(); // This application manages its own taskrate

	smokechamberTemperature = smokechamberthermocouple.readFahrenheit();
	firepotTemperature = fireboxthermocouple.readFahrenheit();

	smokerData.filteredSmokeChamberTemp = ((smokechamberTemperature * 0.5) + (smokerData.filteredSmokeChamberTemp * 0.5));
	smokerData.filteredFirePotTemp = ((firepotTemperature * 0.5) + (smokerData.filteredFirePotTemp * 0.5));

	smokerStateMachine.Run(100);

	IgniterControlTask();
	AugerControlTask();
	FanControlTask();
}
