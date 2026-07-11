#ifndef R1_AB_SCANNER_H
#define R1_AB_SCANNER_H

#include "db.h"
#include "config.h"

typedef struct library_refresh_report {
    int roots_scanned;
    int books_found;
    int tracks_found;
    int errors;
} library_refresh_report;

int library_refresh(audiobook_db *db, const audiobook_config *cfg, library_refresh_report *report);

#endif

