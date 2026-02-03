#include <EEPROM.h>
#include "ThermostatControl.h"
#include "AC2.h"
#include <WiFiClient.h>
#include <SPI.h>
#include <Wire.h>
#include <Servo.h>
#include "max6675.h"
#include "smokerIDX.h"
#include "SmokerControl.h"

SmokerData smokerData = {
	.filteredSmokeChamberTemp = 0.0f,
	.filteredFireBoxTemp = 0.0f,
	.setpoint = 0.0f,
	.smokesetpoint = 0.0f,
	.recipeStepIndex = 0,
	.selectedRecipeIndex = -1,
	.igniter = SmokerData::Mode::Off,
	.auger = SmokerData::Mode::Off,
	.fan = SmokerData::Mode::Off
};

SmokerConfig smokerConfig = {
    .minAutoRestartTemp = 100.0f,
    .minIdleTemp = 175.0f,
    .fireBoxBurningTemp = -200.0f,
    .startupFillTime = 180000UL,      // 3 minutes
    .igniterPreheatTime = 60000UL,    // 1 minute
    .stabilizeTime = 60000UL,         // 1 minute
    .recipeCount = 0
};

UserInputs uiData = {
	.web_start = false,
	.btn_start = false,
	.web_shutdown = false,
	.btn_shutdown = false
};

//#define bluetoothProbe
#ifdef ARDUINO_ARCH_ESP32
	#ifdef bluetoothProbe
		//Inkbird stuff
		#include "InkbirdCom.h"
		BLEScan *pBLEScan;
	#else
		#define CONFIG_LOG_DEFAULT_LEVEL ESP_LOG_NONE
		#define LOG_LOCAL_LEVEL ESP_LOG_NONE
	#endif
#endif

#ifdef ARDUINO_ARCH_ESP32
	constexpr auto ControllerName = "Smoker32";
	int thermoDO = 21;
	int thermoCLK = 19;
	int thermoCS = 5;
	int thermoCS2 = 18;
	//int pinInletDamper = D1;
	//int pinOutletDamper = D2;
	
	int augerPin = 32; //Relay 1
	int fanPin = 33; //Relay 2
	int igniterPin = 25; //Relay 3
	int sparePin = 26; //Relay 4

#else
	constexpr auto ControllerName = "Smoker";
	int thermoDO = D4;
	int thermoCS = D5;
	int thermoCLK = D6;
	int thermoCS2 = D3;
	int pinInletDamper = D1;
	int pinOutletDamper = D2;
#endif

constexpr int On = HIGH;
constexpr int Off = LOW;

MAX6675 smokechamberthermocouple(thermoCLK, thermoCS2, thermoDO);
MAX6675 fireboxthermocouple(thermoCLK, thermoCS, thermoDO);
static float smokechamberTemperature = 0;
static float fireboxTemperature = 0;

Servo InletDamper;  // create servo object to control a servo

//OS
#define Task100mS  100
#define Task1S  1000
#define Task1m  60000
unsigned long lastTime = 0;
unsigned long timeNow = 0;
unsigned long Time1S = 0;
unsigned long Time1m = 0;
//File file;


int DamperSetpoint = 100;
int ServoCMD;
int prevServoCMD;
int servoWriteTimer;
bool servoUpdateRequired;
bool servoAttached;
bool autoMode = true;
bool startup;
bool shutdown = false;
bool inIdle = false;
int transIdleToHeat = 0;
int transIdleToCool = 0;
bool disableFan = false;

int hotPCT = 0;
int idlePCT = 20;
int coldPCT = 100;
int manualPCT = 50;
int minPW = 30;
int maxPW = 120;
int preheatOffset = 50;
int autoIdleTuneThreshold = 2;
unsigned long startupStartTime = 0;
const unsigned long startupTimeoutMs = 15 * 60 * 1000; // 5 minutes in milliseconds
// Function prototype for isLidOpen

struct stChartData
{
	int temperature;
	int damperPCT;
	int setpoint;
	int Probe1;
	int Probe2;
	int Probe3;
	int Probe4;
};
stChartData chartdata[1024];
int chartdataIndex = 0;

enum RunMode
{
	Setup,
	Init,
	Run
};
RunMode runMode = Setup;

//WiFi
char ssid[] = "Bulldog";
char pass[] = "6412108682";
char APssid[] = "DBBSmoker";

WiFiClient client;

// Replace with your unique Thing Speak WRITE API KEY
const char* tsapiKey = "";//"OXMB6DTDS6D6VJYD";
const char* resource = "/update?api_key=";

// Thing Speak API server 
const char* server = "api.thingspeak.com";

String BuildJSON(String paramName, int Value)
{
	return "\"" + paramName + "\":" + Value;
}

void packChartData()
{
	String tmpBuffer = "{\"SmokerData\":[";
	for (int i = 0; i < chartdataIndex; i++)
	{
		if (i>0)
		{
			tmpBuffer += ",";
		}

		tmpBuffer += "{" +
			BuildJSON("index", i) + "," +
			BuildJSON("temperature", chartdata[i].temperature) + "," +
			BuildJSON("damperPCT", chartdata[i].damperPCT) + "," +
			BuildJSON("setpoint", chartdata[i].setpoint) + "," +
			#ifdef bluetoothProbe
				BuildJSON("Probe1", chartdata[i].Probe1) + "," +
				BuildJSON("Probe2", chartdata[i].Probe2) + "," +
				BuildJSON("Probe3", chartdata[i].Probe3) + "," +
				BuildJSON("Probe4", chartdata[i].Probe4) +
			#endif
			"}" ;
	}
	//tmpBuffer += file.read();
	tmpBuffer += "]}";

	AC2.webserver.send(200, "text/plain", tmpBuffer);
}
void packLastChartData()
{
	int idx = chartdataIndex-1;
	if (idx < 0)
	{
		idx = 0;
	}
	String tmpBuffer = "{\"SmokerData\":[";
		tmpBuffer += "{" +
			BuildJSON("index", idx) + "," +
			BuildJSON("temperature", chartdata[idx].temperature) + "," +
			BuildJSON("damperPCT", chartdata[idx].damperPCT) + "," +
			BuildJSON("setpoint", chartdata[idx].setpoint) + "," +
			#ifdef bluetoothProbe
				BuildJSON("Probe1", chartdata[idx].Probe1) + "," +
				BuildJSON("Probe2", chartdata[idx].Probe2) + "," +
				BuildJSON("Probe3", chartdata[idx].Probe3) + "," +
				BuildJSON("Probe4", chartdata[idx].Probe4) +
			#endif
			"}" ;
	tmpBuffer += "]}";

	AC2.webserver.send(200, "text/plain", tmpBuffer);
}

void mainPage()
{
	AC2.webserver.send(200, "text/html", smokerIDX);
}

void get()
{
	//read all local values and send them to the webpage
	if (AC2.webserver.args() > 0)
	{
		if (AC2.webserver.hasArg("smokerTemp"))
		{
			AC2.webserver.send(200, "text/plain", String(ThermostatControl.temperature, 2));
		}
		else if (AC2.webserver.hasArg("fireboxTemp"))
		{
			AC2.webserver.send(200, "text/plain", String(fireboxTemperature, 2));
		}
		else if (AC2.webserver.hasArg("damperPCT"))
		{
			AC2.webserver.send(200, "text/plain", String(DamperSetpoint));
		}
		else if (AC2.webserver.hasArg("autoON"))
		{
			AC2.webserver.send(200, "text/plain", String(autoMode));
		}
		else if (AC2.webserver.hasArg("manualON"))
		{
			AC2.webserver.send(200, "text/plain", String(!autoMode));
		}
		else if (AC2.webserver.hasArg("startup"))
		{
			AC2.webserver.send(200, "text/plain", String(startup));
		}
		else if (AC2.webserver.hasArg("shutdown"))
		{
			AC2.webserver.send(200, "text/plain", String(shutdown));
		}
		else if (AC2.webserver.hasArg("setPointF"))
		{
			AC2.webserver.send(200, "text/plain", String(ThermostatControl.setpoint));
		}
		else if (AC2.webserver.hasArg("setPointDeadband"))
		{
			AC2.webserver.send(200, "text/plain", String(ThermostatControl.hysteresis));
		}
		else if (AC2.webserver.hasArg("hotPCT"))
		{
			AC2.webserver.send(200, "text/plain", String(hotPCT));
		}
		else if (AC2.webserver.hasArg("idlePCT"))
		{
			AC2.webserver.send(200, "text/plain", String(idlePCT));
		}
		else if (AC2.webserver.hasArg("coldPCT"))
		{
			AC2.webserver.send(200, "text/plain", String(coldPCT));
		}
		else if (AC2.webserver.hasArg("manualPCT"))
		{
			AC2.webserver.send(200, "text/plain", String(manualPCT));
		}
		else if (AC2.webserver.hasArg("minPW"))
		{
			AC2.webserver.send(200, "text/plain", String(minPW));
		}
		else if (AC2.webserver.hasArg("maxPW"))
		{
			AC2.webserver.send(200, "text/plain", String(maxPW));
		}
		else if (AC2.webserver.hasArg("preheatOffset"))
		{
			AC2.webserver.send(200, "text/plain", String(preheatOffset));
		}
		else if (AC2.webserver.hasArg("autoIdleTuneThreshold"))
		{
			AC2.webserver.send(200, "text/plain", String(autoIdleTuneThreshold));
		}
		else if (AC2.webserver.hasArg("transIdleToCool"))
		{
			AC2.webserver.send(200, "text/plain", String(transIdleToCool));
		}
		else if (AC2.webserver.hasArg("transIdleToHeat"))
		{
			AC2.webserver.send(200, "text/plain", String(transIdleToHeat));
		}
		else if (AC2.webserver.hasArg("chartdata"))
		{
			packChartData();
		}
		else if (AC2.webserver.hasArg("lastchartdata"))
		{
			packLastChartData();
		}

	}
}

void set()
{
	if (AC2.webserver.args() > 0)
	{
		if (AC2.webserver.hasArg("autoON"))
		{
			autoMode = true;
		}
		else if (AC2.webserver.hasArg("manualON"))
		{
			autoMode = false;
			startup = false;
			shutdown = false;
		}
		else if (AC2.webserver.hasArg("startup"))
		{
			startup = !startup;
			if (startup)
			{
				autoMode = true;
				shutdown = false;
			}

		}
		else if (AC2.webserver.hasArg("shutdown"))
		{
			shutdown = !shutdown;
			if (shutdown)
			{
				autoMode = true;
				startup = false;
			}
		}
		else if (AC2.webserver.hasArg("setPointF"))
		{
			ThermostatControl.setpoint = AC2.webserver.arg("setPointF").toFloat();

			uint8_t tempvar = (uint8_t)(ThermostatControl.setpoint / 10.0);
			EEPROM.write(0, tempvar);
			EEPROM.commit();
		}
		else if (AC2.webserver.hasArg("setPointDeadband"))
		{
			ThermostatControl.hysteresis = AC2.webserver.arg("setPointDeadband").toFloat();
			uint8_t tempvar = (uint8_t)(ThermostatControl.hysteresis * 10.0);
			EEPROM.write(1, tempvar);
			EEPROM.commit();
		}
		else if (AC2.webserver.hasArg("hotPCT"))
		{
			hotPCT = AC2.webserver.arg("hotPCT").toInt();
			EEPROM.write(2, hotPCT);
			EEPROM.commit();
		}
		else if (AC2.webserver.hasArg("idlePCT"))
		{
			idlePCT = AC2.webserver.arg("idlePCT").toInt();
			EEPROM.write(3, idlePCT);
			EEPROM.commit();
		}
		else if (AC2.webserver.hasArg("coldPCT"))
		{
			coldPCT = AC2.webserver.arg("coldPCT").toInt();
			EEPROM.write(4, coldPCT);
			EEPROM.commit();
		}
		else if (AC2.webserver.hasArg("manualPCT"))
		{
			manualPCT = AC2.webserver.arg("manualPCT").toInt();
			EEPROM.write(5, manualPCT);
			EEPROM.commit();
		}
		else if (AC2.webserver.hasArg("minPW"))
		{
			minPW = AC2.webserver.arg("minPW").toInt();
			EEPROM.write(6, minPW);
			EEPROM.commit();
		}
		else if (AC2.webserver.hasArg("maxPW"))
		{
			maxPW = AC2.webserver.arg("maxPW").toInt();
			EEPROM.write(7, maxPW);
			EEPROM.commit();
		}
		else if (AC2.webserver.hasArg("preheatOffset"))
		{
			preheatOffset = AC2.webserver.arg("preheatOffset").toInt();
			EEPROM.write(8, preheatOffset);
			EEPROM.commit();
		}
		else if (AC2.webserver.hasArg("autoIdleTuneThreshold"))
		{
			autoIdleTuneThreshold = AC2.webserver.arg("autoIdleTuneThreshold").toInt();
			EEPROM.write(9, autoIdleTuneThreshold);
			EEPROM.commit();
		}
		else if (AC2.webserver.hasArg("resetDefaults"))
		{
			AC2.webserver.arg("resetDefaults") == "reset";
			EEPROM.write(10, true);
			EEPROM.commit();
			ESP.restart();
		}
	}
	AC2.webserver.send(200, "text/html", "ok");
}

boolean RunSetup()
{
	return true;
}

boolean RunInit()
{
	return true;
}

bool augerPWM(int percent, float periodSeconds) {
    static unsigned long cycleStartTime = 0;
    static bool cycleInitialized = false;

    if (!cycleInitialized) {
        cycleStartTime = millis();
        cycleInitialized = true;
    }

    unsigned long periodMillis = (unsigned long)(periodSeconds * 1000.0);
    unsigned long onTimeMillis = (unsigned long)((percent / 100.0) * periodMillis);
    unsigned long elapsedTime = millis() - cycleStartTime;

    // Reset cycle if period has elapsed
    if (elapsedTime >= periodMillis) {
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
float lookupTableInterpolate(float inputValue, const float* inputArray, const float* outputArray, int arraySize) 
{
    // Handle edge cases
    if (arraySize <= 0) return 0.0;
    if (arraySize == 1) return outputArray[0];
    
    // If input is below minimum, return first output value
    if (inputValue <= inputArray[0]) {
        return outputArray[0];
    }
    
    // If input is above maximum, return last output value
    if (inputValue >= inputArray[arraySize - 1]) {
        return outputArray[arraySize - 1];
    }
    
    // Find the two points to interpolate between
    for (int i = 0; i < arraySize - 1; i++) {
        if (inputValue >= inputArray[i] && inputValue <= inputArray[i + 1]) {
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

void RunTasksPellet()
{
	AC2.task(); //This application manages its own taskrate.

	timeNow = millis();
	unsigned long elapsedTime = timeNow - lastTime;
	if (elapsedTime >= Task100mS) {
		lastTime = timeNow;
		Time1S += elapsedTime;
		Time1m += elapsedTime;

		if (augerPWM(DamperSetpoint, minPW)) 
		{
			
			if (digitalRead(augerPin) == Off)
			{
				digitalWrite(augerPin, On);
			}
			
		} else 
		{
			if (digitalRead(augerPin) == On)
			{
				digitalWrite(augerPin, Off);
			}
		}
			//ESP_LOGI("BBQ", "Doing a loop.");
			// If the flag "doConnect" is true then we have scanned for and found the desired
			// BLE Server with which we wish to connect.  Now we connect to it.  Once we are
			// connected we set the connected flag to be true.
#ifdef bluetoothProbe
			if (doConnect == true)
			{
				if (connectToBLEServer(*pServerAddress))
				{
				ESP_LOGI("BBQ", "We are now connected to the BLE Server.");
				connected = true;
				doConnect = false;// added to prevent searching a second time just to fail
				}
				else
				{
				ESP_LOGI("BBQ", "We have failed to connect to the server; there is nothin more we will do.");
				}
				doConnect = false;
			}
#endif
	}
	if (Time1S >= Task1S) {
		Serial.println("Tick");
		smokechamberTemperature = smokechamberthermocouple.readFahrenheit();
		fireboxTemperature = fireboxthermocouple.readFahrenheit();
		AC2.print("SmokeTemp: ");
		AC2.println(String(smokechamberTemperature));
		AC2.print("FireboxTemp: ");
		AC2.println(String(fireboxTemperature));

		smokerData.filteredSmokeChamberTemp = ((smokechamberTemperature * 0.5) + (smokerData.filteredSmokeChamberTemp * 0.5));
		ThermostatControl.temperature =  smokerData.filteredSmokeChamberTemp;
		ThermostatControl.task();

		if (startup)
		{
			if (startupStartTime == 0)
			{
				startupStartTime = millis();
			}

			digitalWrite(igniterPin, On);
			digitalWrite(fanPin, On);
			DamperSetpoint = 100;
			if (smokerData.filteredSmokeChamberTemp >= (ThermostatControl.setpoint + preheatOffset))
			{
				startup = false;
				shutdown = false;
				startupStartTime = 0;
			}
			else if ((millis() - startupStartTime) >= startupTimeoutMs)
			{
				startup = false;
				shutdown = true;
				startupStartTime = 0;
			}
		}
		else if (shutdown)
		{
			startupStartTime =0;
			digitalWrite(igniterPin, Off);
			if (smokerData.filteredSmokeChamberTemp < 100.0)
			{
				digitalWrite(fanPin, Off);
			}
			else
			{
				digitalWrite(fanPin, On);
			}
			DamperSetpoint = 0;
		}
		else
		{
			startupStartTime =0;

			if (autoMode)
			{
				digitalWrite(igniterPin, Off);
				if (smokerData.filteredSmokeChamberTemp > (ThermostatControl.setpoint + preheatOffset))
				{
					disableFan =true;
				}
				else if (smokerData.filteredSmokeChamberTemp < (ThermostatControl.setpoint))
				{
					disableFan = false;
				}

				if (disableFan)
				{
					digitalWrite(fanPin, Off);
				}
				else
				{
					digitalWrite(fanPin, On);
				}
					
				// Check if temperature has fallen too low while in auto mode
				 if (ThermostatControl.output == ThermostatControl.cmdHeat)
				{
					if (inIdle)
					{
						inIdle = false;
						transIdleToHeat++;
					}

					DamperSetpoint = coldPCT;
				}
				else if (ThermostatControl.output == ThermostatControl.cmdCool)
				{
					if (inIdle)
					{
						inIdle = false;
						transIdleToCool++;
					}
					DamperSetpoint = hotPCT;
				}
				else
				{
					inIdle = true;
					DamperSetpoint = idlePCT;
				}
				if ((transIdleToHeat - transIdleToCool) >= autoIdleTuneThreshold)
				{
					idlePCT++;
					transIdleToHeat = 0;
					transIdleToCool = 0;
				}
				else if ((transIdleToHeat - transIdleToCool) <= (-autoIdleTuneThreshold))
				{
					idlePCT--;
					transIdleToHeat = 0;
					transIdleToCool = 0;
				}
			}
			else
			{
				digitalWrite(igniterPin, Off);
				digitalWrite(fanPin, On);
				DamperSetpoint = manualPCT;
			}
		}

		Time1S = 25;
	}
	if (Time1m >= Task1m) {
		chartdata[chartdataIndex].temperature = ThermostatControl.temperature;
		chartdata[chartdataIndex].setpoint = ThermostatControl.setpoint;
		chartdata[chartdataIndex].damperPCT = DamperSetpoint;
		#ifdef bluetoothProbe
			chartdata[chartdataIndex].Probe1 = Probes[0];
			chartdata[chartdataIndex].Probe2 = Probes[1];
			chartdata[chartdataIndex].Probe3 = Probes[2];
			chartdata[chartdataIndex].Probe4 = Probes[3];
			getBatteryData();
		#endif
		chartdataIndex++;
		Time1m = 50;
	}

}

		// Initialize recipe defaults (clears names, disables steps/recipes)
		static void initRecipeDefaults()
		{
			smokerConfig.recipeCount = 0;
			for (int r = 0; r < MAX_RECIPES; ++r) {
				smokerConfig.recipeData[r].name[0] = '\0';
				smokerConfig.recipeData[r].stepCount = 0;
				smokerConfig.recipeData[r].enabled = false;
				for (int s = 0; s < MAX_RECIPE_STEPS; ++s) {
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

void setup() {
	pinMode(igniterPin, OUTPUT);
	pinMode(fanPin, OUTPUT);
	pinMode(augerPin, OUTPUT);
	//bool success = SPIFFS.begin();
	Serial.begin(115200);
	EEPROM.begin(512);
	AC2.println("Starting");

	WiFi.hostname(ControllerName);
	WiFi.mode(WIFI_AP_STA);
	WiFi.begin(ssid, pass);
	WiFi.softAP(APssid);

	//these three web server calls handle all data within the webpage and can be found in the mainpage, get, and set, functions respectively
	AC2.webserver.on("/", mainPage);
	AC2.webserver.on("/get", get);
	AC2.webserver.on("/set", set);
	delay(5000);
	Serial.println(WiFi.localIP());
	AC2.init(ControllerName, WiFi.localIP(), IPADDR_BROADCAST, 4020, Task100mS);

	// initialize recipe data structures to safe defaults
	initRecipeDefaults();

	float tempSetpoint = 225.0;
	float tempDeadband = 2.0;
	bool resetDefaults = EEPROM.read(10);
	smokerData.filteredSmokeChamberTemp = smokechamberthermocouple.readFahrenheit();
	startup = smokerData.filteredSmokeChamberTemp < 100; //if the smoker is cold, start in startup mode

	if (resetDefaults)
	{
		AC2.println("Loading default settings...");
		uint8_t tempvar = (uint8_t)(tempSetpoint / 10.0);
		EEPROM.write(0, tempvar);
		tempvar = (uint8_t)(tempDeadband * 10.0);
		EEPROM.write(1, tempvar);
		EEPROM.write(2, hotPCT);
		EEPROM.write(3, idlePCT);
		EEPROM.write(4, coldPCT);
		EEPROM.write(5, manualPCT);
		EEPROM.write(6, minPW);
		EEPROM.write(7, maxPW);
		EEPROM.write(8, preheatOffset);
		EEPROM.write(9, autoIdleTuneThreshold);
		EEPROM.write(10, false);
		EEPROM.commit();
	}
	else
	{
		AC2.println("Loading previous settings...");
		tempSetpoint = EEPROM.read(0) * 10.0;
		tempDeadband = EEPROM.read(1) / 10.0;
		hotPCT = EEPROM.read(2);
		idlePCT = EEPROM.read(3);
		coldPCT = EEPROM.read(4);
		manualPCT = EEPROM.read(5);
		minPW = EEPROM.read(6);
		maxPW = EEPROM.read(7);
		preheatOffset = EEPROM.read(8);
		autoIdleTuneThreshold = EEPROM.read(9);
	}

	ThermostatControl.init(tempSetpoint, tempDeadband, ThermostatControl.HeatCool, &AC2.webserver);

#ifdef bluetoothProbe
	setLogLevel();

	ESP_LOGD("BBQ", "Scanning");
	BLEDevice::init("");

	// Retrieve a Scanner and set the callback we want to use to be informed when we
	// have detected a new device.  Specify that we want active scanning and start the
	// scan to run for 30 seconds.
	pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setActiveScan(true);
	pBLEScan->start(30);
#endif
	//get started off right
	timeNow = millis();
	lastTime = timeNow;
}

void loop() {

	//this pretty much defaults to run, add fancy init or setup logic if desired
	switch (runMode)
	{
	case Setup:
		if (RunSetup())
		{
			runMode = Init;
		}
		break;
	case Init:
		if (RunInit())
		{
			runMode = Run;
		}
		break;
	case Run:
		RunTasksPellet();
		break;
	}
}
