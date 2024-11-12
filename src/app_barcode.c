#include "app_barcode.h"
#include "esp_log.h"
#include "display.h"
#include "stdio.h"
#include "file_roller.h"

static const char * TAG = "app_barcode";
static void app_barcode_init();
static void app_barcode_deinit();

apps_controller_app_t app_barcode = {
    .name = "barcode",
    .icon = "app_icon_qr",
    .init = app_barcode_init,
    .deinit = app_barcode_deinit
};

static void app_barcode_init()
{
    ESP_LOGI(TAG, "barcode app init");
    const char *background_image = "emptybat";
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
}

static void app_barcode_deinit()
{
}
