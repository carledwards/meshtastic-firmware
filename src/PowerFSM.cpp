/**
 * @file PowerFSM.cpp
 * @brief Implements the finite state machine for power management.
 *
 * This file contains the implementation of the finite state machine (FSM) for power management.
 * The FSM controls the power states of the device, including SDS (shallow deep sleep), LS (light sleep),
 * NB (normal mode), and POWER (powered mode). The FSM also handles transitions between states and
 * actions to be taken upon entering or exiting each state.
 */
#include "PowerFSM.h"
#include "Default.h"
#include "Led.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "PowerProfileManager.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "main.h"
#include "sleep.h"
#include "target_specific.h"
#include "concurrency/Lock.h"

#if HAS_WIFI && !defined(ARCH_PORTDUINO) || defined(MESHTASTIC_EXCLUDE_WIFI)
#include "mesh/wifi/WiFiAPClient.h"
#endif

#ifndef SLEEP_TIME
#define SLEEP_TIME 30
#endif
#if EXCLUDE_POWER_FSM
FakeFsm powerFSM;
void PowerFSM_setup(){};
void PowerFSM_recreate(){};
#else

static void sdsEnter()
{
    LOG_DEBUG("State: SDS");
    // Check if deep sleep is allowed by current power profile
    if (!PowerProfile::allowDeepSleep()) {
        LOG_INFO("Deep sleep blocked by power profile, staying in light sleep");
        powerFSM.trigger(EVENT_WAKE_TIMER); // Transition back to appropriate state
        return;
    }
    // FIXME - make sure GPS and LORA radio are off first - because we want close to zero current draw
    doDeepSleep(Default::getConfiguredOrDefaultMs(config.power.sds_secs), false, false);
}

static void lowBattSDSEnter()
{
    LOG_DEBUG("State: Lower batt SDS");
    doDeepSleep(Default::getConfiguredOrDefaultMs(config.power.sds_secs), false, true);
}
extern Power *power;

static void shutdownEnter()
{
    LOG_DEBUG("State: SHUTDOWN");
    power->shutdown();
}

#include "error.h"

static uint32_t secsSlept;

static void lsEnter()
{
    LOG_INFO("lsEnter begin, ls_secs=%u", config.power.ls_secs);
    screen->setOn(false);
    secsSlept = 0; // How long have we been sleeping this time
}

static void lsIdle()
{
#ifdef ARCH_ESP32
    // Do we have more sleeping to do?
    if (secsSlept < config.power.ls_secs) {
        // If some other service would stall sleep, don't let sleep happen yet
        if (doPreflightSleep()) {
            // Briefly come out of sleep long enough to blink the led once every few seconds
            uint32_t sleepTime = SLEEP_TIME;

            powerMon->setState(meshtastic_PowerMon_State_CPU_LightSleep);
            ledBlink.set(false); // Never leave led on while in light sleep
            esp_sleep_source_t wakeCause2 = doLightSleep(sleepTime * 1000LL);
            powerMon->clearState(meshtastic_PowerMon_State_CPU_LightSleep);

            switch (wakeCause2) {
            case ESP_SLEEP_WAKEUP_TIMER:
                // Normal case: timer expired, we should just go back to sleep ASAP
                ledBlink.set(true);             // briefly turn on led
                wakeCause2 = doLightSleep(100); // leave led on for 1ms
                secsSlept += sleepTime;
                break;

            case ESP_SLEEP_WAKEUP_UART:
                // Not currently used (because uart triggers in hw have problems)
                powerFSM.trigger(EVENT_SERIAL_CONNECTED);
                break;

            default:
                // We woke for some other reason (button press, device IRQ interrupt)
#ifdef BUTTON_PIN
                bool pressed = !digitalRead(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN);
#else
                bool pressed = false;
#endif
                if (pressed) { // If we woke because of press, instead generate a PRESS event.
                    powerFSM.trigger(EVENT_PRESS);
                } else {
                    // Otherwise let the NB state handle the IRQ (and that state will handle stuff like IRQs etc)
                    // we lie and say "wake timer" because the interrupt will be handled by the regular IRQ code
                    powerFSM.trigger(EVENT_WAKE_TIMER);
                }
                break;
            }
        } else {
            // Someone says we can't sleep now, so just save some power by sleeping the CPU for 100ms or so
            delay(100);
        }
    } else {
        // Time to stop sleeping!
        ledBlink.set(false);
        LOG_INFO("Reached ls_secs, service loop()");
        powerFSM.trigger(EVENT_WAKE_TIMER);
    }
#endif
}

static void lsExit()
{
    LOG_INFO("Exit state: LS");
}

static void nbEnter()
{
    LOG_DEBUG("State: NB");
    screen->setOn(false);
#ifdef ARCH_ESP32
    // Use power profile to determine Bluetooth state
    setBluetoothEnable(PowerProfile::bluetoothEnabled());
#endif
}

static void darkEnter()
{
    // Use power profile to determine Bluetooth state
    setBluetoothEnable(PowerProfile::bluetoothEnabled());
    screen->setOn(false);
}

static void serialEnter()
{
    LOG_DEBUG("State: SERIAL");
    setBluetoothEnable(false);
    screen->setOn(true);
    screen->print("Serial connected\n");
}

static void serialExit()
{
    // Turn bluetooth back on when we leave serial stream API
    setBluetoothEnable(true);
    screen->print("Serial disconnected\n");
}

static void powerEnter()
{
    LOG_DEBUG("State: POWER");
    screen->setOn(true);
    setBluetoothEnable(true);
}

static void powerIdle()
{
    // Check if we should transition away from POWER state
    bool hasUSB = powerStatus && powerStatus->getHasUSB();
    if (!hasUSB) {
        LOG_INFO("Loss of power in Powered");
        powerFSM.trigger(EVENT_POWER_DISCONNECTED);
    }
}

static void powerExit()
{
    screen->setOn(true);
    setBluetoothEnable(true);
}

static void onEnter()
{
    LOG_DEBUG("State: ON");
    screen->setOn(true);
    setBluetoothEnable(true);
}

static void onIdle()
{
    // Check if we should transition to POWER state
    bool hasUSB = powerStatus && powerStatus->getHasUSB();
    if (hasUSB) {
        LOG_INFO("Power connected in ON state");
        powerFSM.trigger(EVENT_POWER_CONNECTED);
    }
}

static void screenPress()
{
    screen->onPress();
}

static void bootEnter()
{
    LOG_DEBUG("State: BOOT");
}

State stateSHUTDOWN(shutdownEnter, NULL, NULL, "SHUTDOWN");
State stateSDS(sdsEnter, NULL, NULL, "SDS");
State stateLowBattSDS(lowBattSDSEnter, NULL, NULL, "SDS");
State stateLS(lsEnter, lsIdle, lsExit, "LS");
State stateNB(nbEnter, NULL, NULL, "NB");
State stateDARK(darkEnter, NULL, NULL, "DARK");
State stateSERIAL(serialEnter, NULL, serialExit, "SERIAL");
State stateBOOT(bootEnter, NULL, NULL, "BOOT");
State stateON(onEnter, onIdle, NULL, "ON");
State statePOWER(powerEnter, powerIdle, powerExit, "POWER");
Fsm powerFSM(&stateBOOT);

// Mutex to protect FSM reconfiguration
static concurrency::Lock powerFSMLock;

// State preservation for FSM recreation
static State* preservedState = nullptr;
static bool fsmRecreationPending = false;
static bool fsmInitialSetupComplete = false;

/**
 * Create/recreate the PowerFSM based on current power profile settings.
 * This is the single source of truth for FSM structure.
 */
static void PowerFSM_create()
{
    concurrency::LockGuard guard(&powerFSMLock);
    
    LOG_INFO("PowerFSM creating based on current power profile");
    
    // Determine initial state based on hardware
    bool hasUSB = powerStatus && powerStatus->getHasUSB();
    State* initialState;
    
    // If recreating, preserve current state
    if (preservedState) {
        initialState = preservedState;
        preservedState = nullptr;
        LOG_INFO("PowerFSM recreating, preserving state: %s", initialState->name);
    } else {
        // First time setup - start from BOOT
        initialState = &stateBOOT;
        LOG_INFO("PowerFSM initial setup, starting from BOOT");
    }
    
    // Create new FSM instance
    powerFSM = Fsm(initialState);
    
    // Add boot transition only for initial setup
    if (initialState == &stateBOOT) {
        powerFSM.add_timed_transition(&stateBOOT, hasUSB ? &statePOWER : &stateON, 3 * 1000, NULL, "boot timeout");
    }

    // === Core State Transitions (Profile-Independent) ===
    
    // Wake timer transitions - use profile to determine target state
    State* wakeTarget = PowerProfile::bluetoothEnabled() ? &stateDARK : &stateNB;
    
#ifdef ARCH_ESP32
    powerFSM.add_transition(&stateLS, wakeTarget, EVENT_WAKE_TIMER, NULL, "Wake timer");
#else // Don't go into a no-bluetooth state on low power platforms
    powerFSM.add_transition(&stateLS, &stateDARK, EVENT_WAKE_TIMER, NULL, "Wake timer");
#endif

    // Packet handling transitions
    powerFSM.add_transition(&stateLS, wakeTarget, EVENT_PACKET_FOR_PHONE, NULL, "Received packet, exiting light sleep");
    powerFSM.add_transition(&stateNB, &stateNB, EVENT_PACKET_FOR_PHONE, NULL, "Received packet, resetting wake");
    powerFSM.add_transition(&stateNB, &stateDARK, EVENT_PACKET_FOR_PHONE, NULL, "Packet for phone");

    // Press event transitions
    powerFSM.add_transition(&stateLS, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateNB, &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&stateDARK, hasUSB ? &statePOWER : &stateON, EVENT_PRESS, NULL, "Press");
    powerFSM.add_transition(&statePOWER, &statePOWER, EVENT_PRESS, screenPress, "Press");
    powerFSM.add_transition(&stateON, &stateON, EVENT_PRESS, screenPress, "Press");
    powerFSM.add_transition(&stateSERIAL, &stateSERIAL, EVENT_PRESS, screenPress, "Press");

    // Critical battery transitions
    powerFSM.add_transition(&stateBOOT, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateLS, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateNB, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateDARK, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateON, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");
    powerFSM.add_transition(&stateSERIAL, &stateLowBattSDS, EVENT_LOW_BATTERY, NULL, "LowBat");

    // Shutdown transitions
    powerFSM.add_transition(&stateBOOT, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateLS, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateNB, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateDARK, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateON, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");
    powerFSM.add_transition(&stateSERIAL, &stateSHUTDOWN, EVENT_SHUTDOWN, NULL, "Shutdown");

    // Input device transitions
    powerFSM.add_transition(&stateLS, &stateON, EVENT_INPUT, NULL, "Input Device");
    powerFSM.add_transition(&stateNB, &stateON, EVENT_INPUT, NULL, "Input Device");
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_INPUT, NULL, "Input Device");
    powerFSM.add_transition(&stateON, &stateON, EVENT_INPUT, NULL, "Input Device");
    powerFSM.add_transition(&statePOWER, &statePOWER, EVENT_INPUT, NULL, "Input Device");

    // Bluetooth pairing transitions
    powerFSM.add_transition(&stateDARK, &stateON, EVENT_BLUETOOTH_PAIR, NULL, "Bluetooth pairing");
    powerFSM.add_transition(&stateON, &stateON, EVENT_BLUETOOTH_PAIR, NULL, "Bluetooth pairing");

    // Serial API transitions
    powerFSM.add_transition(&stateLS, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "serial API");
    powerFSM.add_transition(&stateNB, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "serial API");
    powerFSM.add_transition(&stateDARK, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "serial API");
    powerFSM.add_transition(&stateON, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "serial API");
    powerFSM.add_transition(&statePOWER, &stateSERIAL, EVENT_SERIAL_CONNECTED, NULL, "serial API");
    powerFSM.add_transition(&stateSERIAL, &stateON, EVENT_SERIAL_DISCONNECTED, NULL, "serial disconnect");

    // Power connect/disconnect transitions
    powerFSM.add_transition(&stateLS, &statePOWER, EVENT_POWER_CONNECTED, NULL, "power connect");
    powerFSM.add_transition(&stateNB, &statePOWER, EVENT_POWER_CONNECTED, NULL, "power connect");
    powerFSM.add_transition(&stateDARK, &statePOWER, EVENT_POWER_CONNECTED, NULL, "power connect");
    powerFSM.add_transition(&stateON, &statePOWER, EVENT_POWER_CONNECTED, NULL, "power connect");
    powerFSM.add_transition(&statePOWER, &stateON, EVENT_POWER_DISCONNECTED, NULL, "power disconnected");

    // Phone contact transition
    powerFSM.add_transition(&stateDARK, &stateDARK, EVENT_CONTACT_FROM_PHONE, NULL, "Contact from phone");

    // === Profile-Based Transitions ===

    // Screen timeout transitions (always use profile values)
#ifdef USE_EINK
    if (PowerProfile::getScreenTimeoutSecs() > 0)
#endif
    {
        powerFSM.add_timed_transition(&stateON, &stateDARK,
                                      PowerProfile::getScreenTimeoutSecs() * 1000,
                                      NULL, "Screen-on timeout");
        powerFSM.add_timed_transition(&statePOWER, &stateDARK,
                                      PowerProfile::getScreenTimeoutSecs() * 1000,
                                      NULL, "Screen-on timeout");
    }

    // Message and NodeDB update transitions based on screen responsiveness
    if (PowerProfile::screenStaysResponsive()) {
        LOG_DEBUG("Screen stays responsive - messages will wake screen");
        // These transitions wake the screen for messages
        powerFSM.add_transition(&stateLS, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text (wake)");
        powerFSM.add_transition(&stateNB, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text (wake)");
        powerFSM.add_transition(&stateDARK, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text (wake)");
        
        powerFSM.add_transition(&stateNB, &stateON, EVENT_NODEDB_UPDATED, NULL, "NodeDB update (wake)");
        powerFSM.add_transition(&stateDARK, &stateON, EVENT_NODEDB_UPDATED, NULL, "NodeDB update (wake)");
    } else {
        LOG_DEBUG("Screen not responsive - messages won't wake screen");
        // These transitions handle messages without waking screen
        powerFSM.add_transition(&stateLS, &stateLS, EVENT_RECEIVED_MSG, NULL, "Received text (no wake)");
        powerFSM.add_transition(&stateNB, &stateNB, EVENT_RECEIVED_MSG, NULL, "Received text (no wake)");
        powerFSM.add_transition(&stateDARK, &stateDARK, EVENT_RECEIVED_MSG, NULL, "Received text (no wake)");
        
        powerFSM.add_transition(&stateNB, &stateNB, EVENT_NODEDB_UPDATED, NULL, "NodeDB update (no wake)");
        powerFSM.add_transition(&stateDARK, &stateDARK, EVENT_NODEDB_UPDATED, NULL, "NodeDB update (no wake)");
    }
    
    // Always restart timer if screen is already on
    powerFSM.add_transition(&stateON, &stateON, EVENT_RECEIVED_MSG, NULL, "Received text (restart timer)");
    powerFSM.add_transition(&stateON, &stateON, EVENT_NODEDB_UPDATED, NULL, "NodeDB update (restart timer)");

    // Light sleep transitions based on profile
#ifdef ARCH_ESP32
#if HAS_WIFI || !defined(MESHTASTIC_EXCLUDE_WIFI)
    // Check if light sleep should be enabled based on profile and conditions
    bool isTrackerOrSensor = config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER ||
                             config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER ||
                             config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR;

    bool shouldEnableLightSleep = PowerProfile::allowLightSleep() && !isWifiAvailable() && !isTrackerOrSensor;

    if (shouldEnableLightSleep) {
        powerFSM.add_timed_transition(&stateNB, &stateLS,
                                      PowerProfile::getMinWakeSecs() * 1000, NULL,
                                      "Min wake timeout");

        powerFSM.add_timed_transition(&stateDARK, &stateLS,
                                      PowerProfile::getBluetoothTimeoutSecs() * 1000, NULL,
                                      "Bluetooth timeout");
    } else {
        // If not using light sleep, check periodically if config has drifted out of stateDark
        powerFSM.add_timed_transition(&stateDARK, &stateDARK,
                                      PowerProfile::getScreenTimeoutSecs() * 1000,
                                      NULL, "Screen-on timeout");
    }
#endif // HAS_WIFI || !defined(MESHTASTIC_EXCLUDE_WIFI)
#else // (not) ARCH_ESP32
    // If not ESP32, light-sleep not used. Check periodically if config has drifted out of stateDark
    powerFSM.add_timed_transition(&stateDARK, &stateDARK,
                                  PowerProfile::getScreenTimeoutSecs() * 1000, NULL,
                                  "Screen-on timeout");
#endif

    // Run one iteration to enter the initial state
    powerFSM.run_machine();
    
    LOG_INFO("PowerFSM creation complete - allow_deep_sleep=%d, allow_light_sleep=%d, screen_responsive=%d",
             PowerProfile::allowDeepSleep() ? 1 : 0,
             PowerProfile::allowLightSleep() ? 1 : 0,
             PowerProfile::screenStaysResponsive() ? 1 : 0);
}

/**
 * Recreate the PowerFSM safely, preserving current state.
 * Called when power profiles change.
 */
void PowerFSM_recreate()
{
    concurrency::LockGuard guard(&powerFSMLock);
    
    // Preserve current state
    preservedState = powerFSM.getState();
    LOG_INFO("PowerFSM recreating, preserving state: %s", preservedState ? preservedState->name : "NULL");
    
    // Create new FSM with preserved state
    PowerFSM_create();
}

/**
 * Initial setup of PowerFSM - called once at boot
 */
void PowerFSM_setup()
{
    LOG_INFO("PowerFSM initial setup");
    PowerFSM_create();
    fsmInitialSetupComplete = true;
}

/**
 * Check if FSM recreation is pending and process it safely
 * Called from main loop to avoid self-destruction
 */
void PowerFSM_processRecreation()
{
    if (fsmRecreationPending) {
        fsmRecreationPending = false;
        PowerFSM_recreate();
    }
}

/**
 * Schedule FSM recreation (safe to call from any context)
 */
void PowerFSM_scheduleRecreation()
{
    // Only schedule recreation if initial setup is complete
    if (fsmInitialSetupComplete) {
        fsmRecreationPending = true;
        LOG_DEBUG("PowerFSM recreation scheduled");
    } else {
        LOG_DEBUG("PowerFSM recreation skipped - initial setup not complete");
    }
}

/**
 * Legacy reconfigure function - now schedules recreation
 */
void PowerFSM_reconfigure()
{
    LOG_INFO("PowerFSM_reconfigure called - scheduling recreation");
    PowerFSM_scheduleRecreation();
}

#endif
