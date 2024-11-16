#include "sleep.h"
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "sleep";
static bool inhibit_sleep = false;
static int last_activity = 0;
static int sleep_timeout = 30 * 1000 * 1000; // 30 seconds
static void (*on_sleep_event)() = NULL;

bool sleep_init(int timeout, void (*on_sleep_event_callback)())
{
    if (timeout > 0) {
        sleep_timeout = timeout * 1000 * 1000;
        if (on_sleep_event_callback != NULL) {
            on_sleep_event = on_sleep_event_callback;
        }
        return true;
    }
    return false;
}

bool sleep_deep()
{
    if (esp_timer_get_time() - last_activity > sleep_timeout) {
        if (inhibit_sleep) {
            ESP_LOGI("sleep", "Sleep timeout, but sleep inhibited, will not sleep");
            last_activity = esp_timer_get_time();
            return false;
        }
        ESP_LOGI("sleep", "Going to sleep");
        if (on_sleep_event != NULL) {
            on_sleep_event();
        }
        esp_sleep_enable_touchpad_wakeup();
        esp_deep_sleep_start();
        return true; // never reached
    }
    return false;
}

void sleep_inhibit(bool inhibit)
{
    inhibit_sleep = inhibit;
}

void sleep_register_activity()
{
    last_activity = esp_timer_get_time();
}