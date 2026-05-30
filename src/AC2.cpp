#include "AC2.h"
#include <ArduinoOTA.h>
#include "terminal.h"

static void terminal()
{
	AC2.webserver.send(200, "text/html", terminal_page);
}

static void handleTerminal()
{
	String tmpBuffer = "";
	if (AC2.terminalBufferCount != 0)
	{
		for (int i = 0; i < (AC2.terminalBufferCount); i++)
		{
			tmpBuffer += AC2.terminalBuffer[i];
		}
		AC2.terminalBufferCount = 0;
	}
	AC2.webserver.send(200, "text/plain", tmpBuffer);
}

void AC2Class::print(String message)
{
	if (terminalBufferCount >= ACTerminalBufferSize)
	{
		terminalBuffer[0] = "Buffer Overflow! Messages were lost";
		terminalBufferCount = 1;
	}
	terminalBuffer[terminalBufferCount] = message;
	terminalBufferCount++;
}

void AC2Class::println(String message)
{
	print(message + "<br>");
}
bool AC2Class::init(String name, int taskRateMsConfig)
{
	deviceName = name;
	SetupOTA();
	webserver.on("/terminal", terminal);      //Which routine to handle at root location. This is display page
	webserver.on("/handleTerminal", handleTerminal);
	webserver.begin();

	terminalBufferCount = 0;

#ifdef ARDUINO_ARCH_ESP32
	if (taskRateMsConfig <= 0)
	{
		taskRateMs = 100;
	}
	else
	{
		taskRateMs = taskRateMsConfig;
	}

	if (ac2TaskHandle == nullptr)
	{
		xTaskCreatePinnedToCore(
			AC2TaskEntryPoint,
			"AC2Task",
			4096,
			this,
			1,
			&ac2TaskHandle,
			1);
	}
#endif

	return true;
}

#ifdef ARDUINO_ARCH_ESP32
void AC2Class::AC2TaskEntryPoint(void *parameter)
{
	AC2Class *instance = static_cast<AC2Class *>(parameter);
	instance->task();
}
#endif


void AC2Class::task()
{
#ifdef ARDUINO_ARCH_ESP32
	for (;;)
	{
		ArduinoOTA.handle();
		webserver.handleClient();
		vTaskDelay(pdMS_TO_TICKS(taskRateMs));
	}
#else
	ArduinoOTA.handle();
	webserver.handleClient();
#endif
}

void AC2Class::SetupOTA()
{
	// Port defaults to 8266
 // ArduinoOTA.setPort(8266);

 // Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(deviceName.c_str());

	// No authentication by default
	// ArduinoOTA.setPassword("admin");

	// Password can be set with it's md5 value as well
	// MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
	// ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

	ArduinoOTA.onStart([]() {
		String type;
		if (ArduinoOTA.getCommand() == U_FLASH) {
			type = "sketch";
		}
		else { // U_FS
			type = "filesystem";
		}

		// NOTE: if updating FS this would be the place to unmount FS using FS.end()
		Serial.println("Start updating " + type);
		});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd");
		});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
		});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) {
			Serial.println("Auth Failed");
		}
		else if (error == OTA_BEGIN_ERROR) {
			Serial.println("Begin Failed");
		}
		else if (error == OTA_CONNECT_ERROR) {
			Serial.println("Connect Failed");
		}
		else if (error == OTA_RECEIVE_ERROR) {
			Serial.println("Receive Failed");
		}
		else if (error == OTA_END_ERROR) {
			Serial.println("End Failed");
		}
		});
	ArduinoOTA.begin();
}

AC2Class AC2;

