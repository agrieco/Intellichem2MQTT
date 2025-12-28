/**
 * @file parser.h
 * @brief Status response parser for IntelliChem Action 18 messages
 *
 * Direct port from Python intellichem2mqtt/protocol/inbound.py
 */

#ifndef PROTOCOL_PARSER_H
#define PROTOCOL_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../models/state.h"

// ============================================================================
// Status Payload Byte Mapping
// ============================================================================

/*
 * Payload byte mapping (from IntelliChemStateMessage.ts):
 *
 *   Bytes 0-1:   pH level (Big Endian) / 100
 *   Bytes 2-3:   ORP level (Big Endian) mV
 *   Bytes 4-5:   pH setpoint / 100
 *   Bytes 6-7:   ORP setpoint mV
 *   Bytes 10-11: pH dose time (seconds)
 *   Bytes 14-15: ORP dose time (seconds)
 *   Bytes 16-17: pH dose volume (mL)
 *   Bytes 18-19: ORP dose volume (mL)
 *   Byte 20:     pH tank level (1-7, 0=no tank)
 *   Byte 21:     ORP tank level (1-7, 0=no tank)
 *   Byte 22:     LSI (signed, bit 7 = negative)
 *   Bytes 23-24: Calcium Hardness (ppm)
 *   Byte 26:     Cyanuric Acid (ppm)
 *   Bytes 27-28: Alkalinity (ppm)
 *   Byte 29:     Salt level * 50 (ppm)
 *   Byte 31:     Temperature
 *   Byte 32:     Alarms (bitfield)
 *   Byte 33:     Warnings (bitfield)
 *   Byte 34:     Dosing status (bitfield)
 *   Byte 35:     Status flags (bitfield)
 *   Bytes 36-37: Firmware version
 *   Byte 38:     Water chemistry (0=OK, 1=Corrosive, 2=Scaling)
 */

// Payload byte offsets
#define PAYLOAD_PH_LEVEL        0
#define PAYLOAD_ORP_LEVEL       2
#define PAYLOAD_PH_SETPOINT     4
#define PAYLOAD_ORP_SETPOINT    6
#define PAYLOAD_PH_DOSE_TIME    10
#define PAYLOAD_ORP_DOSE_TIME   14
#define PAYLOAD_PH_DOSE_VOLUME  16
#define PAYLOAD_ORP_DOSE_VOLUME 18
#define PAYLOAD_PH_TANK_LEVEL   20
#define PAYLOAD_ORP_TANK_LEVEL  21
#define PAYLOAD_LSI             22
#define PAYLOAD_CALCIUM         23
#define PAYLOAD_CYA             26
#define PAYLOAD_ALKALINITY      27
#define PAYLOAD_SALT            29
#define PAYLOAD_TEMPERATURE     31
#define PAYLOAD_ALARMS          32
#define PAYLOAD_WARNINGS        33
#define PAYLOAD_DOSING_STATUS   34
#define PAYLOAD_STATUS_FLAGS    35
#define PAYLOAD_FIRMWARE_MINOR  36
#define PAYLOAD_FIRMWARE_MAJOR  37
#define PAYLOAD_WATER_CHEMISTRY 38

// ============================================================================
// Parser Functions
// ============================================================================

/**
 * @brief Parse a complete status response packet
 *
 * Validates the packet structure, checksum, action code, and source address,
 * then extracts all fields into the state structure.
 *
 * @param packet Complete packet bytes including preamble and checksum
 * @param len Length of packet
 * @param state Output state structure (will be modified)
 * @return true if parsing succeeded, false if packet is invalid
 */
bool parser_parse_status(const uint8_t *packet, size_t len,
                         intellichem_state_t *state);

/**
 * @brief Parse only the 41-byte status payload (no packet validation)
 *
 * Use this when you've already validated the packet structure.
 *
 * @param payload Pointer to 41-byte payload
 * @param payload_len Length of payload (must be >= 41)
 * @param address IntelliChem address from packet header
 * @param state Output state structure
 * @return true if parsing succeeded
 */
bool parser_parse_payload(const uint8_t *payload, size_t payload_len,
                          uint8_t address, intellichem_state_t *state);

/**
 * @brief Log parsed state for debugging
 *
 * @param state Pointer to state structure
 */
void parser_log_state(const intellichem_state_t *state);

#endif // PROTOCOL_PARSER_H
