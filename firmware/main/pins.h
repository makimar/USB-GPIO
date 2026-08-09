// Pin reservation and validation.
//
// See CLAUDE.md, "Protocol" -> "Rules": reserved pins (USB, strapping,
// flash) must return `ERR 2 pin reserved` rather than being touched.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// ESP32-C6 has GPIO0-GPIO30.
#define USBGPIO_MIN_GPIO 0
#define USBGPIO_MAX_GPIO 30

// ADC1 is exposed on GPIO0-GPIO6 (see CLAUDE.md v1 feature scope).
#define USBGPIO_ADC_MIN_GPIO 0
#define USBGPIO_ADC_MAX_GPIO 6

// True if `pin` is a valid GPIO number for this chip (does not imply usable).
bool pins_in_range(int pin);

// True if `pin` is reserved (USB Serial/JTAG, strapping, or embedded flash)
// and must be rejected with `ERR 2 pin reserved`.
//
// NOTE: the flash-pin range assumes a module with in-package flash (e.g.
// ESP32-C6-WROOM-1, GPIO24-30). Verify against your exact module/board
// datasheet before relying on this for boards with external flash wiring.
bool pins_is_reserved(int pin);

// True if `pin` may be used for ADC (READ via ADC1, GPIO0-GPIO6) and is
// not otherwise reserved.
bool pins_is_adc_capable(int pin);
