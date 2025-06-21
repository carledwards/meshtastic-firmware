#include "PowerProfileManager.h"
#include "PowerStatus.h"
#include "configuration.h"
#include "Default.h"
#include "main.h"
#include "PowerFSM.h"

// Global instance
PowerProfileManager powerProfileManager;

// System default profiles - these provide sensible defaults for all devices
static meshtastic_Config_PowerConfig_PowerProfile systemDefaultPluggedProfile = {
    .allow_deep_sleep = false,
    .allow_light_sleep = false,
    .bluetooth_enabled = true,
    .wifi_enabled = true,
    .screen_stays_responsive = true,
    .gps_enabled = true,
    .screen_timeout_secs = 0,  // Use system default
    .bluetooth_timeout_secs = 0,  // Use system default
    .min_wake_secs = 0,  // Use system default
    .max_power_state = meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState_MAX_ON
};

static meshtastic_Config_PowerConfig_PowerProfile systemDefaultBatteryProfile = {
    .allow_deep_sleep = false,  // Keep LoRa on for "always connected" use case
    .allow_light_sleep = false,  // Keep CPU active for immediate response
    .bluetooth_enabled = false,  // Turn off BT to save power
    .wifi_enabled = false,  // Turn off WiFi to save power
    .screen_stays_responsive = false,  // Don't wake screen for packets
    .gps_enabled = true,  // Keep GPS on
    .screen_timeout_secs = 30,  // Quick screen timeout
    .bluetooth_timeout_secs = 30,  // Quick BT timeout
    .min_wake_secs = 5,  // Short wake time
    .max_power_state = meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState_MAX_NO_BLUETOOTH
};

// Legacy profiles for backward compatibility
static meshtastic_Config_PowerConfig_PowerProfile legacyPowerSavingProfile = {
    .allow_deep_sleep = true,
    .allow_light_sleep = true,
    .bluetooth_enabled = false,
    .wifi_enabled = false,
    .screen_stays_responsive = true,
    .gps_enabled = true,
    .screen_timeout_secs = 0,
    .bluetooth_timeout_secs = 0,
    .min_wake_secs = 0,
    .max_power_state = meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState_MAX_SDS
};

static meshtastic_Config_PowerConfig_PowerProfile legacyNormalProfile = {
    .allow_deep_sleep = false,
    .allow_light_sleep = false,
    .bluetooth_enabled = true,
    .wifi_enabled = true,
    .screen_stays_responsive = true,
    .gps_enabled = true,
    .screen_timeout_secs = 0,
    .bluetooth_timeout_secs = 0,
    .min_wake_secs = 0,
    .max_power_state = meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState_MAX_DARK
};

// Working profile that gets computed by layering system defaults + role modifiers + user overrides
static meshtastic_Config_PowerConfig_PowerProfile computedProfile;

PowerProfileManager::PowerProfileManager() 
    : currentProfile(nullptr), lastUSBStatus(false), granularEnabled(false)
{
}

void PowerProfileManager::init()
{
    // Check if granular power management is enabled
    granularEnabled.store(config.power.use_granular_power_management);
    
    // Initialize USB status
    if (powerStatus) {
        lastUSBStatus.store(powerStatus->getHasUSB());
    }
    
    // Set initial active profile
    updateActiveProfile();
    
    LOG_INFO("PowerProfileManager initialized, granular mode: %s", 
             granularEnabled.load() ? "enabled" : "disabled");
}

bool PowerProfileManager::updateActiveProfile()
{
    const meshtastic_Config_PowerConfig_PowerProfile* newProfile = selectActiveProfile();
    const meshtastic_Config_PowerConfig_PowerProfile* oldProfile = currentProfile.load();
    
    if (newProfile != oldProfile) {
        currentProfile.store(newProfile);
        
        // Update USB status cache
        if (powerStatus) {
            lastUSBStatus.store(powerStatus->getHasUSB());
        }
        
        LOG_INFO("Power profile changed: %s", 
                 granularEnabled.load() ? 
                 (powerStatus && powerStatus->getHasUSB() ? "Granular Plugged" : "Granular Battery") :
                 (newProfile == &legacyPowerSavingProfile ? "Legacy Power Saving" : "Legacy Normal"));
        
        // Schedule PowerFSM recreation to use new profile settings
        #if !EXCLUDE_POWER_FSM
        PowerFSM_scheduleRecreation();
        #endif
        
        return true;
    }
    
    return false;
}

const meshtastic_Config_PowerConfig_PowerProfile* PowerProfileManager::selectActiveProfile()
{
    if (!granularEnabled.load()) {
        // Legacy mode - use old power saving logic
        return getLegacyProfile();
    }
    
    // Granular mode - compute layered profile
    return computeLayeredProfile();
}

const meshtastic_Config_PowerConfig_PowerProfile* PowerProfileManager::getLegacyProfile()
{
    // Use legacy power saving logic
    bool isRouter = (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER);
    bool isPowerSavingMode = config.power.is_power_saving || isRouter;
    
    if (isPowerSavingMode) {
        return &legacyPowerSavingProfile;
    } else {
        return &legacyNormalProfile;
    }
}

const meshtastic_Config_PowerConfig_PowerProfile* PowerProfileManager::computeLayeredProfile()
{
    bool hasUSB = powerStatus ? powerStatus->getHasUSB() : false;
    
    // Layer 1: Start with system defaults based on power source
    const meshtastic_Config_PowerConfig_PowerProfile* baseProfile;
    
    switch (config.power.force_profile) {
        case meshtastic_Config_PowerConfig_ProfileOverride_PROFILE_ALWAYS_PLUGGED:
            baseProfile = &systemDefaultPluggedProfile;
            break;
            
        case meshtastic_Config_PowerConfig_ProfileOverride_PROFILE_ALWAYS_BATTERY:
            baseProfile = &systemDefaultBatteryProfile;
            break;
            
        case meshtastic_Config_PowerConfig_ProfileOverride_PROFILE_AUTO:
        default:
            // Automatic selection based on power source
            baseProfile = hasUSB ? &systemDefaultPluggedProfile : &systemDefaultBatteryProfile;
            break;
    }
    
    // Copy base profile to working profile
    computedProfile = *baseProfile;
    
    // Layer 2: Apply role-specific modifiers
    applyRoleModifiers(&computedProfile, config.device.role);
    
    // Layer 3: Apply user overrides
    applyUserOverrides(&computedProfile, hasUSB);
    
    return &computedProfile;
}

void PowerProfileManager::applyRoleModifiers(meshtastic_Config_PowerConfig_PowerProfile* profile, 
                                           meshtastic_Config_DeviceConfig_Role role)
{
    switch (role) {
        case meshtastic_Config_DeviceConfig_Role_ROUTER:
            // Routers must stay awake to relay packets
            profile->allow_deep_sleep = false;
            profile->allow_light_sleep = false;  // Stay fully awake for immediate routing
            profile->min_wake_secs = 1;  // Quick response time
            profile->max_power_state = meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState_MAX_DARK;
            break;
            
        case meshtastic_Config_DeviceConfig_Role_TRACKER:
            // Trackers prioritize GPS and location updates
            profile->gps_enabled = true;
            profile->screen_timeout_secs = 10;  // Quick screen timeout to save power
            // Allow some power saving but keep GPS active
            break;
            
        case meshtastic_Config_DeviceConfig_Role_SENSOR:
            // Sensors prioritize power efficiency
            profile->bluetooth_enabled = false;  // Usually don't need BT
            profile->screen_stays_responsive = false;  // Don't wake screen for packets
            profile->screen_timeout_secs = 5;  // Very quick screen timeout
            break;
            
        case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:
            // Muted clients can be more aggressive with power saving
            profile->screen_stays_responsive = false;
            break;
            
        case meshtastic_Config_DeviceConfig_Role_CLIENT:
        default:
            // Default client behavior - no modifications needed
            break;
    }
}

void PowerProfileManager::applyUserOverrides(meshtastic_Config_PowerConfig_PowerProfile* profile, bool hasUSB)
{
    // Apply user overrides from the appropriate profile
    const meshtastic_Config_PowerConfig_PowerProfile* userProfile;
    
    if (hasUSB && config.power.has_plugged_in_profile) {
        userProfile = &config.power.plugged_in_profile;
    } else if (!hasUSB && config.power.has_battery_profile) {
        userProfile = &config.power.battery_profile;
    } else {
        // No user overrides to apply
        return;
    }
    
    // Apply any non-zero/non-default user settings
    // Note: In protobuf, bool fields are always present, but we could add a "use_default" pattern later
    
    // For now, apply all user settings (this gives full user control)
    *profile = *userProfile;
}

const meshtastic_Config_PowerConfig_PowerProfile* PowerProfileManager::getActiveProfile() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = currentProfile.load();
    return profile ? profile : &legacyNormalProfile;  // Fallback to safe default
}

bool PowerProfileManager::isGranularModeEnabled() const
{
    return granularEnabled.load();
}

void PowerProfileManager::forceProfile(const meshtastic_Config_PowerConfig_PowerProfile* profile)
{
    if (profile) {
        currentProfile.store(profile);
        LOG_INFO("Power profile forced to custom profile");
    }
}

// Profile query methods

bool PowerProfileManager::allowDeepSleep() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = getActiveProfile();
    return profile->allow_deep_sleep;
}

bool PowerProfileManager::allowLightSleep() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = getActiveProfile();
    return profile->allow_light_sleep;
}

bool PowerProfileManager::bluetoothEnabled() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = getActiveProfile();
    return profile->bluetooth_enabled;
}

bool PowerProfileManager::wifiEnabled() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = getActiveProfile();
    return profile->wifi_enabled;
}

bool PowerProfileManager::screenStaysResponsive() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = getActiveProfile();
    return profile->screen_stays_responsive;
}

bool PowerProfileManager::gpsEnabled() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = getActiveProfile();
    return profile->gps_enabled;
}

uint32_t PowerProfileManager::getScreenTimeoutSecs() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = getActiveProfile();
    if (profile->screen_timeout_secs > 0) {
        return profile->screen_timeout_secs;
    }
    // Fall back to system default
    return Default::getConfiguredOrDefaultMs(config.display.screen_on_secs, default_screen_on_secs) / 1000;
}

uint32_t PowerProfileManager::getBluetoothTimeoutSecs() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = getActiveProfile();
    if (profile->bluetooth_timeout_secs > 0) {
        return profile->bluetooth_timeout_secs;
    }
    // Fall back to system default
    return Default::getConfiguredOrDefaultMs(config.power.wait_bluetooth_secs, default_wait_bluetooth_secs) / 1000;
}

uint32_t PowerProfileManager::getMinWakeSecs() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = getActiveProfile();
    if (profile->min_wake_secs > 0) {
        return profile->min_wake_secs;
    }
    // Fall back to system default
    return Default::getConfiguredOrDefaultMs(config.power.min_wake_secs, default_min_wake_secs) / 1000;
}

meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState PowerProfileManager::getMaxPowerState() const
{
    const meshtastic_Config_PowerConfig_PowerProfile* profile = getActiveProfile();
    return profile->max_power_state;
}

bool PowerProfileManager::isPowerStateAllowed(meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState state) const
{
    meshtastic_Config_PowerConfig_PowerProfile_MaxPowerState maxState = getMaxPowerState();
    
    // Lower enum values represent deeper power states
    // If max state is MAX_DARK (3), then SDS (0), LS (1), NB (2), and DARK (3) are all disallowed
    // Only ON (4) would be allowed, but since we're checking if 'state' is allowed,
    // we need to check if 'state' is >= maxState
    return state >= maxState;
}
