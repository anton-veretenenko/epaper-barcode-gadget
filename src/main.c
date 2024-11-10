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

#define GPIO_LED 22
static void gpio_init();
static void power_init();
static void nvs_init();
static void fs_init();
static void main_task(void *);
static void sleep_deep();

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

static void main_task(void *)
{
    const char TAG[] = "main_task";
    ESP_LOGI(TAG, "Starting main task");
    int led_state = 0;

    while (true) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        // if (touch_trigger && touch_pin != -1) {
        //     // process touch event
        //     ESP_LOGI(TAG, "Touch: 0");
        //     last_touch = esp_timer_get_time();
        //     led_state = ~led_state & 1;
        //     gpio_set_level(GPIO_LED, led_state);
        //     touch_trigger = false;
        //     touch_pin = -1;
        //     // if (led_state) {
        //     //     display_start(true);
        //     //     display_fill_black();
        //     //     display_finish(true);
        //     // } else {
        //     //     display_start(true);
        //     //     FILE *file = fopen("/storage/img/CLIENT", "rb");
        //     //     display_show_bitmap_file(file, 200, 200);
        //     //     fclose(file);
        //     //     display_text_center_at(5, " CLIENT ");
        //     //     display_finish(true);
        //     // }
        //     size_t total_bytes, used_bytes;
        //     esp_littlefs_info("storage", &total_bytes, &used_bytes);
        //     ESP_LOGI(TAG, "Total bytes: %d, Used bytes: %d", total_bytes, used_bytes);
        //     uint16_t used_percent = (uint16_t)((float)used_bytes / total_bytes * 100);
        //     if (used_percent > 99) used_percent = 99;
        //     display_start(true);
        //     fl_file_t file;
        //     if (fl_next(&file)) {
        //         ESP_LOGI(TAG, "Next file: %s", file.name);
        //         char *path = malloc(strlen(BARCODES_PATH) + strlen(file.name) + 1);
        //         strcpy(path, BARCODES_PATH);
        //         strcat(path, file.name);
        //         FILE *img_file = fopen(path, "rb");
        //         if (img_file) {
        //             display_show_bitmap_file(img_file, 200, 200);
        //             fclose(img_file);
        //             char *str_display = malloc(strlen(file.name) + 1 + 2);
        //             strcpy(str_display, " ");
        //             strcat(str_display, file.name);
        //             strcat(str_display, " ");
        //             display_text_center_at(5, str_display);
        //             memset(str_display, 0, 5+1);
        //             sprintf(str_display, " %d%%", 100-used_percent);
        //             strcat(str_display, "\x80");
        //             display_text_at(200-strlen(str_display)*8, 200-16, str_display);
        //             memset(str_display, 0, 5+1);
        //             strcat(str_display, " ");
        //             strcat(str_display, "99%");
        //             strcat(str_display, "\x7F");
        //             display_text_at(200-strlen(str_display)*8, 200-8, str_display);
        //             free(str_display);
        //         } else {
        //             ESP_LOGE(TAG, "Failed to open file: %s", path);
        //             display_text_center_at(5, "IMAGE OPEN ERROR");
        //         }
        //         free(path);
        //     } else {
        //         ESP_LOGE(TAG, "No next file found");
        //         display_text_center_at(5, "NO IMAGES");
        //     }
        //     display_finish(true);
        // }
        // vTaskDelay(60 / portTICK_PERIOD_MS);
        // // check touch pins released
        // uint16_t touch_value;
        // uint16_t touch_thres;
        // touch_pad_get_thresh(0, &touch_thres);
        // touch_pad_read_filtered(0, &touch_value);
        // if (touch_trigger == false && touch_value > touch_thres) {
        //     touch_pad_clear_status();
        //     touch_pad_intr_enable();
        // }
    }
}

static void sleep_deep()
{
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    ESP_LOGI("sleep", "Going to sleep");
    esp_sleep_enable_touchpad_wakeup();
    esp_deep_sleep_start();
}

void app_main() {

    char *TAG = "app_main";
    // get wakeup reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    int wakeup_pin = esp_sleep_get_touchpad_wakeup_status();

    power_init();
    nvs_init();
    gpio_init();
    vTaskDelay(4000 / portTICK_PERIOD_MS);
    fs_init();
    printf("INIT\n");
    touchpad_init(0 | (1 << TOUCH_PAD_NUM0) | (1 << TOUCH_PAD_NUM9));
    // touchpad_init(0 | (1 << TOUCH_PAD_NUM0));
    display_init();
    
    bluetooth_start();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_TOUCHPAD) {
        ESP_LOGI(TAG, "Woken by Touch: %d", wakeup_pin);
        if (wakeup_pin == TOUCH_PAD_NUM0) {
            // last_touch = esp_timer_get_time();
            // touch_trigger = true;
            // touch_pin = 0;
        }
    } else {
        // add delay to let open terminal after flashing
        vTaskDelay(4000 / portTICK_PERIOD_MS);
    }

    fl_init(BARCODES_PATH);

    xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);
    while (true) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        // if (last_touch == 0) {
        //     last_touch = esp_timer_get_time();
        // }
        // if (esp_timer_get_time() - last_touch > 30 * 1000 * 1000) {
        //     ESP_LOGI("sleep", "No touch for 30 seconds");
        //     sleep_deep();
        // }
    }
}