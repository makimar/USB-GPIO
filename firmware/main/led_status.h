// Thin wrapper around the onboard addressable status LED (WS2812, driven
// via the `led_strip` component over RMT). Not part of the USB protocol -
// purely a visual "am I connected to WiFi" indicator.
#pragma once

#include <stdint.h>

#include "esp_err.h"

// GPIO the onboard status LED is wired to (ESP32-C6-DevKitM-1; also
// happens to be correct for the similarly-named DevKitC-1). This is also
// a strapping pin, so pins.c already rejects it via MODE/WRITE - this
// module doesn't need to check that itself, since it drives the pin
// directly over RMT, bypassing the GPIO command path entirely.
#define LED_STATUS_GPIO 8

// Initializes the LED strip driver. Safe to call even if no LED is
// present on this pin - failures are returned, not asserted, so a caller
// can log and continue without one.
esp_err_t led_status_init(void);

// Sets the LED to solid blue at `brightness` (0-255).
void led_status_set_blue(uint8_t brightness);

// Turns the LED off.
void led_status_off(void);
