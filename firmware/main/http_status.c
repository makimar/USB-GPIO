#include "http_status.h"

#include <stdarg.h>
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

static esp_err_t handle_root(httpd_req_t *req)
{
    size_t len = 0;
    append(&len,
           "<!doctype html><html><head><meta charset='utf-8'>"
           "<meta http-equiv='refresh' content='2'>"
           "<title>USB GPIO Extender</title><style>"
           "body{font-family:monospace;background:#111;color:#eee;padding:1.5em}"
           "table{border-collapse:collapse}"
           "td,th{padding:.3em 1em;text-align:left;border-bottom:1px solid #333}"
           "</style></head><body>"
           "<h1>USB GPIO Extender</h1>"
           "<p>Read-only - reloads every 2s. Pins are only ever changed over USB.</p>"
           "<table><tr><th>Pin</th><th>Level</th><th>PWM</th></tr>");

    for (int pin = USBGPIO_MIN_GPIO; pin <= USBGPIO_MAX_GPIO; pin++) {
        if (pins_is_reserved(pin)) {
            continue;
        }
        int level = gpio_get_level(pin);
        int freq_hz, duty_pct;
        if (cmd_pwm_get_state(pin, &freq_hz, &duty_pct)) {
            append(&len, "<tr><td>%d</td><td>%d</td><td>%d Hz, %d%%</td></tr>", pin, level,
                   freq_hz, duty_pct);
        } else {
            append(&len, "<tr><td>%d</td><td>%d</td><td>-</td></tr>", pin, level);
        }
    }
    append(&len, "</table></body></html>");

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
