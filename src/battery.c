#include "battery.h"
#include "esp_adc_cal.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/adc.h"
#include "esp_log.h"

static const char *TAG = "battery";
#define BATTERY_ADC_CHANNEL ADC1_CHANNEL_6
#define BATTERY_ADC_SAMPLES 64
static esp_adc_cal_characteristics_t adc_chars;

void battery_init()
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_12);
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGW("battery", "ADC cal eFuse Two Point: %lu", adc_chars.vref);
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI("battery", "ADC cal eFuse Vref: %lu", adc_chars.vref);
    } else {
        ESP_LOGE("battery", "ADC cal Default Vref: %lu", adc_chars.vref);
    }
}

uint8_t battery_get_charge()
{
    int charge = 0;
    uint32_t adc_reading = 0;
    for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) {
        adc_reading += adc1_get_raw(BATTERY_ADC_CHANNEL);
    }
    adc_reading /= BATTERY_ADC_SAMPLES;
    ESP_LOGD(TAG, "ADC: %lu", adc_reading);
    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
    ESP_LOGD(TAG, "Voltage: %lu", voltage);
    charge = ((int)voltage - 1500) * 100 / 600; // 1500mV is 0% and 2100mV is 100% (4.2V / 2 by divider)
    if (charge > 99) charge = 99; else if (charge < 0) charge = 0;
    ESP_LOGI(TAG, "Battery charge: %d%%, voltage: %lumV", charge, voltage*2);

    return (uint8_t)(charge & 0xff);
}