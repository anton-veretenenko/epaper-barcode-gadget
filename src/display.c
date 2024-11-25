#include "display.h"
#include "driver/spi_master.h"
#include "hal/spi_types.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "font_Sinclair_S.h"
#include <string.h>

typedef struct
{
    uint8_t cmd;
    bool busy_before;
    bool busy_after;
    uint8_t data_len;
    uint8_t data[4];
} display_cmd_t;

static void display_gpio_init();
static void display_reset_hw();
static void display_sleep();
static void display_init_hw(bool fast);
static void display_send_cmd(display_cmd_t *cmd);
static void display_send_cmd_byte_only(uint8_t cmd);
static void display_send_data_byte_only(uint8_t data);
static void display_send_data(uint8_t *data, uint16_t len);
static void display_wait_busy();
// static void display_update(bool fast);
// static void display_clear_window(uint8_t x, uint8_t y, uint8_t width, uint8_t height);

display_cmd_t display_init_geometry[] = {
    {0x12, true, true, 0, {}},
    {0x01, false, false, 2, {(DISPLAY_HEIGHT-1)%256, (DISPLAY_HEIGHT-1)/256, 0x00}},
    {0x11, false, false, 1, {0x07}}, // data entry direction
    {0x44, false, false, 2, {0, DISPLAY_WIDTH/8-1}}, // 8 bits per pixel
    {0x45, false, false, 4, {0, 0, (DISPLAY_HEIGHT-1)%256, (DISPLAY_HEIGHT-1)/256}}, // 0 to number of lines
    {0x3C, false, false, 1, {0x80}}, // screen border, white - 0x05, black - 0x00, 0xc0 - hi-z, do not refresh border(noisy), 0x80 - vcom, do not refresh border
    {0x18, false, false, 1, {0x80}}, // internal temperature sensor
    {0x21, false, false, 1, {0x80}}, // inverse RED channel
};

display_cmd_t display_init_geometry_bitmap[] = {
    {0x12, true, true, 0, {}},
    {0x01, false, false, 2, {(DISPLAY_HEIGHT-1)%256, (DISPLAY_HEIGHT-1)/256, 0x00}},
    {0x11, false, false, 1, {0x03}}, // data entry direction
    {0x44, false, false, 2, {0, DISPLAY_WIDTH/8-1}}, // 8 bits per pixel
    {0x45, false, false, 4, {0, 0, (DISPLAY_HEIGHT-1)%256, (DISPLAY_HEIGHT-1)/256}}, // 0 to number of lines
    {0x3C, false, false, 1, {0x80}}, // screen border, white - 0x05, black - 0x00, 0xc0 - hi-z, do not refresh border(noisy), 0x80 - vcom, do not refresh border
    {0x18, false, false, 1, {0x80}}, // internal temperature sensor
    {0x21, false, false, 1, {0x80}}, // inverse RED channel
};

display_cmd_t display_init_cmds[] = {
    {0x4e, false, false, 1, {0x00}}, // start x
    {0x4f, false, true, 2, {0x00, 0x00}}, // start y
};

// display_cmd_t display_init_fast_cmds[] = {
//     {0x12, true, true, 0, {}},
        // {0x3C, false, false, 1, {0x80}}, // screen border, white - 0x05, black - 0x00
//     {0x18, false, false, 1, {0x80}}, // internal temperature sensor
//     {0x22, false, false, 1, {0xb1}},
//     {0x20, false, true, 0, {}}, // update display
//     {0x1a, false, false, 2, {0x64, 0x00}}, // write temperature
//     {0x22, false, false, 1, {0x91}}, // force read temperature?
//     {0x20, false, true, 0, {}}, // update display
// };

// faster update
display_cmd_t display_init_fast_cmds[] = {
    {0x4e, false, false, 1, {0x00}}, // start x
    {0x4f, false, true, 2, {0x00, 0x00}}, // start y
    {0x22, false, false, 1, {0xb1}}, // b1 - mode 1 load temp + lut, b9 - mode 2 load temp + lut
    {0x20, false, true, 0, {}}, // update display
    {0x1a, false, false, 2, {0x5a, 0x00}}, // write temperature
    {0x22, false, false, 1, {0x91}}, // 91 - mode 1 load lut, 99 - mode 2 load lut
    // {0x22, false, false, 1, {0xb9}},
    {0x20, false, true, 0, {}}, // update display
};

display_cmd_t display_update_cmds[] = {
    {0x22, false, false, 1, {0xf7}}, // 0xf7 slow mode 1 refresh, 0xff fast mode 2 refresh
    {0x20, false, true, 0, {}}, // update display
};

display_cmd_t display_update_fast_cmds[] = {
    {0x22, false, false, 1, {0xcf}}, // 0xc7 mode 1, 0xcf fast mode 2 update
    {0x20, false, true, 0, {}}, // update display
};

display_cmd_t display_update_window_cmds[] = {
    {0x22, false, false, 1, {0xff}}, // 0xff fast mode 2 refresh for window?
    {0x20, false, true, 0, {}}, // update display
};

display_cmd_t display_sleep_cmds[] = {
    {0x10, false, false, 1, {0x01}},
};

spi_device_handle_t display_spi_device;

static void display_gpio_init()
{
    gpio_config_t io_conf;

    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    io_conf.pin_bit_mask = (1ULL << DISPLAY_PIN_CS) | (1ULL << DISPLAY_PIN_DC) | (1ULL << DISPLAY_PIN_RES);
    gpio_config(&io_conf);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << DISPLAY_PIN_BUSY);
    gpio_config(&io_conf);
}

static void display_reset_hw()
{
    gpio_set_level(DISPLAY_PIN_RES, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(DISPLAY_PIN_RES, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

static void display_wait_busy()
{
    while (gpio_get_level(DISPLAY_PIN_BUSY) == 1) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

static void display_send_cmd(display_cmd_t *cmd)
{
    if (cmd->busy_before) {
        display_wait_busy();
    }

    // transmit command byte
    gpio_set_level(DISPLAY_PIN_DC, 0);
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {cmd->cmd},
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(display_spi_device, &t));

    // transmit data bytes
    gpio_set_level(DISPLAY_PIN_DC, 1);
    for (int i = 0; i < cmd->data_len; i++) {
        t.tx_data[0] = cmd->data[i];
        ESP_ERROR_CHECK(spi_device_polling_transmit(display_spi_device, &t));
    }

    if (cmd->busy_after) {
        display_wait_busy();
    }
}

static void display_send_cmd_byte_only(uint8_t cmd)
{
    // transmit command byte
    gpio_set_level(DISPLAY_PIN_DC, 0);
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {cmd},
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(display_spi_device, &t));
}

static void display_send_data_byte_only(uint8_t data)
{
    // transmit data byte
    gpio_set_level(DISPLAY_PIN_DC, 1);
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {data},
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(display_spi_device, &t));
}

static void display_send_data(uint8_t *data, uint16_t len)
{
    // transmit data bytes
    gpio_set_level(DISPLAY_PIN_DC, 1);
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8
    };
    for (int i = 0; i < len; i++) {
        t.tx_data[0] = data[i];
        ESP_ERROR_CHECK(spi_device_polling_transmit(display_spi_device, &t));
    }
}

static void display_sleep()
{
    display_send_cmd(&display_sleep_cmds[0]);
}

static void display_init_hw(bool fast)
{
    for (int i = 0; i < sizeof(display_init_geometry) / sizeof(display_cmd_t); i++) {
        display_send_cmd(&display_init_geometry[i]);
    }
    if (fast) {
        for (int i = 0; i < sizeof(display_init_fast_cmds) / sizeof(display_cmd_t); i++) {
            display_send_cmd(&display_init_fast_cmds[i]);
        }
    } else {
        for (int i = 0; i < sizeof(display_init_cmds) / sizeof(display_cmd_t); i++) {
            display_send_cmd(&display_init_cmds[i]);
        }
    }
}

void display_update(bool fast)
{
    if (fast) {
        for (int i = 0; i < sizeof(display_update_fast_cmds) / sizeof(display_cmd_t); i++) {
            display_send_cmd(&display_update_fast_cmds[i]);
        }
    } else {
        for (int i = 0; i < sizeof(display_update_cmds) / sizeof(display_cmd_t); i++) {
            display_send_cmd(&display_update_cmds[i]);
        }
    }
}

void display_start(bool fast)
{
    spi_device_acquire_bus(display_spi_device, portMAX_DELAY);
    display_reset_hw();
    display_init_hw(fast);
}

void display_finish(bool fast)
{
    display_update(fast);
    display_sleep();
    spi_device_release_bus(display_spi_device);
}

void display_finish_partial()
{
    for (int i = 0; i < sizeof(display_update_window_cmds) / sizeof(display_cmd_t); i++) {
        display_send_cmd(&display_update_window_cmds[i]);
    }
    display_sleep();
    spi_device_release_bus(display_spi_device);
}

static void display_update_window()
{
    for (int i = 0; i < sizeof(display_update_window_cmds) / sizeof(display_cmd_t); i++) {
        display_send_cmd(&display_update_window_cmds[i]);
    }
}

void display_init()
{
    display_gpio_init();
    // init spi
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = DISPLAY_PIN_DATA,
        .sclk_io_num = DISPLAY_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .isr_cpu_id = 0,
        .max_transfer_sz = 32
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED));
    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .clock_speed_hz = DISPLAY_CLOCK_MHZ * 1000 * 1000, // TODO: seto to 10-20 Mhz, 20Mhz is max for display
        .mode = 0,
        .queue_size = 1,
        .spics_io_num = DISPLAY_PIN_CS,
        .flags = SPI_DEVICE_HALFDUPLEX
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &display_spi_device));
}

void display_deinit()
{
    display_sleep();
    spi_bus_remove_device(display_spi_device);
    spi_bus_free(SPI2_HOST);
    gpio_reset_pin(DISPLAY_PIN_CS);
    gpio_reset_pin(DISPLAY_PIN_DC);
    gpio_reset_pin(DISPLAY_PIN_RES);
    gpio_reset_pin(DISPLAY_PIN_BUSY);
    gpio_reset_pin(DISPLAY_PIN_CLK);
    gpio_reset_pin(DISPLAY_PIN_DATA);
}

bool display_busy()
{
    return gpio_get_level(DISPLAY_PIN_BUSY) == 1;
}

void display_fill_black()
{
    display_cmd_t cmd_data_dir = {0x11, false, false, 1, {0x03}}; // x inc, y inc
    display_send_cmd(&cmd_data_dir);
    display_send_cmd_byte_only(0x24);
    for (int i = 0; i < DISPLAY_WIDTH*DISPLAY_HEIGHT/8; i++) {
        display_send_data_byte_only(0x00);
    }
    // reset pos to 0,0
    for (int i = 0; i < sizeof(display_init_cmds) / sizeof(display_cmd_t); i++) {
        display_send_cmd(&display_init_cmds[i]);
    }
    display_send_cmd_byte_only(0x26);
    for (int i = 0; i < DISPLAY_WIDTH*DISPLAY_HEIGHT/8; i++) {
        display_send_data_byte_only(0x00);
    }
}

void display_fill_white()
{
    display_cmd_t cmd_data_dir = {0x11, false, false, 1, {0x03}}; // x inc, y inc
    display_send_cmd(&cmd_data_dir);
    display_send_cmd_byte_only(0x24);
    for (int i = 0; i < DISPLAY_WIDTH*DISPLAY_HEIGHT/8; i++) {
        display_send_data_byte_only(0xff);
    }
    // reset pos to 0,0
    for (int i = 0; i < sizeof(display_init_cmds) / sizeof(display_cmd_t); i++) {
        display_send_cmd(&display_init_cmds[i]);
    }
    display_send_cmd_byte_only(0x26);
    for (int i = 0; i < DISPLAY_WIDTH*DISPLAY_HEIGHT/8; i++) {
        display_send_data_byte_only(0xff);
    }
}

void display_show_bitmap(const uint8_t *bitmap, const uint8_t width, const uint8_t height)
{
    display_cmd_t cmd_data_dir = {0x11, false, false, 1, {0x03}}; // x inc, y inc
    display_send_cmd(&cmd_data_dir);
    display_send_cmd_byte_only(0x24);
    display_send_data((uint8_t *)bitmap, width*height/8);
}

void display_show_bitmap_file(FILE *file, const uint8_t width, const uint8_t height)
{
    display_cmd_t cmd_data_dir = {0x11, false, false, 1, {0x03}}; // x inc, y inc
    display_send_cmd(&cmd_data_dir);
    display_send_cmd_byte_only(0x24);
    fseek(file, 0, SEEK_SET);
    for (int i = 0; i < width*height/8; i++) {
        uint8_t data;
        uint8_t read = fread(&data, 1, 1, file);
        if (read > 0) {
            display_send_data_byte_only(data);
        } else {
            display_send_data_byte_only(0x00); // black if no data
        }
    }
    fseek(file, 0, SEEK_SET);
    // reset pos to 0,0
    for (int i = 0; i < sizeof(display_init_cmds) / sizeof(display_cmd_t); i++) {
        display_send_cmd(&display_init_cmds[i]);
    }
    display_send_cmd_byte_only(0x26);
    for (int i = 0; i < width*height/8; i++) {
        uint8_t data;
        uint8_t read = fread(&data, 1, 1, file);
        if (read > 0) {
            display_send_data_byte_only(data);
        } else {
            display_send_data_byte_only(0x00); // black if no data
        }
    }
}

void display_show_bitmap_file_at(FILE *file, const uint8_t width, const uint8_t height, const uint8_t x, const uint8_t y, bool invert)
{
    display_cmd_t cmd_data_dir = {0x11, false, false, 1, {0x03}}; // x inc, y inc
    display_send_cmd(&cmd_data_dir);
    fseek(file, 0, SEEK_SET);
    for (int j = 0; j < height; j++) {
        int fpos_img = ftell(file);
        display_cmd_t cmd_x = {0x4e, false, false, 1, {x/8}}; // reset x to start
        display_cmd_t cmd_y = {0x4f, false, false, 2, {(y+j)%256, (y+j)/256}}; // set y to current line
        display_send_cmd(&cmd_x);
        display_send_cmd(&cmd_y);
        display_send_cmd_byte_only(0x24);
        for (int i = 0; i < width/8; i++) {
            uint8_t data;
            uint8_t read = fread(&data, 1, 1, file);
            if (invert) data = ~data;
            if (read > 0) display_send_data_byte_only(data);
            else display_send_data_byte_only(0x00); // black if no data
        }
        fseek(file, fpos_img, SEEK_SET);
        display_send_cmd(&cmd_x);
        display_send_cmd(&cmd_y);
        display_send_cmd_byte_only(0x26);
        for (int i = 0; i < width/8; i++) {
            uint8_t data;
            uint8_t read = fread(&data, 1, 1, file);
            if (invert) data = ~data;
            // data = ~data;
            if (read > 0) display_send_data_byte_only(data);
            else display_send_data_byte_only(0x00); // black if no data
        }
    }
}

void display_show_bitmap_file_at_with_mask(FILE *file_img, FILE *file_mask, const uint8_t width, const uint8_t height, const uint8_t x, const uint8_t y, bool invert_image, bool invert_mask)
{
    display_cmd_t cmd_data_dir = {0x11, false, false, 1, {0x03}}; // x inc, y inc
    display_send_cmd(&cmd_data_dir);
    fseek(file_img, 0, SEEK_SET);
    fseek(file_mask, 0, SEEK_SET);
    for (int j = 0; j < height; j++) {
        int fpos_img = ftell(file_img);
        int fpos_mask = ftell(file_mask);
        display_cmd_t cmd_x = {0x4e, false, false, 1, {x/8}}; // reset x to start
        display_cmd_t cmd_y = {0x4f, false, false, 2, {(y+j)%256, (y+j)/256}}; // set y to current line
        display_send_cmd(&cmd_x);
        display_send_cmd(&cmd_y);
        display_send_cmd_byte_only(0x24);
        for (int i = 0; i < width/8; i++) {
            uint8_t data;
            uint8_t read = fread(&data, 1, 1, file_img);
            uint8_t mask;
            fread(&mask, 1, 1, file_mask);
            if (invert_image) data = ~data;
            if (invert_mask) mask = ~mask;
            data = mask | data;
            if (read > 0) display_send_data_byte_only(data);
            else display_send_data_byte_only(0x00); // black if no data
        }
        fseek(file_img, fpos_img, SEEK_SET);
        fseek(file_mask, fpos_mask, SEEK_SET);
        display_send_cmd(&cmd_x);
        display_send_cmd(&cmd_y);
        display_send_cmd_byte_only(0x26);
        for (int i = 0; i < width/8; i++) {
            uint8_t data;
            uint8_t read = fread(&data, 1, 1, file_img);
            uint8_t mask;
            fread(&mask, 1, 1, file_mask);
            if (invert_image) data = ~data;
            if (invert_mask) mask = ~mask;
            data = mask | data;
            // data = ~data;
            if (read > 0) display_send_data_byte_only(data);
            else display_send_data_byte_only(0x00); // black if no data
        }
    }
}

void display_text_at(uint8_t x, uint8_t y, const char *text)
{
    const char *TAG = "display_text_at";
    const uint8_t font_width = font_Sinclair_S[0];
    const uint8_t font_height = font_Sinclair_S[1];
    const uint8_t font_start = font_Sinclair_S[2];
    const uint8_t font_end = font_Sinclair_S[3];
    uint8_t text_len = strlen(text);
    if (text_len == 0 || x > DISPLAY_WIDTH || y > DISPLAY_HEIGHT) {
        ESP_LOGE(TAG, "Invalid text or position");
        return;
    }
    if (text_len > (DISPLAY_WIDTH - x) / font_width) {
        text_len = (DISPLAY_WIDTH - x) / font_width;
    }
    display_cmd_t cmd_data_dir = {0x11, false, false, 1, {0x07}}; // x inc, y inc, update Y on ram write
    display_send_cmd(&cmd_data_dir);
    // display_cmd_t cmd_x = {0x44, false, false, 2, {(DISPLAY_WIDTH-x)/8, (DISPLAY_WIDTH-x)/8+text_len-1}};
    // display_cmd_t cmd_y = {0x44, false, false, 4, {y%256, y/256, y%256+font_height-1, y/256+font_height-1}};
    display_cmd_t cmd_x = {0x4e, false, false, 1, {x/8}}; // start x
    display_cmd_t cmd_y = {0x4f, false, false, 2, {y%256, y/256}}; // start y
    display_send_cmd(&cmd_x);
    display_send_cmd(&cmd_y);
    // for (int line = 0; line < font_height; line++) {
    //     for (int i = 0; i < text_len; i++) {
    //         char c = text[i];
    //         if (c < font_start || c > font_end) {
    //             c = font_start;
    //         }
    //         uint8_t font_index = (c - font_start) * font_height + line + 4;
    //         // display_send_data_byte_only(font_Sinclair_S[font_index]);
    //         display_send_data_byte_only(0x00);
    //     }
    // }
    // for (int line = 0; line < 6; line++) {
    //     display_send_data_byte_only(0x00);
    //     // display_send_data_byte_only(0x00);
    // }
    for (int i = 0; i < text_len; i++) {
        char c = text[i];
        if (c < font_start || c > (font_end+font_start)) {
            c = font_start;
        }
        display_send_cmd_byte_only(0x24);
        uint16_t font_index = (c - font_start) * font_height + 4;
        // ESP_LOGI(TAG, "Char: %c, index: %d", c, font_index);
        for (int line = 0; line < font_height; line++) {
            display_send_data_byte_only(~font_Sinclair_S[font_index + line]);
        }
        display_send_cmd(&cmd_x);
        display_send_cmd(&cmd_y);
        display_send_cmd_byte_only(0x26);
        for (int line = 0; line < font_height; line++) {
            display_send_data_byte_only(~font_Sinclair_S[font_index + line]);
        }
        cmd_x.data[0] += 1;
        display_send_cmd(&cmd_x);
        display_send_cmd(&cmd_y);
    }
}

// static void display_clear_window(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
// {
//     if (x > DISPLAY_WIDTH || y > DISPLAY_HEIGHT || width == 0 || height == 0) {
//         return;
//     }
//     if (x + width > DISPLAY_WIDTH) {
//         width = DISPLAY_WIDTH - x;
//     }
//     if (y + height > DISPLAY_HEIGHT) {
//         height = DISPLAY_HEIGHT - y;
//     }
    
//     display_cmd_t cmd_x = {0x4e, false, false, 1, {x/8}}; // start x
//     display_cmd_t cmd_y = {0x4f, false, false, 2, {y%256, y/256}}; // start y
//     display_cmd_t cmd_win_x = {0x44, false, false, 2, {x/8, (x+width)/8-1}};
//     display_cmd_t cmd_win_y = {0x45, false, false, 4, {y%256, y/256, (y+height-1)%256, (y+height-1)/256}};
//     // spi_device_acquire_bus(display_spi_device, portMAX_DELAY);
//     // display_init_hw(true, true);
//     display_send_cmd(&cmd_win_x);
//     display_send_cmd(&cmd_win_y);
//     display_send_cmd(&cmd_x);
//     display_send_cmd(&cmd_y);
//     display_send_cmd_byte_only(0x24);
//     for (int i = 0; i < width*height/8; i++) {
//         display_send_data_byte_only(0x00);
//     }
//     // display_sleep();
//     // spi_device_release_bus(display_spi_device);
// }

void display_text_center_at(uint8_t y, const char *text)
{
    const uint8_t font_width = font_Sinclair_S[0];
    uint8_t text_len = strlen(text);
    uint8_t x = (DISPLAY_WIDTH - text_len * font_width) / 2;
    display_text_at(x, y, text);
}

void display_fill_rect_at(const uint8_t width, const uint8_t height, uint8_t x, uint8_t y)
{
    display_cmd_t cmd_data_dir = {0x11, false, false, 1, {0x03}}; // x inc, y inc
    display_send_cmd(&cmd_data_dir);
    for (int j = 0; j < height; j++) {
        display_cmd_t cmd_x = {0x4e, false, false, 1, {x/8}}; // start x
        display_cmd_t cmd_y = {0x4f, false, false, 2, {(y+j)%256, (y+j)/256}}; // start y
        display_send_cmd(&cmd_x);
        display_send_cmd(&cmd_y);
        display_send_cmd_byte_only(0x24);
        for (int i = 0; i < width/8; i++) {
            display_send_data_byte_only(0x00);
        }
        display_send_cmd(&cmd_x);
        display_send_cmd(&cmd_y);
        display_send_cmd_byte_only(0x26);
        for (int i = 0; i < width/8; i++) {
            display_send_data_byte_only(0x00);
        }
    }
}