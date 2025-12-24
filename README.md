# Intellichem2MQTT

A Python application that reads data from Pentair IntelliChem pool chemistry controllers via RS-485 and publishes to MQTT with Home Assistant auto-discovery support.

## Features

- **RS-485 Communication**: Direct communication with IntelliChem via USB-to-RS485 adapter
- **Home Assistant Integration**: MQTT Discovery for automatic entity creation
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
- **Async Architecture**: Non-blocking I/O for reliable operation
- **Systemd Service**: Run as a background service on Raspberry Pi

## Requirements

- Python 3.9+
- USB-to-RS485 adapter connected to IntelliChem
- MQTT broker (e.g., Home Assistant's built-in broker)
- Raspberry Pi or Linux system

## Installation

### 1. Clone the Repository

```bash
cd /opt
sudo git clone https://github.com/yourusername/intellichem2mqtt.git
sudo chown -R $USER:$USER intellichem2mqtt
cd intellichem2mqtt
```

### 2. Create Virtual Environment

```bash
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

### 3. Configure

```bash
# Create config directory
sudo mkdir -p /etc/intellichem2mqtt

# Copy and edit configuration
sudo cp config/config.example.yaml /etc/intellichem2mqtt/config.yaml
sudo nano /etc/intellichem2mqtt/config.yaml
```

Edit the configuration file to match your setup:
- Set your serial port (usually `/dev/ttyUSB0`)
- Set your MQTT broker hostname/IP
- Set MQTT credentials if required

### 4. Add User to dialout Group

```bash
sudo usermod -a -G dialout $USER
# Log out and back in for changes to take effect
```

### 5. Test the Application

```bash
# Activate virtual environment
source /opt/intellichem2mqtt/venv/bin/activate

# Run the application
python -m intellichem2mqtt -c /etc/intellichem2mqtt/config.yaml
```

### 6. Install as a Service

```bash
# Copy service file
sudo cp systemd/intellichem2mqtt.service /etc/systemd/system/

# Edit service file if needed (change user, paths)
sudo nano /etc/systemd/system/intellichem2mqtt.service

# Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable intellichem2mqtt
sudo systemctl start intellichem2mqtt

# Check status
sudo systemctl status intellichem2mqtt
```

## Configuration

### Serial Port

The IntelliChem communicates via RS-485 at 9600 baud. Common serial port paths:

- USB adapter: `/dev/ttyUSB0`
- Raspberry Pi GPIO: `/dev/ttyAMA0` or `/dev/serial0`

### IntelliChem Address

Most installations have a single IntelliChem at address 144. If you have multiple units, they use addresses 144-158.

### MQTT Topics

State topics are published under the configured prefix:

```
intellichem2mqtt/intellichem/status          # Full JSON state
intellichem2mqtt/intellichem/ph/level
intellichem2mqtt/intellichem/ph/setpoint
intellichem2mqtt/intellichem/orp/level
intellichem2mqtt/intellichem/orp/setpoint
intellichem2mqtt/intellichem/temperature
...
```

## Home Assistant

Entities are automatically discovered via MQTT. After starting the service, you should see a new "IntelliChem" device in Home Assistant with sensors for all monitored values.

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

## Troubleshooting

### Check Logs

```bash
# View service logs
sudo journalctl -u intellichem2mqtt -f

# Enable debug logging
# Edit config.yaml and set logging.level to DEBUG
```

### Serial Port Issues

```bash
# Check if port exists
ls -la /dev/ttyUSB*

# Check permissions
groups $USER  # Should include 'dialout'

# Test serial port
python -c "import serial; s = serial.Serial('/dev/ttyUSB0', 9600); print('OK')"
```

### MQTT Issues

```bash
# Test MQTT connection
mosquitto_sub -h homeassistant.local -t "intellichem2mqtt/#" -v

# Check broker connectivity
mosquitto_pub -h homeassistant.local -t "test" -m "hello"
```

## Hardware Setup

### Wiring

Connect your USB-to-RS485 adapter to the IntelliChem:

1. **A+ (Data+)**: Connect to IntelliChem RS-485 A+
2. **B- (Data-)**: Connect to IntelliChem RS-485 B-
3. **GND**: Connect to IntelliChem Ground (recommended)

The RS-485 connection is typically found on the IntelliChem's communication port, shared with the pool controller connection.

### Recommended Adapters

- FTDI-based USB-to-RS485 adapters
- CH340-based adapters (ensure drivers are installed)

## Protocol Information

This application implements the Pentair IntelliChem RS-485 protocol:

- **Baud Rate**: 9600
- **Data Format**: 8N1 (8 data bits, no parity, 1 stop bit)
- **Packet Format**: Preamble + Header + Payload + Checksum
- **Status Request**: Action 210
- **Status Response**: Action 18 (41-byte payload)

The protocol was reverse-engineered from the [nodejs-poolController](https://github.com/tagyoureit/nodejs-poolController) project.

## License

MIT License - see LICENSE file for details.

## Acknowledgments

- [nodejs-poolController](https://github.com/tagyoureit/nodejs-poolController) - Protocol documentation and reference implementation
- [Pentair](https://www.pentair.com/) - IntelliChem hardware
