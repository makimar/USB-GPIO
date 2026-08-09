// MODE, WRITE, READ, READALL, RESET.
#pragma once

void cmd_gpio_mode(int argc, char **argv);
void cmd_gpio_write(int argc, char **argv);
void cmd_gpio_read(int argc, char **argv);
void cmd_gpio_readall(int argc, char **argv);

// Returns every usable pin to input, no pull. Used by RESET (in addition to
// cmd_pwm_reset_all()).
void cmd_gpio_reset_all(void);
