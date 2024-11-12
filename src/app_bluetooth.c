#include "app_bluetooth.h"
#include "esp_log.h"
#include "display.h"
#include "stdio.h"
#include "file_roller.h"
#include "bluetooth.h"

static const char * TAG = "app_bluetooth";
static void app_bluetooth_init();
static void app_bluetooth_deinit();

apps_controller_app_t app_bluetooth = {
    .name = "bluetooth",
    .icon = "app_icon_bluetooth",
    .init = app_bluetooth_init,
    .deinit = app_bluetooth_deinit
};

static void app_bluetooth_init()
{
    ESP_LOGI(TAG, "bluetooth app init");
    const char *background_image = "bluetooth";
    char *path = malloc(strlen(STORAGE_PATH) + strlen(background_image) + 1);
    if (path == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return;
    }
    strcpy(path, STORAGE_PATH);
    strcat(path, background_image);
    display_start(true);
    FILE *img_file = fopen(path, "rb");
    if (img_file) {
        display_show_bitmap_file(img_file, 200, 200);
        fclose(img_file);
    } else {
        ESP_LOGE(TAG, "Failed to open file");
        display_text_center_at(5, "IMAGE OPEN ERROR");
    }
    display_finish(true);
    free(path);
    bluetooth_start();
}

static void app_bluetooth_deinit()
{
    bluetooth_stop();
}