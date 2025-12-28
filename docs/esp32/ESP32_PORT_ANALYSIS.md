# ESP32 Portability Analysis: Intellichem2MQTT

## Executive Summary

**Verdict: YES, ESP32 porting is feasible** - requires rewrite from Python to C/C++ (ESP-IDF).

### Selected Configuration
- **Language**: C/C++ with ESP-IDF (smallest footprint)
- **Features**: Full control (setpoints, dosing enable/disable)
- **HA Integration**: Auto-discovery enabled

| Aspect | Current (Python) | ESP32 Target | Feasibility |
|--------|------------------|--------------|-------------|
| Language | Python 3.9+ | **C/C++ ESP-IDF** | Major rewrite |
| Memory | 30-60MB | **~93KB RAM** | Excellent margin |
| Protocol | 41/21-byte packets | Same | Direct port |
| Serial | pyserial-asyncio | ESP-IDF UART | Straightforward |
| MQTT | aiomqtt | ESP-IDF MQTT | Straightforward |
| Concurrency | asyncio | FreeRTOS tasks | Architectural change |

---

## Current Codebase Profile

```
Total Code:        3,185 lines Python
Dependencies:      4 (pyserial-asyncio, aiomqtt, pydantic, pyyaml)
Serial Protocol:   RS-485 @ 9600 baud, 8N1
Packet Size:       11-52 bytes (status response = 41-byte payload)
Poll Interval:     30-300 seconds (configurable)
Memory Usage:      ~4KB buffer + ~3KB state
CPU Usage:         <1% duty cycle
```

---

## ESP32 Device Recommendations

### Tier 1: Best Choice

**ESP32-S3-WROOM-1 (8MB PSRAM)**
- **RAM**: 512KB SRAM + 8MB PSRAM
- **Flash**: 8-16MB
- **CPU**: Dual-core Xtensa @ 240MHz
- **WiFi**: 802.11 b/g/n
- **Price**: ~$6-10
- **Why**: Plenty of RAM for MicroPython runtime, PSRAM allows comfortable memory margins

**Dev Boards**:
- ESP32-S3-DevKitC-1 (~$10)
- Adafruit QT Py ESP32-S3 (~$13)
- Unexpected Maker FeatherS3 (~$25)

### Tier 2: Capable

**ESP32-WROOM-32E**
- **RAM**: 520KB SRAM (no PSRAM)
- **Flash**: 4-16MB
- **CPU**: Dual-core Xtensa @ 240MHz
- **Price**: ~$3-5
- **Why**: Sufficient for C/C++ implementation, tight for MicroPython

**Dev Boards**:
- ESP32-DevKitC V4 (~$6)
- Adafruit HUZZAH32 (~$20)

### Tier 3: Budget/Compact (Recommended for C/C++)

**ESP32-C3-MINI-1**
- **RAM**: 400KB SRAM
- **Flash**: 4MB
- **CPU**: Single-core RISC-V @ 160MHz
- **Price**: ~$2-3
- **Why**: Minimal cost, single-core sufficient for polling application

**Dev Boards**:
- ESP32-C3-DevKitM-1 (~$5)
- Seeed XIAO ESP32-C3 (~$5)

---

## RS-485 Interface Options

The ESP32 needs an RS-485 transceiver for the IntelliChem connection:

### Option A: MAX485 Module (~$1-2)
```
ESP32 GPIO TX → DI (Data In)
ESP32 GPIO RX ← RO (Data Out)
ESP32 GPIO DE → DE/RE (Direction control, or tie to VCC for TX-only)
```

### Option B: Integrated RS-485 HAT
- Waveshare RS485 Board for ESP32 (~$8)
- Pre-wired, includes terminal blocks

### Option C: Auto-Direction RS-485
- MAX13487E - Automatic direction control
- Simplifies wiring (no DE/RE GPIO needed)

---

## Porting Strategy: ESP-IDF C/C++ (Selected)

**Pros**:
- Minimal memory footprint
- Works on any ESP32 variant
- Production-ready, OTA updates supported

**Cons**:
- Complete rewrite
- Longer development time
- More complex debugging

**Estimated Effort**: 80-120 hours

**Architecture**:
```c
// FreeRTOS task structure
void serial_task(void *pvParams);    // Handle RS-485 communication
void mqtt_task(void *pvParams);      // MQTT publish/subscribe
void main_task(void *pvParams);      // Orchestration and polling

// ESP-IDF components
uart_driver_install()                // Serial port
esp_mqtt_client_init()               // MQTT client
nvs_flash_init()                     // Configuration storage
```

---

## Memory Budget (ESP32-WROOM-32E, C/C++)

```
Component                    RAM (KB)    Flash (KB)
─────────────────────────────────────────────────────
FreeRTOS kernel              ~20         ~30
WiFi stack                   ~50         ~150
MQTT client                  ~5          ~20
Serial buffer (4KB)          4           -
State structures             1           -
Protocol parsing             2           ~10
Config (NVS)                 1           ~5
Application logic            ~10         ~50
─────────────────────────────────────────────────────
TOTAL                        ~93KB       ~265KB
Available                    520KB       4MB
Margin                       427KB       3.7MB
```

**Verdict**: Plenty of headroom on standard ESP32

---

## Protocol Porting Complexity

### Easy to Port (Direct Translation)

1. **Packet Structure** - Simple byte arrays
   ```c
   uint8_t preamble[] = {0xFF, 0x00, 0xFF};
   uint8_t header[] = {0xA5, 0x00, dest, src, action, len};
   ```

2. **Checksum Calculation**
   ```c
   uint16_t checksum = 0;
   for (int i = 0; i < len; i++) checksum += data[i];
   ```

3. **Big-Endian Extraction**
   ```c
   uint16_t be16(uint8_t *buf, int offset) {
       return (buf[offset] << 8) | buf[offset + 1];
   }
   ```

### Moderate Complexity

4. **Bitfield Parsing** - Requires careful bit masking
5. **State Management** - Replace Pydantic with C structs
6. **MQTT Discovery** - JSON generation for Home Assistant

### Higher Complexity

7. **Async I/O Model** - Replace asyncio with FreeRTOS tasks/queues
8. **Reconnection Logic** - Implement retry with backoff
9. **Configuration Management** - NVS instead of YAML/env vars

---

## Recommended ESP32 Hardware Setup (Optimized for Size)

### Smallest Viable Hardware

**ESP32-C3-MINI-1** - The absolute minimum for this project:
- **RAM**: 400KB SRAM (need ~93KB)
- **Flash**: 4MB (need ~265KB)
- **CPU**: Single-core RISC-V @ 160MHz (plenty for 30s polling)
- **Price**: ~$2-3 for module, ~$5 for dev board
- **Size**: 13.2 x 16.6mm module

### Bill of Materials (Budget Build)

| Component | Model | Price | Notes |
|-----------|-------|-------|-------|
| MCU Board | ESP32-C3-DevKitM-1 | ~$5 | Smallest/cheapest option |
| RS-485 | MAX485 module | ~$2 | Or MAX13487E for auto-direction |
| Power | 5V 1A USB adapter | ~$3 | Or use pool equipment power |
| Enclosure | IP65 junction box | ~$5 | Weatherproof for outdoor |
| Wiring | 22AWG twisted pair | ~$5 | For RS-485 connection |

**Total**: ~$20

### Bill of Materials (Comfortable Build)

| Component | Model | Price | Notes |
|-----------|-------|-------|-------|
| MCU Board | ESP32-WROOM-32E DevKit | ~$6 | More GPIO, dual-core |
| RS-485 | MAX485 module | ~$2 | Or MAX13487E for auto-direction |
| Power | 5V 1A USB adapter | ~$3 | Or use pool equipment power |
| Enclosure | IP65 junction box | ~$5 | Weatherproof for outdoor |
| Wiring | 22AWG twisted pair | ~$5 | For RS-485 connection |

**Total**: ~$21

### Wiring Diagram

```
                    ESP32
                    ┌───────────┐
IntelliChem         │           │
RS-485 Bus    ───A──┤           │
              ───B──┤           │
                    │GPIO17 TX──┼──────┬──────────────┐
                    │GPIO18 RX──┼──────│──────────────│─┐
                    │GPIO19 DE──┼──────│──────────┐   │ │
                    │           │      │          │   │ │
                    │3.3V───────┼──────│──────┐   │   │ │
                    │GND────────┼──────│────┐ │   │   │ │
                    └───────────┘      │    │ │   │   │ │
                                       │    │ │   │   │ │
                                  MAX485    │ │   │   │ │
                                 ┌───────┐  │ │   │   │ │
                                 │DE ────┼──│─│───┘   │ │
                                 │RE ────┼──┘ │       │ │
                                 │DI ────┼────│───────┘ │
                                 │RO ────┼────│─────────┘
                                 │VCC────┼────┘
                                 │GND────┼─────(common ground)
                                 │A ─────┼─────RS485-A (to IntelliChem)
                                 │B ─────┼─────RS485-B (to IntelliChem)
                                 └───────┘
```

---

## Implementation Phases

### Phase 1: Core Protocol (Week 1-2)
- [ ] Port packet structure and checksum
- [ ] Implement status request (Action 210)
- [ ] Implement status response parsing (Action 18)
- [ ] Test with serial monitor

### Phase 2: Serial Communication (Week 2-3)
- [ ] Configure UART for 9600 8N1
- [ ] Implement packet buffering
- [ ] Add timeout handling
- [ ] Validate against real IntelliChem

### Phase 3: MQTT Integration (Week 3-4)
- [ ] Basic MQTT connection
- [ ] State publishing
- [ ] Home Assistant discovery
- [ ] LWT (availability) support

### Phase 4: Control Features (Week 4-5)
- [ ] Command reception
- [ ] Configuration command building (Action 146)
- [ ] Rate limiting
- [ ] Validation

### Phase 5: Production Hardening (Week 5-6)
- [ ] OTA updates
- [ ] Watchdog timer
- [ ] WiFi reconnection
- [ ] Configuration via web portal or MQTT

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Protocol timing issues | Medium | High | Use hardware timers, test extensively |
| WiFi interference | Low | Medium | Use external antenna, test range |
| Memory exhaustion | Low | High | Use C/C++, profile memory usage |
| RS-485 noise | Medium | Medium | Use shielded cable, proper termination |
| IntelliChem firmware variations | Unknown | Medium | Test with multiple units if possible |

---

## Conclusion

**ESP32 porting is definitely feasible** and optimal for:
- Lower power consumption vs Raspberry Pi (~0.5W vs ~3W)
- Smaller form factor (fits in waterproof junction box)
- Lower cost (~$20 vs ~$50+)
- Direct RS-485 integration (no USB adapter needed)
- Dedicated single-purpose device
- OTA firmware updates via ESP-IDF

**Recommended Hardware**: ESP32-C3-DevKitM-1 (~$5) for smallest/cheapest, or ESP32-WROOM-32E (~$6) for dual-core headroom.

**Development Effort**: 80-120 hours for full C/C++ implementation with:
- Status polling and parsing
- Full control commands (Action 146)
- Home Assistant MQTT auto-discovery
- WiFi reconnection and OTA updates

The IntelliChem protocol is simple enough that the main challenge is architectural (async Python → FreeRTOS tasks), not algorithmic. The protocol files (`protocol/inbound.py`, `protocol/commands.py`) translate directly to C structs and byte manipulation.

---

## Reference: Python Source Files for Porting

| Python File | Purpose | ESP32 Equivalent |
|-------------|---------|------------------|
| `protocol/constants.py` | Protocol constants | C header file |
| `protocol/message.py` | Packet structure | C struct + functions |
| `protocol/inbound.py` | Status parsing (41 bytes) | C parsing function |
| `protocol/commands.py` | Command building (21 bytes) | C command builder |
| `serial/buffer.py` | Packet reassembly | C ring buffer |
| `serial/connection.py` | UART management | ESP-IDF UART driver |
| `mqtt/client.py` | MQTT wrapper | ESP-IDF MQTT client |
| `mqtt/discovery.py` | HA discovery | JSON generation |
| `mqtt/publisher.py` | State publishing | MQTT publish calls |
| `mqtt/command_handler.py` | Command reception | MQTT subscribe + handlers |
| `models/intellichem.py` | State structures | C structs |
| `config.py` | Configuration | NVS + Kconfig |
