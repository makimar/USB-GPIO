#include "cmd_pwm.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "pins.h"
#include "protocol.h"

// LEDC duty resolution used for every channel. 10 bits (1023 steps) is
// enough for 0-100% duty at the audio/motor-control frequencies this
// project targets, and keeps the max usable frequency comfortably high.
#define PWM_DUTY_RESOLUTION LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY ((1 << 10) - 1)

// The C6 has LEDC_CHANNEL_MAX channels but only LEDC_TIMER_MAX timers, so
// channel N shares its timer with channel (N + LEDC_TIMER_MAX). Two active
// channels on the same timer cannot run independent frequencies -
// configuring one silently changes the other's. Documented in
// docs/protocol.md; v1 accepts this as a hardware limit.
static int channel_for_pin[USBGPIO_MAX_GPIO + 1]; // -1 if pin has no active channel
static int pin_for_channel[LEDC_CHANNEL_MAX];      // -1 if channel is free
static int pin_freq_hz[USBGPIO_MAX_GPIO + 1];      // valid only where channel_for_pin[pin] != -1
static int pin_duty_pct[USBGPIO_MAX_GPIO + 1];     // ditto
static bool initialized;

static void lazy_init(void)
{
    if (initialized) {
        return;
    }
    for (int i = 0; i <= USBGPIO_MAX_GPIO; i++) {
        channel_for_pin[i] = -1;
    }
    for (int i = 0; i < LEDC_CHANNEL_MAX; i++) {
        pin_for_channel[i] = -1;
    }
    initialized = true;
}

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

static int find_free_channel(void)
{
    for (int i = 0; i < LEDC_CHANNEL_MAX; i++) {
        if (pin_for_channel[i] == -1) {
            return i;
        }
    }
    return -1;
}

void cmd_pwm_start(int argc, char **argv)
{
    // PWM <pin> <freq_hz> <duty_pct>
    lazy_init();
    if (argc != 4) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "usage: PWM <pin> <freq_hz> <duty_pct>");
        return;
    }
    int pin;
    if (!parse_usable_pin(argv[1], &pin)) {
        return;
    }
    long freq_hz, duty_pct;
    if (!protocol_parse_int(argv[2], &freq_hz) || freq_hz <= 0) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "freq_hz must be a positive integer");
        return;
    }
    if (!protocol_parse_int(argv[3], &duty_pct) || duty_pct < 0 || duty_pct > 100) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "duty_pct must be 0-100");
        return;
    }

    int channel = channel_for_pin[pin];
    if (channel == -1) {
        channel = find_free_channel();
        if (channel == -1) {
            protocol_reply_err(PROTO_ERR_NO_PWM_CHANNEL, "no free pwm channel");
            return;
        }
    }
    int timer_sel = channel % LEDC_TIMER_MAX;

    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = PWM_DUTY_RESOLUTION,
        .timer_num = timer_sel,
        .freq_hz = (uint32_t)freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&tcfg) != ESP_OK) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "unsupported freq_hz for this resolution");
        return;
    }

    uint32_t duty = (uint32_t)((duty_pct * PWM_MAX_DUTY + 50) / 100);
    ledc_channel_config_t ccfg = {
        .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel,
        .timer_sel = timer_sel,
        .duty = duty,
        .hpoint = 0,
    };
    if (ledc_channel_config(&ccfg) != ESP_OK) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "ledc_channel_config failed");
        return;
    }

    channel_for_pin[pin] = channel;
    pin_for_channel[channel] = pin;
    pin_freq_hz[pin] = (int)freq_hz;
    pin_duty_pct[pin] = (int)duty_pct;
    protocol_reply_ok(NULL);
}

void cmd_pwm_stop(int argc, char **argv)
{
    // PWMSTOP <pin>
    lazy_init();
    if (argc != 2) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "usage: PWMSTOP <pin>");
        return;
    }
    int pin;
    if (!parse_usable_pin(argv[1], &pin)) {
        return;
    }
    int channel = channel_for_pin[pin];
    if (channel == -1) {
        protocol_reply_err(PROTO_ERR_PWM_NOT_ACTIVE, "pin has no active pwm");
        return;
    }
    ledc_stop(LEDC_LOW_SPEED_MODE, channel, 0);
    gpio_reset_pin(pin);
    channel_for_pin[pin] = -1;
    pin_for_channel[channel] = -1;
    protocol_reply_ok(NULL);
}

void cmd_pwm_reset_all(void)
{
    lazy_init();
    for (int channel = 0; channel < LEDC_CHANNEL_MAX; channel++) {
        int pin = pin_for_channel[channel];
        if (pin == -1) {
            continue;
        }
        ledc_stop(LEDC_LOW_SPEED_MODE, channel, 0);
        gpio_reset_pin(pin);
        channel_for_pin[pin] = -1;
        pin_for_channel[channel] = -1;
    }
}

bool cmd_pwm_get_state(int pin, int *out_freq_hz, int *out_duty_pct)
{
    lazy_init();
    if (pin < 0 || pin > USBGPIO_MAX_GPIO || channel_for_pin[pin] == -1) {
        return false;
    }
    *out_freq_hz = pin_freq_hz[pin];
    *out_duty_pct = pin_duty_pct[pin];
    return true;
}
