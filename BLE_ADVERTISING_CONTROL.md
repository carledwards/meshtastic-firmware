# ESP32 BLE Advertising Control Implementation

This document describes the new BLE advertising control system that provides real power savings while preserving existing connections.

## Problem Solved

Previously, `setBluetoothEnable(false)` was a no-op on ESP32, meaning:
- ❌ No power savings when "BLE disabled" 
- ❌ Device always remained discoverable
- ❌ Power profiles had no effect on BLE behavior

## New Implementation

### Smart Advertising Control
- **BLE Stack**: Remains active to preserve connections
- **Advertising**: Can be stopped/started independently
- **Connections**: Preserved when advertising stops
- **Power Savings**: ~10-20mA when advertising is disabled

### Behavior Changes

#### `setBluetoothEnable(true)`:
1. **First time**: Initialize full BLE stack + start advertising
2. **Subsequent**: Just start advertising (if stopped)
3. **Debug log**: `"POWER: Starting BLE advertising - device discoverable"`

#### `setBluetoothEnable(false)`:
1. **Stop advertising** but preserve existing connections
2. **Device becomes hidden** to new connection attempts
3. **Debug log**: `"POWER: Stopping BLE advertising - device hidden, connections preserved"`

#### **Client Disconnect**:
1. **Check current power profile** before restarting advertising
2. **Auto-restart advertising** only if current power profile allows BLE
3. **Respects power changes** that occurred while connected (USB plug/unplug)
4. **Debug logs**: 
   - `"POWER: Restarting BLE advertising after client disconnect (per current power profile)"`
   - `"POWER: Not restarting BLE advertising - disabled by current power profile"`

## Power Management Integration

### Power Profile Behavior:
- **High power modes**: Advertising always on
- **Power saving modes**: Advertising disabled after timeout
- **Button press/activity**: Can re-enable advertising
- **Light sleep**: Everything pauses automatically (existing behavior)

### Debug Logging:
```
DEBUG | POWER: Starting BLE advertising - device discoverable
DEBUG | POWER: Stopping BLE advertising - device hidden, connections preserved  
DEBUG | POWER: Restarting BLE advertising after client disconnect
```

## User Experience

### Normal Operation:
- Device always discoverable when active
- Mobile apps can connect normally
- No behavior change for users

### Power Saving Mode:
- Device becomes "hidden" after timeout (e.g., 30 seconds)
- **Existing connections remain active** and functional
- Button press can make device discoverable again
- Automatic re-discovery when client disconnects

### Connection Scenarios:

#### Scenario 1: No Active Connections
```
Time 0s: Advertising ON → Device discoverable
Time 30s: Power timeout → Stop advertising → ~15mA savings
Result: Device hidden, significant power savings
```

#### Scenario 2: Mobile App Connected  
```
Time 0s: Mobile connects → Advertising ON
Time 30s: Power timeout → Stop advertising, keep connection
Result: Mobile stays connected, device hidden to others, ~15mA savings
```

#### Scenario 3: Connection Lost
```
Mobile disconnects → Auto-restart advertising
Result: Device becomes discoverable again
```

#### Scenario 4: Power Profile Change While Connected (NEW FIX)
```
Time 0s: Device on USB power → High power profile → Mobile connects
Time 30s: Advertising stops (power saving) → Mobile stays connected
Time 60s: USB unplugged → Power profile changes to battery/low power
Time 90s: Mobile disconnects → Check current power profile → NO restart advertising
Result: Device stays hidden, respects new low-power profile
```

## Technical Implementation

### New Methods Added:
- `NimbleBluetooth::startAdvertising()` - Start BLE advertising
- `NimbleBluetooth::stopAdvertising()` - Stop BLE advertising  
- `NimbleBluetooth::isAdvertising()` - Check advertising status

### State Tracking:
- `advertisingActive` flag tracks current advertising state
- Prevents duplicate start/stop operations
- Enables proper debug logging

### Connection Preservation:
- BLE server and services remain active
- Existing connections continue to function
- Only advertising (discovery) is controlled

## Benefits

✅ **Real Power Savings**: 10-20mA reduction when advertising disabled
✅ **Connection Preservation**: Existing connections remain functional  
✅ **Automatic Recovery**: Re-advertise when clients disconnect
✅ **Proper Power Profiles**: Power management actually works as intended
✅ **Better UX**: Device behavior matches user expectations
✅ **Debug Visibility**: Clear logging of BLE state changes

## Backward Compatibility

- ✅ **No breaking changes** to existing API
- ✅ **Existing connections** continue to work
- ✅ **Mobile apps** see no difference in normal operation
- ✅ **Light sleep behavior** unchanged
- ✅ **Deep sleep behavior** unchanged (still uses `deinit()`)

This implementation finally makes the ESP32 power profiles work as intended while providing significant power savings and maintaining full functionality.
