#pragma once

/*
    SmokerOutputs.h - Output control functions for the Smoker

    This header declares the control functions for managing the smoker's
    output devices: igniter, auger, and fan.

    These functions should be called periodically from the main loop to
    update the control signals based on the current smoker state and setpoints.
*/

// ============================================================================
// Function Prototypes
// ============================================================================

/**
 * @brief Control igniter output based on current state and setpoints
 *
 * This function manages the igniter relay control, determining when the
 * igniter should be on or off based on the smoker's control logic.
 */
void IgniterControlTask();

/**
 * @brief Control auger output based on current state and setpoints
 *
 * This function manages the auger relay control, applying PWM or on/off
 * logic to control pellet feed rate based on temperature setpoints.
 */
void AugerControlTask();

/**
 * @brief Control fan output based on current state and setpoints
 *
 * This function manages the fan relay control, determining when the fan
 * should be on or off based on temperature and smoke level setpoints.
 */
void FanControlTask();
