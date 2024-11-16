#include "touchpad.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "stdio.h"
#include "string.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sleep.h"

static const char *TAG = "TOUCHPAD";
RTC_DATA_ATTR long last_touch = 0;
volatile bool touch_trigger = false;
volatile int touch_pin = -1;
int touch_pad_timestamps[TOUCH_PAD_MAX] = {0};
bool touch_pad_pressed[TOUCH_PAD_MAX] = {false};
const uint8_t touch_pad_nums[15] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
};
ESP_EVENT_DEFINE_BASE(TOUCH_EVENTS);
ESP_EVENT_DEFINE_BASE(TOUCH_EVENTS_RESOLVED);
esp_event_loop_handle_t touchpad_event_loop;
esp_event_loop_handle_t touchpad_resolved_event_loop;

static void touchpad_isr(void* arg);
static void on_touchpad_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void on_touchpad_resolved_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

void touchpad_init(uint16_t touchpad_mask)
{
    ESP_ERROR_CHECK( touch_pad_init() );

    esp_event_loop_args_t loop_args = {
        .queue_size = 5,
        .task_name = "touch_event_loop_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = configMINIMAL_STACK_SIZE*2,
        .task_core_id = tskNO_AFFINITY
    };
    ESP_ERROR_CHECK( esp_event_loop_create(&loop_args, &touchpad_event_loop) );
    esp_event_loop_args_t loop_args_resolved = {
        .queue_size = 1,
        .task_name = "touch_resolved_event_loop_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = configMINIMAL_STACK_SIZE*2,
        .task_core_id = tskNO_AFFINITY
    };
    ESP_ERROR_CHECK( esp_event_loop_create(&loop_args_resolved, &touchpad_resolved_event_loop) );
    ESP_ERROR_CHECK( esp_event_handler_register_with(touchpad_event_loop, TOUCH_EVENTS, TOUCH_EVENT_PRESS, on_touchpad_event, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register_with(touchpad_event_loop, TOUCH_EVENTS, TOUCH_EVENT_RELEASE, on_touchpad_event, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register_with(touchpad_resolved_event_loop, TOUCH_EVENTS_RESOLVED, TOUCH_EVENT_RESOLVED_PRESS, on_touchpad_resolved_event, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register_with(touchpad_resolved_event_loop, TOUCH_EVENTS_RESOLVED, TOUCH_EVENT_RESOLVED_LONG_PRESS, on_touchpad_resolved_event, NULL) );

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
            touch_pad_set_thresh(i, touch_value * 2.5f / 4);
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
        last_touch = esp_timer_get_time();

        for (int i = 0; i < TOUCH_PAD_MAX; i++) {
            if (pad_intr & (1 << i)) {
                touch_pad_timestamps[i] = esp_timer_get_time();
                esp_event_post_to(touchpad_event_loop, TOUCH_EVENTS, TOUCH_EVENT_PRESS, &(touch_pad_nums[i]), sizeof(touch_pad_nums[i]), 0);
                // revert interrupt to detect touch release
                touch_pad_set_trigger_mode(TOUCH_TRIGGER_ABOVE);
                break;
            }
        }
    } else
    if (mode == TOUCH_TRIGGER_ABOVE) {
        gpio_set_level(22, 1);
        // search touch pad from previous touch
        for (int i = 0; i < TOUCH_PAD_MAX; i++) {
            if (pad_intr & (1 << i)) {
                if (touch_pad_timestamps[i] != 0) {
                    touch_pad_timestamps[i] = 0; // reset timestamp to trigger long press wait loop to end in event handler
                    esp_event_post_to(touchpad_event_loop, TOUCH_EVENTS, TOUCH_EVENT_RELEASE, &(touch_pad_nums[i]), sizeof(touch_pad_nums[i]), 0);
                    // revert interrupt to detect touch press
                    touch_pad_set_trigger_mode(TOUCH_TRIGGER_BELOW);
                    break;
                }
            }
        }
    }
    touch_pad_intr_enable();
}

static void on_touchpad_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    ESP_LOGD(TAG, "Touch event, base=%s, id=%ld", event_base, event_id);
    sleep_register_activity();
    uint8_t touchpad_num = *((uint8_t*)event_data);
    if (event_id == TOUCH_EVENT_PRESS) {
        ESP_LOGD(TAG, "TOUCH_EVENT_PRESS data=%d ts=%d", touchpad_num, touch_pad_timestamps[touchpad_num]);
        touch_pad_pressed[touchpad_num] = true;
        int duration = 0;
        while (touch_pad_timestamps[touchpad_num] != 0) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            duration = (esp_timer_get_time() - touch_pad_timestamps[touchpad_num]) / 1000;
            if (duration > 600)
                break;    
        }
        if (duration > 600 && touch_pad_timestamps[touchpad_num] != 0) {
            ESP_LOGD(TAG, "TOUCH_EVENT_PRESS LONG data=%d ts=%d", touchpad_num, touch_pad_timestamps[touchpad_num]);
            esp_event_post_to(touchpad_event_loop, TOUCH_EVENTS, TOUCH_EVENT_RELEASE, &(touch_pad_nums[touchpad_num]), sizeof(touch_pad_nums[touchpad_num]), 0);
        }
    } else if (event_id == TOUCH_EVENT_RELEASE) {
        if (touch_pad_timestamps[touchpad_num] != 0) {
            ESP_LOGD(TAG, "TOUCH_EVENT_RELEASE LONG data=%d", touchpad_num);
            touch_pad_pressed[touchpad_num] = false;
            esp_event_post_to(touchpad_resolved_event_loop, TOUCH_EVENTS_RESOLVED, TOUCH_EVENT_RESOLVED_LONG_PRESS, &(touch_pad_nums[touchpad_num]), sizeof(touch_pad_nums[touchpad_num]), 0);
        } else {
            ESP_LOGD(TAG, "TOUCH_EVENT_RELEASE data=%d", touchpad_num);
            if (touch_pad_pressed[touchpad_num] == true) {
                esp_event_post_to(touchpad_resolved_event_loop, TOUCH_EVENTS_RESOLVED, TOUCH_EVENT_RESOLVED_PRESS, &(touch_pad_nums[touchpad_num]), sizeof(touch_pad_nums[touchpad_num]), 0);
                touch_pad_pressed[touchpad_num] = false;
            }
        }
    }
}

static void on_touchpad_resolved_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    uint8_t touchpad_num = *((uint8_t*)event_data);
    switch (event_id) {
        case TOUCH_EVENT_RESOLVED_PRESS: {
            ESP_LOGI(TAG, "TOUCH_EVENT_RESOLVED_PRESS: %d", touchpad_num);
        } break;
        case TOUCH_EVENT_RESOLVED_LONG_PRESS: {
            ESP_LOGI(TAG, "TOUCH_EVENT_RESOLVED_LONG_PRESS: %d", touchpad_num);
        } break;
    }
}