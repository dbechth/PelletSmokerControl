#include <Arduino.h>
#include "SmokerOutputs.h"
#include "SmokerControl.h"

extern int augerPin;
extern int fanPin;
extern int igniterPin;

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

bool fanPWM(int percent, float periodSeconds)
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

void IgniterControlTask()
{
    // Igniter modes: Off (0), On (1)
    switch (smokerData.igniter.mode)
    {
    case IgniterControl::Mode::Off:
        digitalWrite(igniterPin, Off);
        break;

    case IgniterControl::Mode::On:
        digitalWrite(igniterPin, On);
        break;

    default:
        digitalWrite(igniterPin, Off);
        break;
    }
}

void AugerControlTask()
{
    // Auger modes: Off (0), On (1), Auto (2), Manual (3), Mass (4)
    switch (smokerData.auger.mode)
    {
    case AugerControl::Mode::Off:
        smokerData.auger.dutyCycle = 0.0f;
        break;

    case AugerControl::Mode::On:
        smokerData.auger.dutyCycle = 100.0f;
        break;

    case AugerControl::Mode::Auto:
    {
        // Extract temperature and duty cycle arrays from transfer function
        float tempArray[11];
        float dutyCycleArray[11];
        for (int i = 0; i < 11; i++)
        {
            dutyCycleArray[i] = smokerConfig.tunable.augerTransferFunc[i][0];
            tempArray[i] = smokerConfig.tunable.augerTransferFunc[i][1];
        }
        
        float dutyCycleOffset = 0.0f;
        if (smokerData.filteredSmokeChamberTemp >= smokerConfig.operating.setpoint + 5.0f)
        {
            dutyCycleOffset = -10.0f;
        }
        else if (smokerData.filteredSmokeChamberTemp <= smokerConfig.operating.setpoint - 5.0f)
        {
            dutyCycleOffset = 10.0f; 
        }
        else
        {
            dutyCycleOffset = (smokerConfig.operating.setpoint - smokerData.filteredSmokeChamberTemp) * (10.0f / 5.0f); //bad proportional control, just for testing
        }

        if (abs(dutyCycleOffset) < 5.0f)
        {
            dutyCycleOffset = 0.0f; // add a deadband to prevent constant small adjustments
        }

        // Interpolate setpoint to get auto duty cycle
        smokerData.auger.dutyCycle = lookupTableInterpolate(smokerConfig.operating.setpoint, tempArray, dutyCycleArray, 11);
        smokerData.auger.dutyCycle += dutyCycleOffset; // apply offset based on current temp vs setpoint

        break;
    }

    case AugerControl::Mode::Manual:
        // Manual mode: Use duty cycle from smokerData
        break;

    case AugerControl::Mode::Mass:
        smokerData.auger.dutyCycle = 0.0f;
        break;

    default:
        smokerData.auger.dutyCycle = 0.0f;
        break;
    }

    smokerData.auger.frequency = smokerConfig.tunable.augerFrequency;
    if (augerPWM(smokerData.auger.dutyCycle, smokerData.auger.frequency))
    {
        digitalWrite(augerPin, On);
    }
    else
    {
        digitalWrite(augerPin, Off);
    }
}

void FanControlTask()
{
    // Fan modes: Off (0), On (1), Auto (2), Manual (3), Override (4)
    switch (smokerData.fan.mode)
    {
    case FanControl::Mode::Off:
        smokerData.fan.dutyCycle = 0.0f;
        break;

    case FanControl::Mode::On:
        smokerData.fan.dutyCycle = 100.0f;
        break;

    case FanControl::Mode::Auto:
    {
        // Extract smoke level and duty cycle arrays from transfer function
        float smokeArray[11];
        float dutyCycleArray[11];
        for (int i = 0; i < 11; i++)
        {
            dutyCycleArray[i] = smokerConfig.tunable.fanTransferFunc[i][0];
            smokeArray[i] = smokerConfig.tunable.fanTransferFunc[i][1];
        }
        // Interpolate smoke setpoint to get auto duty cycle
        smokerData.fan.dutyCycle = lookupTableInterpolate(smokerConfig.operating.smokesetpoint, smokeArray, dutyCycleArray, 11);
        break;
    }

    case FanControl::Mode::Manual:
        // Manual mode: Use duty cycle from smokerData
        break;

    case FanControl::Mode::Override:
        // Override mode: Use duty cycle from smokerData
        break;

    default:
        smokerData.fan.dutyCycle = 0.0f;
        break;
    }

    if (smokerData.fan.mode != FanControl::Mode::Override)
    {
        smokerData.fan.frequency = smokerConfig.tunable.fanFrequency;
    }

    if (fanPWM(smokerData.fan.dutyCycle, smokerData.fan.frequency))
    {
        digitalWrite(fanPin, On);
    }
    else
    {
        digitalWrite(fanPin, Off);
    }
}
