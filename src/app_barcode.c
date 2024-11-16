#include "app_barcode.h"
#include "esp_log.h"
#include "display.h"
#include "stdio.h"
#include "file_roller.h"
#include "touchpad.h"
#include "esp_event.h"

static const char * TAG = "app_barcode";
static void app_barcode_init();
static void app_barcode_deinit();
static void on_touch_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void draw_barcode();

apps_controller_app_t app_barcode = {
    .name = "barcode",
    .icon = "app_icon_qr",
    .init = app_barcode_init,
    .deinit = app_barcode_deinit
};

RTC_DATA_ATTR static fl_file_t barcode_file;

static void app_barcode_init()
{
    ESP_LOGI(TAG, "barcode app init");
    ESP_ERROR_CHECK( esp_event_handler_register_with(touchpad_resolved_event_loop, TOUCH_EVENTS_RESOLVED, TOUCH_EVENT_RESOLVED_PRESS, on_touch_event, NULL) );
    if (barcode_file.name == NULL) fl_next(&barcode_file);
    draw_barcode();
}

static void app_barcode_deinit()
{
    ESP_LOGI(TAG, "barcode app deinit");
    ESP_ERROR_CHECK( esp_event_handler_unregister_with(touchpad_resolved_event_loop, TOUCH_EVENTS_RESOLVED, TOUCH_EVENT_RESOLVED_PRESS, on_touch_event) );
}

static void on_touch_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    ESP_LOGI(TAG, "on_touch_event");
    uint8_t touchpad_num = *((uint8_t*)event_data);
    if (touchpad_num == TOUCHPAD_RIGHT) {
        fl_next(&barcode_file);
        draw_barcode();
    } else if (touchpad_num == TOUCHPAD_LEFT) {
        fl_prev(&barcode_file);
        draw_barcode();
    } else if (touchpad_num == TOUCHPAD_SELECT) {
        // do something with select button
        draw_barcode();
    }
}

static void draw_barcode()
{
        size_t total_bytes, used_bytes;
        esp_littlefs_info("storage", &total_bytes, &used_bytes);
        ESP_LOGI(TAG, "Total bytes: %d, Used bytes: %d", total_bytes, used_bytes);
        uint16_t used_percent = (uint16_t)((float)used_bytes / total_bytes * 100);
        if (used_percent > 99) used_percent = 99;
        display_start(true);
        if (barcode_file.name != NULL) {
            ESP_LOGI(TAG, "Will draw file: %s", barcode_file.name);
            char *path = malloc(strlen(BARCODES_PATH) + strlen(barcode_file.name) + 1);
            strcpy(path, BARCODES_PATH);
            strcat(path, barcode_file.name);
            FILE *img_file = fopen(path, "rb");
            if (img_file) {
                display_show_bitmap_file(img_file, 200, 200);
                fclose(img_file);
                char *str_display = malloc(strlen(barcode_file.name) + 1 + 2);
                strcpy(str_display, " ");
                strcat(str_display, barcode_file.name);
                strcat(str_display, " ");
                display_text_center_at(5, str_display);
                memset(str_display, 0, 5+1);
                sprintf(str_display, " %d%%", 100-used_percent);
                strcat(str_display, "\x80");
                display_text_at(200-strlen(str_display)*8, 200-16, str_display);
                memset(str_display, 0, 5+1);
                strcat(str_display, " ");
                strcat(str_display, "99%");
                strcat(str_display, "\x7F");
                display_text_at(200-strlen(str_display)*8, 200-8, str_display);
                free(str_display);
            } else {
                ESP_LOGE(TAG, "Failed to open file: %s", path);
                display_fill_white();
                display_text_center_at(5, "IMAGE OPEN ERROR");
            }
            free(path);
        } else {
            ESP_LOGE(TAG, "No next file found");
            display_fill_white();
            display_text_center_at(5, "NO IMAGES");
        }
        display_finish(true);
}