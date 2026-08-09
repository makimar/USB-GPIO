#include "cmd_gpio.h"

#include <string.h>

#include "driver/gpio.h"

#include "pins.h"
#include "protocol.h"

// Parses and validates a pin argument shared by MODE/WRITE/READ. On failure
// it has already sent the appropriate ERR reply; caller should just return.
static bool parse_usable_pin(const char *tok, int *out_pin)
{
    long val;
    if (!protocol_parse_int(tok, &val)) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "pin must be an integer");
        return false;
    }
    if (!pins_in_range((int)val)) {
        protocol_reply_err(PROTO_ERR_INVALID_PIN, "pin out of range");
        return false;
    }
    if (pins_is_reserved((int)val)) {
        protocol_reply_err(PROTO_ERR_PIN_RESERVED, "pin reserved");
        return false;
    }
    *out_pin = (int)val;
    return true;
}

void cmd_gpio_mode(int argc, char **argv)
{
    // MODE <pin> <in|in_pu|in_pd|out|out_od>
    if (argc != 3) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "usage: MODE <pin> <in|in_pu|in_pd|out|out_od>");
        return;
    }
    int pin;
    if (!parse_usable_pin(argv[1], &pin)) {
        return;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    const char *mode = argv[2];
    if (strcmp(mode, "in") == 0) {
        cfg.mode = GPIO_MODE_INPUT;
    } else if (strcmp(mode, "in_pu") == 0) {
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    } else if (strcmp(mode, "in_pd") == 0) {
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    } else if (strcmp(mode, "out") == 0) {
        // INPUT_OUTPUT, not plain OUTPUT: GPIO_MODE_OUTPUT disables the
        // input buffer, which would make READ/READALL unable to see what
        // the pin is actually driving.
        cfg.mode = GPIO_MODE_INPUT_OUTPUT;
    } else if (strcmp(mode, "out_od") == 0) {
        cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    } else {
        protocol_reply_err(PROTO_ERR_INVALID_MODE, "unknown mode");
        return;
    }

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "gpio_config failed");
        return;
    }
    protocol_reply_ok(NULL);
}

void cmd_gpio_write(int argc, char **argv)
{
    // WRITE <pin> <0|1>
    if (argc != 3) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "usage: WRITE <pin> <0|1>");
        return;
    }
    int pin;
    if (!parse_usable_pin(argv[1], &pin)) {
        return;
    }
    long level;
    if (!protocol_parse_int(argv[2], &level) || (level != 0 && level != 1)) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "level must be 0 or 1");
        return;
    }

    esp_err_t err = gpio_set_level(pin, (uint32_t)level);
    if (err != ESP_OK) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "gpio_set_level failed");
        return;
    }
    protocol_reply_ok(NULL);
}

void cmd_gpio_read(int argc, char **argv)
{
    // READ <pin> -> OK 0|1
    if (argc != 2) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "usage: READ <pin>");
        return;
    }
    int pin;
    if (!parse_usable_pin(argv[1], &pin)) {
        return;
    }
    int level = gpio_get_level(pin);
    protocol_reply_ok_fmt("%d", level);
}

void cmd_gpio_readall(int argc, char **argv)
{
    // READALL -> OK <hex bitmask>, bit N holds GPIO N's level (0 for
    // reserved/unusable pins).
    (void)argv;
    if (argc != 1) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "usage: READALL");
        return;
    }
    uint32_t mask_lo = 0; // pins 0-31
    for (int pin = USBGPIO_MIN_GPIO; pin <= USBGPIO_MAX_GPIO; pin++) {
        if (pins_is_reserved(pin)) {
            continue;
        }
        if (gpio_get_level(pin)) {
            mask_lo |= (1UL << pin);
        }
    }
    protocol_reply_ok_fmt("%lx", (unsigned long)mask_lo);
}

void cmd_gpio_reset_all(void)
{
    for (int pin = USBGPIO_MIN_GPIO; pin <= USBGPIO_MAX_GPIO; pin++) {
        if (pins_is_reserved(pin)) {
            continue;
        }
        gpio_reset_pin(pin); // back to input, no pull (IDF default reset state)
    }
}
