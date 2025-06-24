# Button-Only BLE Wake Implementation

## Problem Analysis

**Current Issue:**
- Device in power saving mode wakes on packet → BLE starts → Phone reconnects → Data dump
- Creates poor UX with connection drops and data bursts
- Wastes power with unnecessary BLE cycling

**Root Cause:**
The PowerFSM transitions from LS → NB/DARK on packet wake, which automatically enables BLE via `setBluetoothEnable(powerProfileManager.bluetoothEnabled())`.

## Proposed Solution: Button-Only BLE Wake

### Core Concept
- **Packet wake**: Process packet, stay in light sleep (BLE off)
- **Button wake**: Enable BLE for user interaction
- **Configurable**: New power profile option for this behavior

### Implementation Strategy

#### 1. New Power Profile Option
Add `ble_wake_mode` to power profiles:
```cpp
enum BleWakeMode {
    BLE_WAKE_AUTO,      // Current behavior (wake on packet)
    BLE_WAKE_BUTTON     // Only wake BLE on button press
};
```

#### 2. Modified Power FSM Transitions

**Current Problematic Transitions:**
```cpp
// These cause BLE to start on packet wake
powerFSM.add_transition(&stateLS, wakeTarget, EVENT_WAKE_TIMER, NULL, "Wake timer");
powerFSM.add_transition(&stateLS, wakeTarget, EVENT_PACKET_FOR_PHONE, NULL, "Received packet");
```

**New Button-Only Transitions:**
```cpp
if (powerProfileManager.getBleWakeMode() == BLE_WAKE_BUTTON) {
    // Packet wake stays in LS (no BLE)
    powerFSM.add_transition(&stateLS, &stateLS, EVENT_WAKE_TIMER, NULL, "Wake timer (no BLE)");
    powerFSM.add_transition(&stateLS, &stateLS, EVENT_PACKET_FOR_PHONE, NULL, "Packet processed (no BLE)");
    
    // Only button enables BLE
    powerFSM.add_transition(&stateLS, &stateDARK, EVENT_PRESS, NULL, "Button wake (enable BLE)");
} else {
    // Current behavior (auto BLE wake)
    powerFSM.add_transition(&stateLS, wakeTarget, EVENT_WAKE_TIMER, NULL, "Wake timer");
    powerFSM.add_transition(&stateLS, wakeTarget, EVENT_PACKET_FOR_PHONE, NULL, "Received packet");
}
```

#### 3. New Light Sleep State: LS_NO_BLE

Create a specialized light sleep state that processes packets without enabling BLE:

```cpp
static void lsNoBleEnter()
{
    LOG_INFO("lsNoBleEnter - processing packets without BLE");
    screen->setOn(false);
    setBluetoothEnable(false);  // Ensure BLE stays off
    secsSlept = 0;
}

static void lsNoBleIdle()
{
    // Same as lsIdle() but never enables BLE
    // Process packets, handle mesh traffic, but keep BLE off
}

State stateLSNoBLE(lsNoBleEnter, lsNoBleIdle, lsExit, "LS_NO_BLE");
```

#### 4. BLE Timeout Management

When button enables BLE, add configurable timeout:

```cpp
// BLE active timeout (default 5 minutes)
powerFSM.add_timed_transition(&stateDARK, &stateLSNoBLE,
                              powerProfileManager.getBleTimeoutSecs() * 1000,
                              NULL, "BLE timeout - return to no-BLE sleep");
```

#### 5. Visual Feedback

Add LED/screen indication when BLE is active:
```cpp
static void darkEnter()
{
    if (powerProfileManager.getBleWakeMode() == BLE_WAKE_BUTTON) {
        // Visual indication that BLE is active
        ledBlink.set(true);  // Brief blink
        delay(100);
        ledBlink.set(false);
        
        // Optional: Show "BLE Active" on screen briefly
        if (screen) {
            screen->setOn(true);
            screen->print("BLE Active\n");
            delay(1000);
            screen->setOn(false);
        }
    }
    
    setBluetoothEnable(powerProfileManager.bluetoothEnabled());
    screen->setOn(false);
}
```

### Configuration Options

#### Power Profile Settings
```cpp
class PowerProfile {
    // Existing settings...
    
    BleWakeMode getBleWakeMode() const;
    uint32_t getBleTimeoutSecs() const;  // How long BLE stays active after button
    bool getBleVisualFeedback() const;   // Show LED/screen when BLE active
};
```

#### Default Values
- **Router/Repeater**: `BLE_WAKE_BUTTON` (maximize power savings)
- **Client**: `BLE_WAKE_AUTO` (maintain current UX)
- **Tracker**: `BLE_WAKE_BUTTON` (critical power savings)

### Benefits

1. **Dramatic Power Savings**: BLE only active when user wants it
2. **Clean Phone Experience**: No unexpected reconnections or data dumps
3. **User Control**: Clear intent when enabling phone connectivity
4. **Configurable**: Can be enabled per device role or user preference
5. **Backward Compatible**: Default behavior unchanged

### Implementation Files to Modify

1. **`src/PowerProfileManager.h/cpp`**: Add BLE wake mode settings
2. **`src/PowerFSM.cpp`**: Add button-only transitions and LS_NO_BLE state
3. **`src/PowerFSM.h`**: Add new state definition
4. **`protobufs/meshtastic/config.proto`**: Add configuration options
5. **`src/platform/esp32/main-esp32.cpp`**: Enhance visual feedback

### Testing Strategy

1. **Power Consumption**: Measure current draw in LS_NO_BLE vs normal LS
2. **Packet Processing**: Verify mesh packets still processed correctly
3. **Button Response**: Confirm BLE starts immediately on button press
4. **Phone App**: Test clean connection experience
5. **Timeout Behavior**: Verify BLE shuts down after timeout

### Future Enhancements

1. **Smart Wake**: BLE wake on specific packet types (emergency messages)
2. **Scheduled Wake**: BLE active during specific time windows
3. **Proximity Wake**: BLE wake when phone is nearby (RSSI-based)
4. **Voice Wake**: "Hey Meshtastic" voice activation (if audio hardware available)

This implementation provides the exact behavior you requested: packets wake the device for mesh processing, but BLE only comes alive when the user presses the button, eliminating the reconnection data dump issue while maximizing battery life.
