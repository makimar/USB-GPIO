// PWM, PWMSTOP.
#pragma once

void cmd_pwm_start(int argc, char **argv);
void cmd_pwm_stop(int argc, char **argv);

// Stops every active PWM channel and detaches its pin. Used by RESET (in
// addition to cmd_gpio_reset_all()).
void cmd_pwm_reset_all(void);
