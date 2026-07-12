#ifndef R1_AB_SCANNER_H
#define R1_AB_SCANNER_H

#include "db.h"
#include "config.h"

typedef struct library_refresh_report {
    int roots_scanned;
    int books_found;
    int tracks_found;
    int tracks_updated;
    int tracks_unchanged;
    int books_removed;
    int errors;
} library_refresh_report;

int library_refresh(audiobook_db *db, const audiobook_config *cfg, library_refresh_report *report);
int library_scan_incremental(audiobook_db *db, const audiobook_config *cfg, library_refresh_report *report);
int db_has_library(audiobook_db *adb);

#endif

