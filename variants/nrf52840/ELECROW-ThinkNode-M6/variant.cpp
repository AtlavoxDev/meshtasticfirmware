/*
  Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.
  Copyright (c) 2018, Adafruit Industries (adafruit.com)

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "variant.h"
#include "ShutdownReason.h"
#include "nrf.h"
#include "power.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    // P0 - pins 0 and 1 are hardwired for xtal and should never be enabled
    0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

void initVariant()
{
    // Wake-source check. The user's stated rule:
    //   "If the boot was caused by an intentional button press → boot.
    //    If not → only boot if the previous shutdown was unintentional."
    //
    // We can't read RESETREAS or NRF_GPIO->LATCH to learn what woke the chip
    // (the Adafruit UF2 bootloader clears both before jumping to the app), so
    // we use a behavioral proxy: poll PIN_BUTTON1 for a brief window right
    // here at the very start of initVariant(). If the user is holding the
    // button, we'll see it go LOW during the sample window and we allow the
    // boot. If we never see LOW, the wake was caused by something else (USB
    // VBUS detect, brown-out, NFC field) and we bounce back to SYSTEM_OFF.
    //
    // This means the UX is "hold the button briefly to power on" - a quick
    // tap that releases before this code runs (~200-600ms after the wake)
    // will not be detected. That's the trade-off the user has accepted in
    // exchange for a simple, consistent, easy-to-reason-about rule.
    //
    // Skipped when the previous shutdown was NOT user-initiated (auto low-
    // battery, auto on-battery, or cold-boot UNKNOWN) - those should always
    // boot. GPREGRET[1] survives SYSTEM_OFF; nrf52Setup() clears it later.
    uint8_t prevReason = NRF_POWER->GPREGRET2 & 0xFF;
    if (isUserShutdownReason(prevReason)) {
        pinMode(PIN_BUTTON1, INPUT_PULLUP);
        bool buttonPressDetected = false;
        // ~500ms total window: 100 iterations * 5ms each.
        for (int i = 0; i < 100; i++) {
            if (digitalRead(PIN_BUTTON1) == LOW) {
                buttonPressDetected = true;
                break;
            }
            delay(5);
        }

        if (!buttonPressDetected) {
            // Wake was not from a button press. Bounce back to SYSTEM_OFF
            // silently. Turn off any LEDs the bootloader may have lit, re-arm
            // SENSE_LOW on the button so the next press wakes us, then write
            // SYSTEMOFF. GPREGRET[1] is intentionally left untouched - the
            // next wake re-evaluates with the same prevReason.
#ifdef LED_POWER
            pinMode(LED_POWER, OUTPUT);
            digitalWrite(LED_POWER, LED_STATE_OFF);
#endif
#ifdef LED_PAIRING
            pinMode(LED_PAIRING, OUTPUT);
            digitalWrite(LED_PAIRING, LED_STATE_OFF);
#endif
            nrf_gpio_cfg_input(PIN_BUTTON1, NRF_GPIO_PIN_PULLUP);
            nrf_gpio_cfg_sense_set(PIN_BUTTON1, NRF_GPIO_PIN_SENSE_LOW);
            NRF_POWER->SYSTEMOFF = 1;
            while (true) {
            }
        }
    }

    pinMode(LED_PAIRING, OUTPUT);
    ledOff(LED_PAIRING);

    pinMode(VDD_FLASH_EN, OUTPUT);
    digitalWrite(VDD_FLASH_EN, HIGH);
}

// called from main-nrf52.cpp during the cpuDeepSleep() function
void variant_shutdown()
{
    // This sets the pin to OUTPUT and LOW for the pins *not* in the if block.
    // EXT_PWR_DETECT and EXT_CHRG_DETECT are skipped here because they are
    // explicitly reconfigured as inputs below for the auto-recovery branch.
    for (int pin = 0; pin < 48; pin++) {
        if (pin == PIN_GPS_EN || pin == ADC_CTRL || pin == PIN_BUTTON1 || pin == EXT_PWR_DETECT || pin == EXT_CHRG_DETECT ||
            pin == PIN_SPI_MISO || pin == PIN_SPI_MOSI || pin == PIN_SPI_SCK || pin == SX126X_CS || pin == SX126X_RESET ||
            pin == SX126X_BUSY || pin == SX126X_DIO1) {
            continue;
        }
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        if (pin >= 32) {
            NRF_P1->DIRCLR = (1 << (pin - 32));
        } else {
            NRF_GPIO->DIRCLR = (1 << pin);
        }
    }

    digitalWrite(PIN_GPS_EN, LOW);
    digitalWrite(ADC_CTRL, LOW);
    // digitalWrite(RTC_POWER, LOW);

    // Button wake is always armed - pressing the button is how the user turns the device back on.
    nrf_gpio_cfg_input(PIN_BUTTON1, NRF_GPIO_PIN_PULLUP);
    nrf_gpio_cfg_sense_set(PIN_BUTTON1, NRF_GPIO_PIN_SENSE_LOW);

    // For unintentional shutdowns (low battery, on-battery timeout, unknown/crash) also wake
    // when external power returns - either solar charging or USB plug-in. For intentional user
    // shutdowns (button long-press, admin command, menu), button is the only wake path so the
    // user can charge on USB or leave the device in sun without bringing the radio back online.
    if (!isUserShutdownReason(pendingShutdownReason)) {
        // Solar charger active (EXT_CHRG_DETECT_VALUE == LOW per variant.h)
        nrf_gpio_cfg_input(EXT_CHRG_DETECT, NRF_GPIO_PIN_PULLUP);
        nrf_gpio_cfg_sense_set(EXT_CHRG_DETECT, NRF_GPIO_PIN_SENSE_LOW);

        // USB plugged in (EXT_PWR_DETECT idles low when unplugged, goes HIGH on VBUS)
        nrf_gpio_cfg_input(EXT_PWR_DETECT, NRF_GPIO_PIN_PULLDOWN);
        nrf_gpio_cfg_sense_set(EXT_PWR_DETECT, NRF_GPIO_PIN_SENSE_HIGH);
    }
}
