#include "config.h"
#include "common.h"

#include <stdlib.h>

#include <stdio.h>

void config_init(audiobook_config *cfg) {
    if (!cfg) return;
    *cfg = (audiobook_config){0};
    ab_copy_str(cfg->app_root, sizeof(cfg->app_root), "/usr/data/audiobooks");
    ab_copy_str(cfg->library_root, sizeof(cfg->library_root), "/usr/data/mnt/sd_0/Audiobooks");
    ab_copy_str(cfg->db_path, sizeof(cfg->db_path), "/usr/data/audiobooks/library.db");
    ab_copy_str(cfg->cover_cache_dir, sizeof(cfg->cover_cache_dir), "/usr/data/audiobooks/cache/covers");
    ab_copy_str(cfg->resume_socket, sizeof(cfg->resume_socket), "/usr/data/audiobooks/run/resume.sock");
    ab_copy_str(cfg->fb_path, sizeof(cfg->fb_path), "/dev/fb0");
    ab_copy_str(cfg->touch_path, sizeof(cfg->touch_path), "/dev/input/event1");
    ab_copy_str(cfg->pcm_device, sizeof(cfg->pcm_device), "/dev/snd/pcmC0D0p");
    /* Try the stock device font location first, fall back to our packaged copy. */
    ab_copy_str(cfg->font_path, sizeof(cfg->font_path), "/usr/resource/fonts/msyh.ttf");
    FILE *font_test = fopen(cfg->font_path, "rb");
    if (font_test) {
        fclose(font_test);
    } else {
        ab_copy_str(cfg->font_path, sizeof(cfg->font_path), "/usr/share/audiobooks/fonts/msyh.ttf");
    }
    cfg->scan_interval_ms = 3000;
    cfg->save_interval_ms = 5000;
    cfg->back_skip_ms = 15000;
    cfg->forward_skip_ms = 30000;
    cfg->default_speed = 1.0f;
}

static void env_str(char *dst, size_t dst_len, const char *name) {
    const char *v = getenv(name);
    if (v && v[0]) ab_copy_str(dst, dst_len, v);
}

void config_load_env(audiobook_config *cfg) {
    if (!cfg) return;
    env_str(cfg->app_root, sizeof(cfg->app_root), "AUDIOBOOK_APP_ROOT");
    env_str(cfg->library_root, sizeof(cfg->library_root), "AUDIOBOOK_LIBRARY_ROOT");
    env_str(cfg->db_path, sizeof(cfg->db_path), "AUDIOBOOK_DB_PATH");
    env_str(cfg->cover_cache_dir, sizeof(cfg->cover_cache_dir), "AUDIOBOOK_COVER_CACHE_DIR");
    env_str(cfg->resume_socket, sizeof(cfg->resume_socket), "AUDIOBOOK_RESUME_SOCKET");
    env_str(cfg->fb_path, sizeof(cfg->fb_path), "AUDIOBOOK_FB_PATH");
    env_str(cfg->touch_path, sizeof(cfg->touch_path), "AUDIOBOOK_TOUCH_PATH");
    env_str(cfg->pcm_device, sizeof(cfg->pcm_device), "AUDIOBOOK_PCM_DEVICE");
    env_str(cfg->font_path, sizeof(cfg->font_path), "AUDIOBOOK_FONT_PATH");
}
