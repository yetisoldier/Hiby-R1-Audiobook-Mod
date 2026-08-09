#include <stdio.h>
#include "music_catalog.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: music_catalog_cleanup_test DB\n");
        return 2;
    }
    const char *paths[] = {argv[1]};
    music_catalog_cleanup_result_t result = {0};
    int rc = music_catalog_remove_audiobooks(paths, 1, &result);
    printf("checked=%d changed=%d removed=%d failed=%d rc=%d\n",
           result.databases_checked, result.databases_changed,
           result.audiobook_rows_removed, result.databases_failed, rc);
    return rc == 0 ? 0 : 1;
}
