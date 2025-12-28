/**
 * @file state.h
 * @brief Data structures for IntelliChem state
 *
 * Direct port from Python intellichem2mqtt/models/intellichem.py
 */

#ifndef MODELS_STATE_H
#define MODELS_STATE_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Enumerations
// ============================================================================

/**
 * @brief Dosing status values from IntelliChem
 */
typedef enum {
    DOSING_STATUS_DOSING = 0,
    DOSING_STATUS_MONITORING = 1,
    DOSING_STATUS_MIXING = 2
} dosing_status_t;

/**
 * @brief Water chemistry warning status
 */
typedef enum {
    WATER_CHEMISTRY_OK = 0,
    WATER_CHEMISTRY_CORROSIVE = 1,
    WATER_CHEMISTRY_SCALING = 2
} water_chemistry_t;

// ============================================================================
// Chemical State (pH or ORP)
// ============================================================================

/**
 * @brief State for a chemical measurement (pH or ORP)
 */
typedef struct {
    float level;                    // Current reading (pH value or ORP mV)
    float setpoint;                 // Target setpoint
    uint16_t dose_time;             // Current dosing time in seconds
    uint16_t dose_volume;           // Dose volume in mL
    uint8_t tank_level;             // Tank level 0-6 (7 levels)
    dosing_status_t dosing_status;  // Current dosing status
    bool is_dosing;                 // Whether actively dosing
} chemical_state_t;

// ============================================================================
// Alarms
// ============================================================================

/**
 * @brief Alarm states from IntelliChem
 */
typedef struct {
    bool flow;              // Flow alarm - no water flow detected
    bool ph_tank_empty;     // pH chemical tank empty
    bool orp_tank_empty;    // ORP chemical tank empty
    bool probe_fault;       // Probe fault detected
} alarms_t;

// ============================================================================
// Warnings
// ============================================================================

/**
 * @brief Warning states from IntelliChem
 */
typedef struct {
    bool ph_lockout;            // pH dosing locked out
    bool ph_daily_limit;        // pH daily dosing limit reached
    bool orp_daily_limit;       // ORP daily dosing limit reached
    bool invalid_setup;         // Invalid setup configuration
    bool chlorinator_comm_error; // Cannot communicate with chlorinator
    water_chemistry_t water_chemistry;  // Water chemistry status
} warnings_t;

// ============================================================================
// Complete IntelliChem State
// ============================================================================

/**
 * @brief Complete IntelliChem state model
 */
typedef struct {
    uint8_t address;            // IntelliChem address on RS-485 bus (144-158)
    chemical_state_t ph;        // pH measurement state
    chemical_state_t orp;       // ORP measurement state
    float lsi;                  // Langelier Saturation Index
    uint16_t calcium_hardness;  // Calcium Hardness in ppm
    uint8_t cyanuric_acid;      // Cyanuric Acid in ppm (0-210)
    uint16_t alkalinity;        // Alkalinity in ppm
    uint16_t salt_level;        // Salt level in ppm (from IntelliChlor)
    uint8_t temperature;        // Water temperature (typically Fahrenheit)
    char firmware[12];          // Firmware version string "X.XXX"
    alarms_t alarms;            // Active alarms
    warnings_t warnings;        // Active warnings
    bool flow_detected;         // Water flow is detected
    bool comms_lost;            // Communication with IntelliChem lost
    int64_t last_update_ms;     // Timestamp of last successful update (ms since boot)
} intellichem_state_t;

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Initialize state to default values
 * @param state Pointer to state structure
 */
static inline void intellichem_state_init(intellichem_state_t *state) {
    if (state == NULL) return;

    state->address = 144;
    state->ph.level = 0.0f;
    state->ph.setpoint = 7.2f;
    state->ph.dose_time = 0;
    state->ph.dose_volume = 0;
    state->ph.tank_level = 0;
    state->ph.dosing_status = DOSING_STATUS_MONITORING;
    state->ph.is_dosing = false;

    state->orp.level = 0.0f;
    state->orp.setpoint = 650.0f;
    state->orp.dose_time = 0;
    state->orp.dose_volume = 0;
    state->orp.tank_level = 0;
    state->orp.dosing_status = DOSING_STATUS_MONITORING;
    state->orp.is_dosing = false;

    state->lsi = 0.0f;
    state->calcium_hardness = 0;
    state->cyanuric_acid = 0;
    state->alkalinity = 0;
    state->salt_level = 0;
    state->temperature = 0;
    state->firmware[0] = '\0';

    state->alarms.flow = false;
    state->alarms.ph_tank_empty = false;
    state->alarms.orp_tank_empty = false;
    state->alarms.probe_fault = false;

    state->warnings.ph_lockout = false;
    state->warnings.ph_daily_limit = false;
    state->warnings.orp_daily_limit = false;
    state->warnings.invalid_setup = false;
    state->warnings.chlorinator_comm_error = false;
    state->warnings.water_chemistry = WATER_CHEMISTRY_OK;

    state->flow_detected = true;
    state->comms_lost = false;
    state->last_update_ms = 0;
}

/**
 * @brief Check if any alarm is active
 * @param alarms Pointer to alarms structure
 * @return true if any alarm is active
 */
static inline bool alarms_any_active(const alarms_t *alarms) {
    if (alarms == NULL) return false;
    return alarms->flow || alarms->ph_tank_empty ||
           alarms->orp_tank_empty || alarms->probe_fault;
}

/**
 * @brief Check if any warning is active
 * @param warnings Pointer to warnings structure
 * @return true if any warning is active
 */
static inline bool warnings_any_active(const warnings_t *warnings) {
    if (warnings == NULL) return false;
    return warnings->ph_lockout || warnings->ph_daily_limit ||
           warnings->orp_daily_limit || warnings->invalid_setup ||
           warnings->chlorinator_comm_error ||
           warnings->water_chemistry != WATER_CHEMISTRY_OK;
}

/**
 * @brief Convert tank level to percentage (0-100)
 * @param tank_level Tank level (0-6)
 * @return Percentage value
 */
static inline float tank_level_percent(uint8_t tank_level) {
    return (tank_level / 6.0f) * 100.0f;
}

/**
 * @brief Get string representation of dosing status
 * @param status Dosing status enum
 * @return String representation
 */
static inline const char* dosing_status_str(dosing_status_t status) {
    switch (status) {
        case DOSING_STATUS_DOSING:     return "Dosing";
        case DOSING_STATUS_MONITORING: return "Monitoring";
        case DOSING_STATUS_MIXING:     return "Mixing";
        default:                       return "Unknown";
    }
}

/**
 * @brief Get string representation of water chemistry status
 * @param status Water chemistry enum
 * @return String representation
 */
static inline const char* water_chemistry_str(water_chemistry_t status) {
    switch (status) {
        case WATER_CHEMISTRY_OK:        return "OK";
        case WATER_CHEMISTRY_CORROSIVE: return "Corrosive";
        case WATER_CHEMISTRY_SCALING:   return "Scaling";
        default:                        return "Unknown";
    }
}

#endif // MODELS_STATE_H
