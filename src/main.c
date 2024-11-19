#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_private/esp_clk.h"
#include "esp_sleep.h"
#include "sys/time.h"
#include "esp_pm.h"
#include "esp_littlefs.h"
#include "dirent.h"
#include "display.h"
#include "file_roller.h"
#include "bluetooth.h"
#include "touchpad.h"
#include "apps_controller.h"
#include "menu.h"
#include "app_bluetooth.h"
#include "app_barcode.h"
#include "sleep.h"
#include "battery.h"

#define GPIO_LED 22
static void gpio_init();
static void power_init();
static void nvs_init();
static void fs_init();
static void main_task(void *);
static void on_sleep_event();

RTC_DATA_ATTR bool sleeping = false;
RTC_DATA_ATTR int image_index = 0;

static void gpio_init() {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1ULL << GPIO_LED;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    gpio_set_level(GPIO_LED, 1);
}

static void power_init()
{
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 10,
        .light_sleep_enable = false
    };
    esp_pm_configure(&pm_config);
}

static void nvs_init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
}

static void fs_init()
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/storage",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
        .read_only = false,
        .grow_on_mount = false
    };
    ESP_ERROR_CHECK( esp_vfs_littlefs_register(&conf) );
}

static void on_sleep_event()
{
    // switch app to barcode before sleep
    apps_controller_activate_app("barcode");
}

static void main_task(void *)
{
    const char TAG[] = "main_task";
    ESP_LOGI(TAG, "Starting main task");

    while (true) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        // uint16_t touch_value;
        // touch_pad_read_filtered(TOUCHPAD_SELECT, &touch_value);
        // ESP_LOGI(TAG, "SELECT touch value: %d", touch_value);
        // touch_pad_read_filtered(TOUCHPAD_LEFT, &touch_value);
        // ESP_LOGI(TAG, "LEFT touch value: %d", touch_value);
        // touch_pad_read_filtered(TOUCHPAD_RIGHT, &touch_value);
        // ESP_LOGI(TAG, "RIGHT touch value: %d", touch_value);
    }
}

void app_main() {

    char *TAG = "app_main";
    // get wakeup reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    int wakeup_pin = esp_sleep_get_touchpad_wakeup_status();
    uint16_t touchpads_mask = 0 | (1 << TOUCHPAD_LEFT) | (1 << TOUCHPAD_RIGHT) | (1 << TOUCHPAD_SELECT);

    ESP_LOGW(TAG, "INIT, Free heap: %lu", esp_get_free_heap_size());
    power_init();
    nvs_init();
    gpio_init();
    fs_init();
    display_init();
    fl_init(BARCODES_PATH);
    sleep_init(30, on_sleep_event);
    battery_init();
    battery_get_charge();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_TOUCHPAD) {
        ESP_LOGI(TAG, "Woken by Touch: %d", wakeup_pin);
        touchpad_init(touchpads_mask, true);
    } else {
        // add delay to let touch pads charge settle?
        // otherwise touch pads raw values are too low
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        touchpad_init(touchpads_mask, false);
    }

    apps_controller_init();
    apps_controller_add_app(&app_menu);
    apps_controller_add_app(&app_barcode);
    apps_controller_add_app(&app_bluetooth);
    if (wakeup_reason == ESP_SLEEP_WAKEUP_TOUCHPAD) {
        if (wakeup_pin == TOUCHPAD_SELECT || wakeup_pin == TOUCHPAD_LEFT || wakeup_pin == TOUCHPAD_RIGHT) {
            sleep_register_activity();
            apps_controller_activate_app("barcode");
        }
    } else {
        apps_controller_activate_app("menu");
    }

    xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);
    while (true) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        sleep_deep();
    }
}