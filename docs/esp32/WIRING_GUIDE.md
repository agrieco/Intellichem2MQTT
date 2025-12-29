# IntelliChem2MQTT ESP32 Wiring Guide

Complete hardware wiring instructions for connecting an ESP32-C3 to a Pentair IntelliChem controller via RS-485.

## Bill of Materials

| Component | Recommended Part | Qty | Cost | Notes |
|-----------|------------------|-----|------|-------|
| ESP32 Board | ESP32-C3-DevKitM-1 | 1 | ~$5 | Any ESP32-C3 or C6 works |
| RS-485 Transceiver | MAX485 module | 1 | ~$2 | 3.3V compatible |
| Power Supply | 5V 1A USB adapter | 1 | ~$3 | USB-C for DevKitM-1 |
| Wire | 22AWG twisted pair | 10ft | ~$3 | Cat5/Cat6 works well |
| Enclosure | IP65 junction box | 1 | ~$5 | Optional, for outdoor |
| Resistor | 120 ohm 1/4W | 1 | ~$0.10 | Only if cable >30ft |

**Total: ~$20**

---

## Pin Assignment Summary

| ESP32-C3 GPIO | Function | Connects To | Direction |
|---------------|----------|-------------|-----------|
| GPIO4 | UART TX | MAX485 DI | Output |
| GPIO5 | UART RX | MAX485 RO | Input |
| GPIO6 | RS-485 DE | MAX485 DE+RE | Output |
| GPIO9 | Reset Button | GND (momentary) | Input |
| 3.3V | Power | MAX485 VCC | - |
| GND | Ground | MAX485 GND | - |

---

## Wiring Diagram

```
                                          TO INTELLICHEM
                                          RS-485 TERMINALS
                                               │
                                          ┌────┴────┐
                                          │  A (+)  │
                                          │  B (-)  │
                                          └────┬────┘
                                               │
    ┌─────────────────────────────────────────┼───────────────────────┐
    │                                         │                       │
    │   ┌───────────────────┐         ┌──────┴──────┐                │
    │   │                   │         │             │                │
    │   │   ESP32-C3        │         │  MAX485     │                │
    │   │   DevKitM-1       │         │  Module     │                │
    │   │                   │         │             │                │
    │   │   GPIO4 (TX) ─────┼────────►│ DI          │                │
    │   │                   │         │             │                │
    │   │   GPIO5 (RX) ◄────┼─────────│ RO          │                │
    │   │                   │         │             │                │
    │   │   GPIO6 (DE) ─────┼────┬───►│ DE          │                │
    │   │                   │    │    │             │                │
    │   │                   │    └───►│ RE          │ (tie together) │
    │   │                   │         │             │                │
    │   │   3.3V ───────────┼────────►│ VCC         │                │
    │   │                   │         │             │                │
    │   │   GND ────────────┼────────►│ GND      A ─┼────────────────┼──► RS-485 A (+)
    │   │                   │         │          B ─┼────────────────┼──► RS-485 B (-)
    │   │   GPIO9 ──────────┼──┐      │             │                │
    │   │   (Reset Button)  │  │      └─────────────┘                │
    │   │                   │  │                                     │
    │   └───────────────────┘  │                                     │
    │                          │                                     │
    │                       [BTN]─── GND (momentary switch)          │
    │                                                                │
    └────────────────────────────────────────────────────────────────┘
```

---

## Step-by-Step Wiring

### Step 1: ESP32 to MAX485 Module

Connect the ESP32-C3 to the MAX485 transceiver module:

| ESP32-C3 Pin | MAX485 Pin | Wire Color (suggested) |
|--------------|------------|------------------------|
| 3.3V | VCC | Red |
| GND | GND | Black |
| GPIO4 | DI (Data In) | Yellow |
| GPIO5 | RO (Receiver Out) | Green |
| GPIO6 | DE (Driver Enable) | Orange |
| GPIO6 | RE (Receiver Enable) | Orange (same wire) |

**Important:** DE and RE pins on the MAX485 must be tied together. This allows the firmware to control transmit/receive mode with a single GPIO.

```
ESP32 GPIO6 ────┬──── MAX485 DE
                │
                └──── MAX485 RE
```

### Step 2: MAX485 to IntelliChem

Connect the MAX485 module to the IntelliChem controller's RS-485 terminals:

| MAX485 Pin | IntelliChem Terminal | Notes |
|------------|---------------------|-------|
| A | A+ / DATA+ | Differential positive |
| B | B- / DATA- | Differential negative |

**Wire Recommendations:**
- Use twisted pair wire (Cat5/Cat6 Ethernet cable works great)
- Keep cable length under 100ft if possible
- For cables >30ft, add 120 ohm termination resistor across A/B at both ends

### Step 3: Reset Button (Optional)

For easy WiFi credential reset, add a momentary button:

```
ESP32 GPIO9 ────[BTN]──── GND
```

Hold this button during power-on to clear WiFi credentials and enter setup mode.

**Note:** On most ESP32-C3 dev boards, the BOOT button is already connected to GPIO9.

---

## IntelliChem RS-485 Terminal Location

The RS-485 terminals are inside the IntelliChem controller:

1. **Open the enclosure** - Remove screws from the front panel
2. **Locate the terminal block** - Look for labels:
   - "RS-485"
   - "A/B"
   - "DATA+/DATA-"
   - "BUS"
3. **Connect wires:**
   - A (positive) to MAX485 A
   - B (negative) to MAX485 B

```
IntelliChem Internal View:

    ┌─────────────────────────────────┐
    │                                 │
    │   ┌───────────────────────┐     │
    │   │     Main Board        │     │
    │   │                       │     │
    │   │  [RS-485 Terminal]    │     │
    │   │    A+    B-    GND    │     │
    │   │    │     │      │     │     │
    │   └────┼─────┼──────┼─────┘     │
    │        │     │      │           │
    │        │     │      │           │
    └────────┼─────┼──────┼───────────┘
             │     │      │
             │     │      └── (optional ground)
             │     │
         To MAX485 A/B
```

---

## Power Options

### Option 1: USB Power (Recommended for Development)

Simply connect USB-C cable to the ESP32-C3 dev board.

- **Voltage:** 5V from USB
- **Current:** ~100mA typical, 300mA peak
- **Pros:** Easy, widely available
- **Cons:** Requires USB power adapter near installation

### Option 2: External 5V Power Supply

For permanent installations:

```
5V Power Supply ──►┬──► ESP32 5V/VIN pin
                   │
                   └──► GND to ESP32 GND
```

- Use a quality 5V 1A+ power supply
- Consider weatherproof/outdoor-rated supplies

### Option 3: Power from Pool Equipment (Advanced)

Some pool controllers provide 5V or 12V power. Check your equipment manual.

**If 12V available:**
- Use a DC-DC buck converter (12V to 5V)
- Or use ESP32 boards with onboard voltage regulator (supports up to 12V input)

---

## Common MAX485 Module Pinouts

Different MAX485 modules have different pinouts. Here are the most common:

### Type A: 8-Pin Module (Most Common)

```
┌─────────────────────┐
│  MAX485 Breakout    │
│                     │
│   VCC  ────  3.3V   │
│   GND  ────  GND    │
│   DE   ────  GPIO6  │
│   RE   ────  GPIO6  │ (tie to DE)
│   DI   ────  GPIO4  │
│   RO   ────  GPIO5  │
│   A    ────  RS485+ │
│   B    ────  RS485- │
│                     │
└─────────────────────┘
```

### Type B: 4-Pin Module (Auto Direction)

Some modules have automatic direction control:

```
┌─────────────────────┐
│  RS485-to-TTL       │
│                     │
│   VCC  ────  3.3V   │
│   GND  ────  GND    │
│   TXD  ────  GPIO5  │ (Note: TXD is OUTPUT from module)
│   RXD  ────  GPIO4  │ (Note: RXD is INPUT to module)
│   A    ────  RS485+ │
│   B    ────  RS485- │
│                     │
└─────────────────────┘
```

**Important:** Auto-direction modules don't need GPIO6 connected. Set `CONFIG_RS485_DE_PIN = -1` in configuration.

---

## Troubleshooting Wiring Issues

### No Communication with IntelliChem

1. **Check A/B polarity** - Most common issue! Try swapping A and B
2. **Verify RS-485 wiring** - Use multimeter to check continuity
3. **Check for loose connections** - Ensure all terminals are tight
4. **Test MAX485 module** - Try a known-good module

### Intermittent Communication

1. **Check cable quality** - Use twisted pair, not flat ribbon
2. **Add termination** - 120 ohm resistor across A/B if cable >30ft
3. **Check power** - Insufficient power causes random resets
4. **Check for interference** - Route cable away from high-voltage wires

### ESP32 Not Responding

1. **Check power LED** - Should be lit on dev board
2. **Try different USB cable** - Some cables are charge-only
3. **Check GPIO connections** - Verify no shorts to ground/3.3V

### Serial Monitor Shows Garbled Text

1. **Baud rate mismatch** - Serial monitor should be 115200
2. **Run:** `idf.py monitor -b 115200`

---

## Safety Considerations

1. **Power off equipment** before making any connections
2. **Use proper wire gauges** - 22-24 AWG for signal, 18-20 AWG for power
3. **Weatherproof outdoor connections** - Use IP65+ enclosure
4. **Ground properly** - Connect all grounds together
5. **Do not exceed voltage ratings** - MAX485 is 3.3V logic

---

## Reference Configurations

### Default Pin Configuration (from firmware)

```c
CONFIG_UART_PORT_NUM     = 1      // UART peripheral
CONFIG_UART_TX_PIN       = 4      // GPIO4
CONFIG_UART_RX_PIN       = 5      // GPIO5
CONFIG_UART_BAUD_RATE    = 9600   // IntelliChem protocol
CONFIG_RS485_DE_PIN      = 6      // GPIO6 (set -1 for auto-direction)
CONFIG_PROV_RESET_GPIO   = 9      // GPIO9 (BOOT button)
CONFIG_INTELLICHEM_ADDRESS = 144  // RS-485 address (0x90)
```

### ESP32-C3 Safe GPIO Reference

| GPIO Range | Safe to Use | Notes |
|------------|-------------|-------|
| 0-10 | Yes | General purpose |
| 11-17 | No | Used by flash |
| 18-19 | No | USB D-/D+ |
| 20-21 | Maybe | UART0 console |

---

## Quick Test Procedure

After wiring, verify the setup:

1. **Flash firmware:**
   ```bash
   cd esp32/intellichem_esp32
   idf.py flash monitor
   ```

2. **Check serial output for:**
   ```
   IntelliChem2MQTT ESP32
   ========================================
   UART port: 1 (TX=4, RX=5)
   RS-485 DE pin: 6
   ```

3. **Connect to WiFi setup:**
   - Connect phone/computer to `IntelliChem-Setup`
   - Configure WiFi and MQTT settings

4. **Verify IntelliChem communication:**
   ```
   Heartbeat: serial[polls=1 resp=1 err=0] ...
   ```

   - `resp > 0` = Communication working!
   - `resp = 0` = Check wiring/address

---

## Support

If you encounter issues:

1. Check the [Troubleshooting](#troubleshooting-wiring-issues) section
2. Review serial monitor output for specific error messages
3. Open an issue on GitHub with:
   - Your hardware configuration
   - Serial monitor output
   - Photo of wiring (if applicable)
