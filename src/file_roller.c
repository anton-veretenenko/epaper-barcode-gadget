#include "file_roller.h"
#include "esp_littlefs.h"
#include "lfs.h"
#include <dirent.h>
#include "esp_system.h"
#include "esp_log.h"

#define FL_MAX_FILES 50
#define FL_MAX_NAME (LFS_NAME_MAX)
#define TAG "file_roller"

typedef struct
{
    fl_file_t files[FL_MAX_FILES];
    uint8_t count;
    int16_t current;
} fl_files_t;
fl_files_t fl_files;
RTC_DATA_ATTR char fl_last_file[FL_MAX_NAME];

void fl_init(char *path)
{
    // list files on the filesystem
    DIR *dir;
    struct dirent *ent;
    fl_files.count = 0;
    fl_files.current = 0;
    if ((dir = opendir(path)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            ent->d_type == DT_DIR ? printf("D ") : printf("F ");
            if (ent->d_type != DT_DIR) {
                char *name = malloc(strlen(ent->d_name) + 1);
                memcpy(name, ent->d_name, strlen(ent->d_name) + 1);
                fl_files.files[fl_files.count].name = name;
                if (strcmp(fl_last_file, ent->d_name) == 0) {
                    fl_files.current = fl_files.count;
                }
                fl_files.count++;
            }
            ESP_LOGI(TAG, "%s", ent->d_name);
        }
        closedir(dir);
    } else {
        perror("Unable to read directory");
    }
}

void fl_deinit()
{
    for (int i = 0; i < fl_files.count; i++) {
        free(fl_files.files[i].name);
    }
}

bool fl_next(fl_file_t *fl_file)
{
    if (fl_files.count == 0) {
        return false;
    }
    fl_files.current++;
    if (fl_files.current >= fl_files.count) {
        fl_files.current = 0;
    }
    *fl_file = fl_files.files[fl_files.current];
    memcpy(fl_last_file, fl_file->name, strlen(fl_file->name)+1);
    return true;
}

bool fl_prev(fl_file_t *fl_file)
{
    if (fl_files.count == 0) {
        return false;
    }
    fl_files.current--;
    if (fl_files.current < 0) {
        fl_files.current = fl_files.count - 1;
    }
    *fl_file = fl_files.files[fl_files.current];
    memcpy(fl_last_file, fl_file->name, strlen(fl_file->name)+1);
    return true;
}