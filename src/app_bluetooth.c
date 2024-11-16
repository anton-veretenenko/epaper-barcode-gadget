#include "app_bluetooth.h"
#include "esp_log.h"
#include "display.h"
#include "stdio.h"
#include "file_roller.h"
#include "bluetooth.h"
#include "sleep.h"

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
    sleep_inhibit(true);
    bluetooth_start();
    const char *background_image = "bluetooth";
    char *path = malloc(strlen(STORAGE_PATH) + strlen(background_image) + 1);
    if (path == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return;
    }
    strcpy(path, STORAGE_PATH);
    strcat(path, background_image);
    display_start(true);
    display_fill_white();
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
    uint8_t mac[6] = {0};
    bluetooth_get_mac(mac);
    char str_mac[6*2+5+1];
    sprintf(str_mac, "%02X:%02X:%02X:%02X:%02X:%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    display_start(true);
    display_text_center_at(160, str_mac);
    display_finish_partial();
}

static void app_bluetooth_deinit()
{
    bluetooth_stop();
    sleep_inhibit(false);
}