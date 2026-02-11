#include <EEPROM.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "AC2.h"
#include <WiFiClient.h>
#include <SPI.h>
#include "max6675.h"
#include "SmokerControl.h"
#include "SmokerOutputs.h"
#include "SmokerStateMachine.h"
#include "WebInterface.h"
#include "DataLogger.h"

unsigned long lastTime;
unsigned long timeNow;

#define task500ms 500

const char *CONFIG_FILE = "/smokerConfig.json";

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
			{33.0f, 175.0f},   // 0% duty = 175F
			{37.0f, 200.0f},   // 10% duty = 185F
			{41.0f, 225.0f},   // 20% duty = 195F
			{47.0f, 250.0f},   // 30% duty = 205F
			{52.0f, 275.0f},   // 40% duty = 215F
			{58.0f, 300.0f},   // 50% duty = 225F
			{64.0f, 325.0f},   // 60% duty = 235F
			{72.0f, 350.0f},   // 70% duty = 245F
			{80.0f, 375.0f},   // 80% duty = 255F
			{90.0f, 400.0f},   // 90% duty = 265F
			{100.0f, 425.0f}   // 100% duty = 275F
		},
		.fanTransferFunc = {
					{100.0f, 0.0f}, // 100% duty = 0% smoke
					{95.0f, 10.0f}, // 90% duty = 10% smoke
					{90.0f, 20.0f}, // 80% duty = 20% smoke
					{85.0f, 30.0f}, // 70% duty = 30% smoke
					{80.0f, 40.0f}, // 60% duty = 40% smoke
					{75.0f, 50.0f}, // 50% duty = 50% smoke
					{70.0f, 60.0f}, // 40% duty = 60% smoke
					{65.0f, 70.0f}, // 30% duty = 70% smoke
					{60.0f, 80.0f}, // 20% duty = 80% smoke
					{55.0f, 90.0f}, // 10% duty = 90% smoke
					{50.0f, 100.0f} // 0% duty = 100% smoke
				}},
	.recipe = {.recipeStepIndex = 0, .selectedRecipeIndex = -1},
	.logging = {
		.enabled = true,
		.logIntervalMs = 30000,
		.maxLogFiles = 1,
		.maxLogFileSizeBytes = 500000}};

UserInputs uiData = {
	.btn_Startup = false,
	.btn_Auto = false,
	.btn_Shutdown = false,
	.btn_Manual = false};

int thermoDO = 21;
int thermoCLK = 19;
int thermoCS = 5;
MAX6675 firepotthermocouple(thermoCLK, thermoCS, thermoDO);
static float firepotTemperature = 0;

int thermoDO2 = 18;
int thermoCLK2 = 17;
int thermoCS2 = 4;
MAX6675 smokechamberthermocouple(thermoCLK2, thermoCS2, thermoDO2);
static float smokechamberTemperature = 0;

int augerPin = 32;	 // Relay 1
int fanPin = 33;	 // Relay 2
int igniterPin = 25; // Relay 3
int sparePin = 26;	 // Relay 4

constexpr auto ControllerName = "PelletSmoker32";
char ssid[] = "Bulldog";
char pass[] = "6412108682";
char APssid[] = "DBBSmoker";

SmokerStateMachine smokerStateMachine;

WebInterface webInterface(AC2.webserver);

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

bool SaveConfigToSPIFFS(const SmokerConfig &config)
{
	StaticJsonDocument<4096> doc;

	doc["operating"]["setpoint"] = config.operating.setpoint;
	doc["operating"]["smokesetpoint"] = config.operating.smokesetpoint;

	doc["tunable"]["minAutoRestartTemp"] = config.tunable.minAutoRestartTemp;
	doc["tunable"]["minIdleTemp"] = config.tunable.minIdleTemp;
	doc["tunable"]["firePotBurningTemp"] = config.tunable.firePotBurningTemp;
	doc["tunable"]["startupFillTime"] = config.tunable.startupFillTime;
	doc["tunable"]["igniterPreheatTime"] = config.tunable.igniterPreheatTime;
	doc["tunable"]["stabilizeTime"] = config.tunable.stabilizeTime;

	// Save auger transfer function
	JsonArray augerTransfer = doc["tunable"]["augerTransferFunc"].to<JsonArray>();
	for (int i = 0; i < 11; ++i)
	{
		JsonArray point = augerTransfer.add<JsonArray>();
		point.add(config.tunable.augerTransferFunc[i][0]);
		point.add(config.tunable.augerTransferFunc[i][1]);
	}

	// Save fan transfer function
	JsonArray fanTransfer = doc["tunable"]["fanTransferFunc"].to<JsonArray>();
	for (int i = 0; i < 11; ++i)
	{
		JsonArray point = fanTransfer.add<JsonArray>();
		point.add(config.tunable.fanTransferFunc[i][0]);
		point.add(config.tunable.fanTransferFunc[i][1]);
	}

	// Save auto-mode frequencies
	doc["tunable"]["augerFrequency_Auto"] = config.tunable.augerFrequency;
	doc["tunable"]["fanfrequency_Auto"] = config.tunable.fanFrequency;

	doc["recipe"]["recipeStepIndex"] = config.recipe.recipeStepIndex;
	doc["recipe"]["selectedRecipeIndex"] = config.recipe.selectedRecipeIndex;

	JsonArray recipes = doc["recipe"]["recipeData"].to<JsonArray>();
	for (int r = 0; r < MAX_RECIPES; ++r)
	{
		JsonObject recipe = recipes.createNestedObject();
		recipe["name"] = config.recipe.recipeData[r].name;
		recipe["stepCount"] = config.recipe.recipeData[r].stepCount;
		recipe["enabled"] = config.recipe.recipeData[r].enabled;

		JsonArray steps = recipe["steps"].to<JsonArray>();
		for (int s = 0; s < MAX_RECIPE_STEPS; ++s)
		{
			JsonObject step = steps.createNestedObject();
			step["name"] = config.recipe.recipeData[r].steps[s].name;
			step["enabled"] = config.recipe.recipeData[r].steps[s].enabled;
			step["startTempSetpoint"] = config.recipe.recipeData[r].steps[s].startTempSetpoint;
			step["endTempSetpoint"] = config.recipe.recipeData[r].steps[s].endTempSetpoint;
			step["startSmokeSetpoint"] = config.recipe.recipeData[r].steps[s].startSmokeSetpoint;
			step["endSmokeSetpoint"] = config.recipe.recipeData[r].steps[s].endSmokeSetpoint;
			step["stepDurationMs"] = config.recipe.recipeData[r].steps[s].stepDurationMs;
			step["meatProbeExitTemp"] = config.recipe.recipeData[r].steps[s].meatProbeExitTemp;
		}
	}

	// Save logging configuration
	doc["logging"]["enabled"] = config.logging.enabled;
	doc["logging"]["logIntervalMs"] = config.logging.logIntervalMs;
	doc["logging"]["maxLogFiles"] = config.logging.maxLogFiles;
	doc["logging"]["maxLogFileSizeBytes"] = config.logging.maxLogFileSizeBytes;

	File file = SPIFFS.open(CONFIG_FILE, "w");
	if (!file)
	{
		Serial.println("Failed to open config file for writing");
		return false;
	}

	if (serializeJson(doc, file) == 0)
	{
		Serial.println("Failed to write config to file");
		file.close();
		return false;
	}

	file.close();
	return true;
}

bool LoadConfigFromSPIFFS(SmokerConfig &config)
{
	if (!SPIFFS.exists(CONFIG_FILE))
	{
		Serial.println("Config file does not exist");
		return false;
	}

	File file = SPIFFS.open(CONFIG_FILE, "r");
	if (!file)
	{
		Serial.println("Failed to open config file for reading");
		return false;
	}

	StaticJsonDocument<4096> doc;
	DeserializationError error = deserializeJson(doc, file);
	file.close();

	if (error)
	{
		Serial.print("Config file is corrupt: ");
		Serial.println(error.c_str());
		return false;
	}

	if (!doc["recipe"].containsKey("recipeData") || !doc["recipe"]["recipeData"].is<JsonArray>())
	{
		Serial.println("Config file missing recipeData");
		return false;
	}

	config.operating.setpoint = doc["operating"]["setpoint"];
	config.operating.smokesetpoint = doc["operating"]["smokesetpoint"];

	config.tunable.minAutoRestartTemp = doc["tunable"]["minAutoRestartTemp"];
	config.tunable.minIdleTemp = doc["tunable"]["minIdleTemp"];
	config.tunable.firePotBurningTemp = doc["tunable"]["firePotBurningTemp"];
	config.tunable.startupFillTime = doc["tunable"]["startupFillTime"];
	config.tunable.igniterPreheatTime = doc["tunable"]["igniterPreheatTime"];
	config.tunable.stabilizeTime = doc["tunable"]["stabilizeTime"];

	// Load auto-mode frequencies (provide sensible defaults)
	config.tunable.augerFrequency = doc["tunable"]["augerFrequency_Auto"] | 1.0f;
	config.tunable.fanFrequency = doc["tunable"]["fanfrequency_Auto"] | 1.0f;

	// Load auger transfer function
	if (doc["tunable"]["augerTransferFunc"].is<JsonArray>())
	{
		JsonArray augerTransfer = doc["tunable"]["augerTransferFunc"].as<JsonArray>();
		for (int i = 0; i < 11 && i < augerTransfer.size(); ++i)
		{
			if (augerTransfer[i].is<JsonArray>())
			{
				JsonArray point = augerTransfer[i].as<JsonArray>();
				config.tunable.augerTransferFunc[i][0] = point[0];
				config.tunable.augerTransferFunc[i][1] = point[1];
			}
		}
	}

	// Load fan transfer function
	if (doc["tunable"]["fanTransferFunc"].is<JsonArray>())
	{
		JsonArray fanTransfer = doc["tunable"]["fanTransferFunc"].as<JsonArray>();
		for (int i = 0; i < 11 && i < fanTransfer.size(); ++i)
		{
			if (fanTransfer[i].is<JsonArray>())
			{
				JsonArray point = fanTransfer[i].as<JsonArray>();
				config.tunable.fanTransferFunc[i][0] = point[0];
				config.tunable.fanTransferFunc[i][1] = point[1];
			}
		}
	}

	config.recipe.recipeStepIndex = doc["recipe"]["recipeStepIndex"];
	config.recipe.selectedRecipeIndex = doc["recipe"]["selectedRecipeIndex"];

	JsonArray recipes = doc["recipe"]["recipeData"].as<JsonArray>();
	for (int r = 0; r < MAX_RECIPES && r < recipes.size(); ++r)
	{
		JsonObject recipe = recipes[r];
		strlcpy(config.recipe.recipeData[r].name, recipe["name"] | "", sizeof(config.recipe.recipeData[r].name));
		config.recipe.recipeData[r].stepCount = recipe["stepCount"];
		config.recipe.recipeData[r].enabled = recipe["enabled"];

		if (recipe["steps"].is<JsonArray>())
		{
			JsonArray steps = recipe["steps"].as<JsonArray>();
			for (int s = 0; s < MAX_RECIPE_STEPS && s < steps.size(); ++s)
			{
				JsonObject step = steps[s];
				strlcpy(config.recipe.recipeData[r].steps[s].name, step["name"] | "", sizeof(config.recipe.recipeData[r].steps[s].name));
				config.recipe.recipeData[r].steps[s].enabled = step["enabled"];
				config.recipe.recipeData[r].steps[s].startTempSetpoint = step["startTempSetpoint"];
				config.recipe.recipeData[r].steps[s].endTempSetpoint = step["endTempSetpoint"];
				config.recipe.recipeData[r].steps[s].startSmokeSetpoint = step["startSmokeSetpoint"];
				config.recipe.recipeData[r].steps[s].endSmokeSetpoint = step["endSmokeSetpoint"];
				config.recipe.recipeData[r].steps[s].stepDurationMs = step["stepDurationMs"];
				config.recipe.recipeData[r].steps[s].meatProbeExitTemp = step["meatProbeExitTemp"];
			}
		}
	}

	// Load logging configuration (provide defaults if not found)
	config.logging.enabled = doc["logging"]["enabled"] | false;
	config.logging.logIntervalMs = doc["logging"]["logIntervalMs"] | 5000;
	config.logging.maxLogFiles = doc["logging"]["maxLogFiles"] | 10;
	config.logging.maxLogFileSizeBytes = doc["logging"]["maxLogFileSizeBytes"] | 100000;

	return true;
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

	AC2.init(ControllerName, WiFi.localIP(), IPADDR_BROADCAST, 4020, 100);

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

	// Initialize DataLogger with config
	LogConfig logConfig = {
		.enabled = smokerConfig.logging.enabled,
		.logIntervalMs = smokerConfig.logging.logIntervalMs,
		.maxLogFiles = smokerConfig.logging.maxLogFiles,
		.maxLogFileSizeBytes = smokerConfig.logging.maxLogFileSizeBytes};
	DataLogger::init(logConfig);

	// initialize filtered temperatures to first read values
	smokerData.filteredSmokeChamberTemp = smokechamberthermocouple.readFahrenheit();
	smokerData.filteredFirePotTemp = firepotthermocouple.readFahrenheit();
	timeNow = millis();
	lastTime = timeNow;
}

void loop()
{
	AC2.task();

	timeNow = millis();
	unsigned long elapsedTime = timeNow - lastTime;
	if (elapsedTime >= task500ms)
	{
		lastTime = timeNow;
		smokechamberTemperature = smokechamberthermocouple.readFahrenheit();
		firepotTemperature = firepotthermocouple.readFahrenheit();

		smokerData.filteredSmokeChamberTemp = ((smokechamberTemperature * 0.5) + (smokerData.filteredSmokeChamberTemp * 0.5));
		smokerData.filteredFirePotTemp = ((firepotTemperature * 0.5) + (smokerData.filteredFirePotTemp * 0.5));

		smokerStateMachine.Run(task500ms);
	}

	IgniterControlTask();
	AugerControlTask();
	FanControlTask();

	// Log data if enabled
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
