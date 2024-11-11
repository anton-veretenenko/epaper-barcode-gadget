#include "menu.h"
#include "esp_log.h"
#include "display.h"
#include "stdio.h"

static const char * TAG = "menu_app";
static void menu_init();
static void menu_deinit();

apps_controller_app_t menu_app = {
    .name = "menu",
    .init = menu_init,
    .deinit = menu_deinit
};

static void menu_init()
{
    ESP_LOGI(TAG, "Menu app init");
    display_start(true);
    FILE *img_file = fopen("/storage/barcode/3000010007775", "rb");
    if (img_file) {
        display_show_bitmap_file(img_file, 200, 200);
        fclose(img_file);
    } else {
        ESP_LOGE(TAG, "Failed to open file");
        display_text_center_at(5, "IMAGE OPEN ERROR");
    }
    display_finish(true);
}

static void menu_deinit()
{
    ESP_LOGI(TAG, "Menu app deinit");
}