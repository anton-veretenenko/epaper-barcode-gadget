#include "touchpad.h"
#include "driver/touch_pad.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "stdio.h"
#include "string.h"
#include "esp_log.h"
#include "driver/gpio.h"

const char *TAG = "TOUCHPAD";
RTC_DATA_ATTR long last_touch = 0;
volatile bool touch_trigger = false;
volatile int touch_pin = -1;
int touch_pad_timestamps[TOUCH_PAD_MAX] = {0};
esp_event_loop_handle_t touch_event_loop;
ESP_EVENT_DECLARE_BASE(TOUCH_EVENT_BASE);
ESP_EVENT_DEFINE_BASE(TOUCH_EVENT_BASE);
int32_t TOUCH_EVENT_SHORT = 0;
int32_t TOUCH_EVENT_LONG = 1;
const uint8_t touch_pad_nums[15] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
};

static void touchpad_isr(void* arg);
static void on_touchpad_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

void touchpad_init(uint16_t touchpad_mask)
{
    ESP_ERROR_CHECK( touch_pad_init() );

    esp_event_loop_args_t loop_args = {
        .queue_size = 2,
        .task_name = "touchpad_event_loop_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = configMINIMAL_STACK_SIZE*2,
        .task_core_id = tskNO_AFFINITY
    };
    ESP_ERROR_CHECK( esp_event_loop_create(&loop_args, &touch_event_loop) );
    ESP_ERROR_CHECK( esp_event_handler_register_with(touch_event_loop, TOUCH_EVENT_BASE, TOUCH_EVENT_SHORT, on_touchpad_event, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register_with(touch_event_loop, TOUCH_EVENT_BASE, TOUCH_EVENT_LONG, on_touchpad_event, NULL) );

    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    ESP_LOGI(TAG, "Touch pad mask: %02X\n", touchpad_mask);
    // enable touch pads
    for (int i = 0; i < TOUCH_PAD_MAX; i++) {
        if (touchpad_mask & (1 << i)) {
            touch_pad_config(i, 0);
        }
    }
    // then read no touch values and init new thesholds
    touch_pad_filter_start(20);
    for (int i = 0; i < TOUCH_PAD_MAX; i++) {
        if (touchpad_mask & (1 << i)) {
            // init threshold
            uint16_t touch_value;
            touch_pad_read_filtered(i, &touch_value);
            ESP_LOGI(TAG, "touch pad %d no touch value: %d\n", i, touch_value);
            touch_pad_set_thresh(i, touch_value * 3 / 4);
        }
    }
    touch_pad_set_trigger_mode(TOUCH_TRIGGER_BELOW);
    touch_pad_isr_register(touchpad_isr, NULL);
    touch_pad_intr_enable();
}

void touchpad_sleep_wakeup_init()
{
    touch_pad_intr_disable();
    touch_pad_set_trigger_mode(TOUCH_TRIGGER_BELOW);
    touch_pad_isr_register(touchpad_isr, NULL);
    touch_pad_intr_enable();
}

static void touchpad_isr(void* arg)
{
    uint32_t pad_intr = touch_pad_get_status();
    touch_pad_clear_status();
    touch_pad_intr_clear();
    touch_pad_intr_disable();
    touch_trigger_mode_t mode;
    touch_pad_get_trigger_mode(&mode);

    if (mode == TOUCH_TRIGGER_BELOW) {
        gpio_set_level(22, 0);
        memset(touch_pad_timestamps, 0, sizeof(touch_pad_timestamps));
        last_touch = esp_timer_get_time();

        for (int i = 0; i < TOUCH_PAD_MAX; i++) {
            if (pad_intr & (1 << i)) {
                touch_pad_timestamps[i] = esp_timer_get_time();
                break;
            }
        }
        // revert interrupt to detect touch release
        touch_pad_set_trigger_mode(TOUCH_TRIGGER_ABOVE);
    } else
    if (mode == TOUCH_TRIGGER_ABOVE) {
        gpio_set_level(22, 1);
        // search touch pad from previous touch
        for (int i = 0; i < TOUCH_PAD_MAX; i++) {
            if (touch_pad_timestamps[i] != 0) {
                long touch_duration = esp_timer_get_time() - touch_pad_timestamps[i];
                touch_pad_timestamps[i] = 0;
                if (touch_duration < 600 * 1000) { // 600ms
                    // short touch detected for pad i
                    ESP_ERROR_CHECK( esp_event_post_to(touch_event_loop, TOUCH_EVENT_BASE, TOUCH_EVENT_SHORT, &(touch_pad_nums[i]), sizeof(touch_pad_nums[i]), 0) );
                } else {
                    // long touch detected for pad i
                    ESP_ERROR_CHECK( esp_event_post_to(touch_event_loop, TOUCH_EVENT_BASE, TOUCH_EVENT_LONG, &(touch_pad_nums[i]), sizeof(touch_pad_nums[i]), 0) );
                }
                break;
            }
        }
        // revert interrupt to detect touch press
        touch_pad_set_trigger_mode(TOUCH_TRIGGER_BELOW);
    }
    touch_pad_intr_enable();
}

static void on_touchpad_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    ESP_LOGI("TOUCH_EVENT", "Touch event, base=%s, id=%ld", event_base, event_id);
    if (event_id == TOUCH_EVENT_SHORT) {
        ESP_LOGI("TOUCH_EVENT", "Short touch event, data=%d", *((uint8_t*)event_data));
    } else if (event_id == TOUCH_EVENT_LONG) {
        ESP_LOGI("TOUCH_EVENT", "Long touch event, data=%d", *((uint8_t*)event_data));
    }
}