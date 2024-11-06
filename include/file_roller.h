#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct
{
    char *name;
} fl_file_t;

void fl_init(char *path);
void fl_deinit();
bool fl_next(fl_file_t *file);
bool fl_prev(fl_file_t *file);