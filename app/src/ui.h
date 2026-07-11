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
    UI_SCREEN_TITLES,
    UI_SCREEN_NOW_PLAYING,
    UI_SCREEN_CHAPTERS,
    UI_SCREEN_FINISHED,
} ui_screen_id;

typedef struct ui_context {
    fb_context fb;
    font_context font;
    touch_context touch;
    audiobook_config cfg;
    ui_screen_id screen;
    book_list books;
    track_list tracks;
    cover_art cover;
    int selected_book;
    int selected_track;
} ui_context;

int ui_init(ui_context *ui, const audiobook_config *cfg);
void ui_shutdown(ui_context *ui);
int ui_load_titles(ui_context *ui, audiobook_db *db);
int ui_load_book(ui_context *ui, audiobook_db *db, int index);
int ui_render(ui_context *ui, const audiobook_player *player);
int ui_handle_touch(ui_context *ui, const touch_event *ev, audiobook_player *player, audiobook_db *db, resume_state *resume);

#endif

