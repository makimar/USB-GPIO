// Single task: read a line from USB CDC (USB Serial/JTAG, routed to
// stdin/stdout via CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG), dispatch it, reply.
// See CLAUDE.md "Protocol" and docs/protocol.md for the command set.
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

#include "cmd_adc.h"
#include "cmd_gpio.h"
#include "cmd_pwm.h"
#include "protocol.h"
#include "usbgpio_version.h"

#define LINE_BUF_SIZE 128

static void handle_line(char *line)
{
    char *argv[PROTO_MAX_ARGS];
    int argc = protocol_tokenize(line, argv, PROTO_MAX_ARGS);
    if (argc == 0) {
        return; // blank line, no reply (keeps re-sync after a stray \n cheap)
    }

    const char *cmd = argv[0];
    if (strcmp(cmd, "MODE") == 0) {
        cmd_gpio_mode(argc, argv);
    } else if (strcmp(cmd, "WRITE") == 0) {
        cmd_gpio_write(argc, argv);
    } else if (strcmp(cmd, "READ") == 0) {
        cmd_gpio_read(argc, argv);
    } else if (strcmp(cmd, "READALL") == 0) {
        cmd_gpio_readall(argc, argv);
    } else if (strcmp(cmd, "PWM") == 0) {
        cmd_pwm_start(argc, argv);
    } else if (strcmp(cmd, "PWMSTOP") == 0) {
        cmd_pwm_stop(argc, argv);
    } else if (strcmp(cmd, "ADC") == 0) {
        cmd_adc_read(argc, argv);
    } else if (strcmp(cmd, "VERSION") == 0) {
        protocol_reply_ok("usbgpio " USBGPIO_VERSION);
    } else if (strcmp(cmd, "RESET") == 0) {
        cmd_pwm_reset_all();
        cmd_gpio_reset_all();
        protocol_reply_ok(NULL);
    } else {
        protocol_reply_err(PROTO_ERR_UNKNOWN_COMMAND, "unknown command");
    }
}

void app_main(void)
{
    // CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG alone leaves stdin/stdout on the
    // ROM's polling console implementation, whose blocking fgets() never
    // yields to the scheduler - with nothing to read, that starves the
    // IDLE0 task long enough to trip the task watchdog. Installing the
    // driver and switching the VFS to it makes reads properly
    // interrupt-driven/blocking instead.
    usb_serial_jtag_driver_config_t usj_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usj_cfg));
    usb_serial_jtag_vfs_use_driver();

    // Host must tolerate this banner (CLAUDE.md "Protocol" -> "Rules").
    printf("READY\n");
    fflush(stdout);

    char line[LINE_BUF_SIZE];
    while (true) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            continue; // no data yet; USB CDC read just timed out
        }
        handle_line(line);
    }
}
