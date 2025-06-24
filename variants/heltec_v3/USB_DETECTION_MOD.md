# Heltec V3 USB Power Detection Modification

This document describes how to add hardware USB power detection to the Heltec V3 board.

## Problem
The standard Heltec V3 firmware relies on voltage-based USB detection, which uses an unrealistic 4.2V threshold. This causes USB power to never be detected properly.

## Solution
Add a hardware voltage divider to connect USB power (VUSB) to GPIO19, enabling reliable USB detection.

## Hardware Modification Required

### Components Needed:
- 1x 10kΩ resistor
- 2x 3.3kΩ resistors (or 1x 6.6kΩ resistor)

### Wiring:
```
VUSB (5V) ──[10kΩ]──●──[3.3kΩ]──[3.3kΩ]── GND
                     │
                   GPIO19
                  (1.99V when USB connected)
```

### Steps:
1. Locate VUSB on the board (near USB connector or power management section)
2. Solder 10kΩ resistor from VUSB to GPIO19 pin (leftmost pin on bottom header)
3. Solder two 3.3kΩ resistors in series from GPIO19 pin to GND
4. Test with multimeter: GPIO19 should read ~2V when USB connected, 0V when disconnected

## Software Usage

### Build Commands:
- **Standard build (no USB detection)**: `pio run -e heltec-v3`
- **USB detection build**: `pio run -e heltec-v3-usb-detect`

### Flash Command:
```bash
pio run -e heltec-v3-usb-detect -t upload
```

## How It Works
- When USB is connected: GPIO19 reads 1.99V (HIGH)
- When USB is disconnected: GPIO19 reads 0V (LOW)
- The existing power management code automatically detects this and sets USB power status correctly
- Power state machine and power profiles will now respond properly to USB connect/disconnect events

## Benefits
- ✅ Instant USB power detection (no more 20-second delays)
- ✅ Proper power state management
- ✅ Correct power profile switching
- ✅ Accurate power status in UI and telemetry
- ✅ No impact on standard Heltec V3 builds

## Voltage Calculations
- Input: 5V (VUSB)
- Voltage divider: 10kΩ / (10kΩ + 6.6kΩ) = 0.398
- Output: 5V × 0.398 = 1.99V
- Safe margin: 1.99V is well above 1.5V HIGH threshold and below 3.3V maximum
