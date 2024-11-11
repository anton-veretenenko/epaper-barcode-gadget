#include "apps_controller.h"
#include "inttypes.h"
#include "stdbool.h"
#include "string.h"
#include "esp_log.h"
#include "touchpad.h"
#include "esp_event.h"

static const char* TAG = "apps_controller";

struct {
    apps_controller_app_t *apps;
    int count;
    int active;
} apps_installed;

static int touchpad_menu_num_ = 0;
static void on_touch_long_press(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

void apps_controller_init(int touchpad_menu_num)
{
    ESP_LOGI(TAG, "Initializing apps controller: menu touchpad %d", touchpad_menu_num);
    apps_installed.apps = NULL;
    apps_installed.count = 0;
    apps_installed.active = -1;
    touchpad_menu_num_ = touchpad_menu_num;
    ESP_ERROR_CHECK( esp_event_handler_register_with(touchpad_resolved_event_loop, TOUCH_EVENTS_RESOLVED, TOUCH_EVENT_RESOLVED_LONG_PRESS, on_touch_long_press, NULL) );
}

void apps_controller_deinit()
{
    apps_installed.apps[apps_installed.active].deinit();
    free(apps_installed.apps);
    apps_installed.apps = NULL;
    apps_installed.count = 0;
    apps_installed.active = 0;
}

void apps_controller_add_app(apps_controller_app_t *app)
{
    for (int i = 0; i < apps_installed.count; i++) {
        if (strcmp(apps_installed.apps[i].name, app->name) == 0) {
            ESP_LOGE(TAG, "App %s already installed", app->name);
            return;
        }
    }
    apps_controller_app_t *new_apps = realloc(apps_installed.apps, sizeof(apps_controller_app_t) * (apps_installed.count + 1));
    if (new_apps == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for app %s", app->name);
        return;
    }
    apps_installed.apps = new_apps;
    memcpy(&apps_installed.apps[apps_installed.count], app, sizeof(apps_controller_app_t));
    apps_installed.count++;
}

void apps_controller_activate_app(char *name)
{
    ESP_LOGI(TAG, "Activating app: %s", name);
    for (int i = 0; i < apps_installed.count; i++) {
        if (strcmp(apps_installed.apps[i].name, name) == 0) {
            if (apps_installed.active != i) {
                if (apps_installed.active != -1)
                    apps_installed.apps[apps_installed.active].deinit(); // deinit current app
                apps_installed.active = i;
                apps_installed.apps[apps_installed.active].init(); // init new app
            } else {
                ESP_LOGW(TAG, "App is already active: %s", name);
            }
            return;
        }
    }
    ESP_LOGE(TAG, "App %s not found", name);
}

apps_controller_apps_info_t *  apps_controller_list_apps()
{
    apps_controller_apps_info_t *apps_info = malloc(sizeof(apps_controller_apps_info_t));
    apps_info->apps = malloc(sizeof(apps_controller_app_info_t) * apps_installed.count);
    for (int i = 0; i < apps_installed.count; i++) {
        apps_info->apps[i].name = apps_installed.apps[i].name;
    }
    apps_info->count = apps_installed.count;
    return apps_info;
}

static void on_touch_long_press(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    uint8_t touchpad_num = *((uint8_t*)event_data);
    if (event_id == TOUCH_EVENT_RESOLVED_LONG_PRESS && touchpad_num == touchpad_menu_num_) {
        apps_controller_activate_app("menu");
    }
}