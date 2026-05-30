#include <EEPROM.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <WiFiClient.h>

#include "AC2.h"
#include "DataLogger.h"
#include "SmokerControl.h"
#include "SmokerOutputs.h"
#include "SmokerStateMachine.h"
#include "WebInterface.h"
#include "max6675.h"

constexpr int InputsTaskRateMs = 500;
constexpr int OutputsTaskRateMs = 10;
constexpr int StateMachineTaskRateMs = 1000;

constexpr auto ControllerName = "PelletSmoker32";
char APssid[] = "DBBSmoker";
char pass[] = "6412108682";
char ssid[] = "Bulldog";

// Output relay pins
int augerPin = 32;
int fanPin = 33;
int igniterPin = 25;
int sparePin = 26;

// Thermocouple wiring
int thermoCLK = 19;
int thermoCLK2 = 17;
int thermoCS = 5;
int thermoCS2 = 4;
int thermoDO = 21;
int thermoDO2 = 18;

// Runtime measurements
static float firepotTemperature = 0;
static float smokechamberTemperature = 0;

MAX6675 firepotthermocouple(thermoCLK, thermoCS, thermoDO);
MAX6675 smokechamberthermocouple(thermoCLK2, thermoCS2, thermoDO2);

SmokerData smokerData = {
	.filteredSmokeChamberTemp = 0.0f,
	.filteredFirePotTemp = 0.0f,
	.igniter = {.mode = IgniterControl::Mode::Off},
	.auger = {.mode = AugerControl::Mode::Off, .dutyCycle = 0.0f, .frequency = 0.0f, .Mass = 0.0f},
	.fan = {.mode = FanControl::Mode::Off, .dutyCycle = 0.0f, .frequency = 0.0f}};

SmokerConfig smokerConfig = {
	.operating = {
		.setpoint = 200.0f,
		.smokesetpoint = 0.0f},
	.tunable = {
		.minAutoRestartTemp = 100.0f,
		.minIdleTemp = 175.0f,
		.firePotBurningTemp = 200.0f,
		.startupFillTime = 10UL,
		.igniterPreheatTime = 60000UL,
		.stabilizeTime = 60000UL,
		.augerFrequency = 10.0f,
		.fanFrequency = 0.5f,
		.augerTransferFunc = {
			{33.0f, 175.0f},
			{37.0f, 200.0f},
			{41.0f, 225.0f},
			{47.0f, 250.0f},
			{52.0f, 275.0f},
			{58.0f, 300.0f},
			{64.0f, 325.0f},
			{72.0f, 350.0f},
			{80.0f, 375.0f},
			{90.0f, 400.0f},
			{100.0f, 500.0f}
		},
		.fanTransferFunc = {
            {100.0f, 0.0f},
            {95.0f, 10.0f},
            {90.0f, 20.0f},
            {85.0f, 30.0f},
            {80.0f, 40.0f},
            {75.0f, 50.0f},
            {70.0f, 60.0f},
            {65.0f, 70.0f},
            {60.0f, 80.0f},
            {55.0f, 90.0f},
            {50.0f, 100.0f}
				}},
	.recipe = {.recipeStepIndex = 0, .selectedRecipeIndex = -1},
	.logging = {
		.enabled = true,
		.logIntervalMs = 5000,
		.maxLogFiles = 1,
		.maxLogFileSizeBytes = 500000,
		.smokeChamberTempThreshold = 1.0f,
		.firePotTempThreshold = 1.0f,
		.setpointThreshold = 1.0f,
		.smokeSetpointThreshold = 1.0f,
		.igniterModeThreshold = 1,
		.augerModeThreshold = 1,
		.augerDutyCycleThreshold = 1.0f,
		.augerFrequencyThreshold = 1.0f,
		.fanModeThreshold = 1,
		.fanDutyCycleThreshold = 1.0f,
		.fanFrequencyThreshold = 1.0f}};

UserInputs uiData = {
	.btn_Startup = false,
	.btn_Auto = false,
	.btn_Shutdown = false,
	.btn_Manual = false};

SmokerStateMachine smokerStateMachine;
WebInterface webInterface(AC2.webserver);

static TaskHandle_t inputsTaskHandle = nullptr;
static TaskHandle_t outputsTaskHandle = nullptr;
static TaskHandle_t stateMachineTaskHandle = nullptr;

// Initialize recipe defaults (clears names, disables steps/recipes)
static void initRecipeDefaults()
{
	for (int r = 0; r < MAX_RECIPES; ++r)
	{
		smokerConfig.recipe.recipeData[r].name[0] = '\0';
		smokerConfig.recipe.recipeData[r].stepCount = 0;
		smokerConfig.recipe.recipeData[r].enabled = false;
		for (int s = 0; s < MAX_RECIPE_STEPS; ++s)
		{
			smokerConfig.recipe.recipeData[r].steps[s].name[0] = '\0';
			smokerConfig.recipe.recipeData[r].steps[s].startTempSetpoint = 0.0f;
			smokerConfig.recipe.recipeData[r].steps[s].endTempSetpoint = 0.0f;
			smokerConfig.recipe.recipeData[r].steps[s].startSmokeSetpoint = 0.0f;
			smokerConfig.recipe.recipeData[r].steps[s].endSmokeSetpoint = 0.0f;
			smokerConfig.recipe.recipeData[r].steps[s].stepDurationMs = 0;
			smokerConfig.recipe.recipeData[r].steps[s].meatProbeExitTemp = 0.0f;
			smokerConfig.recipe.recipeData[r].steps[s].enabled = false;
		}
	}
}

static void RunInputsCycle()
{
	// Read thermocouples
	smokechamberTemperature = smokechamberthermocouple.readFahrenheit();
	firepotTemperature = firepotthermocouple.readFahrenheit();

	// Apply low-pass filter to smooth readings
	smokerData.filteredSmokeChamberTemp = ((smokechamberTemperature * 0.5) + (smokerData.filteredSmokeChamberTemp * 0.5));
	smokerData.filteredFirePotTemp = ((firepotTemperature * 0.5) + (smokerData.filteredFirePotTemp * 0.5));
}

static void RunLoggingCycle()
{
	DataLogger::logData(
		smokerData.filteredSmokeChamberTemp,
		smokerData.filteredFirePotTemp,
		smokerConfig.operating.setpoint,
		smokerConfig.operating.smokesetpoint,
		smokerConfig.operating.activeState,
		static_cast<int>(smokerData.igniter.mode),
		static_cast<int>(smokerData.auger.mode),
		smokerData.auger.dutyCycle,
		smokerData.auger.frequency,
		static_cast<int>(smokerData.fan.mode),
		smokerData.fan.dutyCycle,
		smokerData.fan.frequency);
}

static void RunOutputsCycle()
{
	IgniterControlTask();
	AugerControlTask();
	FanControlTask();
}

static void InputsTask(void *parameter)
{
	(void)parameter;
	TickType_t lastWakeTime = xTaskGetTickCount();
	const TickType_t periodTicks = pdMS_TO_TICKS(InputsTaskRateMs);

	for (;;)
	{
		RunInputsCycle();
		RunLoggingCycle();
		vTaskDelayUntil(&lastWakeTime, periodTicks);
	}
}

static void OutputsTask(void *parameter);
static void StateMachineTask(void *parameter);

static void OutputsTask(void *parameter)
{
	(void)parameter;
	TickType_t lastWakeTime = xTaskGetTickCount();
	const TickType_t periodTicks = pdMS_TO_TICKS(OutputsTaskRateMs);

	for (;;)
	{
		RunOutputsCycle();
		vTaskDelayUntil(&lastWakeTime, periodTicks);
	}
}

static void StateMachineTask(void *parameter)
{
	(void)parameter;
	TickType_t lastWakeTime = xTaskGetTickCount();
	const TickType_t periodTicks = pdMS_TO_TICKS(StateMachineTaskRateMs);

	for (;;)
	{
		smokerStateMachine.Run(StateMachineTaskRateMs);
		vTaskDelayUntil(&lastWakeTime, periodTicks);
	}
}

void setup()
{
	pinMode(igniterPin, OUTPUT);
	pinMode(fanPin, OUTPUT);
	pinMode(augerPin, OUTPUT);

	Serial.begin(115200);

	if (!SPIFFS.begin(true))
	{
		Serial.println("SPIFFS mount failed");
		return;
	}

	WiFi.hostname(ControllerName);
	WiFi.mode(WIFI_STA);
	WiFi.setSleep(true);
	WiFi.begin(ssid, pass);

	Serial.print("Connecting to WiFi: ");
	int timeout = 0;
	while (WiFi.status() != WL_CONNECTED && timeout < 20)
	{
		delay(500);
		Serial.print(".");
		timeout++;
	}
	Serial.println();

	if (WiFi.status() == WL_CONNECTED)
	{
		Serial.print("WiFi Connected - IP: ");
		Serial.println(WiFi.localIP());
	}
	else
	{
		Serial.println("ERROR: WiFi connection failed. Check SSID/password.");
	}

	webInterface.begin();

	AC2.init(ControllerName, 100);

	if (!LoadConfigFromSPIFFS(smokerConfig))
	{
		Serial.println("Loading defaults and saving to SPIFFS");
		initRecipeDefaults();
		SaveConfigToSPIFFS(smokerConfig);
	}
	else
	{
		Serial.println("Config loaded from SPIFFS");
	}

	Serial.print("Web Interface available at: http://");
	Serial.print(WiFi.localIP());
	Serial.println("/");

	LogConfig logConfig = {
		.enabled = smokerConfig.logging.enabled,
		.logIntervalMs = smokerConfig.logging.logIntervalMs,
		.maxLogFiles = smokerConfig.logging.maxLogFiles,
		.maxLogFileSizeBytes = smokerConfig.logging.maxLogFileSizeBytes,
		.smokeChamberTempThreshold = smokerConfig.logging.smokeChamberTempThreshold,
		.firePotTempThreshold = smokerConfig.logging.firePotTempThreshold,
		.setpointThreshold = smokerConfig.logging.setpointThreshold,
		.smokeSetpointThreshold = smokerConfig.logging.smokeSetpointThreshold,
		.igniterModeThreshold = smokerConfig.logging.igniterModeThreshold,
		.augerModeThreshold = smokerConfig.logging.augerModeThreshold,
		.augerDutyCycleThreshold = smokerConfig.logging.augerDutyCycleThreshold,
		.augerFrequencyThreshold = smokerConfig.logging.augerFrequencyThreshold,
		.fanModeThreshold = smokerConfig.logging.fanModeThreshold,
		.fanDutyCycleThreshold = smokerConfig.logging.fanDutyCycleThreshold,
		.fanFrequencyThreshold = smokerConfig.logging.fanFrequencyThreshold};
	DataLogger::init(logConfig);

	// Initialize filtered temperatures to first read values
	smokerData.filteredSmokeChamberTemp = smokechamberthermocouple.readFahrenheit();
	smokerData.filteredFirePotTemp = firepotthermocouple.readFahrenheit();

	const bool isHotStart = smokerData.filteredSmokeChamberTemp >= smokerConfig.tunable.minAutoRestartTemp;
	DataLogger::startSession(isHotStart);

	if (inputsTaskHandle == nullptr)
	{
		xTaskCreatePinnedToCore(
			InputsTask,
			"InputsTask",
			3072,
			nullptr,
			1,
			&inputsTaskHandle,
			1);
	}

	if (outputsTaskHandle == nullptr)
	{
		xTaskCreatePinnedToCore(
			OutputsTask,
			"OutputsTask",
			2048,
			nullptr,
			1,
			&outputsTaskHandle,
			1);
	}

	if (stateMachineTaskHandle == nullptr)
	{
		xTaskCreatePinnedToCore(
			StateMachineTask,
			"StateMachineTask",
			2048,
			nullptr,
			1,
			&stateMachineTaskHandle,
			1);
	}
}

void loop()
{
	delay(1000);
}
