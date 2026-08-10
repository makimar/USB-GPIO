#include "pins.h"

static const int USB_JTAG_PINS[] = {12, 13};
// Full ESP32-C6 strapping set (chip-level, confirmed against esptool's
// boot-mode-selection docs) - GPIO8/GPIO9 select boot mode; GPIO4/GPIO5
// and GPIO15 (JTAG signal source selection) are also strapping and were
// missing here originally.
static const int STRAPPING_PINS[] = {4, 5, 8, 9, 15};
static const int FLASH_PINS[] = {24, 25, 26, 27, 28, 29, 30};

static bool in_list(int pin, const int *list, int len)
{
    for (int i = 0; i < len; i++) {
        if (list[i] == pin) {
            return true;
        }
    }
    return false;
}

bool pins_in_range(int pin)
{
    return pin >= USBGPIO_MIN_GPIO && pin <= USBGPIO_MAX_GPIO;
}

bool pins_is_reserved(int pin)
{
    if (in_list(pin, USB_JTAG_PINS, sizeof(USB_JTAG_PINS) / sizeof(USB_JTAG_PINS[0]))) {
        return true;
    }
    if (in_list(pin, STRAPPING_PINS, sizeof(STRAPPING_PINS) / sizeof(STRAPPING_PINS[0]))) {
        return true;
    }
    if (in_list(pin, FLASH_PINS, sizeof(FLASH_PINS) / sizeof(FLASH_PINS[0]))) {
        return true;
    }
    return false;
}

bool pins_is_adc_capable(int pin)
{
    return pin >= USBGPIO_ADC_MIN_GPIO && pin <= USBGPIO_ADC_MAX_GPIO && !pins_is_reserved(pin);
}
