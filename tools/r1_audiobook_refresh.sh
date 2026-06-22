#!/bin/sh
set -u

BASE=${AUDIOBOOK_BASE_DIR:-/usr/data/audiobooks}
REQUEST=${AUDIOBOOK_REFRESH_REQUEST:-$BASE/refresh.request}
LOG=${AUDIOBOOK_REFRESH_LOG:-$BASE/refresh.log}
HELPER=${AUDIOBOOK_DB_MAINT_HELPER:-$BASE/bin/r1_audiobook_db_maint}
DB=${AUDIOBOOK_DB_PATH:-/usr/data/usrlocal_media.db}
SD_ROOT=${AUDIOBOOK_SD_ROOT:-/usr/data/mnt/sd_0}
AUDIOBOOKS_DIR=${AUDIOBOOK_DB_AUDIOBOOKS_DIR:-$SD_ROOT/Audiobooks}
MUSIC_DIR=${AUDIOBOOK_DB_MUSIC_DIR:-$SD_ROOT/Music}
VIEW_ROOT=${AUDIOBOOK_VIEW_ROOT:-$SD_ROOT/Audiobooks/_views}
CATALOG=${AUDIOBOOK_CATALOG:-$BASE/catalog.tsv}
CATALOG_ALBUM_PATTERNS=${AUDIOBOOK_CATALOG_ALBUM_PATTERNS:-$BASE/catalog-albums.txt}
CATALOG_BOOKS=${AUDIOBOOK_CATALOG_BOOKS:-$BASE/catalog-books.tsv}
CATALOG_TITLES=${AUDIOBOOK_CATALOG_TITLES:-$BASE/catalog-view-title.tsv}
CATALOG_AUTHORS=${AUDIOBOOK_CATALOG_AUTHORS:-$BASE/catalog-view-author.tsv}
CATALOG_SERIES=${AUDIOBOOK_CATALOG_SERIES:-$BASE/catalog-view-series.tsv}
LOCK_DIR=${AUDIOBOOK_REFRESH_LOCK:-$BASE/refresh.lock}

mkdir -p "$BASE" 2>/dev/null || exit 0

log() {
  printf '%s %s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$*" >>"$LOG" 2>/dev/null || true
}

claim_lock() {
  if mkdir "$LOCK_DIR" 2>/dev/null; then
    echo $$ >"$LOCK_DIR/pid" 2>/dev/null || true
    return 0
  fi

  old_pid=$(cat "$LOCK_DIR/pid" 2>/dev/null || true)
  case "$old_pid" in ''|*[!0-9]*) old_pid= ;; esac
  if [ -n "$old_pid" ] && [ -d "/proc/$old_pid" ]; then
    log "refresh already-running lock=$LOCK_DIR pid=$old_pid"
    return 1
  fi

  rm -rf "$LOCK_DIR" 2>/dev/null || true
  if mkdir "$LOCK_DIR" 2>/dev/null; then
    echo $$ >"$LOCK_DIR/pid" 2>/dev/null || true
    log "refresh recovered-stale-lock lock=$LOCK_DIR"
    return 0
  fi

  log "refresh lock-unavailable lock=$LOCK_DIR"
  return 1
}

tmp="$REQUEST.$$"
if date '+%Y-%m-%dT%H:%M:%S%z' >"$tmp" 2>/dev/null; then
  if mv -f "$tmp" "$REQUEST" 2>/dev/null; then
    chmod 666 "$REQUEST" 2>/dev/null || true
    log "refresh requested"
  fi
fi

rm -f "$tmp" 2>/dev/null || true

if ! claim_lock; then
  exit 0
fi
trap 'rm -rf "$LOCK_DIR" 2>/dev/null || true' EXIT HUP INT TERM

if [ ! -x "$HELPER" ]; then
  log "refresh helper-missing helper=$HELPER"
  exit 0
fi
if [ ! -s "$DB" ]; then
  log "refresh db-missing db=$DB"
  exit 0
fi

log "refresh start db=$DB music=$MUSIC_DIR audiobooks=$AUDIOBOOKS_DIR"
"$HELPER" \
  --db "$DB" \
  --sd-root "$SD_ROOT" \
  --music-dir "$MUSIC_DIR" \
  --audiobooks-dir "$AUDIOBOOKS_DIR" \
  --view-root "$VIEW_ROOT" \
  --base-dir "$BASE" \
  --catalog "$CATALOG" \
  --album-patterns "$CATALOG_ALBUM_PATTERNS" \
  --books-catalog "$CATALOG_BOOKS" \
  --titles-catalog "$CATALOG_TITLES" \
  --authors-catalog "$CATALOG_AUTHORS" \
  --series-catalog "$CATALOG_SERIES" \
  --verbose >>"$LOG" 2>&1
rc=$?
if [ "$rc" -eq 0 ]; then
  for mirror_db in /data/usrlocal_media.db "$SD_ROOT/usrlocal_media.db"; do
    [ "$mirror_db" = "$DB" ] && continue
    mirror_parent=${mirror_db%/*}
    [ "$mirror_parent" = "$mirror_db" ] || mkdir -p "$mirror_parent" 2>/dev/null || true
    if cp -f "$DB" "$mirror_db" 2>/dev/null; then
      chmod 666 "$mirror_db" 2>/dev/null || true
      log "refresh mirror-copy mirror=$mirror_db"
    fi
  done
fi
log "refresh done rc=$rc"
exit 0
