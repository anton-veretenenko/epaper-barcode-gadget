#pragma once

#include "inttypes.h"
#include "stdbool.h"

bool sleep_init(int timeout, void (*on_sleep_event_callback)()); // set sleep timeout in seconds
void sleep_inhibit(bool inhibit); // prevent from sleep
void sleep_register_activity();
bool sleep_deep();