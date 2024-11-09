#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_littlefs.h"
#include "lfs.h"

#define FL_MAX_FILES 50
#define FL_MAX_NAME (LFS_NAME_MAX)
#define BARCODES_PATH "/storage/barcode/"

typedef struct
{
    char *name;
} fl_file_t;

void fl_init(char *path);
void fl_deinit();
bool fl_next(fl_file_t *file);
bool fl_prev(fl_file_t *file);
bool fl_delete(char *name);
FILE *fl_init_write(char *name);
bool fl_space_available(uint16_t size);
bool fl_exists(char *name);