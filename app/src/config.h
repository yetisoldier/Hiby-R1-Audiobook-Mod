#ifndef R1_AB_CONFIG_H
#define R1_AB_CONFIG_H

#include <stddef.h>
#include <stdint.h>

typedef struct audiobook_config {
    char app_root[256];
    char library_root[256];
    char db_path[256];
    char cover_cache_dir[256];
    char resume_socket[256];
    char fb_path[64];
    char touch_path[64];
    char pcm_device[64];
    char font_path[256];
    uint32_t scan_interval_ms;
    uint32_t save_interval_ms;
    uint32_t back_skip_ms;
    uint32_t forward_skip_ms;
    float default_speed;
} audiobook_config;

void config_init(audiobook_config *cfg);
void config_load_env(audiobook_config *cfg);

#endif

