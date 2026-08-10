#include "led_status.h"

#include "led_strip.h"

static led_strip_handle_t s_strip;

esp_err_t led_status_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_STATUS_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz, matches WS2812 bit timing
    };
    return led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
}

void led_status_set_blue(uint8_t brightness)
{
    if (s_strip == NULL) {
        return;
    }
    led_strip_set_pixel(s_strip, 0, 0, 0, brightness);
    led_strip_refresh(s_strip);
}

void led_status_off(void)
{
    if (s_strip == NULL) {
        return;
    }
    led_strip_clear(s_strip);
}
