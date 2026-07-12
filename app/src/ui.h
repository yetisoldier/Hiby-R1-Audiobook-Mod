#ifndef R1_AB_UI_H
#define R1_AB_UI_H

#include "cover.h"
#include "db.h"
#include "fb.h"
#include "font.h"
#include "player.h"
#include "resume.h"
#include "touch.h"

typedef enum {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_CONTINUE,
    UI_SCREEN_TITLES,
    UI_SCREEN_NOW_PLAYING,
    UI_SCREEN_CHAPTERS,
    UI_SCREEN_FINISHED,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_BOOKMARKS,
    UI_SCREEN_AUTHORS,
    UI_SCREEN_SERIES,
    UI_SCREEN_FOLDERS,
} ui_screen_id;

typedef struct theme_image {
    uint16_t *pixels;
    int width;
    int height;
    char path[256];
} theme_image;

typedef struct theme_assets {
    theme_image icon;
    theme_image divider;
} theme_assets;

typedef struct ui_context {
    fb_context fb;
    font_context font;
    touch_context touch;
    audiobook_config cfg;
    ui_screen_id screen;
    book_list books;
    track_list tracks;
    track_list chapters;
    cover_art cover;
    book_list continue_books;
    book_list finished_books;
    bookmark_list bookmarks;
    char_list authors;
    char_list series;
    char_list folders;
    int selected_book;
    int selected_track;
    int selected_list;
    int scroll;
    theme_assets assets;
} ui_context;

int ui_init(ui_context *ui, const audiobook_config *cfg);
void ui_shutdown(ui_context *ui);
int ui_load_titles(ui_context *ui, audiobook_db *db);
int ui_load_continue(ui_context *ui, audiobook_db *db);
int ui_load_finished(ui_context *ui, audiobook_db *db);
int ui_load_bookmarks(ui_context *ui, audiobook_db *db);
int ui_load_authors(ui_context *ui, audiobook_db *db);
int ui_load_series(ui_context *ui, audiobook_db *db);
int ui_load_folders(ui_context *ui, audiobook_db *db);
int ui_load_book(ui_context *ui, audiobook_db *db, int index);
int ui_render(ui_context *ui, const audiobook_player *player);
int ui_handle_touch(ui_context *ui, const touch_event *ev, audiobook_player *player, audiobook_db *db, resume_state *resume);

#endif
