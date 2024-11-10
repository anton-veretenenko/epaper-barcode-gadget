#pragma once

#include "stdbool.h"
#include "inttypes.h"

void touchpad_init(uint16_t touchpad_mask);
void touchpad_sleep_wakeup_init();

enum {
    TOUCH_EVENT_PRESS = 0,
    TOUCH_EVENT_RELEASE,
};