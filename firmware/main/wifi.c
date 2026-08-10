#include "wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"

#include "http_status.h"
#include "led_status.h"

static const char *TAG = "wifi";

// mDNS/HTTP only need to start once; WiFi reconnects (e.g. the AP
// rebooting) shouldn't restart them.
static bool s_services_started;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        led_status_off();
        ESP_LOGW(TAG, "WiFi disconnected, retrying");
        esp_wifi_connect();
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));

    led_status_set_blue(20);

    if (s_services_started) {
        return;
    }
    s_services_started = true;

    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        mdns_hostname_set(CONFIG_USBGPIO_MDNS_HOSTNAME);
        mdns_instance_name_set("USB GPIO Extender");
    } else {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
    }

    http_status_start();
}

void wifi_start(void)
{
    if (strlen(CONFIG_USBGPIO_WIFI_SSID) == 0) {
        ESP_LOGI(TAG, "no WiFi SSID configured (idf.py menuconfig); staying USB-only");
        return;
    }

    if (led_status_init() != ESP_OK) {
        ESP_LOGW(TAG, "status LED init failed; continuing without it");
    }

    // Every step below is best-effort: a failure here must never take
    // down USB GPIO handling, so we log and bail out of WiFi setup
    // instead of ESP_ERROR_CHECK()-ing (which would abort the whole
    // device) or letting a failure cascade into the next call.
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL, NULL);

    wifi_config_t sta_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    snprintf((char *)sta_cfg.sta.ssid, sizeof(sta_cfg.sta.ssid), "%s", CONFIG_USBGPIO_WIFI_SSID);
    snprintf((char *)sta_cfg.sta.password, sizeof(sta_cfg.sta.password), "%s",
             CONFIG_USBGPIO_WIFI_PASSWORD);

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &sta_cfg) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start sequence failed; staying USB-only");
        return;
    }

    ESP_LOGI(TAG, "connecting to '%s'...", CONFIG_USBGPIO_WIFI_SSID);
}
