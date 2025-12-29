# IntelliChem2MQTT ESP32 Firmware

A standalone ESP32-C3 firmware that bridges Pentair IntelliChem pool chemistry controllers to MQTT with Home Assistant auto-discovery.

## Features

- **Full IntelliChem Protocol Support**: Status polling and control commands
- **Home Assistant Auto-Discovery**: 35+ entities automatically appear
- **Bidirectional Control**: Adjust setpoints, enable/disable dosing
- **Comprehensive Logging**: Debug via serial monitor
- **Low Power**: ~0.5W typical consumption
- **Compact**: Fits in weatherproof junction box

## Hardware Requirements

### Minimum Components (~$20 total)

| Component | Model | Price | Notes |
|-----------|-------|-------|-------|
| ESP32 Board | ESP32-C3-DevKitM-1 | ~$5 | Or any ESP32-C3/S3 dev board |
| RS-485 Transceiver | MAX485 module | ~$2 | Converts UART to RS-485 |
| Power Supply | 5V USB adapter | ~$3 | Or power from pool equipment |
| Enclosure | IP65 junction box | ~$5 | Weatherproof for outdoor |
| Wire | 22AWG twisted pair | ~$5 | For RS-485 to IntelliChem |

### Recommended Boards

- **ESP32-C3-DevKitM-1** (~$5) - Smallest, cheapest, single-core RISC-V
- **ESP32-S3-DevKitC-1** (~$10) - Dual-core, more GPIO, USB-OTG
- **Seeed XIAO ESP32-C3** (~$5) - Ultra-compact form factor

---

## Wiring Diagram

```
                              ┌─────────────────┐
                              │   ESP32-C3      │
                              │   DevKitM-1     │
                              │                 │
    ┌───────────────┐         │  GPIO4 (TX)─────┼──────┐
    │               │         │  GPIO5 (RX)─────┼────┐ │
    │  IntelliChem  │         │  GPIO6 (DE)─────┼──┐ │ │
    │               │         │                 │  │ │ │
    │   RS-485      │         │  3.3V───────────┼──│─│─│──┐
    │   A (+)  ─────┼─────────│  GND────────────┼──│─│─│──│──┐
    │   B (-)  ─────┼─────────│                 │  │ │ │  │  │
    │               │         └─────────────────┘  │ │ │  │  │
    │   (Panel      │                              │ │ │  │  │
    │    inside)    │                              │ │ │  │  │
    └───────────────┘                              │ │ │  │  │
                                                   │ │ │  │  │
                              ┌────────────────────│─│─│──│──│─┐
                              │    MAX485 Module   │ │ │  │  │ │
                              │                    │ │ │  │  │ │
                              │  DE ───────────────┘ │ │  │  │ │
                              │  RE ─────────────────┘─│──│──│─│─── (tie to DE)
                              │  DI ───────────────────┘  │  │ │
                              │  RO ──────────────────────┘  │ │
                              │  VCC ────────────────────────┘ │
                              │  GND ──────────────────────────┘
                              │  A ────────────────────────────── RS485-A (+)
                              │  B ────────────────────────────── RS485-B (-)
                              └────────────────────────────────┘
```

### Wiring Notes

1. **RS-485 A/B Lines**: Connect to IntelliChem panel's RS-485 terminals (A=+, B=-)
2. **DE/RE Pins**: Tie together for half-duplex operation, or use separate GPIO
3. **Common Ground**: Ensure all grounds are connected
4. **Cable**: Use twisted pair for RS-485 (Cat5/Cat6 works well)
5. **Termination**: Add 120Ω resistor across A/B at cable ends if >30ft

### IntelliChem RS-485 Location

The RS-485 terminals are inside the IntelliChem controller panel:
- Open the controller enclosure
- Look for terminal block labeled "RS-485" or "A/B"
- May also be labeled "DATA+" and "DATA-"

---

## Installation

### Prerequisites

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install git wget flex bison gperf python3 python3-pip \
    python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
    dfu-util libusb-1.0-0
```

**macOS:**
```bash
brew install cmake ninja dfu-util python3
```

### 1. Install ESP-IDF

```bash
# Create directory and clone ESP-IDF v5.3
mkdir -p ~/esp && cd ~/esp
git clone --depth 1 --branch v5.3 --recursive https://github.com/espressif/esp-idf.git

# Install toolchain for ESP32-C3
cd esp-idf
./install.sh esp32c3

# Source the environment (required for each terminal session)
source export.sh
```

### 2. Clone This Repository

```bash
git clone https://github.com/yourusername/Intellichem2MQTT.git
cd Intellichem2MQTT/esp32/intellichem_esp32
```

### 3. Configure Settings

```bash
# Open configuration menu
idf.py menuconfig
```

Navigate to **IntelliChem2MQTT Configuration** and set:

| Setting | Description | Default |
|---------|-------------|---------|
| MQTT Broker URI | MQTT broker address | "mqtt://192.168.1.100" |
| MQTT Username | MQTT username (if required) | "" |
| MQTT Password | MQTT password (if required) | "" |
| IntelliChem Address | RS-485 address (usually 144) | 144 |
| Poll Interval | Status poll interval in seconds | 30 |
| Enable Control | Allow setpoint changes via MQTT | Enabled |

Press `S` to save, `Q` to quit.

**Note:** WiFi credentials are configured via web interface, not menuconfig. See [WiFi Setup](#wifi-setup) below.

### 4. Build Firmware

```bash
idf.py build
```

Build output shows firmware size:
```
intellichem_esp32.bin binary size 0xf7e60 bytes (1015 KB)
```

### 5. Flash to ESP32

Connect ESP32 via USB, then:

```bash
# Flash firmware
idf.py flash

# Or specify port explicitly
idf.py -p /dev/ttyUSB0 flash      # Linux
idf.py -p /dev/cu.usbserial* flash # macOS
```

### 6. Monitor Serial Output

```bash
idf.py monitor
```

Press `Ctrl+]` to exit monitor.

Or combine flash and monitor:
```bash
idf.py flash monitor
```

---

## WiFi Setup

WiFi credentials are configured via a captive portal, not compiled into the firmware. This allows easy network changes without reflashing.

### First-Time Setup

1. **Flash firmware** and power on the ESP32
2. **Connect to the setup network** on your phone/computer:
   - **Network Name:** `IntelliChem-Setup`
   - **No password required** (open network)
3. **Setup page opens automatically** (captive portal)
   - If it doesn't, go to: `http://192.168.4.1`
4. **Enter your WiFi credentials** in the form
5. Click **Connect**
6. Device saves credentials and connects to your network

The serial monitor shows connection progress and IP address when connected.

### Resetting WiFi Credentials

To clear saved WiFi and re-enter setup mode:

1. **Hold GPIO9** (BOOT button on most dev boards) during power-on
2. Release after "Reset button held - clearing WiFi credentials" appears
3. Device restarts in setup mode with `IntelliChem-Setup` network (open, no password)

### Settings (menuconfig)

Navigate to: **IntelliChem2MQTT Configuration → WiFi Provisioning**

| Setting | Default | Description |
|---------|---------|-------------|
| Reset GPIO | 9 | GPIO to hold for credential reset |

---

## Configuration via Serial

After flashing, configuration can be changed via `menuconfig`:

```bash
idf.py menuconfig
idf.py flash
```

### WiFi Settings

WiFi credentials are configured via web interface, not menuconfig.
See [WiFi Setup](#wifi-setup) for details.

### MQTT Settings

Navigate to: **IntelliChem2MQTT Configuration → MQTT Configuration**

| Setting | Example | Description |
|---------|---------|-------------|
| Broker URI | `mqtt://192.168.1.100:1883` | MQTT broker address |
| Username | `homeassistant` | Leave blank if no auth |
| Password | `secret` | Leave blank if no auth |
| Topic Prefix | `intellichem2mqtt` | Base topic for all messages |
| Discovery Prefix | `homeassistant` | HA discovery topic prefix |
| QoS | 1 | MQTT QoS level (0, 1, or 2) |

### IntelliChem Settings

Navigate to: **IntelliChem2MQTT Configuration → IntelliChem Configuration**

| Setting | Default | Description |
|---------|---------|-------------|
| Address | 144 (0x90) | IntelliChem RS-485 address |
| Poll Interval | 30 | Seconds between status requests |
| Timeout | 5000 | Response timeout in ms |
| Enable Control | Yes | Allow control commands |

### UART/RS-485 Settings

Navigate to: **IntelliChem2MQTT Configuration → UART Configuration**

| Setting | Default | Description |
|---------|---------|-------------|
| UART Port | 1 | UART peripheral (0, 1, or 2) |
| TX GPIO | 4 | GPIO for UART TX → MAX485 DI |
| RX GPIO | 5 | GPIO for UART RX ← MAX485 RO |
| DE GPIO | 6 | GPIO for RS-485 direction control |
| Baud Rate | 9600 | Must be 9600 for IntelliChem |

**ESP32-C3 GPIO Notes:**
- GPIO 0-10: Safe for general use
- GPIO 11-17: May be used by flash (avoid)
- GPIO 18-19: USB D-/D+ (do NOT use)
- GPIO 20-21: UART0 console

---

## Home Assistant Integration

Once connected, entities automatically appear in Home Assistant via MQTT discovery.

### Sensors (17 entities)

| Entity | Description |
|--------|-------------|
| `sensor.intellichem_ph` | Current pH level |
| `sensor.intellichem_orp` | Current ORP (mV) |
| `sensor.intellichem_temperature` | Water temperature |
| `sensor.intellichem_ph_setpoint` | pH target |
| `sensor.intellichem_orp_setpoint` | ORP target |
| `sensor.intellichem_lsi` | Langelier Saturation Index |
| `sensor.intellichem_salt` | Salt level (ppm) |
| `sensor.intellichem_calcium_hardness` | Calcium hardness (ppm) |
| `sensor.intellichem_cyanuric_acid` | CYA level (ppm) |
| `sensor.intellichem_alkalinity` | Total alkalinity (ppm) |
| `sensor.intellichem_ph_tank_level` | pH chemical tank level |
| `sensor.intellichem_orp_tank_level` | ORP chemical tank level |
| `sensor.intellichem_ph_dose_time` | Today's pH dosing time |
| `sensor.intellichem_orp_dose_time` | Today's ORP dosing time |
| `sensor.intellichem_firmware` | Controller firmware version |
| `sensor.intellichem_water_chemistry` | Water balance status |

### Binary Sensors (11 entities)

| Entity | Description |
|--------|-------------|
| `binary_sensor.intellichem_flow` | Flow sensor status |
| `binary_sensor.intellichem_ph_dosing` | pH pump active |
| `binary_sensor.intellichem_orp_dosing` | ORP pump active |
| `binary_sensor.intellichem_alarm_*` | Various alarm states |
| `binary_sensor.intellichem_warning_*` | Various warning states |

### Controls (7 entities)

| Entity | Type | Description |
|--------|------|-------------|
| `number.intellichem_ph_setpoint` | Number | Set pH target (7.0-7.6) |
| `number.intellichem_orp_setpoint` | Number | Set ORP target (400-800 mV) |
| `number.intellichem_calcium_hardness` | Number | Set calcium hardness |
| `number.intellichem_cyanuric_acid` | Number | Set CYA level |
| `number.intellichem_alkalinity` | Number | Set alkalinity |
| `switch.intellichem_ph_dosing_enabled` | Switch | Enable/disable pH dosing |
| `switch.intellichem_orp_dosing_enabled` | Switch | Enable/disable ORP dosing |

---

## MQTT Topics

### State Topics (Published)

```
intellichem2mqtt/intellichem/ph                    # Current pH
intellichem2mqtt/intellichem/orp                   # Current ORP (mV)
intellichem2mqtt/intellichem/temperature           # Water temp (°F)
intellichem2mqtt/intellichem/state                 # Full JSON state
intellichem2mqtt/intellichem/availability          # "online" or "offline"
```

### Command Topics (Subscribe)

```
intellichem2mqtt/intellichem/set/ph_setpoint       # Set pH target
intellichem2mqtt/intellichem/set/orp_setpoint      # Set ORP target
intellichem2mqtt/intellichem/set/ph_dosing_enabled # "ON" or "OFF"
intellichem2mqtt/intellichem/set/orp_dosing_enabled
intellichem2mqtt/intellichem/set/calcium_hardness
intellichem2mqtt/intellichem/set/cyanuric_acid
intellichem2mqtt/intellichem/set/alkalinity
```

---

## Troubleshooting

### No Serial Output

1. Check USB connection
2. Try different USB cable (some are charge-only)
3. Verify correct port: `ls /dev/tty*` (Linux) or `ls /dev/cu.*` (macOS)

### WiFi Won't Connect

1. Reset credentials (hold GPIO9 at boot) and reconfigure via web interface
2. Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
3. Check router for MAC filtering
4. Try moving closer to router during setup
5. Check serial monitor for detailed error messages
6. Verify SSID and password are correct (case-sensitive)

### No IntelliChem Data

1. Verify wiring (A/B not swapped)
2. Check IntelliChem address (default 144)
3. Monitor serial output for packet logs
4. Try different DE GPIO timing

### MQTT Connection Failed

1. Verify broker address and port
2. Check username/password
3. Ensure broker allows anonymous connections or has correct credentials

### Serial Monitor Shows Garbled Text

1. Baud rate mismatch - should be 115200 for monitor
2. Try: `idf.py monitor -b 115200`

---

## Building for Development

### Clean Build

```bash
idf.py fullclean
idf.py build
```

### Size Analysis

```bash
idf.py size           # Summary
idf.py size-components # Per-component breakdown
```

### Debug Logging

Edit `sdkconfig` or use menuconfig to increase log levels:
```
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
```

---

## Updating Firmware

### Via USB

```bash
idf.py flash
```

### OTA Updates (Future)

OTA update support is planned for a future release.

---

## License

MIT License - See LICENSE file for details.

## Credits

- Protocol documentation derived from [nodejs-poolController](https://github.com/tagyoureit/nodejs-poolController)
- ESP-IDF framework by Espressif Systems
