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

unsigned long lastTime;
unsigned long timeNow;

#define task1000ms 1000

const char* CONFIG_FILE = "/smokerConfig.json";

SmokerData smokerData = {
	.filteredSmokeChamberTemp = 0.0f,
	.filteredFirePotTemp = 0.0f,
	.igniter = {.mode = IgniterControl::Mode::Off},
	.auger = {.mode = AugerControl::Mode::Off, .dutyCycle_Manual = 0.0f, .frequency_Manual = 0.0f, .Mass = 0.0f},
	.fan = {.mode = FanControl::Mode::Off, .dutyCycle_Manual = 0.0f, .frequency_Manual = 0.0f}};

SmokerConfig smokerConfig = {
	.operating = {
		.setpoint = 175.0f,
		.smokesetpoint = 50.0f
	},
	.tunable = {
		.minAutoRestartTemp = 100.0f,
		.minIdleTemp = 175.0f,
		.firePotBurningTemp = 200.0f,
		.startupFillTime = 180000UL,   // 3 minutes
		.igniterPreheatTime = 60000UL, // 1 minute
		.stabilizeTime = 60000UL	   // 1 minute
	},
	.recipe = {
		.recipeStepIndex = 0,
		.selectedRecipeIndex = -1
	}
};

UserInputs uiData = {
	.btn_Startup = false,
	.btn_Auto = false,
	.btn_Shutdown = false,
	.btn_Manual = false};

constexpr auto ControllerName = "PelletSmoker32";
int thermoDO = 21;
int thermoCLK = 19;
int thermoCS = 5;
int thermoCS2 = 18;

int augerPin = 32;   // Relay 1
int fanPin = 33;     // Relay 2
int igniterPin = 25; // Relay 3
int sparePin = 26;   // Relay 4

const int On = HIGH;
const int Off = LOW;

MAX6675 smokechamberthermocouple(thermoCLK, thermoCS2, thermoDO);
MAX6675 firepotthermocouple(thermoCLK, thermoCS, thermoDO);
static float smokechamberTemperature = 0;
static float firepotTemperature = 0;

char ssid[] = "Bulldog";
char pass[] = "6412108682";
char APssid[] = "DBBSmoker";

SmokerStateMachine smokerStateMachine;

WebInterface webInterface(AC2.webserver);

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

float lookupTableInterpolate(float inputValue, const float *inputArray, const float *outputArray, int arraySize)
{
	if (arraySize <= 0)
		return 0.0;
	if (arraySize == 1)
		return outputArray[0];

	if (inputValue <= inputArray[0])
		return outputArray[0];
	if (inputValue >= inputArray[arraySize - 1])
		return outputArray[arraySize - 1];

	for (int i = 0; i < arraySize - 1; i++)
	{
		if (inputValue >= inputArray[i] && inputValue <= inputArray[i + 1])
		{
			float x1 = inputArray[i];
			float x2 = inputArray[i + 1];
			float y1 = outputArray[i];
			float y2 = outputArray[i + 1];

			return y1 + (inputValue - x1) * (y2 - y1) / (x2 - x1);
		}
	}

	return outputArray[arraySize - 1];
}

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

bool SaveConfigToSPIFFS(const SmokerConfig& config)
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

bool LoadConfigFromSPIFFS(SmokerConfig& config)
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
	while (WiFi.status() != WL_CONNECTED && timeout < 20) {
		delay(500);
		Serial.print(".");
		timeout++;
	}
	Serial.println();

	if (WiFi.status() == WL_CONNECTED) {
		Serial.print("WiFi Connected - IP: ");
		Serial.println(WiFi.localIP());
	} else {
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

	smokerData.filteredSmokeChamberTemp = smokechamberthermocouple.readFahrenheit();
	smokerData.filteredFirePotTemp = firepotthermocouple.readFahrenheit();

}

void loop()
{
	AC2.task();

	timeNow = millis();
	unsigned long elapsedTime = timeNow - lastTime;
	if (elapsedTime >= task1000ms) 
	{
		lastTime = timeNow;
		smokechamberTemperature = smokechamberthermocouple.readFahrenheit();
		firepotTemperature = firepotthermocouple.readFahrenheit();

		smokerData.filteredSmokeChamberTemp = ((smokechamberTemperature * 0.5) + (smokerData.filteredSmokeChamberTemp * 0.5));
		smokerData.filteredFirePotTemp = ((firepotTemperature * 0.5) + (smokerData.filteredFirePotTemp * 0.5));

		smokerStateMachine.Run(task1000ms);

		IgniterControlTask();
		AugerControlTask();
		FanControlTask();
	}
}
