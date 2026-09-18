#include "http_status.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "cmd_pwm.h"
#include "pins.h"

static const char *TAG = "http_status";

// Shared render buffer. Handlers all run in esp_http_server's single
// worker task, but the SSE broadcast task below renders into this same
// buffer from its own context, so every use (render + send) must hold
// s_lock.
static char s_html[8192];
static SemaphoreHandle_t s_lock;

// Connected SSE clients (`GET /events`), kept alive across requests via
// esp_http_server's async-handler API. Slots are guarded by s_lock.
#define SSE_MAX_CLIENTS 4
#define SSE_INTERVAL_MS 2000
static httpd_req_t *s_sse_clients[SSE_MAX_CLIENTS];

// One row of a pin header, top to bottom as physically silkscreened.
// `label` is set for non-GPIO pins (power/ground/no-connect); NULL means
// this row is a GPIO and `gpio` is the pin number. `note` is an optional
// annotation for GPIOs with a fixed onboard role.
typedef struct {
    const char *label;
    int gpio;
    const char *note;
} header_pin_t;

// ESP32-C6-DevKitM-1 J1 (left) and J3 (right) headers, pins 1-15
// top-to-bottom, straight from Espressif's official user guide:
// https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitm-1/user_guide.html
// (verified against a photo of the actual board - it's DevKitM-1, not
// the very similarly-named DevKitC-1, which has a different pin set:
// notably GPIO14 vs GPIO10/GPIO11). If you're on different hardware,
// these two tables are the only thing that needs updating - everything
// else queries pins generically.
static const header_pin_t J1_LEFT[] = {
    {"3V3", -1, NULL},        {"RST", -1, NULL}, {NULL, 2, NULL},
    {NULL, 3, NULL},          {NULL, 4, "strapping"}, {NULL, 5, "strapping"},
    {NULL, 0, NULL},          {NULL, 1, NULL},   {NULL, 8, "status LED"},
    {NULL, 6, NULL},          {NULL, 7, NULL},   {NULL, 14, NULL},
    {"GND", -1, NULL},        {"5V", -1, NULL},  {"GND", -1, NULL},
};

static const header_pin_t J3_RIGHT[] = {
    {"GND", -1, NULL},        {NULL, 16, "U0TXD"}, {NULL, 17, "U0RXD"},
    {NULL, 23, NULL},         {NULL, 22, NULL},    {NULL, 21, NULL},
    {NULL, 20, NULL},         {NULL, 19, NULL},    {NULL, 18, NULL},
    {NULL, 15, "strapping"},  {NULL, 9, "strapping"}, {"GND", -1, NULL},
    {NULL, 13, "USB D+"},     {NULL, 12, "USB D-"}, {"GND", -1, NULL},
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

// The two header tables - both the initial page render and every SSE
// event carry exactly this markup, so the page can swap it in wholesale.
// Deliberately single-line (no '\n' anywhere): an SSE `data:` payload
// ends at the first newline.
static void render_board(size_t *len)
{
    append_header_table(len, "J1 (left)", J1_LEFT, sizeof(J1_LEFT) / sizeof(J1_LEFT[0]));
    append_header_table(len, "J3 (right)", J3_RIGHT, sizeof(J3_RIGHT) / sizeof(J3_RIGHT[0]));
}

static void render_sse_frame(size_t *len)
{
    *len = 0;
    append(len, "data: ");
    render_board(len);
    append(len, "\n\n");
}

static esp_err_t handle_root(httpd_req_t *req)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    size_t len = 0;
    append(&len,
           "<!doctype html><html><head><meta charset='utf-8'>"
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
           "<p>Read-only, live over SSE (<span id='conn'>connecting...</span>). "
           "Pins are only ever changed over USB. "
           "Layout matches the ESP32-C6-DevKitM-1 J1/J3 headers, top to bottom.</p>"
           "<div class='board' id='board'>");

    render_board(&len);

    append(&len,
           "</div><script>"
           "var es=new EventSource('/events');"
           "var conn=document.getElementById('conn');"
           "es.onopen=function(){conn.textContent='live';};"
           "es.onerror=function(){conn.textContent='reconnecting...';};"
           "es.onmessage=function(e){document.getElementById('board').innerHTML=e.data;};"
           "</script></body></html>");

    esp_err_t err = httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (err == ESP_OK) {
        err = httpd_resp_send(req, s_html,
                              (ssize_t)(len < sizeof(s_html) ? len : sizeof(s_html) - 1));
    }

    xSemaphoreGive(s_lock);
    return err;
}

static esp_err_t handle_events(httpd_req_t *req)
{
    // Handlers run one at a time (single httpd worker), so scanning and
    // later claiming a slot can't race another handler - only the
    // broadcast task, hence the lock.
    int slot = -1;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
        if (s_sse_clients[i] == NULL) {
            slot = i;
            break;
        }
    }
    xSemaphoreGive(s_lock);

    if (slot < 0) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "too many SSE clients");
    }

    // Detach the request from the worker so the socket stays open after
    // this handler returns; all further sends go through the async copy.
    httpd_req_t *async = NULL;
    if (httpd_req_async_handler_begin(req, &async) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "async handler begin failed");
    }

    httpd_resp_set_type(async, "text/event-stream");
    httpd_resp_set_hdr(async, "Cache-Control", "no-cache");

    // First event right away so the page has data before the first tick.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t len;
    render_sse_frame(&len);
    esp_err_t err = httpd_resp_send_chunk(async, s_html, (ssize_t)len);
    if (err == ESP_OK) {
        s_sse_clients[slot] = async;
    }
    xSemaphoreGive(s_lock);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SSE client rejected at first send: %s", esp_err_to_name(err));
        httpd_req_async_handler_complete(async);
    }
    return ESP_OK;
}

static void sse_broadcast_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SSE_INTERVAL_MS));

        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool any = false;
        for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
            if (s_sse_clients[i] != NULL) {
                any = true;
                break;
            }
        }
        if (any) {
            size_t len;
            render_sse_frame(&len);
            for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
                if (s_sse_clients[i] == NULL) {
                    continue;
                }
                // A failed send is how we learn a client went away (tab
                // closed, laptop asleep, socket LRU-purged): drop it and
                // free the async request. Browsers reconnect on their own.
                if (httpd_resp_send_chunk(s_sse_clients[i], s_html, (ssize_t)len) != ESP_OK) {
                    httpd_req_async_handler_complete(s_sse_clients[i]);
                    s_sse_clients[i] = NULL;
                }
            }
        }
        xSemaphoreGive(s_lock);
    }
}

void http_status_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE(TAG, "mutex create failed");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // With up to SSE_MAX_CLIENTS sockets parked open indefinitely,
    // let the server reclaim the least-recently-active one instead of
    // refusing new connections outright; a purged SSE socket surfaces
    // as a failed send above and the browser reconnects.
    config.lru_purge_enable = true;

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

    httpd_uri_t events = {
        .uri = "/events",
        .method = HTTP_GET,
        .handler = handle_events,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &events);

    // Same priority as the main (USB protocol) task, and it spends its
    // life blocked in vTaskDelay - it must never starve USB handling
    // (CLAUDE.md, "WiFi status").
    if (xTaskCreate(sse_broadcast_task, "sse_bcast", 4096, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "SSE broadcast task create failed");
    }
}
