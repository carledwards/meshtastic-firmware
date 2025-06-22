#pragma once

#include "configuration.h"
#include "PowerStatus.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include "Observer.h"
#include <atomic>

/**
 * @brief Thread-safe power profile manager for granular power management
 * 
 * This class provides atomic switching between power profiles based on power source
 * and user configuration, enabling fine-grained control over power management behavior
 * without requiring FSM reconfiguration.
 */
class PowerProfileManager {
private:
    // Atomic pointer to current active profile for thread safety
    std::atomic<const meshtastic_Config_PowerConfig_PowerProfile*> currentProfile;
    
    // Track if granular power management is enabled
    std::atomic<bool> granularEnabled;

    // Observer for PowerStatus changes
    CallbackObserver<PowerProfileManager, const meshtastic::Status *> powerStatusObserver =
        CallbackObserver<PowerProfileManager, const meshtastic::Status *>(this, &PowerProfileManager::onPowerStatusUpdate);

    /**
     * @brief Callback for PowerStatus changes
     * @param newStatus The new power status
     * @return 0 on success
     */
    int onPowerStatusUpdate(const meshtastic::Status *newStatus);

    /**
     * @brief Determine which profile should be active based on current conditions
     * @return Pointer to the appropriate power profile
     */
    const meshtastic_Config_PowerConfig_PowerProfile* selectActiveProfile();

    /**
     * @brief Get default profile for legacy power saving mode
     * @return Pointer to a static legacy profile
     */
    const meshtastic_Config_PowerConfig_PowerProfile* getLegacyProfile();

    /**
     * @brief Compute layered profile from system defaults + role modifiers + user overrides
     * @return Pointer to computed profile
     */
    const meshtastic_Config_PowerConfig_PowerProfile* computeLayeredProfile();

    /**
     * @brief Apply role-specific modifications to a profile
     * @param profile Profile to modify
     * @param role Device role to apply modifiers for
     */
    void applyRoleModifiers(meshtastic_Config_PowerConfig_PowerProfile* profile, 
                           meshtastic_Config_DeviceConfig_Role role);

    /**
     * @brief Apply user overrides to a profile
     * @param profile Profile to modify
     * @param hasUSB Whether device is currently on USB power
     */
    void applyUserOverrides(meshtastic_Config_PowerConfig_PowerProfile* profile, bool hasUSB);

public:
    // Cache the last known USB status to detect changes
    std::atomic<bool> lastUSBStatus;
    
    PowerProfileManager();
    
    /**
     * @brief Initialize the power profile manager
     * Called during system startup after config is loaded
     */
    void init();
    
    /**
     * @brief Update active profile based on current power status
     * Called periodically by PowerFSMThread to detect power source changes
     * @return true if profile changed, false if no change
     */
    bool updateActiveProfile();
    
    /**
     * @brief Get the currently active power profile (thread-safe)
     * @return Pointer to current active profile, never null
     */
    const meshtastic_Config_PowerConfig_PowerProfile* getActiveProfile() const;
    
    /**
     * @brief Check if granular power management is enabled
     * @return true if using granular profiles, false if using legacy mode
     */
    bool isGranularModeEnabled() const;
    
    /**
     * @brief Force a specific profile to be active (for testing/debugging)
     * @param profile Pointer to profile to force active
     */
    void forceProfile(const meshtastic_Config_PowerConfig_PowerProfile* profile);
    
    // Profile query methods - these check the current active profile atomically
    
    /**
     * @brief Check if deep sleep (SDS) is allowed by current profile
     * @return true if SDS is permitted
     */
    bool allowDeepSleep() const;
    
    /**
     * @brief Check if light sleep is allowed by current profile
     * @return true if light sleep is permitted
     */
    bool allowLightSleep() const;
    
    /**
     * @brief Check if Bluetooth should stay enabled in power saving states
     * @return true if Bluetooth should remain on
     */
    bool bluetoothEnabled() const;
    
    /**
     * @brief Check if WiFi should stay enabled in power saving states
     * @return true if WiFi should remain on
     */
    bool wifiEnabled() const;
    
    /**
     * @brief Check if screen should wake on incoming packets
     * @return true if screen should respond to packets
     */
    bool screenStaysResponsive() const;
    
    /**
     * @brief Check if GPS should stay enabled in power saving states
     * @return true if GPS should remain on
     */
    bool gpsEnabled() const;
    
    /**
     * @brief Get screen timeout from current profile
     * @return Screen timeout in seconds, 0 for system default
     */
    uint32_t getScreenTimeoutSecs() const;
    
    /**
     * @brief Get Bluetooth timeout from current profile
     * @return Bluetooth timeout in seconds, 0 for system default
     */
    uint32_t getBluetoothTimeoutSecs() const;
    
    /**
     * @brief Get minimum wake time from current profile
     * @return Minimum wake time in seconds, 0 for system default
     */
    uint32_t getMinWakeSecs() const;
    
    /**
     * @brief Get maximum power state allowed by current profile
     * @return Maximum power state enum value
     */
    meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState getMaxPowerState() const;
    
    /**
     * @brief Check if a specific power state is allowed by current profile
     * @param state The power state to check
     * @return true if the state is allowed
     */
    bool isPowerStateAllowed(meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState state) const;
};

// Global instance
extern PowerProfileManager powerProfileManager;

// Convenience functions for use throughout the codebase
namespace PowerProfile {
    /**
     * @brief Check if deep sleep is currently allowed
     */
    inline bool allowDeepSleep() { return powerProfileManager.allowDeepSleep(); }
    
    /**
     * @brief Check if light sleep is currently allowed
     */
    inline bool allowLightSleep() { return powerProfileManager.allowLightSleep(); }
    
    /**
     * @brief Check if Bluetooth should stay enabled
     */
    inline bool bluetoothEnabled() { return powerProfileManager.bluetoothEnabled(); }
    
    /**
     * @brief Check if WiFi should stay enabled
     */
    inline bool wifiEnabled() { return powerProfileManager.wifiEnabled(); }
    
    /**
     * @brief Check if screen should respond to packets
     */
    inline bool screenStaysResponsive() { return powerProfileManager.screenStaysResponsive(); }
    
    /**
     * @brief Check if GPS should stay enabled
     */
    inline bool gpsEnabled() { return powerProfileManager.gpsEnabled(); }
    
    /**
     * @brief Get current screen timeout
     */
    inline uint32_t getScreenTimeoutSecs() { return powerProfileManager.getScreenTimeoutSecs(); }
    
    /**
     * @brief Get current Bluetooth timeout
     */
    inline uint32_t getBluetoothTimeoutSecs() { return powerProfileManager.getBluetoothTimeoutSecs(); }
    
    /**
     * @brief Get current minimum wake time
     */
    inline uint32_t getMinWakeSecs() { return powerProfileManager.getMinWakeSecs(); }
    
    /**
     * @brief Check if a power state is allowed
     */
    inline bool isPowerStateAllowed(meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState state) {
        return powerProfileManager.isPowerStateAllowed(state);
    }
}
