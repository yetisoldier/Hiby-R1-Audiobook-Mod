#!/bin/sh
set -u

BASE=${AUDIOBOOK_BASE_DIR:-/usr/data/audiobooks}
HELPER=${AUDIOBOOK_DB_MAINT_HELPER:-$BASE/bin/r1_audiobook_db_maint}
DB=${AUDIOBOOK_DB_PATH:-/usr/data/usrlocal_media.db}
SD_ROOT=${AUDIOBOOK_SD_ROOT:-/usr/data/mnt/sd_0}
AUDIOBOOK_DB_MIRROR_PATHS=${AUDIOBOOK_DB_MIRROR_PATHS:-/data/usrlocal_media.db $SD_ROOT/usrlocal_media.db}
AUDIOBOOKS_DIR=${AUDIOBOOK_DB_AUDIOBOOKS_DIR:-$SD_ROOT/Audiobooks}
MUSIC_DIR=${AUDIOBOOK_DB_MUSIC_DIR:-$SD_ROOT/Music}
VIEW_ROOT=${AUDIOBOOK_VIEW_ROOT:-$SD_ROOT/Audiobooks/_views}
VIEW_GENERATION_ENABLED=${AUDIOBOOK_VIEW_GENERATION_ENABLED:-0}
CATALOG=${AUDIOBOOK_CATALOG:-$BASE/catalog.tsv}
CATALOG_ALBUM_PATTERNS=${AUDIOBOOK_CATALOG_ALBUM_PATTERNS:-$BASE/catalog-albums.txt}
CATALOG_BOOKS=${AUDIOBOOK_CATALOG_BOOKS:-$BASE/catalog-books.tsv}
CATALOG_TITLES=${AUDIOBOOK_CATALOG_TITLES:-$BASE/catalog-view-title.tsv}
CATALOG_AUTHORS=${AUDIOBOOK_CATALOG_AUTHORS:-$BASE/catalog-view-author.tsv}
CATALOG_SERIES=${AUDIOBOOK_CATALOG_SERIES:-$BASE/catalog-view-series.tsv}
REFRESH_REQUEST=${AUDIOBOOK_REFRESH_REQUEST:-$BASE/refresh.request}
SEED_DB=${AUDIOBOOK_DB_SEED:-$BASE/bin/r1_usrlocal_media_seed.db}
LOG=${AUDIOBOOK_DB_MAINT_LOG:-$BASE/db-maint.log}
PID_FILE=${AUDIOBOOK_DB_MAINT_PID:-$BASE/db-maint.pid}
LOCK_DIR=${AUDIOBOOK_DB_MAINT_LOCK:-$BASE/db-maint.lock}
LOG_MAX_BYTES=${AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES:-524288}

BOOT_DELAY_SECONDS=${AUDIOBOOK_DB_BOOT_DELAY_SECONDS:-20}
BOOT_STABLE_TIMEOUT_SECONDS=${AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS:-180}
STABLE_POLL_SECONDS=${AUDIOBOOK_DB_STABLE_POLL_SECONDS:-3}
INTERVAL_SECONDS=${AUDIOBOOK_DB_INTERVAL_SECONDS:-30}
STABLE_SECONDS=${AUDIOBOOK_DB_STABLE_SECONDS:-15}
FULL_REFRESH_INTERVAL_SECONDS=${AUDIOBOOK_DB_FULL_REFRESH_INTERVAL_SECONDS:-0}
RUN_ON_MTIME_ONLY=${AUDIOBOOK_DB_RUN_ON_MTIME_ONLY:-0}
MTIME_ONLY_MIN_RERUN_SECONDS=${AUDIOBOOK_DB_MTIME_ONLY_MIN_RERUN_SECONDS:-0}
ZERO_AUDIO_RETRY_TIMEOUT_SECONDS=${AUDIOBOOK_DB_ZERO_AUDIO_RETRY_TIMEOUT_SECONDS:-600}
ZERO_AUDIO_RETRY_POLL_SECONDS=${AUDIOBOOK_DB_ZERO_AUDIO_RETRY_POLL_SECONDS:-5}
LOCKED_DB_RETRY_TIMEOUT_SECONDS=${AUDIOBOOK_DB_LOCKED_DB_RETRY_TIMEOUT_SECONDS:-600}
LOCKED_DB_RETRY_POLL_SECONDS=${AUDIOBOOK_DB_LOCKED_DB_RETRY_POLL_SECONDS:-5}
LAST_AUDIOBOOK_TRACKS=
TRANSIENT_ZERO_AUDIO_RC=20
TRANSIENT_LOCKED_DB_RC=21

rotate_log_if_needed() {
  case "$LOG_MAX_BYTES" in ''|*[!0-9]*|0) return 0 ;; esac
  [ -f "$LOG" ] || return 0
  size=$(wc -c <"$LOG" 2>/dev/null | awk '{ print $1 }')
  case "$size" in ''|*[!0-9]*) return 0 ;; esac
  [ "$size" -le "$LOG_MAX_BYTES" ] && return 0
  mv -f "$LOG" "$LOG.1" 2>/dev/null || return 0
  printf '%s rotated log previous_size=%s max=%s previous=%s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$size" "$LOG_MAX_BYTES" "$LOG.1" >>"$LOG"
}

log() {
  rotate_log_if_needed
  printf '%s %s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$*" >>"$LOG"
}

now_seconds() {
  date '+%s' 2>/dev/null || echo 0
}

db_signature() {
  [ -s "$DB" ] || return 1
  path_signature "$DB"
}

path_signature() {
  sig_path=$1
  [ -s "$sig_path" ] || return 1
  size=$(wc -c <"$sig_path" 2>/dev/null | awk '{ print $1 }')
  mtime=$(date -r "$sig_path" '+%s' 2>/dev/null || ls --full-time "$sig_path" 2>/dev/null | awk '{ print $6 " " $7 " " $8 }')
  [ -n "$size" ] && [ -n "$mtime" ] || return 1
  printf '%s:%s\n' "$size" "$mtime"
}

db_size_signature() {
  [ -s "$DB" ] || return 1
  wc -c <"$DB" 2>/dev/null | awk '{ print $1 }'
}

signature_size() {
  sig_value=$1
  case "$sig_value" in
    *:*) printf '%s\n' "${sig_value%%:*}" ;;
    *) printf '%s\n' "$sig_value" ;;
  esac
}

mirror_db_signature() {
  mirror_seen=" $DB "
  for mirror_db in $AUDIOBOOK_DB_MIRROR_PATHS; do
    [ -n "$mirror_db" ] || continue
    case "$mirror_seen" in
      *" $mirror_db "*) continue ;;
    esac
    mirror_seen="$mirror_seen$mirror_db "
    mirror_sig=$(path_signature "$mirror_db" 2>/dev/null || true)
    [ -n "$mirror_sig" ] || continue
    printf '%s=%s;' "$mirror_db" "$mirror_sig"
  done
}

refresh_request_signature() {
  path_signature "$REFRESH_REQUEST" 2>/dev/null || true
}

has_audiobook_audio_files() {
  [ -d "$AUDIOBOOKS_DIR" ] || return 1
  find "$AUDIOBOOKS_DIR" -type f 2>/dev/null | while IFS= read -r audio_path; do
    case "$audio_path" in
      *.[mM][pP]3|*.[mM]4[bB]|*.[mM]4[aA]|*.[fF][lL][aA][cC]|*.[wW][aA][vV]|*.[aA][aA][cC]|*.[oO][gG][gG]|*.[oO][pP][uU][sS]|*.[aA][pP][eE]|*.[dD][sS][fF]|*.[dD][fF][fF])
        printf 'found\n'
        break
        ;;
    esac
  done | grep -q .
}

ensure_db_seeded() {
  reason=$1
  if [ -s "$DB" ]; then
    return 0
  fi
  if [ ! -f "$SEED_DB" ]; then
    log "seed-skip reason=$reason db-missing seed-missing db=$DB seed=$SEED_DB"
    return 1
  fi
  db_parent=${DB%/*}
  if [ "$db_parent" != "$DB" ]; then
    mkdir -p "$db_parent"
  fi
  if cp -f "$SEED_DB" "$DB"; then
    chmod 666 "$DB" 2>/dev/null || true
    sync
    log "seeded-db reason=$reason db=$DB seed=$SEED_DB"
    return 0
  fi
  log "seed-failed reason=$reason db=$DB seed=$SEED_DB"
  return 1
}

remove_own_lock() {
  lock_pid=$(cat "$LOCK_DIR/pid" 2>/dev/null || true)
  if [ "$lock_pid" = "$$" ]; then
    rm -rf "$LOCK_DIR" 2>/dev/null || true
  fi
}

cleanup() {
  remove_own_lock
  rm -f "$PID_FILE"
}

pid_is_db_watcher() {
  check_pid=$1
  [ -n "$check_pid" ] || return 1
  [ -d "/proc/$check_pid" ] || return 1

  if [ -r "/proc/$check_pid/cmdline" ]; then
    cmdline=$(tr '\000' ' ' <"/proc/$check_pid/cmdline" 2>/dev/null || true)
    case "$cmdline" in
      *r1_audiobook_db_watch.sh*) return 0 ;;
    esac
  fi

  ps_line=$(ps | awk -v pid="$check_pid" '$1 == pid { $1=""; print }' 2>/dev/null | head -n 1)
  case "$ps_line" in
    *r1_audiobook_db_watch.sh*) return 0 ;;
  esac
  return 1
}

claim_lock() {
  if mkdir "$LOCK_DIR" 2>/dev/null; then
    echo $$ >"$LOCK_DIR/pid" 2>/dev/null || true
    return 0
  fi

  old_pid=$(cat "$LOCK_DIR/pid" 2>/dev/null || true)
  case "$old_pid" in
    ''|*[!0-9]*) old_pid= ;;
  esac
  if [ -n "$old_pid" ] && [ -d "/proc/$old_pid" ]; then
    if pid_is_db_watcher "$old_pid"; then
      log "exit reason=already-running pid=$old_pid lock=$LOCK_DIR"
      exit 0
    fi
    log "stale-lock-live-pid-not-watcher pid=$old_pid lock=$LOCK_DIR"
  fi

  rm -rf "$LOCK_DIR" 2>/dev/null || true
  if mkdir "$LOCK_DIR" 2>/dev/null; then
    echo $$ >"$LOCK_DIR/pid" 2>/dev/null || true
    log "recovered-stale-lock lock=$LOCK_DIR"
    return 0
  fi

  log "exit reason=lock-unavailable lock=$LOCK_DIR"
  exit 0
}

run_maint_one_db() {
  reason=$1
  target_db=$2
  role=$3
  [ -x "$HELPER" ] || {
    log "skip reason=$reason helper-not-executable helper=$HELPER"
    return 1
  }
  [ -s "$target_db" ] || {
    log "skip reason=$reason role=$role db-missing db=$target_db"
    return 1
  }
  log "run reason=$reason role=$role db=$target_db music=$MUSIC_DIR audiobooks=$AUDIOBOOKS_DIR"
  helper_output="$BASE/db-maint-run.$$.$role"
  view_args=
  case "$VIEW_GENERATION_ENABLED" in
    1|true|yes|on) view_args="--view-root $VIEW_ROOT" ;;
  esac
  # shellcheck disable=SC2086
  "$HELPER" \
    --db "$target_db" \
    --sd-root "$SD_ROOT" \
    --music-dir "$MUSIC_DIR" \
    --audiobooks-dir "$AUDIOBOOKS_DIR" \
    $view_args \
    --base-dir "$BASE" \
    --catalog "$CATALOG" \
    --album-patterns "$CATALOG_ALBUM_PATTERNS" \
    --books-catalog "$CATALOG_BOOKS" \
    --titles-catalog "$CATALOG_TITLES" \
    --authors-catalog "$CATALOG_AUTHORS" \
    --series-catalog "$CATALOG_SERIES" \
    --verbose >"$helper_output" 2>&1
  rc=$?
  helper_locked=0
  if [ -f "$helper_output" ]; then
    if grep -qi "database is locked" "$helper_output" 2>/dev/null; then
      helper_locked=1
    fi
    cat "$helper_output" >>"$LOG"
    LAST_AUDIOBOOK_TRACKS=$(awk -F': ' '/^audiobook tracks:/ { print $2 }' "$helper_output" | tail -n 1 | awk '{ print $1 }')
    rm -f "$helper_output"
  fi
  rotate_log_if_needed
  if [ "$rc" -eq 0 ]; then
    if [ "${LAST_AUDIOBOOK_TRACKS:-}" = 0 ] && has_audiobook_audio_files; then
      log "defer-zero-audiobooks reason=$reason role=$role db=$target_db audio-files-present=1"
      return "$TRANSIENT_ZERO_AUDIO_RC"
    fi
    log "done reason=$reason role=$role db=$target_db audiobook_tracks=${LAST_AUDIOBOOK_TRACKS:-unknown}"
  else
    if [ "$helper_locked" = 1 ]; then
      log "failed-locked reason=$reason role=$role db=$target_db rc=$rc"
      return "$TRANSIENT_LOCKED_DB_RC"
    fi
    log "failed reason=$reason role=$role db=$target_db rc=$rc"
  fi
  return "$rc"
}

copy_primary_to_mirror() {
  reason=$1
  mirror_db=$2
  [ -s "$DB" ] || {
    log "mirror-copy-skip reason=$reason source-missing source=$DB mirror=$mirror_db"
    return 1
  }
  mirror_parent=${mirror_db%/*}
  if [ "$mirror_parent" != "$mirror_db" ]; then
    mkdir -p "$mirror_parent" 2>/dev/null || true
  fi
  mirror_tmp="$mirror_db.tmp.$$"
  if cp -f "$DB" "$mirror_tmp" 2>/dev/null && mv -f "$mirror_tmp" "$mirror_db" 2>/dev/null; then
    chmod 666 "$mirror_db" 2>/dev/null || true
    log "mirror-copy reason=$reason source=$DB mirror=$mirror_db"
    return 0
  fi
  rm -f "$mirror_tmp" 2>/dev/null || true
  log "mirror-copy-failed reason=$reason source=$DB mirror=$mirror_db"
  return 1
}

copy_db_to_primary() {
  reason=$1
  source_db=$2
  [ -s "$source_db" ] || {
    log "primary-copy-skip reason=$reason source-missing source=$source_db primary=$DB"
    return 1
  }
  [ "$source_db" = "$DB" ] && return 0
  primary_parent=${DB%/*}
  if [ "$primary_parent" != "$DB" ]; then
    mkdir -p "$primary_parent" 2>/dev/null || true
  fi
  primary_tmp="$DB.tmp.$$"
  if cp -f "$source_db" "$primary_tmp" 2>/dev/null && mv -f "$primary_tmp" "$DB" 2>/dev/null; then
    chmod 666 "$DB" 2>/dev/null || true
    log "primary-copy reason=$reason source=$source_db primary=$DB"
    return 0
  fi
  rm -f "$primary_tmp" 2>/dev/null || true
  log "primary-copy-failed reason=$reason source=$source_db primary=$DB"
  return 1
}

db_maintenance_status() {
  target_db=$1
  role=$2
  [ -x "$HELPER" ] || return 1
  [ -s "$target_db" ] || return 1
  helper_output="$BASE/db-maint-check.$$.$role"
  "$HELPER" --db "$target_db" --needs-maintenance --verbose >"$helper_output" 2>&1
  rc=$?
  if [ -f "$helper_output" ]; then
    if [ "$rc" -ne 0 ] && [ "$rc" -ne 10 ]; then
      cat "$helper_output" >>"$LOG"
    fi
    rm -f "$helper_output"
  fi
  case "$rc" in
    10)
      log "check-needs-maint role=$role db=$target_db"
      ;;
    0)
      ;;
    *)
      log "check-failed role=$role db=$target_db rc=$rc"
      ;;
  esac
  return "$rc"
}

promote_clean_sd_db() {
  reason=$1
  mode=$2
  sd_db="$SD_ROOT/usrlocal_media.db"
  [ -s "$sd_db" ] || return 1
  [ "$sd_db" = "$DB" ] && return 1

  if db_maintenance_status "$sd_db" sd-promote-check; then
    :
  else
    sd_status=$?
    [ "$sd_status" = 0 ] || return 1
  fi

  case "$mode" in
    force)
      ;;
    if-primary-needs)
      db_maintenance_status "$DB" primary-promote-check
      primary_status=$?
      [ "$primary_status" = 10 ] || return 1
      ;;
    *)
      return 1
      ;;
  esac

  copy_db_to_primary "$reason" "$sd_db" || return 1
  LAST_AUDIOBOOK_TRACKS=
  run_maint_one_db "$reason-promoted-sd" "$DB" primary
  return $?
}

run_maint() {
  reason=$1
  LAST_AUDIOBOOK_TRACKS=
  primary_tracks=
  promote_mode=if-primary-needs
  case "$reason" in
    mirror-stable*) promote_mode=force ;;
  esac
  promote_clean_sd_db "$reason" "$promote_mode"
  promote_rc=$?
  case "$promote_rc" in
    0|"$TRANSIENT_ZERO_AUDIO_RC"|"$TRANSIENT_LOCKED_DB_RC")
      primary_rc=$promote_rc
      ;;
    *)
    run_maint_one_db "$reason" "$DB" primary
    primary_rc=$?
      ;;
  esac
  primary_tracks=${LAST_AUDIOBOOK_TRACKS:-}

  mirror_seen=" $DB "
  for mirror_db in $AUDIOBOOK_DB_MIRROR_PATHS; do
    [ -n "$mirror_db" ] || continue
    case "$mirror_seen" in
      *" $mirror_db "*) continue ;;
    esac
    mirror_seen="$mirror_seen$mirror_db "
    case "$primary_rc" in
      "$TRANSIENT_ZERO_AUDIO_RC"|"$TRANSIENT_LOCKED_DB_RC")
        log "mirror-skip reason=$reason db=$mirror_db primary-transient rc=$primary_rc"
        continue
        ;;
    esac
    if [ -s "$mirror_db" ]; then
      if [ "$primary_rc" -eq 0 ]; then
        copy_primary_to_mirror "$reason" "$mirror_db" || run_maint_one_db "$reason" "$mirror_db" mirror || true
      else
        run_maint_one_db "$reason" "$mirror_db" mirror || true
      fi
    else
      log "mirror-skip reason=$reason db=$mirror_db missing"
    fi
  done

  LAST_AUDIOBOOK_TRACKS=$primary_tracks
  return "$primary_rc"
}

db_needs_maint_one_db() {
  db_maintenance_status "$1" "$2"
  [ "$?" = 10 ]
}

any_db_needs_maintenance() {
  if db_needs_maint_one_db "$DB" primary; then
    return 0
  fi

  mirror_seen=" $DB "
  for mirror_db in $AUDIOBOOK_DB_MIRROR_PATHS; do
    [ -n "$mirror_db" ] || continue
    case "$mirror_seen" in
      *" $mirror_db "*) continue ;;
    esac
    mirror_seen="$mirror_seen$mirror_db "
    if db_needs_maint_one_db "$mirror_db" mirror; then
      return 0
    fi
  done
  return 1
}

retry_zero_audiobooks_if_needed() {
  reason=$1
  case "${LAST_AUDIOBOOK_TRACKS:-}" in
    0) ;;
    *) return 0 ;;
  esac
  case "$ZERO_AUDIO_RETRY_TIMEOUT_SECONDS" in ''|*[!0-9]*) return 0 ;; esac
  [ "$ZERO_AUDIO_RETRY_TIMEOUT_SECONDS" -gt 0 ] || return 0
  case "$ZERO_AUDIO_RETRY_POLL_SECONDS" in ''|*[!0-9]*|0) retry_poll=5 ;; *) retry_poll=$ZERO_AUDIO_RETRY_POLL_SECONDS ;; esac

  start_time=$(now_seconds)
  log "zero-audiobook-retry-start reason=$reason timeout=${ZERO_AUDIO_RETRY_TIMEOUT_SECONDS}s poll=${retry_poll}s dir=$AUDIOBOOKS_DIR"
  while :; do
    if has_audiobook_audio_files; then
      log "zero-audiobook-retry-ready reason=$reason"
      run_maint "$reason-audiobook-ready"
      retry_rc=$?
      case "$retry_rc" in
        "$TRANSIENT_LOCKED_DB_RC")
          log "zero-audiobook-retry-locked reason=$reason"
          ;;
        *)
          case "${LAST_AUDIOBOOK_TRACKS:-}" in
            0)
              log "zero-audiobook-retry-still-empty reason=$reason"
              ;;
            *)
              return 0
              ;;
          esac
          ;;
      esac
      case "$retry_rc" in
        "$TRANSIENT_ZERO_AUDIO_RC"|"$TRANSIENT_LOCKED_DB_RC"|0) ;;
        *)
          log "zero-audiobook-retry-stop reason=$reason rc=$retry_rc"
          return 0
          ;;
      esac
    fi

    now=$(now_seconds)
    elapsed=$((now - start_time))
    if [ "$elapsed" -ge "$ZERO_AUDIO_RETRY_TIMEOUT_SECONDS" ]; then
      log "zero-audiobook-retry-timeout reason=$reason timeout=${ZERO_AUDIO_RETRY_TIMEOUT_SECONDS}s"
      return 0
    fi
    sleep "$retry_poll"
  done
}

retry_locked_db_if_needed() {
  reason=$1
  rc=$2
  [ "$rc" = "$TRANSIENT_LOCKED_DB_RC" ] || return 0
  case "$LOCKED_DB_RETRY_TIMEOUT_SECONDS" in ''|*[!0-9]*) return 0 ;; esac
  [ "$LOCKED_DB_RETRY_TIMEOUT_SECONDS" -gt 0 ] || return 0
  case "$LOCKED_DB_RETRY_POLL_SECONDS" in ''|*[!0-9]*|0) retry_poll=5 ;; *) retry_poll=$LOCKED_DB_RETRY_POLL_SECONDS ;; esac

  start_time=$(now_seconds)
  log "locked-db-retry-start reason=$reason timeout=${LOCKED_DB_RETRY_TIMEOUT_SECONDS}s poll=${retry_poll}s"
  while :; do
    now=$(now_seconds)
    elapsed=$((now - start_time))
    if [ "$elapsed" -ge "$LOCKED_DB_RETRY_TIMEOUT_SECONDS" ]; then
      log "locked-db-retry-timeout reason=$reason timeout=${LOCKED_DB_RETRY_TIMEOUT_SECONDS}s"
      return 0
    fi

    sleep "$retry_poll"
    run_maint "$reason-db-unlocked"
    retry_rc=$?
    case "$retry_rc" in
      "$TRANSIENT_LOCKED_DB_RC")
        log "locked-db-retry-still-locked reason=$reason"
        ;;
      "$TRANSIENT_ZERO_AUDIO_RC")
        retry_zero_audiobooks_if_needed "$reason-db-unlocked" || true
        return 0
        ;;
      0)
        retry_zero_audiobooks_if_needed "$reason-db-unlocked" || true
        return 0
        ;;
      *)
        log "locked-db-retry-stop reason=$reason rc=$retry_rc"
        return 0
        ;;
    esac
  done
}

run_maint_with_retries() {
  reason=$1
  run_maint "$reason"
  rc=$?
  case "$rc" in
    "$TRANSIENT_LOCKED_DB_RC")
      retry_locked_db_if_needed "$reason" "$rc" || true
      ;;
    "$TRANSIENT_ZERO_AUDIO_RC"|0)
      retry_zero_audiobooks_if_needed "$reason" || true
      ;;
  esac
  return 0
}

wait_for_stable_db() {
  reason=$1
  timeout=$2
  case "$timeout" in ''|*[!0-9]*) timeout=0 ;; esac
  case "$STABLE_POLL_SECONDS" in ''|*[!0-9]*|0) stable_poll=3 ;; *) stable_poll=$STABLE_POLL_SECONDS ;; esac

  start_time=$(now_seconds)
  stable_sig=
  stable_since=0
  last_wait_log=

  while :; do
    ensure_db_seeded "$reason-wait" || true
    sig=$(db_signature 2>/dev/null || true)
    now=$(now_seconds)

    if [ -n "$sig" ]; then
      if [ "$sig" != "$stable_sig" ]; then
        stable_sig=$sig
        stable_since=$now
        log "wait-stable reason=$reason sig=$sig"
      else
        age=$((now - stable_since))
        if [ "$age" -ge "$STABLE_SECONDS" ]; then
          log "stable reason=$reason sig=$sig age=${age}s"
          return 0
        fi
      fi
    else
      if [ "$last_wait_log" != missing ]; then
        log "wait-stable reason=$reason db-missing db=$DB"
        last_wait_log=missing
      fi
    fi

    if [ "$timeout" -gt 0 ]; then
      elapsed=$((now - start_time))
      if [ "$elapsed" -ge "$timeout" ]; then
        log "wait-stable-timeout reason=$reason timeout=${timeout}s last_sig=${stable_sig:-}"
        return 1
      fi
    fi

    sleep "$stable_poll"
  done
}

mkdir -p "$BASE" "$BASE/bin" "$BASE/resume.d"
claim_lock
trap 'cleanup' EXIT
trap 'cleanup; exit 0' HUP INT TERM
echo $$ >"$PID_FILE"
case "$RUN_ON_MTIME_ONLY" in 1|true|yes) RUN_ON_MTIME_ONLY=1 ;; *) RUN_ON_MTIME_ONLY=0 ;; esac
case "$MTIME_ONLY_MIN_RERUN_SECONDS" in ''|*[!0-9]*) MTIME_ONLY_MIN_RERUN_SECONDS=0 ;; esac

log "start interval=${INTERVAL_SECONDS}s stable=${STABLE_SECONDS}s boot_stable_timeout=${BOOT_STABLE_TIMEOUT_SECONDS}s zero_audio_retry=${ZERO_AUDIO_RETRY_TIMEOUT_SECONDS}s locked_db_retry=${LOCKED_DB_RETRY_TIMEOUT_SECONDS}s full_refresh=${FULL_REFRESH_INTERVAL_SECONDS}s run_on_mtime_only=${RUN_ON_MTIME_ONLY} mtime_only_min_rerun=${MTIME_ONLY_MIN_RERUN_SECONDS}s view_generation=${VIEW_GENERATION_ENABLED} view_root=$VIEW_ROOT helper=$HELPER seed=$SEED_DB"

sleep "$BOOT_DELAY_SECONDS"

last_sig=
last_size=
last_seen_sig=
last_seen_size=
last_seen_at=0
last_mirror_sig=
last_seen_mirror_sig=
last_seen_mirror_at=0
last_run_at=0
last_refresh_sig=$(refresh_request_signature 2>/dev/null || true)

ensure_db_seeded boot || true
wait_for_stable_db boot "$BOOT_STABLE_TIMEOUT_SECONDS" || true
run_maint_with_retries boot || true
last_sig=$(db_signature 2>/dev/null || true)
last_size=$(db_size_signature 2>/dev/null || true)
last_seen_sig=$last_sig
last_seen_size=$last_size
last_seen_at=$(now_seconds)
last_mirror_sig=$(mirror_db_signature 2>/dev/null || true)
last_seen_mirror_sig=$last_mirror_sig
last_seen_mirror_at=$last_seen_at
last_run_at=$last_seen_at

while :; do
  sleep "$INTERVAL_SECONDS"
  ensure_db_seeded loop || true
  sig=$(db_signature 2>/dev/null || true)
  size=$(db_size_signature 2>/dev/null || true)
  sig_size=$(signature_size "$sig")
  [ -n "$size" ] || size=$sig_size
  now=$(now_seconds)
  refresh_sig=$(refresh_request_signature 2>/dev/null || true)
  if [ -n "$refresh_sig" ] && [ "$refresh_sig" != "$last_refresh_sig" ]; then
    last_refresh_sig=$refresh_sig
    log "manual-refresh-request sig=$refresh_sig"
    run_maint_with_retries manual-refresh || true
    last_sig=$(db_signature 2>/dev/null || echo "$sig")
    last_size=$(db_size_signature 2>/dev/null || echo "$size")
    [ -n "$last_size" ] || last_size=$(signature_size "$last_sig")
    last_mirror_sig=$(mirror_db_signature 2>/dev/null || true)
    last_seen_mirror_sig=$last_mirror_sig
    last_seen_mirror_at=$now
    last_seen_sig=$last_sig
    last_seen_size=$last_size
    last_seen_at=$now
    last_run_at=$now
    continue
  fi
  if [ -z "$sig" ]; then
    continue
  fi
  if [ "$sig" != "$last_seen_sig" ]; then
    last_seen_sig=$sig
    last_seen_size=$size
    last_seen_at=$now
    log "db-change sig=$sig size=${size:-?}"
    continue
  fi
  age=$((now - last_seen_at))
  since_run=$((now - last_run_at))
  if [ "$sig" != "$last_sig" ] && [ "$age" -ge "$STABLE_SECONDS" ]; then
    run_reason=db-stable
    should_run=1
    compare_last_size=$(signature_size "$last_sig")
    [ -n "$compare_last_size" ] || compare_last_size=$last_size
    if [ -n "$size" ] && [ -n "$compare_last_size" ] && [ "$size" = "$compare_last_size" ]; then
      run_reason=db-stable-mtime
      should_run=0
      if [ "$RUN_ON_MTIME_ONLY" = 1 ]; then
        should_run=1
      elif [ "$FULL_REFRESH_INTERVAL_SECONDS" -gt 0 ] && [ "$since_run" -ge "$FULL_REFRESH_INTERVAL_SECONDS" ]; then
        should_run=1
        run_reason=periodic-mtime
      elif [ "$MTIME_ONLY_MIN_RERUN_SECONDS" -gt 0 ] && [ "$since_run" -ge "$MTIME_ONLY_MIN_RERUN_SECONDS" ]; then
        should_run=1
        run_reason=periodic-mtime
      elif any_db_needs_maintenance; then
        should_run=1
        run_reason=content-repair-mtime
      fi
    fi
    if [ "$should_run" = 1 ]; then
      run_maint_with_retries "$run_reason" || true
      last_sig=$(db_signature 2>/dev/null || echo "$sig")
      last_size=$(db_size_signature 2>/dev/null || true)
      [ -n "$last_size" ] || last_size=$(signature_size "$last_sig")
      last_mirror_sig=$(mirror_db_signature 2>/dev/null || true)
      last_seen_mirror_sig=$last_mirror_sig
      last_seen_mirror_at=$now
      last_seen_sig=$last_sig
      last_seen_size=$last_size
      last_seen_at=$now
      last_run_at=$now
    else
      log "skip reason=mtime-only sig=$sig size=${size:-?} since_run=${since_run}s"
      last_sig=$sig
      last_size=$size
      last_seen_sig=$last_sig
      last_seen_size=$last_size
      last_seen_at=$now
    fi
    continue
  fi
  mirror_sig=$(mirror_db_signature 2>/dev/null || true)
  if [ -n "$mirror_sig" ]; then
    if [ "$mirror_sig" != "$last_seen_mirror_sig" ]; then
      last_seen_mirror_sig=$mirror_sig
      last_seen_mirror_at=$now
      log "mirror-db-change sig=$mirror_sig"
      continue
    fi
    mirror_age=$((now - last_seen_mirror_at))
    if [ "$mirror_sig" != "$last_mirror_sig" ] && [ "$mirror_age" -ge "$STABLE_SECONDS" ]; then
      run_maint_with_retries mirror-stable || true
      last_sig=$(db_signature 2>/dev/null || echo "$sig")
      last_size=$(db_size_signature 2>/dev/null || echo "$size")
      last_seen_sig=$last_sig
      last_seen_size=$last_size
      last_seen_at=$now
      last_mirror_sig=$(mirror_db_signature 2>/dev/null || true)
      last_seen_mirror_sig=$last_mirror_sig
      last_seen_mirror_at=$now
      last_run_at=$now
      continue
    fi
  fi
  if [ "$FULL_REFRESH_INTERVAL_SECONDS" -gt 0 ] && [ "$since_run" -ge "$FULL_REFRESH_INTERVAL_SECONDS" ]; then
    run_maint_with_retries periodic || true
    last_sig=$(db_signature 2>/dev/null || echo "$sig")
    last_size=$(db_size_signature 2>/dev/null || echo "$size")
    last_mirror_sig=$(mirror_db_signature 2>/dev/null || true)
    last_seen_mirror_sig=$last_mirror_sig
    last_seen_mirror_at=$now
    last_seen_sig=$last_sig
    last_seen_size=$last_size
    last_seen_at=$now
    last_run_at=$now
  fi
done
