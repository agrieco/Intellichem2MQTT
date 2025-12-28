# ESP32-C3 Development & Testing Setup

## Prerequisites Installed

| Component | Version | Location |
|-----------|---------|----------|
| ESP-IDF | v5.3 | `~/esp/esp-idf` |
| RISC-V Toolchain | esp-13.2.0 | `~/.espressif/tools/riscv32-esp-elf/` |
| QEMU (ESP32-C3) | esp_develop_8.2.0 | `~/.espressif/tools/qemu-riscv32/` |
| CMake | 3.28.3 | System |
| Ninja | 1.11.1 | System |

---

## Quick Start

### 1. Source the ESP-IDF Environment

**Every new terminal session** requires sourcing the ESP-IDF environment:

```bash
source ~/esp/esp-idf/export.sh
```

Or add to your shell profile for convenience:
```bash
echo 'alias get_idf="source ~/esp/esp-idf/export.sh"' >> ~/.bashrc
source ~/.bashrc
# Then just run: get_idf
```

### 2. Navigate to Project

```bash
cd /home/ubuntu/code/Intellichem2MQTT/esp32/intellichem_esp32
```

---

## Build Commands

### Full Build
```bash
idf.py build
```

### Clean Build
```bash
idf.py fullclean
idf.py build
```

### Reconfigure (menuconfig)
```bash
idf.py menuconfig
```

### Set Target (if needed)
```bash
idf.py set-target esp32c3
```

---

## Testing with QEMU Emulator

### Method 1: Interactive Monitor (requires TTY)

```bash
idf.py qemu monitor
```

- Press `Ctrl+]` to exit monitor
- Press `Ctrl+A` then `X` to exit QEMU

### Method 2: Direct QEMU Execution (headless/scripted)

```bash
# Build first
idf.py build

# Run QEMU directly with timeout
timeout 10 qemu-system-riscv32 \
    -M esp32c3 \
    -drive file=build/flash_image.bin,if=mtd,format=raw \
    -drive file=build/qemu_efuse.bin,if=none,format=raw,id=efuse \
    -global driver=nvram.esp32c3.efuse,property=drive,value=efuse \
    -global driver=timer.esp32c3.timg,property=wdt_disable,value=true \
    -nographic
```

### Method 3: QEMU with UART Socket (for automated testing)

```bash
# Start QEMU with UART on TCP port 5555
qemu-system-riscv32 \
    -M esp32c3 \
    -drive file=build/flash_image.bin,if=mtd,format=raw \
    -drive file=build/qemu_efuse.bin,if=none,format=raw,id=efuse \
    -global driver=nvram.esp32c3.efuse,property=drive,value=efuse \
    -global driver=timer.esp32c3.timg,property=wdt_disable,value=true \
    -nographic \
    -serial tcp::5555,server,nowait &

# Connect to UART output
nc localhost 5555
```

### Method 4: GDB Debugging

```bash
# Start QEMU with GDB server
qemu-system-riscv32 \
    -M esp32c3 \
    -drive file=build/flash_image.bin,if=mtd,format=raw \
    -drive file=build/qemu_efuse.bin,if=none,format=raw,id=efuse \
    -global driver=nvram.esp32c3.efuse,property=drive,value=efuse \
    -global driver=timer.esp32c3.timg,property=wdt_disable,value=true \
    -nographic \
    -s -S &

# In another terminal, connect GDB
riscv32-esp-elf-gdb build/intellichem_esp32.elf \
    -ex "target remote :1234" \
    -ex "continue"
```

---

## Flashing to Real Hardware

### Connect ESP32-C3 via USB

The ESP32-C3 has built-in USB-to-UART, so just connect via USB-C.

### Flash and Monitor

```bash
idf.py flash monitor
```

### Flash Only
```bash
idf.py flash
```

### Monitor Only
```bash
idf.py monitor
```

### Specify Port (if multiple devices)
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## UART Testing for IntelliChem Protocol

### Simulating RS-485 Input in QEMU

QEMU exposes UART on TCP sockets. You can send test packets:

```python
#!/usr/bin/env python3
"""Send test IntelliChem packets to QEMU UART"""
import socket
import time

# IntelliChem status response packet (Action 18, 41-byte payload)
# Preamble + Header + Payload + Checksum
TEST_PACKET = bytes([
    # Preamble
    0xFF, 0x00, 0xFF,
    # Header: start, sub, dest, src, action, length
    0xA5, 0x00, 0x10, 0x90, 0x12, 0x29,  # Action 18 (0x12), 41 bytes (0x29)
    # Payload (41 bytes) - sample IntelliChem status
    0x02, 0xD4,  # pH = 724 (7.24)
    0x02, 0xBC,  # ORP = 700
    0x02, 0xD0,  # pH setpoint = 720 (7.20)
    0x02, 0x8A,  # ORP setpoint = 650
    0x00, 0x00,  # Reserved
    0x00, 0x3C,  # pH dose time = 60 seconds
    0x00, 0x00,  # Reserved
    0x00, 0x00,  # Reserved
    0x00, 0x1E,  # ORP dose time = 30 seconds
    0x00, 0x64,  # pH dose volume = 100 mL
    0x00, 0x32,  # ORP dose volume = 50 mL
    0x05,        # pH tank level = 5
    0x04,        # ORP tank level = 4
    0x00,        # LSI = 0.00
    0x01, 0x2C,  # Calcium hardness = 300
    0x00,        # Reserved
    0x32,        # Cyanuric acid = 50
    0x00, 0x50,  # Alkalinity = 80
    0x3C,        # Salt level = 3000 (60 * 50)
    0x00,        # Reserved
    0x52,        # Temperature = 82°F
    0x00,        # Alarms = none
    0x00,        # Warnings = none
    0x00,        # Dosing status
    0x01,        # Status flags
    0x01, 0x00,  # Firmware version 1.0
    0x00,        # Water chemistry = OK
    0x00, 0x00,  # Reserved
    # Checksum (calculated)
    0x00, 0x00   # Placeholder - calculate actual checksum
])

def calculate_checksum(data):
    """Calculate Pentair checksum (sum of header + payload)"""
    # Skip preamble (3 bytes), sum rest except last 2 bytes
    return sum(data[3:-2]) & 0xFFFF

def send_packet(host='localhost', port=5555):
    """Send test packet to QEMU UART socket"""
    packet = bytearray(TEST_PACKET)
    checksum = calculate_checksum(packet)
    packet[-2] = (checksum >> 8) & 0xFF
    packet[-1] = checksum & 0xFF

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        s.send(packet)
        print(f"Sent {len(packet)} bytes")
        time.sleep(0.1)
        response = s.recv(256)
        print(f"Received: {response.hex()}")

if __name__ == "__main__":
    send_packet()
```

Save as `test_protocol.py` and run:
```bash
python3 test_protocol.py
```

---

## Project Structure

```
Intellichem2MQTT/
├── esp32/
│   └── intellichem_esp32/           # ESP-IDF project root
│       ├── CMakeLists.txt           # Project CMake config
│       ├── sdkconfig                # ESP32-C3 configuration
│       ├── main/
│       │   ├── CMakeLists.txt       # Component CMake
│       │   └── intellichem_esp32.c  # Main source file
│       └── build/                   # Build artifacts
│           ├── flash_image.bin      # Combined firmware for QEMU
│           ├── qemu_efuse.bin       # eFuse image for QEMU
│           └── intellichem_esp32.elf # Debug symbols
├── docs/esp32/
│   ├── ESP32_PORT_ANALYSIS.md       # Feasibility analysis
│   └── DEVELOPMENT_SETUP.md         # This file
└── intellichem2mqtt/                # Original Python implementation
    └── protocol/                    # Reference for porting
        ├── constants.py
        ├── message.py
        ├── inbound.py
        └── commands.py
```

---

## Common Issues & Solutions

### "cmake not found"
```bash
sudo apt-get install cmake ninja-build
```

### "python not found"
ESP-IDF uses `python3`. The export script sets up aliases.

### QEMU "monitor requires TTY"
Use direct QEMU command instead of `idf.py qemu monitor` when running headless.

### Build fails with "No space left on device"
```bash
idf.py fullclean
```

### Wrong chip target
```bash
idf.py set-target esp32c3
idf.py fullclean
idf.py build
```

---

## Useful Commands Reference

| Command | Description |
|---------|-------------|
| `idf.py build` | Compile firmware |
| `idf.py fullclean` | Clean all build artifacts |
| `idf.py menuconfig` | Configure project settings |
| `idf.py flash` | Flash to hardware |
| `idf.py monitor` | Serial monitor |
| `idf.py flash monitor` | Flash and monitor |
| `idf.py qemu monitor` | Run in QEMU emulator |
| `idf.py size` | Show firmware size breakdown |
| `idf.py size-components` | Size by component |

---

## Memory Analysis

```bash
# Show memory usage summary
idf.py size

# Show per-component breakdown
idf.py size-components

# Show per-file breakdown
idf.py size-files
```

---

## Environment Variables

| Variable | Description |
|----------|-------------|
| `IDF_PATH` | Path to ESP-IDF (`~/esp/esp-idf`) |
| `IDF_TARGET` | Target chip (`esp32c3`) |
| `ESPPORT` | Serial port for flashing |

---

## Re-installing ESP-IDF (if needed)

```bash
# Remove old installation
rm -rf ~/esp/esp-idf ~/.espressif

# Clone fresh
mkdir -p ~/esp && cd ~/esp
git clone --depth 1 --branch v5.3 --recursive https://github.com/espressif/esp-idf.git

# Install toolchain for ESP32-C3
cd esp-idf
./install.sh esp32c3

# Install QEMU
python3 tools/idf_tools.py install qemu-riscv32

# Source environment
source export.sh
```
