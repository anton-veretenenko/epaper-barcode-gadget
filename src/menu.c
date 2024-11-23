#include "menu.h"
#include "esp_log.h"
#include "display.h"
#include "stdio.h"
#include "display.h"
#include "file_roller.h"
#include "touchpad.h"
#include "esp_event.h"

static const char * TAG = "app_menu";
static const char * filename_icon_mask = "menu_select_mask";
static const char * filename_icon_mask_96 = "menu_select_mask_96";
static void menu_init();
static void menu_deinit();
static void menu_draw_icons();
static void on_touch_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

apps_controller_app_t app_menu = {
    .name = "menu",
    .icon = "app_icon_clock", // will not be used
    .init = menu_init,
    .deinit = menu_deinit
};

static int menu_app_selected = 0;
static int menu_apps_count = 0;
static const int apps_per_page = 4;
static int menu_page = 0;
static int display_counter = 0;

static void menu_init()
{
    ESP_LOGI(TAG, "Menu app init");
    menu_app_selected = 1; // skip menu app
    menu_apps_count = 0;
    menu_page = 0;
    display_counter = 0;
    ESP_ERROR_CHECK( esp_event_handler_register_with(touchpad_resolved_event_loop, TOUCH_EVENTS_RESOLVED, TOUCH_EVENT_RESOLVED_PRESS, on_touch_event, NULL) );
    menu_draw_icons();
}

static void menu_deinit()
{
    ESP_LOGI(TAG, "Menu app deinit");
    ESP_ERROR_CHECK( esp_event_handler_unregister_with(touchpad_resolved_event_loop, TOUCH_EVENTS_RESOLVED, TOUCH_EVENT_RESOLVED_PRESS, on_touch_event) );
}

static void menu_draw_icons()
{
    apps_controller_apps_info_t *apps = apps_controller_list_apps();
    menu_apps_count = apps->count;
    char *path_mask = malloc(strlen(STORAGE_PATH) + strlen(filename_icon_mask) + 1);
    if (path_mask == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return;
    }
    strcpy(path_mask, STORAGE_PATH);
    strcat(path_mask, filename_icon_mask);
    FILE *icon_mask = fopen(path_mask, "rb");
    if (icon_mask == NULL) {
        ESP_LOGE(TAG, "Failed to open mask file: %s", path_mask);
        free(path_mask);
        free(apps);
        return;
    }
    free(path_mask);
    path_mask = malloc(strlen(STORAGE_PATH) + strlen(filename_icon_mask_96) + 1);
    if (path_mask == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return;
    }
    strcpy(path_mask, STORAGE_PATH);
    strcat(path_mask, filename_icon_mask_96);
    FILE *icon_mask_96 = fopen(path_mask, "rb");
    if (icon_mask_96 == NULL) {
        ESP_LOGE(TAG, "Failed to open mask file: %s", path_mask);
        free(path_mask);
        free(apps);
        return;
    }
    free(path_mask);
    uint8_t apps_start = menu_page * apps_per_page;
    uint8_t row = 0;
    uint8_t col = 0;
    display_start(true);
    display_fill_white();
    if (display_counter == 0) {
        display_update(true);
    }
    for (int i = apps_start; i < apps_start + apps_per_page - 1 || i < menu_apps_count; i++) {
        if (i == 0) continue; // skip menu app
        char *path_icon = malloc(strlen(STORAGE_PATH) + strlen(apps->apps[i].icon) + 1);
        if (path_icon == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory");
            break;
        }
        strcpy(path_icon, STORAGE_PATH);
        strcat(path_icon, apps->apps[i].icon);
        FILE *icon = fopen(path_icon, "rb");
        if (icon == NULL) {
            ESP_LOGE(TAG, "Failed to open icon file: %s", path_icon);
            free(path_icon);
            break;
        }
        if (menu_app_selected == i) {
            display_show_bitmap_file_at(icon_mask_96, 96, 96, (col*80)+(col*24), (row*80)+(row*24), false);
            // display_show_bitmap_file_at(icon_mask_96, 96, 96, 0, 0, false);
            // display_show_bitmap_file_at_with_mask(icon, icon_mask, 80, 80, (col*80)+8+(col*24), (row*80)+8+(row*24), true, false);
            display_show_bitmap_file_at(icon, 80, 80, (col*80)+8+(col*24), (row*80)+8+(row*24), true);
            // display_fill_rect_at(32, 4, (col*80)+32+(col*24), (row*80)+8+(row*24)+80+4);
            // display_fill_rect_at(100, 100, 0, 0);
        } else {
            display_show_bitmap_file_at(icon, 80, 80, (col*80)+8+(col*24), (row*80)+8+(row*24), false);
        }
        free(path_icon);
        fclose(icon);
        if (++col == 2) {
            col = 0;
            row++;
        }
    }
    if (display_counter == 0)
        // display_finish_partial();
        display_finish(true);
    else
        display_finish(true);
        // display_finish_partial();
    if (++display_counter > 5) display_counter = 0;
    
    fclose(icon_mask);
    fclose(icon_mask_96);
    
    free(apps);
}

static void on_touch_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    uint8_t touchpad_num = *((uint8_t*)event_data);
    if (touchpad_num == TOUCHPAD_RIGHT) {
        menu_app_selected++;
        if (menu_app_selected >= menu_apps_count) {
            menu_app_selected = 1;
        }
        menu_draw_icons();
    } else if (touchpad_num == TOUCHPAD_LEFT) {
        menu_app_selected--;
        if (menu_app_selected < 1) {
            menu_app_selected = menu_apps_count - 1;
        }
        menu_draw_icons();
    } else if (touchpad_num == TOUCHPAD_SELECT) {
        apps_controller_apps_info_t *apps = apps_controller_list_apps();
        apps_controller_activate_app(apps->apps[menu_app_selected].name);
        free(apps);
    }
}