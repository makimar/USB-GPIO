#include "http_status.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "cmd_pwm.h"
#include "pins.h"

static const char *TAG = "http_status";

// esp_http_server's default config serves requests from a single worker
// task, so a static (not per-request stack) buffer is safe here without
// a lock - there's never more than one handler running at a time.
static char s_html[8192];

// One row of a pin header, top to bottom as physically silkscreened.
// `label` is set for non-GPIO pins (power/ground/no-connect); NULL means
// this row is a GPIO and `gpio` is the pin number. `note` is an optional
// annotation for GPIOs with a fixed onboard role.
typedef struct {
    const char *label;
    int gpio;
    const char *note;
} header_pin_t;

// ESP32-C6-DevKitC-1 J1 (left) and J3 (right) headers, pins 1-16
// top-to-bottom, straight from Espressif's official user guide:
// https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html
// If you're on different hardware, these two tables are the only thing
// that needs updating - everything else queries pins generically.
static const header_pin_t J1_LEFT[] = {
    {"3V3", -1, NULL}, {"RST", -1, NULL},         {NULL, 4, NULL}, {NULL, 5, NULL},
    {NULL, 6, NULL},   {NULL, 7, NULL},            {NULL, 0, NULL}, {NULL, 1, NULL},
    {NULL, 8, "status LED"}, {NULL, 10, NULL},     {NULL, 11, NULL}, {NULL, 2, NULL},
    {NULL, 3, NULL},   {"5V", -1, NULL},           {"GND", -1, NULL}, {"NC", -1, NULL},
};

static const header_pin_t J3_RIGHT[] = {
    {"GND", -1, NULL}, {NULL, 16, "U0TXD"},        {NULL, 17, "U0RXD"}, {NULL, 15, NULL},
    {NULL, 23, NULL},  {NULL, 22, NULL},            {NULL, 21, NULL},   {NULL, 20, NULL},
    {NULL, 19, NULL},  {NULL, 18, NULL},            {NULL, 9, NULL},    {"GND", -1, NULL},
    {NULL, 13, "USB D+"}, {NULL, 12, "USB D-"},     {"GND", -1, NULL},  {"NC", -1, NULL},
};

static void append(size_t *len, const char *fmt, ...)
{
    // Clamp instead of subtracting directly: once *len reaches
    // sizeof(s_html), `sizeof(s_html) - *len` would underflow (both are
    // size_t) and hand vsnprintf a huge bogus size.
    size_t remaining = (*len < sizeof(s_html)) ? sizeof(s_html) - *len : 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s_html + *len, remaining, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *len += (size_t)n;
    }
}

static void append_pin_row(size_t *len, const header_pin_t *p)
{
    if (p->label != NULL) {
        append(len, "<tr class='fixed'><td>%s</td><td>-</td></tr>", p->label);
        return;
    }

    int pin = p->gpio;
    if (pins_is_reserved(pin)) {
        // Reserved pins (strapping/USB/flash) are never touched via
        // MODE/WRITE, so their electrical level isn't meaningful GPIO
        // status - show the reason instead of a misleading 0/1.
        append(len, "<tr class='reserved'><td>GPIO%d</td><td>reserved%s%s</td></tr>", pin,
               p->note ? " - " : "", p->note ? p->note : "");
        return;
    }

    int level = gpio_get_level(pin);
    int freq_hz, duty_pct;
    if (cmd_pwm_get_state(pin, &freq_hz, &duty_pct)) {
        append(len, "<tr><td>GPIO%d</td><td>%d (%d Hz, %d%%)</td></tr>", pin, level, freq_hz,
               duty_pct);
    } else {
        append(len, "<tr><td>GPIO%d</td><td>%d</td></tr>", pin, level);
    }
}

static void append_header_table(size_t *len, const char *title, const header_pin_t *rows,
                                 size_t count)
{
    append(len, "<table><tr><th colspan='2'>%s</th></tr><tr><th>Pin</th><th>Status</th></tr>",
           title);
    for (size_t i = 0; i < count; i++) {
        append_pin_row(len, &rows[i]);
    }
    append(len, "</table>");
}

static esp_err_t handle_root(httpd_req_t *req)
{
    size_t len = 0;
    append(&len,
           "<!doctype html><html><head><meta charset='utf-8'>"
           "<meta http-equiv='refresh' content='2'>"
           "<title>USB GPIO Extender</title><style>"
           "body{font-family:monospace;background:#111;color:#eee;padding:1.5em}"
           "h1{margin-bottom:.2em}"
           "p{color:#999;margin-top:0}"
           ".board{display:flex;gap:2em;flex-wrap:wrap}"
           "table{border-collapse:collapse}"
           "th{text-align:left;padding:.3em 1em;border-bottom:1px solid #555}"
           "td{padding:.25em 1em;border-bottom:1px solid #333}"
           ".fixed td{color:#777}"
           ".reserved td{color:#a66}"
           "</style></head><body>"
           "<h1>USB GPIO Extender</h1>"
           "<p>Read-only, reloads every 2s. Pins are only ever changed over USB. "
           "Layout matches the ESP32-C6-DevKitC-1 J1/J3 headers, top to bottom.</p>"
           "<div class='board'>");

    append_header_table(&len, "J1 (left)", J1_LEFT, sizeof(J1_LEFT) / sizeof(J1_LEFT[0]));
    append_header_table(&len, "J3 (right)", J3_RIGHT, sizeof(J3_RIGHT) / sizeof(J3_RIGHT[0]));

    append(&len, "</div></body></html>");

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, s_html, (ssize_t)(len < sizeof(s_html) ? len : sizeof(s_html) - 1));
}

void http_status_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_root,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &root);
}
