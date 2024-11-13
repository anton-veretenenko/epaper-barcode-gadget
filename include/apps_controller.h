#pragma once
#include "inttypes.h"
#include "stdbool.h"

typedef struct
{
    const char *name;
    const char *icon; // app icon filename, 80x80 pixels
    void (*init)();
    void (*deinit)();
} apps_controller_app_t;

typedef struct
{
    const char *name;
    const char *icon;
    bool active;
} apps_controller_app_info_t;

typedef struct 
{
    apps_controller_app_info_t *apps;
    int count;
} apps_controller_apps_info_t;

void apps_controller_init(int touchpad_menu_num);
void apps_controller_deinit();
void apps_controller_add_app(apps_controller_app_t *app);
void apps_controller_activate_app(const char *name);
apps_controller_apps_info_t * apps_controller_list_apps();