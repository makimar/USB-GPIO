// PWM, PWMSTOP.
#pragma once

#include <stdbool.h>

void cmd_pwm_start(int argc, char **argv);
void cmd_pwm_stop(int argc, char **argv);

// Stops every active PWM channel and detaches its pin. Used by RESET (in
// addition to cmd_gpio_reset_all()).
void cmd_pwm_reset_all(void);

// Reports whether `pin` currently has an active PWM channel, and if so,
// its last-configured frequency/duty. Used by the WiFi status page
// (http_status.c); the USB protocol itself never needs to query this,
// since the host already knows what it last configured.
bool cmd_pwm_get_state(int pin, int *out_freq_hz, int *out_duty_pct);
