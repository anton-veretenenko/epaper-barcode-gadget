#pragma once

/* SSD1681 Controller driver in 4-wire SPI mode */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>

#define DISPLAY_WIDTH 200
#define DISPLAY_HEIGHT 200
#define DISPLAY_PIN_BUSY 12
#define DISPLAY_PIN_RES 14
#define DISPLAY_PIN_DC 27
#define DISPLAY_PIN_CS 26
#define DISPLAY_PIN_CLK 25
#define DISPLAY_PIN_DATA 33
#define DISPLAY_CLOCK_MHZ 10

void display_init();
void display_deinit();
bool display_busy();
void display_start(bool fast);
void display_finish(bool fast);
void display_fill_black();
void display_fill_white();
void display_show_bitmap(const uint8_t *bitmap, const uint8_t width, const uint8_t height);
void display_show_bitmap_file(FILE *file, const uint8_t width, const uint8_t height);
void display_text_at(uint8_t x, uint8_t y, const char *text);
void display_text_center_at(uint8_t y, const char *text);