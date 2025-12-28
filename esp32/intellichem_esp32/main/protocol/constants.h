/**
 * @file constants.h
 * @brief Protocol constants for Pentair IntelliChem RS-485 communication
 *
 * Direct port from Python intellichem2mqtt/protocol/constants.py
 */

#ifndef PROTOCOL_CONSTANTS_H
#define PROTOCOL_CONSTANTS_H

#include <stdint.h>

// ============================================================================
// Packet Structure
// ============================================================================

#define PREAMBLE_BYTE_1     0xFF
#define PREAMBLE_BYTE_2     0x00
#define PREAMBLE_BYTE_3     0xFF
#define HEADER_START_BYTE   0xA5
#define HEADER_SUB_BYTE     0x00

// Preamble as array initializer
#define PREAMBLE_BYTES      { 0xFF, 0x00, 0xFF }
#define PREAMBLE_LENGTH     3
#define HEADER_LENGTH       6
#define CHECKSUM_LENGTH     2

// Minimum packet: 3 preamble + 6 header + 0 payload + 2 checksum
#define MIN_PACKET_SIZE     11

// ============================================================================
// Device Addresses
// ============================================================================

#define INTELLICHEM_ADDRESS_MIN     144
#define INTELLICHEM_ADDRESS_MAX     158
#define DEFAULT_INTELLICHEM_ADDRESS 144

// Controller address (we act as the controller)
#define CONTROLLER_ADDRESS          16

// ============================================================================
// Action Codes
// ============================================================================

#define ACTION_STATUS_REQUEST   210   // Request IntelliChem status
#define ACTION_STATUS_RESPONSE  18    // IntelliChem status response
#define ACTION_CONFIG_COMMAND   146   // Configuration command to IntelliChem
#define ACTION_OCP_BROADCAST    147   // OCP broadcast (Touch controllers)

// ============================================================================
// Payload Lengths
// ============================================================================

#define STATUS_PAYLOAD_LENGTH   41
#define CONFIG_PAYLOAD_LENGTH   21

// ============================================================================
// Serial Port Settings
// ============================================================================

#define DEFAULT_BAUDRATE        9600
#define DEFAULT_DATABITS        8
#define DEFAULT_STOPBITS        1

// ============================================================================
// Timing (milliseconds unless noted)
// ============================================================================

#define DEFAULT_POLL_INTERVAL_S     30      // seconds
#define DEFAULT_TIMEOUT_MS          5000    // milliseconds
#define COMMS_LOST_THRESHOLD_S      30      // seconds without response

// ============================================================================
// Alarm Bit Masks (byte 32 of status payload)
// ============================================================================

#define ALARM_FLOW              0x01
#define ALARM_PH                0x06
#define ALARM_ORP               0x18
#define ALARM_PH_TANK_EMPTY     0x20
#define ALARM_ORP_TANK_EMPTY    0x40
#define ALARM_PROBE_FAULT       0x80

// ============================================================================
// Warning Bit Masks (byte 33 of status payload)
// ============================================================================

#define WARNING_PH_LOCKOUT          0x01
#define WARNING_PH_DAILY_LIMIT      0x02
#define WARNING_ORP_DAILY_LIMIT     0x04
#define WARNING_INVALID_SETUP       0x08
#define WARNING_CHLORINATOR_COMM    0x10

// ============================================================================
// Dosing Status Masks (byte 34 of status payload)
// ============================================================================

#define DOSING_PH_TYPE_MASK     0x03
#define DOSING_ORP_TYPE_MASK    0x0C
#define DOSING_PH_STATUS_MASK   0x30
#define DOSING_ORP_STATUS_MASK  0xC0

// ============================================================================
// Status Flags Masks (byte 35 of status payload)
// ============================================================================

#define STATUS_MANUAL_DOSING    0x08
#define STATUS_USE_INTELLICHLOR 0x10
#define STATUS_HMI_ADVANCED     0x20
#define STATUS_PH_SUPPLY_ACID   0x40
#define STATUS_COMMS_LOST       0x80

// ============================================================================
// Tank Levels
// ============================================================================

#define TANK_LEVEL_MIN      0
#define TANK_LEVEL_MAX      7
#define TANK_CAPACITY       6

// ============================================================================
// Chemistry Ranges
// ============================================================================

#define PH_MIN              0.0f
#define PH_MAX              14.0f
#define ORP_MIN             0
#define ORP_MAX             2000

// Setpoint ranges (for control validation)
#define PH_SETPOINT_MIN     7.0f
#define PH_SETPOINT_MAX     7.6f
#define ORP_SETPOINT_MIN    400
#define ORP_SETPOINT_MAX    800

// Water chemistry ranges
#define CALCIUM_HARDNESS_MIN    25
#define CALCIUM_HARDNESS_MAX    800
#define CYANURIC_ACID_MIN       0
#define CYANURIC_ACID_MAX       210
#define ALKALINITY_MIN          25
#define ALKALINITY_MAX          800

// ============================================================================
// Buffer Sizes
// ============================================================================

#define PACKET_BUFFER_SIZE      256     // Ring buffer for incoming bytes
#define MAX_PACKET_SIZE         64      // Max expected packet size
#define TX_BUFFER_SIZE          64      // Transmit buffer

#endif // PROTOCOL_CONSTANTS_H
