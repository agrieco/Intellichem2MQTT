# Intellichem2MQTT

[![Build and Publish Docker Image](https://github.com/agrieco/Intellichem2MQTT/actions/workflows/docker-publish.yml/badge.svg)](https://github.com/agrieco/Intellichem2MQTT/actions/workflows/docker-publish.yml)

> **🤖 VIBE CODED WITH CLAUDE CODE**
>
> This entire project was built through conversational AI programming with [Claude Code](https://claude.com/claude-code). From implementing the Pentair RS-485 protocol (thanks to [nodejs-poolController](https://github.com/tagyoureit/nodejs-poolController) for the protocol documentation!) to Docker deployment — all vibe coded. Use at your own risk, but it works great on my pool! 🏊

A Python application that reads data from Pentair IntelliChem pool chemistry controllers via RS-485 and publishes to MQTT with Home Assistant auto-discovery support.

## Features

- **RS-485 Communication**: Direct communication with IntelliChem via USB-to-RS485 adapter
- **Home Assistant Integration**: MQTT Discovery for automatic entity creation
- **Docker Support**: Pre-built multi-architecture images (amd64, arm64, arm/v7)
- **Environment Variable Config**: No config file needed for Docker deployments
- **Log-Only Mode**: Test without MQTT broker configured
- **Full Control Mode**: Adjust setpoints and enable/disable dosing from Home Assistant
- **Comprehensive Monitoring**:
  - pH level and setpoint
  - ORP level and setpoint
  - Tank levels (pH and ORP)
  - Dosing status and volumes
  - Water temperature
  - Langelier Saturation Index (LSI)
  - Calcium hardness, alkalinity, cyanuric acid
  - Salt level (if IntelliChlor present)
  - Alarms and warnings
  - Firmware version
- **Control Capabilities** (when enabled):
  - Adjust pH setpoint (7.0-7.6)
  - Adjust ORP setpoint (400-800 mV)
  - Set calcium hardness, alkalinity, cyanuric acid
  - Enable/disable pH and ORP dosing

## Quick Start (Docker)

The easiest way to run Intellichem2MQTT is with Docker.

### 1. Pull the Image

```bash
docker pull ghcr.io/agrieco/intellichem2mqtt:latest
```

### 2. Create docker-compose.yaml

```yaml
services:
  intellichem2mqtt:
    image: ghcr.io/agrieco/intellichem2mqtt:latest
    container_name: intellichem2mqtt
    restart: unless-stopped
    devices:
      - /dev/ttyUSB0:/dev/ttyUSB0
    environment:
      - SERIAL_PORT=/dev/ttyUSB0
      - MQTT_HOST=192.168.1.100        # Your MQTT broker IP
      - MQTT_PORT=1883
      - MQTT_USERNAME=your-username     # Optional
      - MQTT_PASSWORD=your-password     # Optional
      - INTELLICHEM_POLL_INTERVAL=60   # Poll every 60 seconds
      - LOG_LEVEL=INFO
      - TZ=America/New_York
```

### 3. Run

```bash
docker compose up -d
docker logs -f intellichem2mqtt
```

## Environment Variables

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `MQTT_HOST` | Yes* | - | MQTT broker hostname/IP |
| `MQTT_PORT` | No | 1883 | MQTT broker port |
| `MQTT_USERNAME` | No | - | MQTT username |
| `MQTT_PASSWORD` | No | - | MQTT password |
| `SERIAL_PORT` | No | /dev/ttyUSB0 | Serial device path |
| `INTELLICHEM_ADDRESS` | No | 144 | IntelliChem address (144-158) |
| `INTELLICHEM_POLL_INTERVAL` | No | 60 | Poll interval in seconds |
| `INTELLICHEM_TIMEOUT` | No | 5 | Response timeout in seconds |
| `INTELLICHEM_CONTROL_ENABLED` | No | false | Enable control features |
| `INTELLICHEM_CONTROL_RATE_LIMIT` | No | 5 | Min seconds between commands |
| `LOG_LEVEL` | No | INFO | DEBUG, INFO, WARNING, ERROR |

*If `MQTT_HOST` is not set, runs in **log-only mode** (useful for testing).

**Control Features**: Set `INTELLICHEM_CONTROL_ENABLED=true` to enable setpoint adjustment and dosing control. Control entities will appear in Home Assistant.

## Log Output

```
2025-12-24 08:06:45 - Starting poll loop (interval=60s)
2025-12-24 08:06:46 - pH=7.00 ORP=585.0mV T=77°F LSI=-0.64 flow=Y
```

## Manual Installation

### Requirements

- Python 3.9+
- USB-to-RS485 adapter connected to IntelliChem
- MQTT broker (e.g., Home Assistant's built-in broker)
- Raspberry Pi or Linux system

### Install from Source

```bash
# Clone repository
git clone https://github.com/agrieco/Intellichem2MQTT.git
cd Intellichem2MQTT

# Create virtual environment
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

# Create configuration
cp config/config.example.yaml config.yaml
nano config.yaml  # Edit with your settings

# Run
python -m intellichem2mqtt -c config.yaml
```

### Run as Systemd Service

```bash
# Copy and edit service file
sudo cp systemd/intellichem2mqtt.service /etc/systemd/system/
sudo nano /etc/systemd/system/intellichem2mqtt.service

# Enable and start
sudo systemctl daemon-reload
sudo systemctl enable intellichem2mqtt
sudo systemctl start intellichem2mqtt

# Check status
sudo systemctl status intellichem2mqtt
sudo journalctl -u intellichem2mqtt -f
```

## Home Assistant

Entities are automatically discovered via MQTT. After starting the service, you'll see an "IntelliChem" device in Home Assistant.

### Entities Created

**Sensors:**
- pH Level, pH Setpoint, pH Tank Level
- ORP Level, ORP Setpoint, ORP Tank Level
- Temperature, LSI
- Calcium Hardness, Alkalinity, Cyanuric Acid
- Salt Level, Firmware

**Binary Sensors:**
- Flow Detected
- Flow Alarm, pH Tank Empty, ORP Tank Empty
- Probe Fault, Communication Lost
- pH Lockout, pH/ORP Daily Limits
- pH Dosing, ORP Dosing

**Control Entities** (when `INTELLICHEM_CONTROL_ENABLED=true`):
- pH Setpoint Control (Number slider, 7.0-7.6)
- ORP Setpoint Control (Number slider, 400-800 mV)
- Calcium Hardness Setting (Number input)
- Cyanuric Acid Setting (Number input)
- Alkalinity Setting (Number input)
- pH Dosing Enable (Switch)
- ORP Dosing Enable (Switch)

### MQTT Topics

**State Topics (published by app):**
```
intellichem2mqtt/intellichem/ph/level
intellichem2mqtt/intellichem/ph/setpoint
intellichem2mqtt/intellichem/orp/level
intellichem2mqtt/intellichem/orp/setpoint
intellichem2mqtt/intellichem/temperature
intellichem2mqtt/intellichem/lsi
intellichem2mqtt/intellichem/availability
...
```

**Command Topics (when control enabled):**
```
intellichem2mqtt/intellichem/set/ph_setpoint      # Payload: float (7.0-7.6)
intellichem2mqtt/intellichem/set/orp_setpoint     # Payload: int (400-800)
intellichem2mqtt/intellichem/set/calcium_hardness # Payload: int (25-800)
intellichem2mqtt/intellichem/set/cyanuric_acid    # Payload: int (0-210)
intellichem2mqtt/intellichem/set/alkalinity       # Payload: int (25-800)
intellichem2mqtt/intellichem/set/ph_dosing        # Payload: ON/OFF
intellichem2mqtt/intellichem/set/orp_dosing       # Payload: ON/OFF
```

**Command Result:**
```
intellichem2mqtt/intellichem/command/status       # JSON with success/error
```

## Hardware Setup

### Wiring

Connect your USB-to-RS485 adapter to the IntelliChem:

| Adapter | IntelliChem |
|---------|-------------|
| A+ (Data+) | RS-485 A+ |
| B- (Data-) | RS-485 B- |
| GND | Ground (recommended) |

**Note:** If communication fails, try swapping A+ and B- wires.

### Recommended Adapters

- FTDI-based USB-to-RS485 adapters (most reliable)
- CH340-based adapters (ensure drivers are installed)

### IntelliChem Address

Most installations have a single IntelliChem at address **144**. Multiple units use addresses 144-158.

## Troubleshooting

### No Response from IntelliChem

1. **Check wiring**: Try swapping A+ and B- wires
2. **Verify power**: Ensure IntelliChem is powered on
3. **Check serial port**: `ls -la /dev/ttyUSB*`
4. **Test permissions**: User must be in `dialout` group

### Serial Port Issues

```bash
# Check port exists
ls -la /dev/ttyUSB*

# Check group membership
groups $USER  # Should include 'dialout'

# Add user to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in
```

### Docker Serial Port Access

```bash
# Find your serial device
ls -la /dev/ttyUSB*

# Run with correct device
docker run --device=/dev/ttyUSB0:/dev/ttyUSB0 ...
```

### View Logs

```bash
# Docker
docker logs intellichem2mqtt -f

# Systemd
sudo journalctl -u intellichem2mqtt -f
```

## Protocol Information

This application implements the Pentair IntelliChem RS-485 protocol:

- **Baud Rate**: 9600
- **Data Format**: 8N1 (8 data bits, no parity, 1 stop bit)
- **Packet Format**: Preamble + Header + Payload + Checksum
- **Status Request**: Action 210
- **Status Response**: Action 18 (41-byte payload)
- **Configuration Command**: Action 146 (21-byte payload) - for setpoint/dosing control

## Building Docker Image Locally

```bash
# Build for current architecture
docker build -t intellichem2mqtt .

# Build for Raspberry Pi (from x86)
docker buildx build --platform linux/arm/v7 -t intellichem2mqtt .
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Acknowledgments

- [nodejs-poolController](https://github.com/tagyoureit/nodejs-poolController) - Protocol documentation and reference implementation
- [Pentair](https://www.pentair.com/) - IntelliChem hardware
- [Claude Code](https://claude.com/claude-code) - The AI that wrote every line of this code

---

<p align="center">
  <b>🤖 100% Vibe Coded</b><br>
  <i>Built entirely through conversation with Claude Code.<br>
  No humans were mass-producing code in the making of this project.</i>
</p>
