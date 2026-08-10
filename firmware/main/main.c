// Single task: read a line from USB CDC (USB Serial/JTAG, routed to
// stdin/stdout via CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG), dispatch it, reply.
// See CLAUDE.md "Protocol" and docs/protocol.md for the command set.
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "cmd_adc.h"
#include "cmd_gpio.h"
#include "cmd_pwm.h"
#include "protocol.h"
#include "usbgpio_version.h"
#include "wifi.h"

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

// fgets() only ends a line on '\n'. Plenty of real terminals - idf.py
// monitor's default --eol=CR among them - send a bare '\r' for Enter
// instead, which a plain fgets()-based reader would simply never see as a
// complete line. Read byte-by-byte and treat '\r' and '\n' as
// interchangeable terminators; a stray paired byte just produces an empty
// next line, which handle_line() already ignores.
static void read_line(char *buf, size_t buf_size)
{
    size_t len = 0;
    while (len < buf_size - 1) {
        int c = fgetc(stdin);
        if (c == EOF) {
            clearerr(stdin); // don't let a transient EOF wedge future reads
            continue;        // no data yet; loop back and block again
        }
        if (c == '\n' || c == '\r') {
            break;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
}

// ESP_LOGx() (used internally by the WiFi/HTTP/mDNS stack once wifi_start()
// runs) shares the same stdout stream as protocol replies. A log line
// landing mid-printf() can corrupt a reply in transit - this caused a
// real, hard-to-reproduce bug (an intermittent bare "OK" with its data
// silently missing, only seen after WiFi had been running a while).
// Discarding all esp_log output costs nothing for debugging: panics and
// crash dumps write directly to the USB Serial/JTAG peripheral,
// bypassing esp_log entirely, so they're unaffected by this.
static int discard_log(const char *fmt, va_list args)
{
    (void)fmt;
    (void)args;
    return 0;
}

void app_main(void)
{
    esp_log_set_vprintf(discard_log);

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

    // NVS is required by the WiFi driver (calibration/config storage)
    // regardless of whether WiFi ends up enabled - wifi_start() itself is
    // a no-op without a configured SSID, so this is cheap either way.
    // Standard recovery path: a truncated/incompatible NVS partition
    // (first boot, or an IDF/partition-table change) fails once, gets
    // erased, and succeeds on retry.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    // Comes after the USB driver is up and READY has been sent: WiFi
    // setup must never delay or block USB availability. wifi_start()
    // itself only kicks off an async connection attempt (or does nothing,
    // if unconfigured) and returns immediately either way.
    wifi_start();

    char line[LINE_BUF_SIZE];
    while (true) {
        read_line(line, sizeof(line));
        handle_line(line);
    }
}
