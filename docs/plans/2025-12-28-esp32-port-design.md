# ESP32-C3 IntelliChem2MQTT Port - Design Document

**Date**: 2025-12-28
**Status**: Approved
**Branch**: feature/full-control

## Overview

Port the Python IntelliChem2MQTT bridge to ESP32-C3 using ESP-IDF C, enabling a low-cost (~$20) dedicated hardware solution for IntelliChem RS-485 to MQTT bridging with Home Assistant auto-discovery.

## Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Task Architecture | Two-task (serial + mqtt) | Maps to Python asyncio design, minimal overhead |
| Configuration | Compile-time (Kconfig) | Fast development; abstraction layer for future NVS |
| Logging | Verbose with hex dumps | Debugging priority; ESP_LOG with per-component tags |
| HA Discovery | Full (30+ entities) | Feature parity with Python version |

## Project Structure

```
esp32/intellichem_esp32/
├── CMakeLists.txt
├── sdkconfig
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                   # Entry point, task creation
│   ├── Kconfig.projbuild        # Custom config options
│   │
│   ├── config/
│   │   ├── config.h             # config_t struct, getters
│   │   └── config.c             # Load from Kconfig (future: NVS)
│   │
│   ├── protocol/
│   │   ├── constants.h          # All protocol defines
│   │   ├── message.h/.c         # Packet build/validate/checksum
│   │   ├── parser.h/.c          # Status response parser (Action 18)
│   │   ├── commands.h/.c        # Config command builder (Action 146)
│   │   └── buffer.h/.c          # Ring buffer with preamble detection
│   │
│   ├── serial/
│   │   ├── serial_task.h/.c     # UART init, RX/TX, packet processing
│   │   └── rs485.h/.c           # RS-485 direction control (DE/RE pin)
│   │
│   ├── mqtt/
│   │   ├── mqtt_task.h/.c       # WiFi connect, MQTT client, queues
│   │   ├── discovery.h/.c       # HA discovery JSON generation
│   │   └── publisher.h/.c       # State publishing helpers
│   │
│   └── models/
│       └── state.h              # C structs for IntelliChemState
```

## Data Structures

### Core State Model (state.h)

```c
typedef enum {
    DOSING_STATUS_DOSING = 0,
    DOSING_STATUS_MONITORING = 1,
    DOSING_STATUS_MIXING = 2
} dosing_status_t;

typedef enum {
    WATER_CHEMISTRY_OK = 0,
    WATER_CHEMISTRY_CORROSIVE = 1,
    WATER_CHEMISTRY_SCALING = 2
} water_chemistry_t;

typedef struct {
    float level;
    float setpoint;
    uint16_t dose_time;
    uint16_t dose_volume;
    uint8_t tank_level;
    dosing_status_t dosing_status;
    bool is_dosing;
} chemical_state_t;

typedef struct {
    bool flow;
    bool ph_tank_empty;
    bool orp_tank_empty;
    bool probe_fault;
} alarms_t;

typedef struct {
    bool ph_lockout;
    bool ph_daily_limit;
    bool orp_daily_limit;
    bool invalid_setup;
    bool chlorinator_comm_error;
    water_chemistry_t water_chemistry;
} warnings_t;

typedef struct {
    uint8_t address;
    chemical_state_t ph;
    chemical_state_t orp;
    float lsi;
    uint16_t calcium_hardness;
    uint8_t cyanuric_acid;
    uint16_t alkalinity;
    uint16_t salt_level;
    uint8_t temperature;
    char firmware[8];
    alarms_t alarms;
    warnings_t warnings;
    bool flow_detected;
    bool comms_lost;
    int64_t last_update_ms;
} intellichem_state_t;
```

## Protocol Layer

### Constants (constants.h)

```c
// Packet structure
#define PREAMBLE_BYTE_1     0xFF
#define PREAMBLE_BYTE_2     0x00
#define PREAMBLE_BYTE_3     0xFF
#define HEADER_START_BYTE   0xA5
#define HEADER_SUB_BYTE     0x00

// Addresses
#define CONTROLLER_ADDRESS          16
#define INTELLICHEM_ADDRESS_MIN     144
#define INTELLICHEM_ADDRESS_MAX     158
#define DEFAULT_INTELLICHEM_ADDRESS 144

// Action codes
#define ACTION_STATUS_REQUEST   210
#define ACTION_STATUS_RESPONSE  18
#define ACTION_CONFIG_COMMAND   146

// Payload lengths
#define STATUS_PAYLOAD_LENGTH   41
#define CONFIG_PAYLOAD_LENGTH   21
#define MIN_PACKET_SIZE         11
```

### Message Functions (message.h)

```c
size_t message_build(uint8_t *buf, size_t buf_size,
                     uint8_t dest, uint8_t src,
                     uint8_t action, const uint8_t *payload, uint8_t len);

bool message_validate_checksum(const uint8_t *packet, size_t len);

uint8_t message_get_action(const uint8_t *packet);
uint8_t message_get_source(const uint8_t *packet);
uint8_t message_get_payload_len(const uint8_t *packet);
const uint8_t *message_get_payload(const uint8_t *packet);
```

### Parser (parser.h)

```c
bool parser_parse_status(const uint8_t *packet, size_t len,
                         intellichem_state_t *state);
```

### Commands (commands.h)

```c
size_t command_build_config(uint8_t *buf, size_t buf_size,
                            uint8_t intellichem_addr,
                            float ph_setpoint, uint16_t orp_setpoint,
                            uint8_t ph_tank, uint8_t orp_tank,
                            uint16_t calcium, uint8_t cya, uint16_t alk);

size_t command_build_status_request(uint8_t *buf, size_t buf_size,
                                    uint8_t intellichem_addr);
```

## Task Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         main.c                                  │
│  app_main() → init config → create queues → start tasks        │
└─────────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
┌─────────────────────────┐     ┌─────────────────────────┐
│     serial_task         │     │      mqtt_task          │
│  ───────────────────    │     │  ───────────────────    │
│  • UART RX interrupt    │     │  • WiFi connect/monitor │
│  • Packet buffer        │     │  • MQTT connect/reconn  │
│  • Preamble detection   │     │  • HA discovery publish │
│  • Checksum validation  │     │  • State publishing     │
│  • Status parsing       │     │  • Command subscription │
│  • Poll timer (30s)     │     │  • LWT (availability)   │
│  • TX status requests   │     │                         │
│  • TX config commands   │◄────┤  command_queue          │
│                         │     │                         │
│  state_queue ───────────┼────►│                         │
└─────────────────────────┘     └─────────────────────────┘
```

### Inter-Task Communication

```c
// State updates: serial_task → mqtt_task
QueueHandle_t state_queue;      // intellichem_state_t, depth=2

// Commands: mqtt_task → serial_task
QueueHandle_t command_queue;    // command_msg_t, depth=4

typedef struct {
    uint8_t type;               // CMD_SET_PH, CMD_SET_ORP, etc.
    union {
        float ph_setpoint;
        uint16_t orp_setpoint;
        bool dosing_enabled;
    } data;
} command_msg_t;
```

### Task Configuration

| Task | Priority | Stack Size | Rationale |
|------|----------|------------|-----------|
| serial_task | 5 | 4096 bytes | Higher priority for time-sensitive UART |
| mqtt_task | 4 | 8192 bytes | WiFi/TLS stack needs more memory |

## Logging Strategy

Using ESP-IDF's `ESP_LOG` with component tags:

```c
static const char *TAG = "serial";  // per-file tag

// Levels: VERBOSE, DEBUG, INFO, WARN, ERROR
ESP_LOGI(TAG, "Status received: pH=%.2f, ORP=%dmV", ph, orp);
ESP_LOGD(TAG, "TX [%d bytes]: %s", len, hex_str);
```

### Example Output

```
I (1234) main: IntelliChem2MQTT starting, firmware v1.0.0
I (1250) serial: UART1 initialized: 9600 8N1, TX=17 RX=18 DE=19
I (2100) mqtt: WiFi connected, IP: 192.168.1.50
I (2300) mqtt: MQTT connected, publishing HA discovery...
I (2510) serial: Sending status request to 0x90
D (2512) serial: TX [16 bytes]: FF 00 FF A5 00 90 10 D2 00 02 79
D (2650) serial: RX [52 bytes]: FF 00 FF A5 00 10 90 12 29 02 D4...
I (2651) parser: Status: pH=7.24, ORP=700mV, temp=82°F
```

## Implementation Phases

### Phase 1: Protocol Core (QEMU testable)
- constants.h, state.h
- message.c - packet build/validate
- parser.c - status parsing
- buffer.c - ring buffer
- **Test**: Unit tests in app_main()

### Phase 2: Serial Task (QEMU testable)
- serial_task.c - UART, packet processing
- rs485.c - DE/RE control
- Poll timer with status requests
- **Test**: QEMU UART socket + Python test script

### Phase 3: MQTT Task
- mqtt_task.c - WiFi, MQTT client
- publisher.c - state publishing
- discovery.c - HA discovery
- **Test**: Real WiFi + MQTT broker

### Phase 4: Command Handling
- commands.c - config command builder
- MQTT subscribe + command queue
- **Test**: MQTT commands → verify TX

### Phase 5: Production Hardening
- Reconnection with backoff
- Watchdog timer
- Comms lost detection

## Size Estimates

| Component | Lines | Complexity |
|-----------|-------|------------|
| constants.h | ~80 | Low |
| state.h | ~60 | Low |
| message.c | ~100 | Low |
| buffer.c | ~120 | Medium |
| parser.c | ~180 | Medium |
| commands.c | ~120 | Medium |
| serial_task.c | ~250 | Medium |
| mqtt_task.c | ~300 | Medium |
| discovery.c | ~400 | Medium |
| publisher.c | ~150 | Low |
| config.c | ~80 | Low |
| main.c | ~100 | Low |
| **Total** | **~1,940** | - |

## Hardware Requirements

- **MCU**: ESP32-C3 (400KB SRAM, 4MB Flash)
- **Memory Budget**: ~93KB RAM, ~265KB Flash
- **RS-485**: MAX485 module on GPIO17/18/19
- **Power**: 5V USB or external

## References

- Python source: `intellichem2mqtt/protocol/`
- ESP-IDF docs: https://docs.espressif.com/projects/esp-idf/en/v5.3/
- Pentair protocol: nodejs-poolController documentation
