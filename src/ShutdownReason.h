#pragma once
#include <stdint.h>

// Magic bytes recording why the device entered SYSTEM_OFF / deep sleep.
// On nRF52 these are persisted across the SYSTEM_OFF -> reset boundary via
// GPREGRET[1] (GPREGRET[0] is reserved for the bootloader DFU skip magic
// and LittleFS corruption flag).
enum ShutdownReason : uint8_t {
    SHUTDOWN_REASON_NONE = 0x00,             // cleared at boot
    SHUTDOWN_REASON_USER_BUTTON = 0xA1,      // hardware input (button long-press, kbd, ExpressLRS, SDL close)
    SHUTDOWN_REASON_USER_ADMIN = 0xA2,       // admin protobuf shutdown_seconds
    SHUTDOWN_REASON_USER_MENU = 0xA3,        // on-device menu shutdown
    SHUTDOWN_REASON_AUTO_LOW_BATTERY = 0x5A, // PowerFSM lowBattSDS / voltage watchdog
    SHUTDOWN_REASON_AUTO_ON_BATTERY = 0x5B,  // on_battery_shutdown_after_secs timer
    SHUTDOWN_REASON_UNKNOWN = 0xFF,          // never set (crash, brown-out, fresh boot)
};

static inline bool isUserShutdownReason(uint8_t r)
{
    return r == SHUTDOWN_REASON_USER_BUTTON || r == SHUTDOWN_REASON_USER_ADMIN || r == SHUTDOWN_REASON_USER_MENU;
}

// Set by each shutdown-initiating call site, read in cpuDeepSleep() and
// (on nRF52) by variant_shutdown() to decide which GPIO senses to arm.
extern volatile uint8_t pendingShutdownReason;

void setPendingShutdownReason(uint8_t reason);
