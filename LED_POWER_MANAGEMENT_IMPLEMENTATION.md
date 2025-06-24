# LED Power Management Implementation

This document describes the comprehensive LED power management system that integrates LED control with power profiles, providing intelligent LED behavior based on power source, device role, and user configuration.

## Overview

The new LED power management system replaces direct USB power checks with a sophisticated power profile-based approach that provides:

- **Power-aware LED control**: Different LED behavior for battery vs external power
- **Role-specific LED modes**: Customized LED behavior for different device roles
- **Message notification system**: Smart LED notifications for private and public messages
- **Configurable LED modes**: Heartbeat, messages, or both
- **Brightness control**: Adjustable LED brightness per power profile

## Technical Implementation

### 1. Protobuf Configuration

Added new LED configuration to `PowerProfile` in `config.proto`:

```protobuf
message LedConfig {
  enum LedMode {
    DISABLED = 0;                    // LED completely off
    HEARTBEAT_ONLY = 1;             // Power/charging status only
    MESSAGES_ONLY = 2;              // Message notifications only
    HEARTBEAT_AND_MESSAGES = 3;     // Both heartbeat and messages
  }
  
  LedMode mode = 1;                 // LED behavior mode
  uint32 brightness = 2;            // Brightness level (0-255)
}

LedConfig led_config = 11;          // Added to PowerProfile
```

### 2. Power Profile Integration

Updated `PowerProfileManager` with LED control methods:

```cpp
bool statusLedEnabled() const;                    // Check if LED should be active
LedMode getLedMode() const;                       // Get current LED mode
uint32_t getLedBrightness() const;               // Get current brightness
```

### 3. Default Power Profiles

#### Plugged-In Profile (External Power):
```cpp
.led_config = {
    .mode = HEARTBEAT_AND_MESSAGES,
    .brightness = 255  // Full brightness
}
```

#### Battery Profile (Battery Power):
```cpp
.led_config = {
    .mode = DISABLED,
    .brightness = 0    // LED off to save battery
}
```

### 4. Role-Specific LED Behavior

#### Router Role:
- **LED Mode**: `HEARTBEAT_ONLY`
- **Rationale**: Routers typically don't need message notifications, just status indication

#### Sensor Role:
- **LED Mode**: `HEARTBEAT_ONLY`
- **Rationale**: Sensors prioritize power efficiency, minimal LED activity

#### Client/Client_Mute Roles:
- **LED Mode**: `HEARTBEAT_AND_MESSAGES` (when on external power)
- **Rationale**: Full LED functionality for interactive devices

### 5. Enhanced LED Blinker Logic

The new `ledBlinkerPersonalized()` function provides:

#### Power Profile Awareness:
```cpp
if (!powerProfileManager.statusLedEnabled()) {
    ledBlink.set(false);
    return 1000;
}
```

#### Mode-Based Behavior:
- **DISABLED**: LED completely off
- **HEARTBEAT_ONLY**: Shows power/charging status
- **MESSAGES_ONLY**: Shows message notifications only
- **HEARTBEAT_AND_MESSAGES**: Full functionality

#### Message Notification Patterns:
- **Private Messages**: Slow blink (500ms on/off)
- **Public Messages**: Fast short blink (50ms on, 3000ms off)
- **Priority**: Message notifications override heartbeat

#### Heartbeat Patterns:
- **Fully Charged/Good Battery**: Solid on
- **Charging**: 0.5Hz square wave
- **Low Battery**: Fast blink (250ms)
- **No Battery**: Fast blink (250ms)

## Power Savings

### Battery Mode Benefits:
- **LED Disabled**: Saves ~5-15mA depending on LED type
- **Smart Activation**: Only when external power available
- **Role Optimization**: Minimal LED activity for power-sensitive roles

### External Power Mode Benefits:
- **Full Functionality**: Complete LED notification system
- **Maximum Brightness**: Full visibility when power is available
- **Enhanced UX**: Rich visual feedback for user interactions

## User Experience

### Normal Operation:
1. **Plug in USB**: LED activates with full brightness and functionality
2. **Unplug USB**: LED automatically disables to save battery
3. **Message Arrives**: LED shows appropriate notification pattern
4. **Button Press**: Clears message notification flags

### Power Profile Scenarios:

#### Scenario 1: Client on External Power
```
Power: USB connected
Profile: Plugged-in
LED Mode: HEARTBEAT_AND_MESSAGES
Brightness: 255 (full)
Behavior: Shows charging status + message notifications
```

#### Scenario 2: Client on Battery
```
Power: Battery only
Profile: Battery
LED Mode: DISABLED
Brightness: 0
Behavior: LED completely off to save power
```

#### Scenario 3: Router on External Power
```
Power: USB connected
Profile: Plugged-in (modified by role)
LED Mode: HEARTBEAT_ONLY (role override)
Brightness: 255
Behavior: Shows power status only, no message notifications
```

## Configuration Options

### Global LED Control:
- **`led_heartbeat_disabled`**: Master LED disable (overrides all profiles)

### Power Profile LED Config:
- **`mode`**: LED behavior mode (disabled, heartbeat, messages, both)
- **`brightness`**: LED brightness level (0-255)

### Automatic Behavior:
- **Power source detection**: Automatic profile switching
- **Role-based optimization**: Automatic LED mode adjustment
- **Message priority**: Smart notification handling

## Debug Logging

Enhanced debug logging for LED behavior:

```
DEBUG | POWER: LED enabled by power profile: mode=HEARTBEAT_AND_MESSAGES, brightness=255
DEBUG | POWER: LED disabled by power profile: battery mode active
DEBUG | POWER: Message notification: private message - slow blink
DEBUG | POWER: Heartbeat mode: charging status - 0.5Hz blink
```

## Backward Compatibility

### Legacy Support:
- **Existing `led_heartbeat_disabled`**: Still respected as master override
- **Default behavior**: Maintains existing LED patterns when power profiles disabled
- **Gradual migration**: Power profiles can be enabled independently

### Migration Path:
1. **Phase 1**: Power profiles disabled by default, existing LED logic active
2. **Phase 2**: Power profiles enabled, enhanced LED control available
3. **Phase 3**: Full power profile integration with user configuration

## Benefits Summary

✅ **Intelligent Power Management**: LED behavior adapts to power source automatically
✅ **Role-Specific Optimization**: LED behavior optimized for device role
✅ **Battery Life Extension**: Significant power savings on battery operation
✅ **Enhanced User Experience**: Rich visual feedback when power allows
✅ **Configurable Behavior**: Full user control over LED modes and brightness
✅ **Message Notifications**: Smart notification system for different message types
✅ **Backward Compatible**: Existing configurations continue to work
✅ **Debug Friendly**: Comprehensive logging for troubleshooting

This implementation provides a sophisticated LED management system that balances functionality with power efficiency, automatically adapting to power conditions while providing rich user feedback when appropriate.
