#pragma once

#include "stdbool.h"
#include "inttypes.h"
#include "esp_event.h"

void touchpad_init(uint16_t touchpad_mask);
void touchpad_sleep_wakeup_init();

extern esp_event_loop_handle_t touchpad_event_loop;
extern esp_event_loop_handle_t touchpad_resolved_event_loop;

ESP_EVENT_DECLARE_BASE(TOUCH_EVENTS);
ESP_EVENT_DECLARE_BASE(TOUCH_EVENTS_RESOLVED);
enum {
    TOUCH_EVENT_PRESS = 0,
    TOUCH_EVENT_RELEASE,
    TOUCH_EVENT_RESOLVED_PRESS,
    TOUCH_EVENT_RESOLVED_LONG_PRESS
};