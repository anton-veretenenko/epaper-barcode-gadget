#pragma once

#include <stdint.h>
#include <stdbool.h>

void bluetooth_start();
void bluetooth_stop();
void bluetooth_get_mac(uint8_t mac[6]);