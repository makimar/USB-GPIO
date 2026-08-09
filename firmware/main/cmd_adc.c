#include "cmd_adc.h"

#include <stddef.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#include "pins.h"
#include "protocol.h"

// 12 dB attenuation gives the widest input range (~0-3.3V); see
// docs/protocol.md for the exact usable range and accuracy caveats.
#define ADC_ATTEN ADC_ATTEN_DB_12

static adc_oneshot_unit_handle_t s_unit;
static adc_cali_handle_t s_cali[USBGPIO_ADC_MAX_GPIO - USBGPIO_ADC_MIN_GPIO + 1];
static bool s_cali_ok[USBGPIO_ADC_MAX_GPIO - USBGPIO_ADC_MIN_GPIO + 1];
static bool s_chan_configured[USBGPIO_ADC_MAX_GPIO - USBGPIO_ADC_MIN_GPIO + 1];

static esp_err_t ensure_unit(void)
{
    if (s_unit != NULL) {
        return ESP_OK;
    }
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    return adc_oneshot_new_unit(&init_cfg, &s_unit);
}

static esp_err_t ensure_channel(int pin)
{
    int idx = pin - USBGPIO_ADC_MIN_GPIO;
    if (s_chan_configured[idx]) {
        return ESP_OK;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t err = adc_oneshot_config_channel(s_unit, (adc_channel_t)idx, &chan_cfg);
    if (err != ESP_OK) {
        return err;
    }
    s_chan_configured[idx] = true;

    // Calibration is best-effort: on chips/eFuse combos without a scheme,
    // adc_cali_create_scheme_curve_fitting() fails and we fall back to an
    // uncalibrated linear approximation in cmd_adc_read().
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = (adc_channel_t)idx,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok[idx] = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali[idx]) == ESP_OK);
    return ESP_OK;
}

void cmd_adc_read(int argc, char **argv)
{
    // ADC <pin> -> OK <raw 0-4095> <millivolts>
    if (argc != 2) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "usage: ADC <pin>");
        return;
    }
    long val;
    if (!protocol_parse_int(argv[1], &val)) {
        protocol_reply_err(PROTO_ERR_BAD_ARGS, "pin must be an integer");
        return;
    }
    int pin = (int)val;
    if (!pins_in_range(pin)) {
        protocol_reply_err(PROTO_ERR_INVALID_PIN, "pin out of range");
        return;
    }
    if (!pins_is_adc_capable(pin)) {
        protocol_reply_err(PROTO_ERR_INVALID_PIN, "pin has no ADC1 channel");
        return;
    }

    if (ensure_unit() != ESP_OK || ensure_channel(pin) != ESP_OK) {
        protocol_reply_err(PROTO_ERR_ADC_UNAVAILABLE, "adc init failed");
        return;
    }

    int idx = pin - USBGPIO_ADC_MIN_GPIO;
    int raw = 0;
    if (adc_oneshot_read(s_unit, (adc_channel_t)idx, &raw) != ESP_OK) {
        protocol_reply_err(PROTO_ERR_ADC_UNAVAILABLE, "adc read failed");
        return;
    }

    int millivolts;
    if (s_cali_ok[idx] && adc_cali_raw_to_voltage(s_cali[idx], raw, &millivolts) == ESP_OK) {
        // calibrated
    } else {
        // Uncalibrated fallback: linear approximation over the 12-bit range
        // at ~3300mV full scale for ADC_ATTEN_DB_12. Less accurate than the
        // calibrated path; see docs/protocol.md.
        millivolts = (raw * 3300) / 4095;
    }

    protocol_reply_ok_fmt("%d %d", raw, millivolts);
}
