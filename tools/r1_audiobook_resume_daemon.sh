#!/bin/sh
set -u

BASE_DIR=${AUDIOBOOK_BASE_DIR:-/usr/data/audiobooks}
HELPER=${AUDIOBOOK_HELPER:-$BASE_DIR/bin/r1_audiobook_resume_helper}
MEMSCAN_HELPER=${AUDIOBOOK_MEMSCAN_HELPER:-$BASE_DIR/bin/r1_audiobook_memscan}
DIRECT_OPEN_HELPER=${AUDIOBOOK_DIRECT_OPEN_HELPER:-$BASE_DIR/bin/r1_audiobook_direct_open}
STORE_DIR=${AUDIOBOOK_STORE_DIR:-$BASE_DIR/resume.d}
CATALOG=${AUDIOBOOK_CATALOG:-$BASE_DIR/catalog.tsv}
CATALOG_ALBUM_PATTERNS=${AUDIOBOOK_CATALOG_ALBUM_PATTERNS:-$BASE_DIR/catalog-albums.txt}
CATALOG_BOOKS=${AUDIOBOOK_CATALOG_BOOKS:-$BASE_DIR/catalog-books.tsv}
USER_INI=${AUDIOBOOK_USER_INI:-/usr/data/user.ini}
LOG=${AUDIOBOOK_RESUME_LOG:-$BASE_DIR/resume-daemon.log}
PID_FILE=${AUDIOBOOK_RESUME_PID:-$BASE_DIR/resume-daemon.pid}
CLOSED_INHERITED_SOCKET_FDS=0
PLAYER_PID_CACHE=
CURRENT_PATH_HEX_CACHE=
CURRENT_PATH_VALUE_CACHE=
LOG_MAX_BYTES=${AUDIOBOOK_RESUME_LOG_MAX_BYTES:-524288}

INTERVAL_SECONDS=${AUDIOBOOK_INTERVAL_SECONDS:-5}
IDLE_INTERVAL_SECONDS=${AUDIOBOOK_IDLE_INTERVAL_SECONDS:-3}
BOOK_TITLE_MARKER_IDLE_POLL_SECONDS=${AUDIOBOOK_BOOK_TITLE_MARKER_IDLE_POLL_SECONDS:-5}
BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS=${AUDIOBOOK_BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS:-15}
DIAGNOSTICS_INTERVAL_SECONDS=${AUDIOBOOK_DIAGNOSTICS_INTERVAL_SECONDS:-60}
MIN_SAVE_MS=${AUDIOBOOK_MIN_SAVE_MS:-3000}
SAVE_BUCKET_MS=${AUDIOBOOK_SAVE_BUCKET_MS:-15000}
case "$SAVE_BUCKET_MS" in ''|*[!0-9]*|0) SAVE_BUCKET_MS=15000 ;; esac
RESTORE_ONLY_BEFORE_MS=${AUDIOBOOK_RESTORE_ONLY_BEFORE_MS:-15000}
RESTORE_MIN_MS=${AUDIOBOOK_RESTORE_MIN_MS:-10000}
RESTORE_REWIND_MS=${AUDIOBOOK_RESTORE_REWIND_MS:-0}
RESTORE_RETRY_AFTER_FAILURE_SECONDS=${AUDIOBOOK_RESTORE_RETRY_AFTER_FAILURE_SECONDS:-30}
RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS=${AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS:-300}
FAILED_RESTORE_SKIP_LOG_BUCKET_MS=${AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS:-30000}
NEW_TRACK_COMMIT_MS=${AUDIOBOOK_NEW_TRACK_COMMIT_MS:-15000}
BACKWARD_SAVE_GUARD_MS=${AUDIOBOOK_BACKWARD_SAVE_GUARD_MS:-5000}
COMPLETED_END_THRESHOLD_MS=${AUDIOBOOK_COMPLETED_END_THRESHOLD_MS:-45000}
HELPER_TIMEOUT_SECONDS=${AUDIOBOOK_HELPER_TIMEOUT_SECONDS:-3}
HELPER_MAX_CONSECUTIVE_FAILURES=${AUDIOBOOK_HELPER_MAX_CONSECUTIVE_FAILURES:-3}
HELPER_FAILURE_BACKOFF_SECONDS=${AUDIOBOOK_HELPER_FAILURE_BACKOFF_SECONDS:-10}
POSITION_SOURCE=${AUDIOBOOK_POSITION_SOURCE:-memory}
PLAYER_POSITION_ADDR=${AUDIOBOOK_PLAYER_POSITION_ADDR:-9115148}
PLAYER_DURATION_ADDR=${AUDIOBOOK_PLAYER_DURATION_ADDR:-9115252}
RESTORE_ENABLED=${AUDIOBOOK_RESTORE_ENABLED:-0}
TRACK_RESTORE_ENABLED=${AUDIOBOOK_TRACK_RESTORE_ENABLED:-1}
TRACK_RESTORE_MAX_STEPS=${AUDIOBOOK_TRACK_RESTORE_MAX_STEPS:-50}
TRACK_RESTORE_KEY_FALLBACK_ENABLED=${AUDIOBOOK_TRACK_RESTORE_KEY_FALLBACK_ENABLED:-0}
TRACK_RESTORE_NEAR_MISS_TRANSPORT_ENABLED=${AUDIOBOOK_TRACK_RESTORE_NEAR_MISS_TRANSPORT_ENABLED:-1}
TRACK_RESTORE_NEAR_MISS_MAX_STEPS=${AUDIOBOOK_TRACK_RESTORE_NEAR_MISS_MAX_STEPS:-4}
TRACK_SWITCH_SETTLE_SECONDS=${AUDIOBOOK_TRACK_SWITCH_SETTLE_SECONDS:-3}
TRACK_SWITCH_POLL_US=${AUDIOBOOK_TRACK_SWITCH_POLL_US:-200000}
TOUCH_NEXT_EVENT_FILE=${AUDIOBOOK_TOUCH_NEXT_EVENT_FILE:-$BASE_DIR/input/touch_next_event1.bin}
TOUCH_FIRST_TRACK_EVENT_FILE=${AUDIOBOOK_TOUCH_FIRST_TRACK_EVENT_FILE:-$BASE_DIR/input/touch_first_track_event1.bin}
TOUCH_FIRST_TRACK_DOWN_EVENT_FILE=${AUDIOBOOK_TOUCH_FIRST_TRACK_DOWN_EVENT_FILE:-$BASE_DIR/input/touch_first_track_down_event1.bin}
TOUCH_FIRST_TRACK_MOVE_EVENT_FILE=${AUDIOBOOK_TOUCH_FIRST_TRACK_MOVE_EVENT_FILE:-$BASE_DIR/input/touch_first_track_move_event1.bin}
TOUCH_FIRST_TRACK_UP_EVENT_FILE=${AUDIOBOOK_TOUCH_FIRST_TRACK_UP_EVENT_FILE:-$BASE_DIR/input/touch_first_track_up_event1.bin}
TOUCH_FIRST_TRACK_HOLD_US=${AUDIOBOOK_TOUCH_FIRST_TRACK_HOLD_US:-250000}
TOUCH_BACK_EVENT_FILE=${AUDIOBOOK_TOUCH_BACK_EVENT_FILE:-$BASE_DIR/input/touch_back_event1.bin}
TOUCH_TRACK_ROW1_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_ROW1_EVENT_FILE:-$BASE_DIR/input/touch_track_row1_event1.bin}
TOUCH_TRACK_ROW2_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_ROW2_EVENT_FILE:-$BASE_DIR/input/touch_track_row2_event1.bin}
TOUCH_TRACK_ROW3_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_ROW3_EVENT_FILE:-$BASE_DIR/input/touch_track_row3_event1.bin}
TOUCH_TRACK_ROW4_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_ROW4_EVENT_FILE:-$BASE_DIR/input/touch_track_row4_event1.bin}
TOUCH_TRACK_ROW5_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_ROW5_EVENT_FILE:-$BASE_DIR/input/touch_track_row5_event1.bin}
TOUCH_TRACK_SWIPE_DOWN_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_SWIPE_DOWN_EVENT_FILE:-$BASE_DIR/input/touch_track_swipe_down_event1.bin}
TOUCH_TRACK_SWIPE_MOVE1_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE1_EVENT_FILE:-$BASE_DIR/input/touch_track_swipe_move1_event1.bin}
TOUCH_TRACK_SWIPE_MOVE2_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE2_EVENT_FILE:-$BASE_DIR/input/touch_track_swipe_move2_event1.bin}
TOUCH_TRACK_SWIPE_MOVE3_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE3_EVENT_FILE:-$BASE_DIR/input/touch_track_swipe_move3_event1.bin}
TOUCH_TRACK_SWIPE_MOVE4_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE4_EVENT_FILE:-$BASE_DIR/input/touch_track_swipe_move4_event1.bin}
TOUCH_TRACK_SWIPE_MOVE5_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE5_EVENT_FILE:-$BASE_DIR/input/touch_track_swipe_move5_event1.bin}
TOUCH_TRACK_SWIPE_MOVE6_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_SWIPE_MOVE6_EVENT_FILE:-$BASE_DIR/input/touch_track_swipe_move6_event1.bin}
TOUCH_TRACK_SWIPE_UP_EVENT_FILE=${AUDIOBOOK_TOUCH_TRACK_SWIPE_UP_EVENT_FILE:-$BASE_DIR/input/touch_track_swipe_up_event1.bin}
TOUCH_TRACK_SWIPE_PHASE_US=${AUDIOBOOK_TOUCH_TRACK_SWIPE_PHASE_US:-50000}
TOUCH_EVENT_NODE=${AUDIOBOOK_TOUCH_EVENT_NODE:-/dev/input/event1}
KEY_NEXT_EVENT_FILE=${AUDIOBOOK_KEY_NEXT_EVENT_FILE:-$BASE_DIR/input/key_next_event0.bin}
KEY_NEXT_EVENT_NODE=${AUDIOBOOK_KEY_NEXT_EVENT_NODE:-/dev/input/event0}
KEY_PREV_EVENT_FILE=${AUDIOBOOK_KEY_PREV_EVENT_FILE:-$BASE_DIR/input/key_prev_event2.bin}
KEY_PREV_EVENT_NODE=${AUDIOBOOK_KEY_PREV_EVENT_NODE:-/dev/input/event2}
BOOK_TITLE_AUTOSTART_ENABLED=${AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED:-1}
BOOK_TITLE_MARKER_ADDR=${AUDIOBOOK_BOOK_TITLE_MARKER_ADDR:-9322496}
BOOK_TITLE_AUTOSTART_DELAY_SECONDS=${AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS:-2}
BOOK_TITLE_TRACK_LIST_OFFSET=${AUDIOBOOK_BOOK_TITLE_TRACK_LIST_OFFSET:-52}
BOOK_TITLE_TRACK_LIST_SCAN_BYTES=${AUDIOBOOK_BOOK_TITLE_TRACK_LIST_SCAN_BYTES:-4096}
BOOK_TITLE_CATALOG_SCAN_PTR_OFFSET=${AUDIOBOOK_BOOK_TITLE_CATALOG_SCAN_PTR_OFFSET:-44}
BOOK_TITLE_CATALOG_SCAN_BYTES=${AUDIOBOOK_BOOK_TITLE_CATALOG_SCAN_BYTES:-8192}
BOOK_TITLE_MEMSCAN_ENABLED=${AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED:-1}
BOOK_TITLE_MEMSCAN_ADDR=${AUDIOBOOK_BOOK_TITLE_MEMSCAN_ADDR:-9113600}
BOOK_TITLE_MEMSCAN_BYTES=${AUDIOBOOK_BOOK_TITLE_MEMSCAN_BYTES:-212992}
BOOK_TITLE_AUTOSTART_REQUIRE_PATH=${AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH:-1}
BOOK_TITLE_CONTEXT_SECONDS=${AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS:-300}
BOOK_TITLE_SOURCE_MAGIC=${AUDIOBOOK_BOOK_TITLE_SOURCE_MAGIC:-2695890197}
BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED=${AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED:-1}
BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED=${AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED:-1}
BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS=${AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS:-1}
BOOK_TITLE_DIRECT_TRACK_SWIPE_SETTLE_SECONDS=${AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SWIPE_SETTLE_SECONDS:-1}
BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES=${AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES:-20}
BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS=${AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS:-5}
BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE=${AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE:-4}
BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED=${AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED:-1}
BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED=${AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED:-1}
BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS=${AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS:-20}
BOOK_TITLE_DIRECT_OPEN_ENABLED=${AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED:-1}
DIRECT_OPEN_PROBE_ADDR=${AUDIOBOOK_DIRECT_OPEN_PROBE_ADDR:-0x760708}
DIRECT_OPEN_SCRATCH_ADDR=${AUDIOBOOK_DIRECT_OPEN_SCRATCH_ADDR:-0x8e4400}
DIRECT_OPEN_TIMEOUT_MS=${AUDIOBOOK_DIRECT_OPEN_TIMEOUT_MS:-6000}
DIRECT_OPEN_ARM_DELAY_US=${AUDIOBOOK_DIRECT_OPEN_ARM_DELAY_US:-200000}
BOOK_TITLE_RESTORE_LOG_BUCKET_MS=${AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS:-5000}
UI_SEEK_FALLBACK_ENABLED=${AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED:-1}
UI_SEEK_BAR_X_MIN=${AUDIOBOOK_UI_SEEK_BAR_X_MIN:-21}
UI_SEEK_BAR_X_MAX=${AUDIOBOOK_UI_SEEK_BAR_X_MAX:-459}
UI_SEEK_BAR_Y=${AUDIOBOOK_UI_SEEK_BAR_Y:-619}
UI_SEEK_MIN_DURATION_MS=${AUDIOBOOK_UI_SEEK_MIN_DURATION_MS:-30000}
UI_SEEK_VERIFY_DELAY_SECONDS=${AUDIOBOOK_UI_SEEK_VERIFY_DELAY_SECONDS:-2}
UI_SEEK_VERIFY_TOLERANCE_MS=${AUDIOBOOK_UI_SEEK_VERIFY_TOLERANCE_MS:-15000}
UI_SEEK_TOUCH_FRAMES=${AUDIOBOOK_UI_SEEK_TOUCH_FRAMES:-2}
UI_SEEK_SCREEN_GUARD_ENABLED=${AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED:-1}
UI_SEEK_SCREEN_MIN_BAR_PIXELS=${AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS:-300}
UI_SEEK_FB_STRIDE=${AUDIOBOOK_UI_SEEK_FB_STRIDE:-960}
PLAY_MODE_ENFORCE_ENABLED=${AUDIOBOOK_PLAY_MODE_ENFORCE_ENABLED:-1}
PLAY_MODE_TARGET=${AUDIOBOOK_PLAY_MODE_TARGET:-3}
PLAY_MODE_USER_INI_OFFSET=${AUDIOBOOK_PLAY_MODE_USER_INI_OFFSET:-592}
PLAY_MODE_MAX_TAPS=${AUDIOBOOK_PLAY_MODE_MAX_TAPS:-4}
PLAY_MODE_TOUCH_X=${AUDIOBOOK_PLAY_MODE_TOUCH_X:-49}
PLAY_MODE_TOUCH_Y=${AUDIOBOOK_PLAY_MODE_TOUCH_Y:-730}
PLAY_MODE_SETTLE_SECONDS=${AUDIOBOOK_PLAY_MODE_SETTLE_SECONDS:-1}
PLAY_MODE_SCREEN_GUARD_ENABLED=${AUDIOBOOK_PLAY_MODE_SCREEN_GUARD_ENABLED:-1}
BACK_GUARD_ENABLED=${AUDIOBOOK_BACK_GUARD_ENABLED:-0}
BACK_GUARD_WINDOW_SECONDS=${AUDIOBOOK_BACK_GUARD_WINDOW_SECONDS:-60}
BACK_GUARD_AFTER_SCREEN_SECONDS=${AUDIOBOOK_BACK_GUARD_AFTER_SCREEN_SECONDS:-8}
BACK_GUARD_IDLE_INTERVAL_SECONDS=${AUDIOBOOK_BACK_GUARD_IDLE_INTERVAL_SECONDS:-1}
BACK_GUARD_SETTLE_SECONDS=${AUDIOBOOK_BACK_GUARD_SETTLE_SECONDS:-1}
BACK_GUARD_EXTRA_BACKS=${AUDIOBOOK_BACK_GUARD_EXTRA_BACKS:-2}
BACK_GUARD_SUBHEADER_MIN_WHITE=${AUDIOBOOK_BACK_GUARD_SUBHEADER_MIN_WHITE:-100}
BACK_GUARD_SUBHEADER_MAX_WHITE=${AUDIOBOOK_BACK_GUARD_SUBHEADER_MAX_WHITE:-50}
BACK_GUARD_HEADER_MIN_WHITE=${AUDIOBOOK_BACK_GUARD_HEADER_MIN_WHITE:-300}
BACK_GUARD_BACK_ARROW_MIN_WHITE=${AUDIOBOOK_BACK_GUARD_BACK_ARROW_MIN_WHITE:-80}

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

clear_restore_failure_state() {
  restore_failed_path=
  restore_failed_at=0
  restore_failed_kind=
  restore_failed_saved_pos=
  restore_seek_failed_key=
  restore_seek_failed_count=0
  failed_restore_skip_log_bucket=
}

restore_retry_delay_seconds() {
  count=${restore_seek_failed_count:-0}
  case "$count" in ''|*[!0-9]*) count=0 ;; esac
  delay=$RESTORE_RETRY_AFTER_FAILURE_SECONDS
  while [ "$count" -gt 1 ]; do
    delay=$((delay * 2))
    if [ "$delay" -ge "$RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS" ]; then
      delay=$RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS
      break
    fi
    count=$((count - 1))
  done
  echo "$delay"
}

note_seek_restore_failure() {
  path=$1
  saved_pos=$2
  key="$path:$saved_pos"
  if [ "$restore_seek_failed_key" != "$key" ]; then
    restore_seek_failed_key=$key
    restore_seek_failed_count=0
    failed_restore_skip_log_bucket=
  fi
  restore_seek_failed_count=$((restore_seek_failed_count + 1))
  restore_failed_path=$path
  restore_failed_kind=seek
  restore_failed_saved_pos=$saved_pos
  restore_failed_at=$(date '+%s' 2>/dev/null || echo 0)
  retry_delay=$(restore_retry_delay_seconds)
  log "restore failed path=$path saved_ms=$saved_pos count=$restore_seek_failed_count retry_in=${retry_delay}s"
}

restore_target_ms() {
  saved_pos=$1
  case "$saved_pos:$RESTORE_REWIND_MS" in
    *[!0-9:]* | :* | *:) printf '%s\n' "$saved_pos"; return ;;
  esac
  [ "$RESTORE_REWIND_MS" -gt 0 ] || {
    printf '%s\n' "$saved_pos"
    return
  }
  if [ "$saved_pos" -gt "$RESTORE_REWIND_MS" ]; then
    printf '%s\n' $((saved_pos - RESTORE_REWIND_MS))
  else
    printf '0\n'
  fi
}

note_track_restore_failure() {
  path=$1
  now=$2
  restore_failed_path=$path
  restore_failed_at=$now
  restore_failed_kind=track
  restore_failed_saved_pos=
  restore_seek_failed_key=
  restore_seek_failed_count=0
  failed_restore_skip_log_bucket=
}

book_title_log_bucket() {
  pos=$1
  case "$BOOK_TITLE_RESTORE_LOG_BUCKET_MS:$pos" in
    *[!0-9:]*|:*|0:*|*:) echo 0 ;;
    *) echo $((pos / BOOK_TITLE_RESTORE_LOG_BUCKET_MS)) ;;
  esac
}

log_book_title_restore_wait() {
  path=$1
  pos=$2
  bucket=$(book_title_log_bucket "$pos")
  key="wait:$book_title_autostart_seq:$path:$bucket"
  if [ "$book_title_restore_wait_log_key" != "$key" ]; then
    log "book-title restore wait seq=$book_title_autostart_seq path=$path pos_ms=$pos"
    book_title_restore_wait_log_key=$key
  fi
}

log_book_title_pre_restore_skip() {
  path=$1
  pos=$2
  bucket=$(book_title_log_bucket "$pos")
  key="skip:$book_title_autostart_seq:$path:$bucket"
  if [ "$book_title_pre_restore_log_key" != "$key" ]; then
    log "book-title skip pre-restore save seq=$book_title_autostart_seq path=$path pos_ms=$pos"
    book_title_pre_restore_log_key=$key
  fi
}

close_inherited_socket_fds() {
  CLOSED_INHERITED_SOCKET_FDS=0
  for fd_path in /proc/$$/fd/*; do
    fd=${fd_path##*/}
    case "$fd" in
      ''|*[!0-9]*|0|1|2) continue ;;
    esac
    link=$(readlink "$fd_path" 2>/dev/null || true)
    case "$link" in
      socket:*)
        if eval "exec ${fd}>&-"; then
          CLOSED_INHERITED_SOCKET_FDS=$((CLOSED_INHERITED_SOCKET_FDS + 1))
        fi
        ;;
    esac
  done
}

track_switch_settle_ticks() {
  case "$TRACK_SWITCH_SETTLE_SECONDS:$TRACK_SWITCH_POLL_US" in
    *[!0-9:]* | :* | *:) printf '15\n'; return ;;
  esac
  [ "$TRACK_SWITCH_POLL_US" -gt 0 ] || {
    printf '15\n'
    return
  }
  ticks=$((TRACK_SWITCH_SETTLE_SECONDS * 1000000 / TRACK_SWITCH_POLL_US))
  [ "$ticks" -gt 0 ] || ticks=1
  printf '%s\n' "$ticks"
}

sleep_track_switch_poll() {
  usleep "$TRACK_SWITCH_POLL_US" 2>/dev/null || sleep 1
}

clear_book_title_autostart() {
  book_title_autostart_until=0
  book_title_autostart_seq=0
  book_title_autostart_reset_key=
  book_title_restore_wait_log_key=
  book_title_pre_restore_log_key=
}

safe_id() {
  printf '%s' "$1" | sed 's#^[Aa]:\\Audiobooks\\##; s#[^A-Za-z0-9._-]#_#g'
}

json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

run_helper() {
  tmp="$BASE_DIR/helper.$$.out"
  rm -f "$tmp"
  "$HELPER" "$@" >"$tmp" 2>&1 &
  helper_pid=$!
  elapsed=0
  while kill -0 "$helper_pid" 2>/dev/null; do
    if [ "$elapsed" -ge "$HELPER_TIMEOUT_SECONDS" ]; then
      kill -9 "$helper_pid" 2>/dev/null || true
      sleep 1
      if kill -0 "$helper_pid" 2>/dev/null; then
        rm -f "$tmp"
        return 124
      fi
      wait "$helper_pid" 2>/dev/null || true
      rm -f "$tmp"
      return 124
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  wait "$helper_pid" 2>/dev/null
  rc=$?
  cat "$tmp" 2>/dev/null
  rm -f "$tmp"
  return "$rc"
}

player_pid() {
  ps | sed -n '/\/usr\/bin\/hiby_player$/ { /grep/d; s/^ *\([0-9][0-9]*\).*/\1/p; q; }'
}

player_pid_cached() {
  if [ -n "${PLAYER_PID_CACHE:-}" ] && [ -d "/proc/$PLAYER_PID_CACHE" ]; then
    printf '%s\n' "$PLAYER_PID_CACHE"
    return 0
  fi
  PLAYER_PID_CACHE=$(player_pid)
  [ -n "$PLAYER_PID_CACHE" ] || return 1
  printf '%s\n' "$PLAYER_PID_CACHE"
}

u32le_from_hex() {
  awk '
    function h2d(h, i, c, n) {
      n = 0
      h = tolower(h)
      for (i = 1; i <= length(h); i++) {
        c = index("0123456789abcdef", substr(h, i, 1)) - 1
        if (c < 0) return -1
        n = n * 16 + c
      }
      return n
    }
    length($0) >= 8 {
      print h2d(substr($0, 7, 2) substr($0, 5, 2) substr($0, 3, 2) substr($0, 1, 2))
      exit
    }'
}

position_ms_memory() {
  pid=$(player_pid_cached)
  [ -n "$pid" ] || return 1
  hex=$(dd if="/proc/$pid/mem" bs=1 skip="$PLAYER_POSITION_ADDR" count=4 2>/dev/null | xxd -p -c 4)
  [ -n "$hex" ] || return 1
  value=$(printf '%s\n' "$hex" | u32le_from_hex)
  case "$value" in ''|*[!0-9]*) return 1 ;; esac
  printf '%s\n' "$value"
}

duration_ms_memory() {
  pid=$(player_pid_cached)
  [ -n "$pid" ] || return 1
  hex=$(dd if="/proc/$pid/mem" bs=1 skip="$PLAYER_DURATION_ADDR" count=4 2>/dev/null | xxd -p -c 4)
  [ -n "$hex" ] || return 1
  value=$(printf '%s\n' "$hex" | u32le_from_hex)
  case "$value" in ''|*[!0-9]*) return 1 ;; esac
  printf '%s\n' "$value"
}

u32le_at_pid_mem() {
  pid=$1
  addr=$2
  hex=$(dd if="/proc/$pid/mem" bs=1 skip="$addr" count=4 2>/dev/null | xxd -p -c 4)
  [ -n "$hex" ] || return 1
  value=$(printf '%s\n' "$hex" | u32le_from_hex)
  case "$value" in ''|*[!0-9]*) return 1 ;; esac
  printf '%s\n' "$value"
}

pid_mem_contains() {
  pid=$1
  addr=$2
  count=$3
  pattern=$4
  [ "$addr" -gt 4096 ] || return 1
  dd if="/proc/$pid/mem" bs=1 skip="$addr" count="$count" 2>/dev/null | grep -a -F -q "$pattern"
}

refresh_catalog_album_patterns() {
  [ -s "$CATALOG" ] || return 0
  tmp="$CATALOG_ALBUM_PATTERNS.tmp.$$"
  tail -n +2 "$CATALOG" 2>/dev/null | cut -f7 | sed '/^$/d' | sort -u >"$tmp" 2>/dev/null || {
    rm -f "$tmp"
    return 0
  }
  if [ -s "$tmp" ]; then
    mv -f "$tmp" "$CATALOG_ALBUM_PATTERNS"
  else
    rm -f "$tmp"
  fi
}

pid_mem_contains_catalog_album() {
  pid=$1
  addr=$2
  count=$3
  [ "$addr" -gt 4096 ] || return 1
  [ -s "$CATALOG_ALBUM_PATTERNS" ] || return 1
  dd if="/proc/$pid/mem" bs=1 skip="$addr" count="$count" 2>/dev/null |
    grep -a -F -q -f "$CATALOG_ALBUM_PATTERNS"
}

pid_mem_first_catalog_path() {
  pid=$1
  addr=$2
  count=$3
  [ "$addr" -gt 4096 ] || return 1
  [ -s "$CATALOG" ] || return 1
  tmp="$BASE_DIR/memscan.$$.bin"
  rm -f "$tmp"
  dd if="/proc/$pid/mem" of="$tmp" bs=1 skip="$addr" count="$count" 2>/dev/null || {
    rm -f "$tmp"
    return 1
  }
  awk -F '\t' 'NR > 1 { print $5 }' "$CATALOG" |
    while IFS= read -r catalog_path; do
      [ -n "$catalog_path" ] || continue
      if grep -a -F -q "$catalog_path" "$tmp"; then
        printf '%s\n' "$catalog_path"
        break
      fi
    done
  rm -f "$tmp"
}

current_path_slot_hex() {
  dd if="$USER_INI" bs=1 skip=40 count=512 2>/dev/null | xxd -p -c 512
}

current_path_slot_preview() {
  dd if="$USER_INI" bs=1 skip=40 count=128 2>/dev/null | tr -d '\000'
}

path_preview_is_audiobook() {
  case "$1" in
    [aA]:\\Audiobooks\\*|:\\Audiobooks\\*|\\Audiobooks\\*)
      return 0
      ;;
  esac
  return 1
}

path_preview_is_music() {
  case "$1" in
    [aA]:\\Music\\*|:\\Music\\*|\\Music\\*)
      return 0
      ;;
  esac
  return 1
}

path_slot_hex_is_audiobook() {
  case "$1" in
    61003a005c0041007500640069006f0062006f006f006b007300*|\
    41003a005c0041007500640069006f0062006f006f006b007300*|\
    3a005c0041007500640069006f0062006f006f006b007300*|\
    5c0041007500640069006f0062006f006f006b007300*)
      return 0
      ;;
  esac
  return 1
}

decode_path_slot_hex() {
  awk '
    function h2d(h, i, c, n) {
      n = 0
      h = tolower(h)
      for (i = 1; i <= length(h); i++) {
        c = index("0123456789abcdef", substr(h, i, 1)) - 1
        if (c < 0) return 63
        n = n * 16 + c
      }
      return n
    }
    {
      out = ""
      for (i = 1; i <= length($0) - 3; i += 4) {
        lo = substr($0, i, 2)
        hi = substr($0, i + 2, 2)
        if (lo == "00" && hi == "00") {
          if (out == "") continue
          break
        }
        if (hi == "00") out = out sprintf("%c", h2d(lo))
        else out = out "?"
      }
      print out
    }'
}

current_path_from_hex() {
  hex=$1
  if [ "$hex" = "${CURRENT_PATH_HEX_CACHE:-}" ]; then
    printf '%s\n' "$CURRENT_PATH_VALUE_CACHE"
    return 0
  fi
  path=$(
    printf '%s\n' "$hex" | decode_path_slot_hex
  )
  case "$path" in
    :\\*) path="a$path" ;;
    \\Audiobooks\\*) path="a:$path" ;;
  esac
  CURRENT_PATH_HEX_CACHE=$hex
  CURRENT_PATH_VALUE_CACHE=$path
  printf '%s\n' "$path"
}

current_path() {
  current_path_from_hex "$(current_path_slot_hex)"
}

position_ms() {
  if [ "$POSITION_SOURCE" = memory ]; then
    position_ms_memory
    return $?
  fi
  out=$(run_helper position)
  rc=$?
  [ "$rc" -eq 0 ] || return "$rc"
  printf '%s\n' "$out" | sed -n 's/^position_ms=//p'
}

book_root_for_path() {
  printf '%s' "${1%\\*}"
}

legacy_record_for_root() {
  printf '%s/%s.json' "$STORE_DIR" "$(safe_id "$1")"
}

record_for_root() {
  legacy_record_for_root "$1"
}

json_value() {
  key=$1
  file=$2
  sed -n 's/.*"'"$key"'"[ 	]*:[ 	]*"\([^"]*\)".*/\1/p' "$file" |
    head -1 |
    sed 's/\\\\/\\/g; s/\\"/"/g'
}

json_number() {
  key=$1
  file=$2
  sed -n 's/.*"'"$key"'"[ 	]*:[ 	]*\([0-9][0-9]*\).*/\1/p' "$file" | head -1
}

json_bool() {
  key=$1
  file=$2
  sed -n 's/.*"'"$key"'"[ 	]*:[ 	]*\(true\|false\).*/\1/p' "$file" | head -1
}

json_number_or_null() {
  case "$1" in
    ''|*[!0-9]*) printf 'null' ;;
    *) printf '%s' "$1" ;;
  esac
}

catalog_field_for_path() {
  catalog_field=$1
  catalog_path=$2
  [ -f "$CATALOG" ] || return 1
  CATALOG_MATCH_PATH=$catalog_path awk -F '\t' -v n="$catalog_field" '
    $5 == ENVIRON["CATALOG_MATCH_PATH"] { print $n; exit }
  ' "$CATALOG"
}

catalog_field_for_root_index() {
  catalog_field=$1
  catalog_root=$2
  catalog_index=$3
  [ -f "$CATALOG" ] || return 1
  CATALOG_MATCH_ROOT=$catalog_root CATALOG_MATCH_INDEX=$catalog_index awk -F '\t' -v n="$catalog_field" '
    $1 == ENVIRON["CATALOG_MATCH_ROOT"] && $2 == ENVIRON["CATALOG_MATCH_INDEX"] { print $n; exit }
  ' "$CATALOG"
}

catalog_first_path_for_root() {
  catalog_root=$1
  [ -f "$CATALOG" ] || return 1
  CATALOG_MATCH_ROOT=$catalog_root awk -F '\t' '
    $1 == ENVIRON["CATALOG_MATCH_ROOT"] && $2 == 1 { print $5; exit }
  ' "$CATALOG"
}

book_title_memscan_root() {
  [ "$BOOK_TITLE_MEMSCAN_ENABLED" = 1 ] || return 1
  [ -x "$MEMSCAN_HELPER" ] || return 1
  [ -s "$CATALOG_BOOKS" ] || return 1
  pid=$1
  root=$(
    "$MEMSCAN_HELPER" \
      --pid "$pid" \
      --catalog-books "$CATALOG_BOOKS" \
      --addr "$BOOK_TITLE_MEMSCAN_ADDR" \
      --bytes "$BOOK_TITLE_MEMSCAN_BYTES" 2>/dev/null |
      head -1
  )
  [ -n "$root" ] || return 1
  printf '%s\n' "$root"
}

book_key_for_path() {
  catalog_field_for_path 9 "$1" || true
}

record_for_book_key() {
  printf '%s/bookkey_%s.json' "$STORE_DIR" "$(safe_id "$1")"
}

record_for_path() {
  record_path=$1
  record_key=$(book_key_for_path "$record_path")
  if [ -n "$record_key" ]; then
    record_for_book_key "$record_key"
    return
  fi
  legacy_record_for_root "$(book_root_for_path "$record_path")"
}

existing_record_for_path() {
  record_path=$1
  primary=$(record_for_path "$record_path")
  if [ -f "$primary" ]; then
    printf '%s\n' "$primary"
    return
  fi
  legacy=$(legacy_record_for_root "$(book_root_for_path "$record_path")")
  if [ -f "$legacy" ]; then
    printf '%s\n' "$legacy"
    return
  fi
  printf '%s\n' "$primary"
}

same_book_root() {
  same_book_path=$1
  same_book_root_value=$2
  case "$same_book_path" in
    "$same_book_root_value"\\*) return 0 ;;
  esac
  return 1
}

record_saved_path_for_current() {
  record=$1
  current_path=$2
  current_root=$3
  saved_path=$(json_value current_path "$record")
  [ -n "$saved_path" ] || return 1
  if same_book_root "$saved_path" "$current_root"; then
    printf '%s\n' "$saved_path"
    return 0
  fi

  current_book_key=$(book_key_for_path "$current_path")
  record_book_key=$(json_value book_key "$record")
  if [ -n "$current_book_key" ] && [ "$current_book_key" = "$record_book_key" ]; then
    saved_index=$(json_number track_index "$record")
    case "$saved_index" in
      ''|*[!0-9]*) ;;
      *)
        mapped_path=$(catalog_field_for_root_index 5 "$current_root" "$saved_index" || true)
        if [ -n "$mapped_path" ]; then
          printf '%s\n' "$mapped_path"
          return 0
        fi
        ;;
    esac
  fi

  printf '%s\n' "$saved_path"
}

completion_state_for_path_position() {
  path=$1
  pos=$2
  case "$pos:$COMPLETED_END_THRESHOLD_MS" in
    *[!0-9:]* | :* | *:) echo false; return ;;
  esac
  track_index=$(catalog_field_for_path 2 "$path" || true)
  track_count=$(catalog_field_for_path 3 "$path" || true)
  case "$track_index:$track_count" in
    *[!0-9:]* | :* | *:) echo false; return ;;
  esac
  [ "$track_count" -gt 0 ] || {
    echo false
    return
  }
  [ "$track_index" -eq "$track_count" ] || {
    echo false
    return
  }
  duration=$(duration_ms_memory 2>/dev/null || echo 0)
  case "$duration" in ''|*[!0-9]*) echo false; return ;; esac
  [ "$duration" -gt 0 ] || {
    echo false
    return
  }
  remaining=$((duration - pos))
  [ "$remaining" -le "$COMPLETED_END_THRESHOLD_MS" ] && {
    echo true
    return
  }
  echo false
}

send_input_event() {
  event_label=$1
  event_file=$2
  event_node=$3
  [ -r "$event_file" ] || {
    log "$event_label unavailable missing=$event_file"
    return 1
  }
  [ -e "$event_node" ] || {
    log "$event_label unavailable missing=$event_node"
    return 1
  }
  cat "$event_file" >"$event_node"
}

emit_input_byte() {
  printf "\\$(printf '%03o' "$1")"
}

emit_input_le16() {
  value=$1
  emit_input_byte $((value & 255))
  emit_input_byte $(((value >> 8) & 255))
}

emit_input_le32() {
  value=$1
  emit_input_byte $((value & 255))
  emit_input_byte $(((value >> 8) & 255))
  emit_input_byte $(((value >> 16) & 255))
  emit_input_byte $(((value >> 24) & 255))
}

emit_input_event() {
  emit_input_le32 0
  emit_input_le32 0
  emit_input_le16 "$1"
  emit_input_le16 "$2"
  emit_input_le32 "$3"
}

emit_touch_abs_frame() {
  x=$1
  y=$2
  include_press=$3
  emit_input_event 3 57 0
  emit_input_event 3 58 63
  emit_input_event 3 48 9
  emit_input_event 3 53 "$x"
  emit_input_event 3 54 "$y"
  emit_input_event 0 2 0
  if [ "$include_press" = 1 ]; then
    emit_input_event 1 330 1
  fi
  emit_input_event 0 0 0
}

write_touch_tap_stream() {
  x=$1
  y=$2
  output=$3
  frames=$UI_SEEK_TOUCH_FRAMES
  case "$frames" in ''|*[!0-9]*) frames=2 ;; esac
  [ "$frames" -gt 0 ] || frames=2
  {
    emit_touch_abs_frame "$x" "$y" 1
    frame=1
    while [ "$frame" -lt "$frames" ]; do
      emit_touch_abs_frame "$x" "$y" 0
      frame=$((frame + 1))
    done
    emit_input_event 1 330 0
    emit_input_event 0 2 0
    emit_input_event 0 0 0
  } >"$output"
}

touch_generated_tap() {
  event_label=$1
  x=$2
  y=$3
  tmp="$BASE_DIR/input/${event_label}.$$.bin"
  write_touch_tap_stream "$x" "$y" "$tmp" || {
    rm -f "$tmp"
    log "$event_label failed writing tap x=$x y=$y"
    return 1
  }
  send_input_event "$event_label" "$tmp" "$TOUCH_EVENT_NODE"
  rc=$?
  rm -f "$tmp"
  return "$rc"
}

play_mode_value() {
  case "$PLAY_MODE_USER_INI_OFFSET" in ''|*[!0-9]*) return 1 ;; esac
  dd if="$USER_INI" bs=1 skip="$PLAY_MODE_USER_INI_OFFSET" count=1 2>/dev/null |
    od -An -tu1 2>/dev/null |
    awk '{ print $1; exit }'
}

play_mode_screen_ready() {
  [ "$PLAY_MODE_SCREEN_GUARD_ENABLED" = 1 ] || return 0
  ui_seek_screen_ready
}

ensure_audiobook_play_mode() {
  [ "$PLAY_MODE_ENFORCE_ENABLED" = 1 ] || return 0
  case "$PLAY_MODE_TARGET:$PLAY_MODE_MAX_TAPS:$PLAY_MODE_TOUCH_X:$PLAY_MODE_TOUCH_Y" in
    *[!0-9:]* | :* | *:) return 1 ;;
  esac
  mode=$(play_mode_value || true)
  case "$mode" in ''|*[!0-9]*) log "play-mode unavailable target=$PLAY_MODE_TARGET"; return 1 ;; esac
  [ "$mode" = "$PLAY_MODE_TARGET" ] && return 0
  if ! play_mode_screen_ready; then
    log "play-mode skipped screen-not-ready mode=$mode target=$PLAY_MODE_TARGET"
    return 1
  fi
  taps=0
  while [ "$taps" -lt "$PLAY_MODE_MAX_TAPS" ]; do
    old_mode=$mode
    touch_generated_tap play-mode "$PLAY_MODE_TOUCH_X" "$PLAY_MODE_TOUCH_Y" || return 1
    taps=$((taps + 1))
    sleep "$PLAY_MODE_SETTLE_SECONDS"
    mode=$(play_mode_value || true)
    case "$mode" in ''|*[!0-9]*) mode=? ;; esac
    log "play-mode tap=$taps mode=$old_mode->$mode target=$PLAY_MODE_TARGET"
    [ "$mode" = "$PLAY_MODE_TARGET" ] && return 0
  done
  log "play-mode failed mode=$mode target=$PLAY_MODE_TARGET taps=$taps"
  return 1
}

ui_seek_screen_ready() {
  [ "$UI_SEEK_SCREEN_GUARD_ENABLED" = 1 ] || return 0
  case "$UI_SEEK_BAR_X_MIN:$UI_SEEK_BAR_X_MAX:$UI_SEEK_BAR_Y:$UI_SEEK_SCREEN_MIN_BAR_PIXELS:$UI_SEEK_FB_STRIDE" in
    *[!0-9:]* | :* | *:) return 1 ;;
  esac
  row_hex=$(
    dd if=/dev/fb0 bs="$UI_SEEK_FB_STRIDE" skip="$UI_SEEK_BAR_Y" count=1 2>/dev/null |
      xxd -p -c "$UI_SEEK_FB_STRIDE"
  )
  [ -n "$row_hex" ] || {
    log "ui-seek screen guard unavailable fb0"
    return 1
  }
  pixels=$(
    printf '%s\n' "$row_hex" |
      awk -v x0="$UI_SEEK_BAR_X_MIN" -v x1="$UI_SEEK_BAR_X_MAX" '
        function h2d(h, i, c, n) {
          n = 0
          h = tolower(h)
          for (i = 1; i <= length(h); i++) {
            c = index("0123456789abcdef", substr(h, i, 1)) - 1
            if (c < 0) return -1
            n = n * 16 + c
          }
          return n
        }
        {
          c = 0
          for (x = x0; x <= x1; x++) {
            i = x * 4 + 1
            if (length($0) < i + 3) continue
            v = h2d(substr($0, i + 2, 2) substr($0, i, 2))
            r = int(v / 2048)
            g = int(v / 32) % 64
            b = v % 32
            white = (r >= 24 && g >= 48 && b >= 24)
            blue = (r <= 10 && g >= 24 && b >= 18)
            if (white || blue) c++
          }
          print c
          exit
        }'
  )
  case "$pixels" in ''|*[!0-9]*) pixels=0 ;; esac
  [ "$pixels" -ge "$UI_SEEK_SCREEN_MIN_BAR_PIXELS" ] || {
    log "ui-seek screen guard blocked pixels=$pixels min=$UI_SEEK_SCREEN_MIN_BAR_PIXELS y=$UI_SEEK_BAR_Y"
    return 1
  }
  return 0
}

fb_white_pixels_region() {
  x0=$1
  y0=$2
  x1=$3
  y1=$4
  case "$x0:$y0:$x1:$y1:$UI_SEEK_FB_STRIDE" in
    *[!0-9:]* | :* | *:) return 1 ;;
  esac
  [ "$x1" -gt "$x0" ] || return 1
  [ "$y1" -gt "$y0" ] || return 1
  rows=$((y1 - y0))
  dd if=/dev/fb0 bs="$UI_SEEK_FB_STRIDE" skip="$y0" count="$rows" 2>/dev/null |
    xxd -p -c "$UI_SEEK_FB_STRIDE" |
    awk -v x0="$x0" -v x1="$x1" '
      function h2d(h, i, c, n) {
        n = 0
        h = tolower(h)
        for (i = 1; i <= length(h); i++) {
          c = index("0123456789abcdef", substr(h, i, 1)) - 1
          if (c < 0) return -1
          n = n * 16 + c
        }
        return n
      }
      {
        for (x = x0; x < x1; x++) {
          i = x * 4 + 1
          if (length($0) < i + 3) continue
          v = h2d(substr($0, i + 2, 2) substr($0, i, 2))
          r = int(v / 2048)
          g = int(v / 32) % 64
          b = v % 32
          if (r >= 24 && g >= 48 && b >= 24) c++
        }
      }
      END { print c + 0 }'
}

audiobook_subheader_visible() {
  pixels=$(fb_white_pixels_region 60 118 220 155 2>/dev/null || echo 0)
  case "$pixels" in ''|*[!0-9]*) pixels=0 ;; esac
  [ "$pixels" -ge "$BACK_GUARD_SUBHEADER_MIN_WHITE" ]
}

audiobook_title_list_visible() {
  subheader_pixels=$(fb_white_pixels_region 60 118 220 155 2>/dev/null || echo 0)
  header_mid_pixels=$(fb_white_pixels_region 170 70 260 110 2>/dev/null || echo 0)
  header_icon_pixels=$(fb_white_pixels_region 400 75 440 110 2>/dev/null || echo 0)
  case "$subheader_pixels" in ''|*[!0-9]*) subheader_pixels=0 ;; esac
  case "$header_mid_pixels" in ''|*[!0-9]*) header_mid_pixels=0 ;; esac
  case "$header_icon_pixels" in ''|*[!0-9]*) header_icon_pixels=0 ;; esac
  [ "$subheader_pixels" -ge "$BACK_GUARD_SUBHEADER_MIN_WHITE" ] || return 1
  [ "$header_mid_pixels" -le 120 ] || return 1
  [ "$header_icon_pixels" -ge 300 ] || return 1
  return 0
}

audiobook_global_back_target_visible() {
  subheader_pixels=$(fb_white_pixels_region 60 118 220 155 2>/dev/null || echo 0)
  header_pixels=$(fb_white_pixels_region 20 70 230 110 2>/dev/null || echo 0)
  back_pixels=$(fb_white_pixels_region 15 75 55 105 2>/dev/null || echo 0)
  case "$subheader_pixels" in ''|*[!0-9]*) subheader_pixels=0 ;; esac
  case "$header_pixels" in ''|*[!0-9]*) header_pixels=0 ;; esac
  case "$back_pixels" in ''|*[!0-9]*) back_pixels=0 ;; esac
  [ "$subheader_pixels" -le "$BACK_GUARD_SUBHEADER_MAX_WHITE" ] || return 1
  [ "$header_pixels" -ge "$BACK_GUARD_HEADER_MIN_WHITE" ] || return 1
  [ "$back_pixels" -ge "$BACK_GUARD_BACK_ARROW_MIN_WHITE" ] || return 1
  return 0
}

enable_audiobook_back_guard_window() {
  now=$1
  [ "$BACK_GUARD_ENABLED" = 1 ] || return 0
  case "$BACK_GUARD_WINDOW_SECONDS" in ''|*[!0-9]*|0) return 0 ;; esac
  audiobook_back_guard_until=$((now + BACK_GUARD_WINDOW_SECONDS))
}

maybe_audiobook_back_guard() {
  [ "$BACK_GUARD_ENABLED" = 1 ] || return 0
  now=$1
  path_preview=${2:-}
  case "$now" in ''|*[!0-9]*) return 0 ;; esac
  active=0
  if [ "${audiobook_back_guard_until:-0}" -gt "$now" ] 2>/dev/null; then
    active=1
  elif [ "${audiobook_back_guard_seen_at:-0}" -gt 0 ] 2>/dev/null &&
       [ $((now - audiobook_back_guard_seen_at)) -le "$BACK_GUARD_AFTER_SCREEN_SECONDS" ] 2>/dev/null; then
    active=1
  elif ! path_preview_is_music "$path_preview" && ! path_preview_is_audiobook "$path_preview"; then
    active=1
  fi
  [ "$active" = 1 ] || return 0

  if audiobook_subheader_visible; then
    audiobook_back_guard_seen_at=$now
    enable_audiobook_back_guard_window "$now"
    if audiobook_title_list_visible && [ "$BOOK_TITLE_CONTEXT_SECONDS" -gt 0 ] 2>/dev/null; then
      book_title_context_until=$((now + BOOK_TITLE_CONTEXT_SECONDS))
    fi
    return 0
  fi

  [ "${audiobook_back_guard_seen_at:-0}" -gt 0 ] 2>/dev/null || return 0
  [ $((now - audiobook_back_guard_seen_at)) -le "$BACK_GUARD_AFTER_SCREEN_SECONDS" ] 2>/dev/null || return 0
  if audiobook_global_back_target_visible; then
    if [ "${audiobook_back_guard_last_fire_at:-0}" -gt 0 ] 2>/dev/null &&
       [ $((now - audiobook_back_guard_last_fire_at)) -lt "$BACK_GUARD_AFTER_SCREEN_SECONDS" ] 2>/dev/null; then
      return 0
    fi
    log "back-guard extra-back after audiobook list count=$BACK_GUARD_EXTRA_BACKS"
    backs=$BACK_GUARD_EXTRA_BACKS
    while [ "$backs" -gt 0 ]; do
      touch_back_to_track_list || log "back-guard extra-back failed remaining=$backs"
      backs=$((backs - 1))
      [ "$backs" -le 0 ] && break
      sleep "$BACK_GUARD_SETTLE_SECONDS"
    done
    audiobook_back_guard_last_fire_at=$now
    audiobook_back_guard_seen_at=0
    audiobook_back_guard_until=0
    sleep "$BACK_GUARD_SETTLE_SECONDS"
  fi
}

track_next() {
  if [ -r "$KEY_NEXT_EVENT_FILE" ]; then
    send_input_event key-next "$KEY_NEXT_EVENT_FILE" "$KEY_NEXT_EVENT_NODE"
    return $?
  fi
  send_input_event touch-next "$TOUCH_NEXT_EVENT_FILE" "$TOUCH_EVENT_NODE"
}

track_prev() {
  send_input_event key-prev "$KEY_PREV_EVENT_FILE" "$KEY_PREV_EVENT_NODE"
}

touch_first_track() {
  if [ -r "$TOUCH_FIRST_TRACK_EVENT_FILE" ]; then
    send_input_event touch-first-track "$TOUCH_FIRST_TRACK_EVENT_FILE" "$TOUCH_EVENT_NODE"
    return $?
  fi
  if [ -r "$TOUCH_FIRST_TRACK_DOWN_EVENT_FILE" ] && [ -r "$TOUCH_FIRST_TRACK_MOVE_EVENT_FILE" ] && [ -r "$TOUCH_FIRST_TRACK_UP_EVENT_FILE" ]; then
    send_input_event touch-first-track-down "$TOUCH_FIRST_TRACK_DOWN_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
    usleep "$TOUCH_FIRST_TRACK_HOLD_US" 2>/dev/null || sleep 1
    send_input_event touch-first-track-move "$TOUCH_FIRST_TRACK_MOVE_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
    usleep "$TOUCH_FIRST_TRACK_HOLD_US" 2>/dev/null || sleep 1
    send_input_event touch-first-track-up "$TOUCH_FIRST_TRACK_UP_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
    return 0
  fi
  send_input_event touch-first-track "$TOUCH_FIRST_TRACK_EVENT_FILE" "$TOUCH_EVENT_NODE"
}

touch_back_to_track_list() {
  send_input_event touch-back "$TOUCH_BACK_EVENT_FILE" "$TOUCH_EVENT_NODE"
}

touch_track_row() {
  row=$1
  case "$row" in
    1) send_input_event touch-track-row1 "$TOUCH_TRACK_ROW1_EVENT_FILE" "$TOUCH_EVENT_NODE" ;;
    2) send_input_event touch-track-row2 "$TOUCH_TRACK_ROW2_EVENT_FILE" "$TOUCH_EVENT_NODE" ;;
    3) send_input_event touch-track-row3 "$TOUCH_TRACK_ROW3_EVENT_FILE" "$TOUCH_EVENT_NODE" ;;
    4) send_input_event touch-track-row4 "$TOUCH_TRACK_ROW4_EVENT_FILE" "$TOUCH_EVENT_NODE" ;;
    5) send_input_event touch-track-row5 "$TOUCH_TRACK_ROW5_EVENT_FILE" "$TOUCH_EVENT_NODE" ;;
    *) return 1 ;;
  esac
}

sleep_track_swipe_phase() {
  usleep "$TOUCH_TRACK_SWIPE_PHASE_US" 2>/dev/null || sleep 1
}

touch_track_swipe_up() {
  send_input_event touch-track-swipe-down "$TOUCH_TRACK_SWIPE_DOWN_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
  sleep_track_swipe_phase
  send_input_event touch-track-swipe-move1 "$TOUCH_TRACK_SWIPE_MOVE1_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
  sleep_track_swipe_phase
  send_input_event touch-track-swipe-move2 "$TOUCH_TRACK_SWIPE_MOVE2_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
  sleep_track_swipe_phase
  send_input_event touch-track-swipe-move3 "$TOUCH_TRACK_SWIPE_MOVE3_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
  sleep_track_swipe_phase
  send_input_event touch-track-swipe-move4 "$TOUCH_TRACK_SWIPE_MOVE4_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
  sleep_track_swipe_phase
  send_input_event touch-track-swipe-move5 "$TOUCH_TRACK_SWIPE_MOVE5_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
  sleep_track_swipe_phase
  send_input_event touch-track-swipe-move6 "$TOUCH_TRACK_SWIPE_MOVE6_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
  sleep_track_swipe_phase
  send_input_event touch-track-swipe-up "$TOUCH_TRACK_SWIPE_UP_EVENT_FILE" "$TOUCH_EVENT_NODE" || return 1
}

book_title_autostart_active_now() {
  now_check=$(date '+%s')
  case "$book_title_autostart_until:$now_check" in
    *[!0-9:]* | :* | *:) return 1 ;;
    *)
      [ "$book_title_autostart_until" -gt "$now_check" ] && return 0
      ;;
  esac
  return 1
}

direct_track_max_swipes() {
  max_swipes=$BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES
  case "$max_swipes" in ''|*[!0-9]*) max_swipes=20 ;; esac
  printf '%s\n' "$max_swipes"
}

direct_track_geometry() {
  saved_index=$1
  case "$saved_index" in ''|*[!0-9]*) return 1 ;; esac
  [ "$saved_index" -gt 0 ] || return 1

  visible_rows=$BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS
  rows_per_swipe=$BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE
  max_swipes=$(direct_track_max_swipes)
  case "$visible_rows" in ''|*[!0-9]*|0) visible_rows=5 ;; esac
  case "$rows_per_swipe" in ''|*[!0-9]*|0) rows_per_swipe=4 ;; esac
  [ "$visible_rows" -le 5 ] || visible_rows=5

  swipes=0
  if [ "$saved_index" -gt "$visible_rows" ]; then
    swipes=$(( (saved_index - visible_rows + rows_per_swipe - 1) / rows_per_swipe ))
  fi
  [ "$swipes" -le "$max_swipes" ] || return 1

  row=$((saved_index - (swipes * rows_per_swipe)))
  while [ "$row" -lt 1 ] && [ "$swipes" -gt 0 ]; do
    swipes=$((swipes - 1))
    row=$((saved_index - (swipes * rows_per_swipe)))
  done
  while [ "$row" -gt "$visible_rows" ]; do
    swipes=$((swipes + 1))
    row=$((saved_index - (swipes * rows_per_swipe)))
  done
  [ "$row" -ge 1 ] && [ "$row" -le 5 ] || return 1
  printf '%s %s\n' "$swipes" "$row"
}

tap_track_list_index() {
  tap_track_target_index=$1
  tap_track_log_label=$2
  tap_track_geometry=$(direct_track_geometry "$tap_track_target_index") || return 1
  set -- $tap_track_geometry
  tap_track_swipes=$1
  tap_track_row=$2

  tap_track_count=0
  while [ "$tap_track_count" -lt "$tap_track_swipes" ]; do
    tap_track_count=$((tap_track_count + 1))
    touch_track_swipe_up || return 1
    sleep "$BOOK_TITLE_DIRECT_TRACK_SWIPE_SETTLE_SECONDS"
    log "$tap_track_log_label swipe=$tap_track_count/$tap_track_swipes"
  done
  touch_track_row "$tap_track_row" || return 1
  log "$tap_track_log_label tapped index=$tap_track_target_index swipes=$tap_track_swipes row=$tap_track_row"
}

book_title_verify_selected_track() {
  saved_path=$1
  saved_index=$2
  selected_row=$3
  log_label=$4
  path_before_select=$5

  saved_root=$(book_root_for_path "$saved_path")
  ticks=$(track_switch_settle_ticks)
  verify_min_ticks=0
  if [ "$path_before_select" = "$saved_path" ]; then
    verify_min_ticks=$ticks
  fi
  verify_seen_ticks=0
  while [ "$ticks" -gt 0 ]; do
    sleep_track_switch_poll
    verify_seen_ticks=$((verify_seen_ticks + 1))
    path_now=$(current_path)
    [ "$path_now" = "$saved_path" ] && [ "$verify_seen_ticks" -ge "$verify_min_ticks" ] && {
      log "$log_label reached path=$path_now saved=$saved_index row=$selected_row"
      return 0
    }
    if [ -n "$path_now" ] && [ "$path_now" != "$path_before_select" ]; then
      selected_root=$(book_root_for_path "$path_now")
      if [ "$selected_root" = "$saved_root" ]; then
        selected_index=$(catalog_field_for_path 2 "$path_now" || true)
        log "$log_label selected path=$path_now index=${selected_index:-?} saved=$saved_index row=$selected_row"
        case "$selected_index:$saved_index:$selected_row" in
          *[!0-9:]* | :* | *:) return 2 ;;
        esac
        delta=$((saved_index - selected_index))
        retry_row=$((selected_row + delta))
        if [ "$retry_row" -ge 1 ] && [ "$retry_row" -le 5 ]; then
          log "$log_label retry selected=$selected_index saved=$saved_index row=$retry_row"
          touch_back_to_track_list || return 2
          sleep "$BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS"
          path_before_retry=$(current_path)
          touch_track_row "$retry_row" || return 2
          retry_ticks=$(track_switch_settle_ticks)
          while [ "$retry_ticks" -gt 0 ]; do
            sleep_track_switch_poll
            path_retry=$(current_path)
            [ "$path_retry" = "$saved_path" ] && {
              log "$log_label retry reached path=$path_retry saved=$saved_index row=$retry_row"
              return 0
            }
            if [ -n "$path_retry" ] && [ "$path_retry" != "$path_before_retry" ]; then
              retry_root=$(book_root_for_path "$path_retry")
              if [ "$retry_root" = "$saved_root" ]; then
                retry_index=$(catalog_field_for_path 2 "$path_retry" || true)
                log "$log_label retry selected path=$path_retry index=${retry_index:-?} saved=$saved_index"
                break
              fi
            fi
            retry_ticks=$((retry_ticks - 1))
          done
        fi
        if track_restore_near_miss_transport "$saved_path" "$saved_index" "book-title-direct-misselect"; then
          return 0
        fi
        return 2
      fi
    fi
    ticks=$((ticks - 1))
  done

  path_now=$(current_path)
  log "$log_label did not reach saved path final_path=$path_now saved_path=$saved_path"
  [ "$path_now" = "$saved_path" ]
}

sleep_direct_open_arm_delay() {
  case "$DIRECT_OPEN_ARM_DELAY_US" in
    ''|*[!0-9]*|0) return 0 ;;
  esac
  usleep "$DIRECT_OPEN_ARM_DELAY_US" 2>/dev/null || sleep 1
}

book_title_direct_open_trigger() {
  pid=$1
  saved_index=$2
  saved_path=$3
  path_before_select=$4
  log_label=$5

  [ "$BOOK_TITLE_DIRECT_OPEN_ENABLED" = 1 ] || return 1
  [ -x "$DIRECT_OPEN_HELPER" ] || {
    log "direct-open helper unavailable helper=$DIRECT_OPEN_HELPER"
    return 1
  }
  case "$saved_index" in ''|*[!0-9]*) return 1 ;; esac
  [ "$saved_index" -gt 0 ] || return 1
  zero_index=$((saved_index - 1))
  [ -n "$pid" ] || return 1

  log "$log_label start saved=$saved_index zero_index=$zero_index helper=$DIRECT_OPEN_HELPER saved_path=$saved_path"
  "$DIRECT_OPEN_HELPER" \
    --pid "$pid" \
    --row-index "$zero_index" \
    --probe-addr "$DIRECT_OPEN_PROBE_ADDR" \
    --scratch-addr "$DIRECT_OPEN_SCRATCH_ADDR" \
    --timeout-ms "$DIRECT_OPEN_TIMEOUT_MS" >>"$LOG" 2>&1 &
  direct_open_pid=$!
  sleep_direct_open_arm_delay
  touch_track_row 1 || {
    wait "$direct_open_pid" 2>/dev/null
    log "$log_label failed to tap trigger row"
    return 1
  }
  wait "$direct_open_pid" 2>/dev/null
  direct_open_status=$?
  if [ "$direct_open_status" -ne 0 ]; then
    log "$log_label helper failed status=$direct_open_status saved=$saved_index"
    return 1
  fi

  book_title_verify_selected_track "$saved_path" "$saved_index" 1 "$log_label" "$path_before_select"
}

book_title_direct_open_row_override() {
  saved_index=$1
  saved_path=$2

  [ "$BOOK_TITLE_DIRECT_OPEN_ENABLED" = 1 ] || return 1
  [ -x "$DIRECT_OPEN_HELPER" ] || return 1
  case "$saved_index" in ''|*[!0-9]*) return 1 ;; esac
  [ "$saved_index" -gt 0 ] || return 1

  pid=$(player_pid) || return 1
  [ -n "$pid" ] || return 1

  touch_back_to_track_list || return 1
  sleep "$BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS"

  path_before_select=$(current_path)
  book_title_direct_open_trigger "$pid" "$saved_index" "$saved_path" "$path_before_select" "direct-open"
}

book_title_direct_track_select() {
  [ "$BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED" = 1 ] || return 1
  book_title_autostart_active_now || return 1

  current_index=$1
  saved_index=$2
  saved_path=$3
  case "$current_index:$saved_index" in
    *[!0-9:]* | :* | *:) return 1 ;;
  esac
  [ "$current_index" -eq 1 ] || return 1
  [ "$saved_index" -gt 1 ] || return 1

  if book_title_direct_open_row_override "$saved_index" "$saved_path"; then
    return 0
  fi

  geometry=$(direct_track_geometry "$saved_index") || {
    log "direct-track-select too many swipes saved=$saved_index max=$(direct_track_max_swipes)"
    return 1
  }
  set -- $geometry
  swipes=$1
  row=$2

  log "direct-track-select start saved=$saved_index swipes=$swipes row=$row saved_path=$saved_path"
  touch_back_to_track_list || return 1
  sleep "$BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS"

  path_before_select=$(current_path)
  tap_track_list_index "$saved_index" "direct-track-select" || return 1
  ticks=$(track_switch_settle_ticks)
  while [ "$ticks" -gt 0 ]; do
    sleep_track_switch_poll
    path_now=$(current_path)
    [ "$path_now" = "$saved_path" ] && {
      log "direct-track-select reached path=$path_now saved=$saved_index"
      return 0
    }
    if [ -n "$path_now" ] && [ "$path_now" != "$path_before_select" ]; then
      selected_root=$(book_root_for_path "$path_now")
      saved_root=$(book_root_for_path "$saved_path")
      if [ "$selected_root" = "$saved_root" ]; then
        selected_index=$(catalog_field_for_path 2 "$path_now" || true)
        log "direct-track-select selected nearby path=$path_now index=${selected_index:-?} saved=$saved_index"
        case "$selected_index:$saved_index:$row" in
          *[!0-9:]* | :* | *:) return 2 ;;
        esac
        delta=$((saved_index - selected_index))
        retry_row=$((row + delta))
        if [ "$retry_row" -ge 1 ] && [ "$retry_row" -le 5 ]; then
          log "direct-track-select retry nearby selected=$selected_index saved=$saved_index row=$retry_row"
          touch_back_to_track_list || return 2
          sleep "$BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS"
          path_before_retry=$(current_path)
          touch_track_row "$retry_row" || return 2
          ticks=$(track_switch_settle_ticks)
          while [ "$ticks" -gt 0 ]; do
            sleep_track_switch_poll
            path_retry=$(current_path)
            [ "$path_retry" = "$saved_path" ] && {
              log "direct-track-select retry reached path=$path_retry saved=$saved_index row=$retry_row"
              return 0
            }
            if [ -n "$path_retry" ] && [ "$path_retry" != "$path_before_retry" ]; then
              retry_root=$(book_root_for_path "$path_retry")
              if [ "$retry_root" = "$saved_root" ]; then
                retry_index=$(catalog_field_for_path 2 "$path_retry" || true)
                log "direct-track-select retry selected path=$path_retry index=${retry_index:-?} saved=$saved_index"
                return 2
              fi
            fi
            ticks=$((ticks - 1))
          done
          path_retry=$(current_path)
          log "direct-track-select retry did not reach saved path final_path=$path_retry saved_path=$saved_path"
        fi
        return 2
      fi
    fi
    ticks=$((ticks - 1))
  done

  path_now=$(current_path)
  log "direct-track-select did not reach saved path final_path=$path_now saved_path=$saved_path"
  [ "$path_now" = "$saved_path" ]
}

book_title_visible_track_select() {
  [ "$BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED" = 1 ] || return 1
  book_title_autostart_active_now || return 1

  current_index=$1
  saved_index=$2
  saved_path=$3
  case "$current_index:$saved_index" in
    *[!0-9:]* | :* | *:) return 1 ;;
  esac
  [ "$current_index" != "$saved_index" ] || return 0

  visible_rows=$BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS
  case "$visible_rows" in ''|*[!0-9]*|0) visible_rows=5 ;; esac
  [ "$visible_rows" -gt 5 ] && visible_rows=5

  delta=$((saved_index - current_index))
  if [ "$delta" -ge 0 ] && [ "$delta" -lt "$visible_rows" ]; then
    row=$((delta + 1))
  elif [ "$delta" -lt 0 ] && [ $((-delta)) -lt "$visible_rows" ]; then
    row=$((visible_rows + delta))
  else
    return 1
  fi
  [ "$row" -ge 1 ] && [ "$row" -le 5 ] || return 1

  log "visible-track-select start current=$current_index saved=$saved_index row=$row saved_path=$saved_path"
  touch_back_to_track_list || return 2
  sleep "$BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS"

  path_before_select=$(current_path)
  touch_track_row "$row" || return 2
  ticks=$(track_switch_settle_ticks)
  while [ "$ticks" -gt 0 ]; do
    sleep_track_switch_poll
    path_now=$(current_path)
    [ "$path_now" = "$saved_path" ] && {
      log "visible-track-select reached path=$path_now saved=$saved_index row=$row"
      return 0
    }
    if [ -n "$path_now" ] && [ "$path_now" != "$path_before_select" ]; then
      selected_root=$(book_root_for_path "$path_now")
      saved_root=$(book_root_for_path "$saved_path")
      if [ "$selected_root" = "$saved_root" ]; then
        selected_index=$(catalog_field_for_path 2 "$path_now" || true)
        log "visible-track-select selected nearby path=$path_now index=${selected_index:-?} saved=$saved_index"
        return 2
      fi
    fi
    ticks=$((ticks - 1))
  done

  path_now=$(current_path)
  log "visible-track-select did not reach saved path final_path=$path_now saved_path=$saved_path"
  return 2
}

save_position() {
  path=$1
  pos=$2
  root=$(book_root_for_path "$path")
  record=$(record_for_path "$path")
  tmp="$record.tmp.$$"
  book_key=$(book_key_for_path "$path")
  track_index=$(catalog_field_for_path 2 "$path" || true)
  track_count=$(catalog_field_for_path 3 "$path" || true)
  media_id=$(catalog_field_for_path 4 "$path" || true)
  chapter_title=$(catalog_field_for_path 6 "$path" || true)
  completed=$(completion_state_for_path_position "$path" "$pos")
  old_completed=
  [ -f "$record" ] && old_completed=$(json_bool completed "$record")
  if [ "$completed" = true ] && [ "$old_completed" != true ]; then
    log "book completed root=$root path=$path pos_ms=$pos threshold_ms=$COMPLETED_END_THRESHOLD_MS"
  elif [ "$completed" = false ] && [ "$old_completed" = true ]; then
    log "book restart clears completed root=$root path=$path pos_ms=$pos"
  fi
  mkdir -p "$STORE_DIR"
  {
    echo "{"
    echo '  "schema_version": 3,'
    printf '  "book_id": "%s",\n' "$(json_escape "$(safe_id "$root")")"
    printf '  "book_key": "%s",\n' "$(json_escape "$book_key")"
    printf '  "root_hiby_path": "%s",\n' "$(json_escape "$root")"
    printf '  "current_path": "%s",\n' "$(json_escape "$path")"
    printf '  "media_id": %s,\n' "$(json_number_or_null "$media_id")"
    printf '  "track_index": %s,\n' "$(json_number_or_null "$track_index")"
    printf '  "track_count": %s,\n' "$(json_number_or_null "$track_count")"
    printf '  "chapter_title": "%s",\n' "$(json_escape "$chapter_title")"
    printf '  "position_ms": %s,\n' "$pos"
    printf '  "updated_at": "%s",\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf '  "completed": %s\n' "$completed"
    echo "}"
  } >"$tmp"
  mv "$tmp" "$record"
}

ui_seek_restore() {
  path=$1
  saved_pos=$2
  [ "$UI_SEEK_FALLBACK_ENABLED" = 1 ] || return 1
  case "$saved_pos:$UI_SEEK_BAR_X_MIN:$UI_SEEK_BAR_X_MAX:$UI_SEEK_BAR_Y" in
    *[!0-9:]* | :* | *: | *::*) return 1 ;;
  esac

  duration=$(duration_ms_memory) || {
    log "ui-seek unavailable duration path=$path saved_ms=$saved_pos"
    return 1
  }
  case "$duration" in ''|*[!0-9]*) return 1 ;; esac
  [ "$duration" -ge "$UI_SEEK_MIN_DURATION_MS" ] || {
    log "ui-seek skipped short-duration path=$path saved_ms=$saved_pos duration_ms=$duration"
    return 1
  }
  [ "$saved_pos" -gt 0 ] && [ "$saved_pos" -lt "$duration" ] || {
    log "ui-seek skipped out-of-range path=$path saved_ms=$saved_pos duration_ms=$duration"
    return 1
  }
  ui_seek_screen_ready || return 1

  range=$((UI_SEEK_BAR_X_MAX - UI_SEEK_BAR_X_MIN))
  [ "$range" -gt 2 ] || return 1
  x=$((UI_SEEK_BAR_X_MIN + (saved_pos * range + duration / 2) / duration))
  [ "$x" -gt "$UI_SEEK_BAR_X_MIN" ] || x=$((UI_SEEK_BAR_X_MIN + 1))
  [ "$x" -lt "$UI_SEEK_BAR_X_MAX" ] || x=$((UI_SEEK_BAR_X_MAX - 1))

  log "ui-seek attempt path=$path saved_ms=$saved_pos duration_ms=$duration x=$x y=$UI_SEEK_BAR_Y"
  touch_generated_tap ui-seek "$x" "$UI_SEEK_BAR_Y" || return 1
  case "$UI_SEEK_VERIFY_DELAY_SECONDS" in
    ''|*[!0-9]*) sleep 2 ;;
    *) sleep "$UI_SEEK_VERIFY_DELAY_SECONDS" ;;
  esac

  path_after=$(current_path)
  [ "$path_after" = "$path" ] || {
    log "ui-seek path changed path=$path after=$path_after saved_ms=$saved_pos"
    return 1
  }

  pos_after=$(position_ms_memory) || {
    log "ui-seek verify unavailable path=$path saved_ms=$saved_pos"
    return 1
  }
  case "$pos_after:$UI_SEEK_VERIFY_TOLERANCE_MS" in
    *[!0-9:]* | :* | *:) return 1 ;;
  esac
  diff=$((pos_after - saved_pos))
  [ "$diff" -ge 0 ] || diff=$((0 - diff))
  tolerance=$((UI_SEEK_VERIFY_TOLERANCE_MS + duration / range))
  if [ "$diff" -le "$tolerance" ]; then
    log "ui-seek restored path=$path saved_ms=$saved_pos pos_ms=$pos_after duration_ms=$duration diff_ms=$diff tolerance_ms=$tolerance"
    return 0
  fi
  log "ui-seek verify failed path=$path saved_ms=$saved_pos pos_ms=$pos_after duration_ms=$duration diff_ms=$diff tolerance_ms=$tolerance"
  return 1
}

maybe_restore() {
  [ "$RESTORE_ENABLED" = 1 ] || return 0
  path=$1
  pos=$2
  root=$(book_root_for_path "$path")
  record=$(existing_record_for_path "$path")
  [ -f "$record" ] || return 0
  [ "$(json_bool completed "$record")" = true ] && return 0
  saved_path=$(record_saved_path_for_current "$record" "$path" "$root" || true)
  saved_pos=$(json_number position_ms "$record")
  [ -n "$saved_path" ] || return 0
  [ "$saved_path" = "$path" ] || return 1
  case "$saved_pos" in ''|*[!0-9]*) return 0 ;; esac
  [ "$saved_pos" -ge "$RESTORE_MIN_MS" ] || return 0
  if [ "$pos" -gt "$RESTORE_ONLY_BEFORE_MS" ]; then
    guard_pos=$((pos + BACKWARD_SAVE_GUARD_MS))
    if [ "${autostart_restore_active:-0}" != 1 ] && [ "$saved_pos" -gt "$guard_pos" ]; then
      log "skip late backward restore for manual position path=$path pos_ms=$pos saved_ms=$saved_pos"
      return 0
    fi
    [ "$saved_pos" -gt "$guard_pos" ] || return 0
    log "late restore path=$path pos_ms=$pos saved_ms=$saved_pos"
  fi
  target_pos=$(restore_target_ms "$saved_pos")
  seconds=$((target_pos / 1000))
  if [ "$target_pos" = "$saved_pos" ]; then
    log "restore path=$path saved_ms=$saved_pos"
  else
    log "restore path=$path saved_ms=$saved_pos target_ms=$target_pos rewind_ms=$RESTORE_REWIND_MS"
  fi
  if run_helper seek --seconds "$seconds" --verify-delay-ms 1500 --verify-tolerance 8 >>"$LOG" 2>&1; then
    return 0
  fi
  if ui_seek_restore "$path" "$target_pos"; then
    return 0
  fi
  note_seek_restore_failure "$path" "$target_pos"
  return 1
}

track_restore_near_miss_transport() {
  [ "$TRACK_RESTORE_NEAR_MISS_TRANSPORT_ENABLED" = 1 ] || return 1
  saved_path=$1
  saved_index=$2
  reason=$3
  mode=$(play_mode_value || true)
  [ "$mode" = "$PLAY_MODE_TARGET" ] || {
    log "track-restore near-miss transport skipped mode=$mode target=$PLAY_MODE_TARGET reason=$reason saved=$saved_index"
    return 1
  }
  path=$(current_path)
  [ "$path" = "$saved_path" ] && return 0
  current_index=$(catalog_field_for_path 2 "$path" || true)
  case "$current_index" in ''|*[!0-9]*) return 1 ;; esac
  case "$saved_index" in ''|*[!0-9]*) return 1 ;; esac
  max_steps=$TRACK_RESTORE_NEAR_MISS_MAX_STEPS
  case "$reason" in
    book-title-direct-misselect)
      [ "$BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED" = 1 ] || return 1
      max_steps=$BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS
      ;;
  esac
  case "$max_steps" in ''|*[!0-9]*) return 1 ;; esac
  if [ "$saved_index" -gt "$current_index" ]; then
    direction=next
    steps=$((saved_index - current_index))
  elif [ "$saved_index" -lt "$current_index" ]; then
    direction=prev
    steps=$((current_index - saved_index))
  else
    return 0
  fi
  [ "$steps" -le "$max_steps" ] || {
    log "track-restore near-miss transport too many steps current=$current_index saved=$saved_index max=$max_steps reason=$reason"
    return 1
  }

  log "track-restore near-miss transport start reason=$reason current=$current_index saved=$saved_index direction=$direction steps=$steps path=$path saved_path=$saved_path"
  count=0
  while [ "$count" -lt "$steps" ]; do
    count=$((count + 1))
    path_before=$(current_path)
    if [ "$direction" = next ]; then
      expected_index=$((current_index + count))
      track_next || return 1
    else
      expected_index=$((current_index - count))
      track_prev || return 1
    fi
    ticks=$(track_switch_settle_ticks)
    while [ "$ticks" -gt 0 ]; do
      sleep_track_switch_poll
      path_now=$(current_path)
      index_now=$(catalog_field_for_path 2 "$path_now" || true)
      [ "$path_now" = "$saved_path" ] && break
      [ "$index_now" = "$expected_index" ] && break
      if [ "$path_now" != "$path_before" ] && [ -n "$index_now" ]; then
        break
      fi
      ticks=$((ticks - 1))
    done
    path_now=$(current_path)
    index_now=$(catalog_field_for_path 2 "$path_now" || true)
    log "track-restore near-miss transport $direction step=$count/$steps path=$path_now index=${index_now:-?}"
    [ "$path_now" = "$saved_path" ] && return 0
  done

  path_now=$(current_path)
  [ "$path_now" = "$saved_path" ] && return 0
  log "track-restore near-miss transport failed reason=$reason final_path=$path_now saved_path=$saved_path"
  return 1
}

maybe_restore_track() {
  [ "$RESTORE_ENABLED" = 1 ] || return 0
  [ "$TRACK_RESTORE_ENABLED" = 1 ] || return 0
  path=$1
  pos=$2
  root=$(book_root_for_path "$path")
  record=$(existing_record_for_path "$path")
  [ -f "$record" ] || return 0
  [ "$(json_bool completed "$record")" = true ] && return 0
  saved_path=$(record_saved_path_for_current "$record" "$path" "$root" || true)
  saved_pos=$(json_number position_ms "$record")
  [ -n "$saved_path" ] || return 0
  [ "$saved_path" != "$path" ] || return 0
  same_book_root "$saved_path" "$root" || return 0
  case "$saved_pos" in ''|*[!0-9]*) return 0 ;; esac
  [ "$saved_pos" -ge "$RESTORE_MIN_MS" ] || return 0
  [ "$pos" -le "$RESTORE_ONLY_BEFORE_MS" ] || return 0
  if [ "${autostart_restore_active:-0}" != 1 ]; then
    log "skip track-restore for manual track selection path=$path saved_path=$saved_path pos_ms=$pos"
    return 0
  fi

  current_index=$(catalog_field_for_path 2 "$path" || true)
  saved_index=$(catalog_field_for_path 2 "$saved_path" || true)
  case "$current_index:$saved_index" in
    *[!0-9:]* | :* | *:)
      log "track-restore unavailable current=${current_index:-?} saved=${saved_index:-?} path=$path saved_path=$saved_path"
      return 1
      ;;
  esac
  if [ "$saved_index" -gt "$current_index" ]; then
    direction=next
    steps=$((saved_index - current_index))
  elif [ "$saved_index" -lt "$current_index" ]; then
    direction=prev
    steps=$((current_index - saved_index))
  else
    return 0
  fi
  [ "$steps" -le "$TRACK_RESTORE_MAX_STEPS" ] || {
    log "track-restore too many steps current=$current_index saved=$saved_index max=$TRACK_RESTORE_MAX_STEPS"
    return 1
  }

  book_title_direct_track_select "$current_index" "$saved_index" "$saved_path"
  direct_status=$?
  if [ "$direct_status" -eq 0 ]; then
    return 0
  fi
  if [ "$direct_status" -eq 2 ]; then
    path_after_direct=$(current_path)
    if [ "$path_after_direct" = "$saved_path" ]; then
      return 0
    fi
    if same_book_root "$path_after_direct" "$root"; then
      current_index_after_direct=$(catalog_field_for_path 2 "$path_after_direct" || true)
      case "$current_index_after_direct:$saved_index" in
        *[!0-9:]* | :* | *:) ;;
        *)
          log "direct-track-select near-miss visible fallback current=$current_index_after_direct saved=$saved_index path=$path_after_direct"
          book_title_visible_track_select "$current_index_after_direct" "$saved_index" "$saved_path"
          visible_after_direct_status=$?
          if [ "$visible_after_direct_status" -eq 0 ]; then
            return 0
          fi
          if [ "$visible_after_direct_status" -eq 2 ]; then
            if track_restore_near_miss_transport "$saved_path" "$saved_index" "direct-near-miss"; then
              return 0
            fi
            log "visible-track-select stopped key fallback after direct near miss saved=$saved_index saved_path=$saved_path"
            return 1
          fi
          ;;
      esac
    fi
    log "direct-track-select stopped key fallback after near miss saved=$saved_index saved_path=$saved_path"
    return 1
  fi
  book_title_visible_track_select "$current_index" "$saved_index" "$saved_path"
  visible_status=$?
  if [ "$visible_status" -eq 0 ]; then
    return 0
  fi
  if [ "$visible_status" -eq 2 ]; then
    if track_restore_near_miss_transport "$saved_path" "$saved_index" "visible-near-miss"; then
      return 0
    fi
    log "visible-track-select stopped key fallback after near miss saved=$saved_index saved_path=$saved_path"
    return 1
  fi
  if [ "$TRACK_RESTORE_KEY_FALLBACK_ENABLED" != 1 ]; then
    log "track-restore key fallback disabled current=$current_index saved=$saved_index saved_path=$saved_path"
    return 1
  fi
  path_after_direct=$(current_path)
  if [ "$path_after_direct" != "$path" ]; then
    if [ "$path_after_direct" = "$saved_path" ]; then
      return 0
    fi
    same_book_root "$path_after_direct" "$root" || {
      log "direct-track-select left book final_path=$path_after_direct saved_path=$saved_path"
      return 1
    }
    path=$path_after_direct
    current_index=$(catalog_field_for_path 2 "$path" || true)
    case "$current_index:$saved_index" in
      *[!0-9:]* | :* | *:) return 1 ;;
    esac
    if [ "$saved_index" -gt "$current_index" ]; then
      direction=next
      steps=$((saved_index - current_index))
    elif [ "$saved_index" -lt "$current_index" ]; then
      direction=prev
      steps=$((current_index - saved_index))
    else
      return 0
    fi
    [ "$steps" -le "$TRACK_RESTORE_MAX_STEPS" ] || {
      log "track-restore too many steps current=$current_index saved=$saved_index max=$TRACK_RESTORE_MAX_STEPS"
      return 1
    }
    log "direct-track-select fallback current=$current_index saved=$saved_index direction=$direction steps=$steps path=$path"
  fi

  log "track-restore start current=$current_index saved=$saved_index direction=$direction steps=$steps path=$path saved_path=$saved_path"
  count=0
  while [ "$count" -lt "$steps" ]; do
    count=$((count + 1))
    path_before=$(current_path)
    if [ "$direction" = next ]; then
      expected_index=$((current_index + count))
    else
      expected_index=$((current_index - count))
    fi
    if [ "$direction" = next ]; then
      track_next || return 1
    else
      track_prev || return 1
    fi
    ticks=$(track_switch_settle_ticks)
    while [ "$ticks" -gt 0 ]; do
      sleep_track_switch_poll
      path_now=$(current_path)
      index_now=$(catalog_field_for_path 2 "$path_now" || true)
      [ "$path_now" = "$saved_path" ] && break
      [ "$index_now" = "$expected_index" ] && break
      if [ "$path_now" != "$path_before" ] && [ -n "$index_now" ]; then
        break
      fi
      ticks=$((ticks - 1))
    done
    path_now=$(current_path)
    index_now=$(catalog_field_for_path 2 "$path_now" || true)
    log "track-restore $direction step=$count/$steps path=$path_now index=${index_now:-?}"
    [ "$path_now" = "$saved_path" ] && return 0
  done

  path_now=$(current_path)
  if [ "$path_now" = "$saved_path" ]; then
    return 0
  fi
  log "track-restore failed final_path=$path_now saved_path=$saved_path"
  return 1
}

should_defer_new_track_save() {
  path=$1
  pos=$2
  root=$(book_root_for_path "$path")
  record=$(existing_record_for_path "$path")
  [ -f "$record" ] || return 1
  saved_path=$(record_saved_path_for_current "$record" "$path" "$root" || true)
  [ -n "$saved_path" ] || return 1
  [ "$saved_path" != "$path" ] || return 1
  if [ "${autostart_restore_active:-0}" = 1 ] && [ "${restored_path:-}" != "$path" ]; then
    return 0
  fi
  [ "$pos" -lt "$NEW_TRACK_COMMIT_MS" ] || return 1
  return 0
}

should_skip_after_completed_restore() {
  path=$1
  pos=$2
  completed_saved_path=$3
  [ -n "$completed_saved_path" ] || return 1
  root=$(book_root_for_path "$path")
  record=$(existing_record_for_path "$path")
  [ -f "$record" ] || return 1
  saved_path=$(record_saved_path_for_current "$record" "$path" "$root" || true)
  [ "$saved_path" = "$completed_saved_path" ] || return 1
  [ "$saved_path" != "$path" ] || return 1
  same_book_root "$saved_path" "$root" || return 1
  [ "$pos" -lt "$NEW_TRACK_COMMIT_MS" ] || return 1

  current_index=$(catalog_field_for_path 2 "$path" || true)
  saved_index=$(catalog_field_for_path 2 "$saved_path" || true)
  case "$current_index:$saved_index" in
    *[!0-9:]* | :* | *:) return 1 ;;
  esac
  [ "$current_index" -gt "$saved_index" ] || return 1
  log "skip track-restore after completed restore current=$current_index saved=$saved_index path=$path completed_saved_path=$completed_saved_path"
  return 0
}

should_skip_failed_restore_save() {
  path=$1
  pos=$2
  [ "$restore_failed_path" = "$path" ] || return 1
  root=$(book_root_for_path "$path")
  record=$(existing_record_for_path "$path")
  [ -f "$record" ] || return 1
  saved_path=$(record_saved_path_for_current "$record" "$path" "$root" || true)
  saved_pos=$(json_number position_ms "$record")
  [ "$saved_path" = "$path" ] || return 1
  case "$saved_pos:$pos" in
    *[!0-9:]* | :* | *:) return 1 ;;
  esac
  guard_pos=$((pos + BACKWARD_SAVE_GUARD_MS))
  [ "$saved_pos" -gt "$guard_pos" ] || return 1
  case "$FAILED_RESTORE_SKIP_LOG_BUCKET_MS" in ''|*[!0-9]*|0) bucket=0 ;; *) bucket=$((pos / FAILED_RESTORE_SKIP_LOG_BUCKET_MS)) ;; esac
  skip_log_key="$path:$saved_pos:$bucket"
  if [ "$failed_restore_skip_log_bucket" != "$skip_log_key" ]; then
    log "skip save after failed restore path=$path pos_ms=$pos saved_ms=$saved_pos"
    failed_restore_skip_log_bucket=$skip_log_key
  fi
  return 0
}

book_title_direct_start_saved_track() {
  [ "$BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED" = 1 ] || return 1
  [ "$BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED" = 1 ] || return 1
  [ "$RESTORE_ENABLED" = 1 ] || return 1

  pid=$1
  track_list_ptr=$2
  catalog_scan_ptr=$3
  allow_memscan_root=${4:-1}

  track_path=
  if [ "$allow_memscan_root" = 1 ]; then
    root=$(book_title_memscan_root "$pid" || true)
    if [ -n "$root" ]; then
      track_path=$(catalog_first_path_for_root "$root")
      if [ -n "$track_path" ]; then
        log "book-title direct-start memscan root=$root path=$track_path"
      else
        log "book-title direct-start memscan no first path root=$root"
      fi
    fi
  fi
  if [ -z "$track_path" ]; then
    track_path=$(pid_mem_first_catalog_path "$pid" "$track_list_ptr" "$BOOK_TITLE_TRACK_LIST_SCAN_BYTES" || true)
  fi
  if [ -z "$track_path" ]; then
    track_path=$(pid_mem_first_catalog_path "$pid" "$catalog_scan_ptr" "$BOOK_TITLE_CATALOG_SCAN_BYTES" || true)
  fi
  [ -n "$track_path" ] || {
    log "book-title direct-start unavailable no catalog path track_list_ptr=$track_list_ptr catalog_scan_ptr=$catalog_scan_ptr"
    return 1
  }

  root=$(book_root_for_path "$track_path")
  record=$(existing_record_for_path "$track_path")
  [ -f "$record" ] || {
    log "book-title direct-start no resume record root=$root path=$track_path"
    return 1
  }
  [ "$(json_bool completed "$record")" = true ] && {
    log "book-title direct-start skipped completed root=$root"
    return 1
  }

  saved_path=$(record_saved_path_for_current "$record" "$track_path" "$root" || true)
  saved_pos=$(json_number position_ms "$record")
  [ -n "$saved_path" ] || return 1
  case "$saved_pos" in ''|*[!0-9]*) return 1 ;; esac
  [ "$saved_pos" -ge "$RESTORE_MIN_MS" ] || return 1

  saved_index=$(catalog_field_for_path 2 "$saved_path" || true)
  track_count=$(catalog_field_for_path 3 "$saved_path" || true)
  case "$saved_index" in ''|*[!0-9]*) return 1 ;; esac

  path_before_direct=$(current_path)
  if [ "$BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED" = 1 ] && [ "$path_before_direct" = "$saved_path" ]; then
    log "book-title direct-start visible-probe saved_index=$saved_index/${track_count:-?} saved_ms=$saved_pos saved_path=$saved_path"
    touch_track_row 1 || return 1
    book_title_verify_selected_track "$saved_path" "$saved_index" 1 "book-title direct-start visible-probe" "$path_before_direct"
    return $?
  fi

  if [ "$saved_index" -gt 1 ] && [ "$BOOK_TITLE_DIRECT_OPEN_ENABLED" = 1 ] && [ -x "$DIRECT_OPEN_HELPER" ]; then
    book_title_direct_open_trigger "$pid" "$saved_index" "$saved_path" "$path_before_direct" "book-title direct-open-start"
    direct_open_start_status=$?
    if [ "$direct_open_start_status" -eq 0 ]; then
      return 0
    fi
    log "book-title direct-open-start fallback status=$direct_open_start_status saved=$saved_index saved_path=$saved_path"
    touch_back_to_track_list || return 1
    sleep "$BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS"
    path_before_direct=$(current_path)
  fi

  geometry=$(direct_track_geometry "$saved_index") || {
    log "book-title direct-start too many swipes saved=$saved_index max=$(direct_track_max_swipes) path=$saved_path"
    return 1
  }
  set -- $geometry
  swipes=$1
  row=$2
  log "book-title direct-start root=$root saved_index=$saved_index/${track_count:-?} saved_ms=$saved_pos swipes=$swipes row=$row saved_path=$saved_path"
  tap_track_list_index "$saved_index" "book-title direct-start" || return 1
  book_title_verify_selected_track "$saved_path" "$saved_index" "$row" "book-title direct-start" "$path_before_direct"
}

book_title_marker_seq() {
  [ "$BOOK_TITLE_AUTOSTART_ENABLED" = 1 ] || return 1
  pid=$(player_pid_cached)
  [ -n "$pid" ] || return 1
  magic=$(u32le_at_pid_mem "$pid" "$BOOK_TITLE_MARKER_ADDR") || return 1
  [ "$magic" = 3235793431 ] || return 1
  seq_addr=$((BOOK_TITLE_MARKER_ADDR + 40))
  u32le_at_pid_mem "$pid" "$seq_addr"
}

book_title_context_active() {
  now=$1
  case "${book_title_context_until:-0}:$now:$BOOK_TITLE_CONTEXT_SECONDS" in
    *[!0-9:]* | :* | *: | *:0) return 1 ;;
    *)
      [ "${book_title_context_until:-0}" -gt "$now" ] && return 0
      ;;
  esac
  return 1
}

poll_interval_due() {
  last_poll=$1
  now=$2
  interval=$3
  case "$last_poll:$now:$interval" in
    *[!0-9:]* | :* | *:) return 0 ;;
  esac
  [ "$interval" -gt 0 ] || return 0
  [ "$last_poll" -le 0 ] && return 0
  [ $((now - last_poll)) -ge "$interval" ]
}

should_poll_book_title_marker() {
  path_preview=$1
  now=$2
  [ "$BOOK_TITLE_AUTOSTART_ENABLED" = 1 ] || return 1
  path_preview_is_audiobook "$path_preview" && return 0
  if path_preview_is_music "$path_preview"; then
    poll_interval_due "${last_book_title_marker_poll_at:-0}" "$now" "$BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS"
    return $?
  fi
  book_title_context_active "$now" && return 0
  poll_interval_due "${last_book_title_marker_poll_at:-0}" "$now" "$BOOK_TITLE_MARKER_IDLE_POLL_SECONDS"
}

should_attempt_restore_for_position() {
  pos=$1
  autostart_active=$2
  case "$pos" in ''|*[!0-9]*) return 1 ;; esac
  [ "$pos" -gt 0 ] || [ "$autostart_active" = 1 ]
}

book_title_should_preplay_direct_start() {
  case "$1" in
    launcher|context|relaxed) return 1 ;;
    *) return 0 ;;
  esac
}

book_title_preplay_allow_memscan_root() {
  case "$1" in
    launcher|context|path|relaxed) printf '%s\n' 0 ;;
    *) printf '%s\n' 1 ;;
  esac
}

diag_inc() {
  name=$1
  eval "$name=\$((\${$name:-0} + 1))"
}

diag_maybe_log() {
  now=$1
  case "$DIAGNOSTICS_INTERVAL_SECONDS:$now" in
    *[!0-9:]* | :* | 0:*) return 0 ;;
  esac
  if [ "${diag_last_log_at:-0}" -le 0 ]; then
    diag_last_log_at=$now
    return 0
  fi
  [ $((now - diag_last_log_at)) -ge "$DIAGNOSTICS_INTERVAL_SECONDS" ] || return 0
  log "stats loops=${diag_loops:-0} audiobook=${diag_audiobook_loops:-0} non_audiobook=${diag_non_audiobook_loops:-0} path_preview=${diag_path_previews:-0} marker_polls=${diag_marker_polls:-0} marker_skips=${diag_marker_skips:-0} position_reads=${diag_position_reads:-0} saves=${diag_saves:-0}"
  diag_last_log_at=$now
  diag_loops=0
  diag_audiobook_loops=0
  diag_non_audiobook_loops=0
  diag_path_previews=0
  diag_marker_polls=0
  diag_marker_skips=0
  diag_position_reads=0
  diag_saves=0
}

maybe_autostart_book_title() {
  [ "$BOOK_TITLE_AUTOSTART_ENABLED" = 1 ] || return 0
  seq=$1
  [ -n "$seq" ] || return 0
  now=$(date +%s 2>/dev/null || echo 0)
  context_active=0
  book_title_context_active "$now" && context_active=1
  pid=$(player_pid)
  [ -n "$pid" ] || return 0
  album_ptr_addr=$((BOOK_TITLE_MARKER_ADDR + 24))
  album_ptr=$(u32le_at_pid_mem "$pid" "$album_ptr_addr") || return 0
  [ "$album_ptr" -gt 4096 ] || return 0
  track_list_ptr_addr=$((album_ptr + BOOK_TITLE_TRACK_LIST_OFFSET))
  track_list_ptr=$(u32le_at_pid_mem "$pid" "$track_list_ptr_addr") || return 0
  [ "$track_list_ptr" -gt 4096 ] || return 0

  match_reason=
  catalog_scan_ptr=0
  source_magic_addr=$((BOOK_TITLE_MARKER_ADDR + 32))
  source_seq_addr=$((BOOK_TITLE_MARKER_ADDR + 44))
  source_magic=$(u32le_at_pid_mem "$pid" "$source_magic_addr" 2>/dev/null || echo 0)
  source_seq=$(u32le_at_pid_mem "$pid" "$source_seq_addr" 2>/dev/null || echo 0)
  case "$source_magic:$source_seq" in
    "$BOOK_TITLE_SOURCE_MAGIC:$seq") match_reason=launcher ;;
  esac

  if [ "$match_reason" = launcher ]; then
    :
  elif pid_mem_contains "$pid" "$track_list_ptr" "$BOOK_TITLE_TRACK_LIST_SCAN_BYTES" 'a:\Audiobooks'; then
    match_reason=path
  else
    catalog_scan_ptr_addr=$((album_ptr + BOOK_TITLE_CATALOG_SCAN_PTR_OFFSET))
    catalog_scan_ptr=$(u32le_at_pid_mem "$pid" "$catalog_scan_ptr_addr" 2>/dev/null || echo 0)
    case "$catalog_scan_ptr" in ''|*[!0-9]*) catalog_scan_ptr=0 ;; esac
    if pid_mem_contains_catalog_album "$pid" "$catalog_scan_ptr" "$BOOK_TITLE_CATALOG_SCAN_BYTES"; then
      match_reason=catalog
    elif [ "$context_active" = 1 ]; then
      match_reason=context
    elif [ "$BOOK_TITLE_AUTOSTART_REQUIRE_PATH" = 1 ]; then
      log "book-title autostart ignored seq=$seq album_ptr=$album_ptr track_list_ptr=$track_list_ptr catalog_scan_ptr=$catalog_scan_ptr source_magic=$source_magic source_seq=$source_seq context_until=${book_title_context_until:-0}"
      return 0
    else
      match_reason=relaxed
      log "book-title autostart relaxed seq=$seq album_ptr=$album_ptr track_list_ptr=$track_list_ptr catalog_scan_ptr=$catalog_scan_ptr"
    fi
  fi

  case "$match_reason" in
    launcher|path|catalog)
      if [ "$BOOK_TITLE_CONTEXT_SECONDS" -gt 0 ] 2>/dev/null; then
        book_title_context_until=$((now + BOOK_TITLE_CONTEXT_SECONDS))
      fi
      enable_audiobook_back_guard_window "$now"
      ;;
  esac

  log "book-title autostart seq=$seq reason=$match_reason album_ptr=$album_ptr track_list_ptr=$track_list_ptr catalog_scan_ptr=$catalog_scan_ptr source_magic=$source_magic source_seq=$source_seq context_active=$context_active context_until=${book_title_context_until:-0}"
  book_title_autostart_until=$((now + RESTORE_RETRY_AFTER_FAILURE_SECONDS))
  book_title_autostart_seq=$seq
  book_title_autostart_reset_key=
  book_title_restore_wait_log_key=
  book_title_pre_restore_log_key=
  restored_path=
  completed_saved_path=
  clear_restore_failure_state
  last_saved_bucket=
  deferred_overwrite_path=
  sleep "$BOOK_TITLE_AUTOSTART_DELAY_SECONDS"
  if book_title_should_preplay_direct_start "$match_reason"; then
    allow_memscan_root=$(book_title_preplay_allow_memscan_root "$match_reason")
    if book_title_direct_start_saved_track "$pid" "$track_list_ptr" "$catalog_scan_ptr" "$allow_memscan_root"; then
      return 0
    fi
  else
    log "book-title direct-start skipped reason=$match_reason track_list_ptr=$track_list_ptr catalog_scan_ptr=$catalog_scan_ptr"
  fi
  if [ "$match_reason" = launcher ] && audiobook_title_list_visible; then
    log "book-title touch-first skipped reason=launcher-title-list track_list_ptr=$track_list_ptr catalog_scan_ptr=$catalog_scan_ptr"
    return 0
  fi
  touch_first_track || return 1
}

main() {
  mkdir -p "$BASE_DIR" "$STORE_DIR"
  case "$INTERVAL_SECONDS" in ''|*[!0-9]*|0) INTERVAL_SECONDS=1 ;; esac
  case "$IDLE_INTERVAL_SECONDS" in ''|*[!0-9]*|0) IDLE_INTERVAL_SECONDS=3 ;; esac
  case "$BOOK_TITLE_MARKER_IDLE_POLL_SECONDS" in ''|*[!0-9]*) BOOK_TITLE_MARKER_IDLE_POLL_SECONDS=5 ;; esac
  case "$BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS" in ''|*[!0-9]*) BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS=15 ;; esac
  case "$DIAGNOSTICS_INTERVAL_SECONDS" in ''|*[!0-9]*) DIAGNOSTICS_INTERVAL_SECONDS=60 ;; esac
  case "$BACK_GUARD_ENABLED" in 1) ;; *) BACK_GUARD_ENABLED=0 ;; esac
  case "$BACK_GUARD_WINDOW_SECONDS" in ''|*[!0-9]*|0) BACK_GUARD_WINDOW_SECONDS=60 ;; esac
  case "$BACK_GUARD_AFTER_SCREEN_SECONDS" in ''|*[!0-9]*|0) BACK_GUARD_AFTER_SCREEN_SECONDS=8 ;; esac
  case "$BACK_GUARD_IDLE_INTERVAL_SECONDS" in ''|*[!0-9]*|0) BACK_GUARD_IDLE_INTERVAL_SECONDS=1 ;; esac
  case "$BACK_GUARD_SETTLE_SECONDS" in ''|*[!0-9]*) BACK_GUARD_SETTLE_SECONDS=1 ;; esac
  case "$BACK_GUARD_EXTRA_BACKS" in ''|*[!0-9]*|0) BACK_GUARD_EXTRA_BACKS=2 ;; esac
  case "$BACK_GUARD_SUBHEADER_MIN_WHITE" in ''|*[!0-9]*|0) BACK_GUARD_SUBHEADER_MIN_WHITE=100 ;; esac
  case "$BACK_GUARD_SUBHEADER_MAX_WHITE" in ''|*[!0-9]*) BACK_GUARD_SUBHEADER_MAX_WHITE=50 ;; esac
  case "$BACK_GUARD_HEADER_MIN_WHITE" in ''|*[!0-9]*|0) BACK_GUARD_HEADER_MIN_WHITE=300 ;; esac
  case "$BACK_GUARD_BACK_ARROW_MIN_WHITE" in ''|*[!0-9]*|0) BACK_GUARD_BACK_ARROW_MIN_WHITE=80 ;; esac
  close_inherited_socket_fds
  refresh_catalog_album_patterns
  echo "$$" >"$PID_FILE"
  log "start interval=${INTERVAL_SECONDS}s idle_interval=${IDLE_INTERVAL_SECONDS}s marker_idle_poll=${BOOK_TITLE_MARKER_IDLE_POLL_SECONDS}s marker_music_poll=${BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS}s diagnostics_interval=${DIAGNOSTICS_INTERVAL_SECONDS}s min_save_ms=$MIN_SAVE_MS save_bucket_ms=$SAVE_BUCKET_MS new_track_commit_ms=$NEW_TRACK_COMMIT_MS backward_save_guard_ms=$BACKWARD_SAVE_GUARD_MS restore_rewind_ms=$RESTORE_REWIND_MS closed_inherited_socket_fds=$CLOSED_INHERITED_SOCKET_FDS position_source=$POSITION_SOURCE duration_addr=$PLAYER_DURATION_ADDR restore_enabled=$RESTORE_ENABLED track_restore_enabled=$TRACK_RESTORE_ENABLED track_key_fallback=$TRACK_RESTORE_KEY_FALLBACK_ENABLED ui_seek_fallback=$UI_SEEK_FALLBACK_ENABLED ui_seek_screen_guard=$UI_SEEK_SCREEN_GUARD_ENABLED ui_seek_touch_frames=$UI_SEEK_TOUCH_FRAMES play_mode_enforce=$PLAY_MODE_ENFORCE_ENABLED play_mode_target=$PLAY_MODE_TARGET play_mode_offset=$PLAY_MODE_USER_INI_OFFSET back_guard=$BACK_GUARD_ENABLED back_guard_window=$BACK_GUARD_WINDOW_SECONDS back_guard_idle_interval=$BACK_GUARD_IDLE_INTERVAL_SECONDS book_title_autostart=$BOOK_TITLE_AUTOSTART_ENABLED book_title_direct_track_select=$BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED book_title_direct_track_preplay=$BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED book_title_memscan=$BOOK_TITLE_MEMSCAN_ENABLED book_title_direct_calibrate=$BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED book_title_recovery_transport=$BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED book_title_recovery_max=$BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS book_title_direct_open=$BOOK_TITLE_DIRECT_OPEN_ENABLED direct_open_probe=$DIRECT_OPEN_PROBE_ADDR direct_open_scratch=$DIRECT_OPEN_SCRATCH_ADDR book_title_require_path=$BOOK_TITLE_AUTOSTART_REQUIRE_PATH book_title_context_seconds=$BOOK_TITLE_CONTEXT_SECONDS catalog_albums=$CATALOG_ALBUM_PATTERNS catalog_books=$CATALOG_BOOKS"

  last_path=
  last_saved_bucket=
  restored_path=
  completed_saved_path=
  clear_restore_failure_state
  helper_failures=0
  deferred_overwrite_path=
  completed_start_over_path=
  last_book_title_seq=$(book_title_marker_seq 2>/dev/null || true)
  book_title_context_until=0
  book_title_autostart_until=0
  book_title_autostart_seq=0
  book_title_autostart_reset_key=
  book_title_restore_wait_log_key=
  book_title_pre_restore_log_key=
  last_book_title_marker_poll_at=0
  audiobook_back_guard_until=0
  audiobook_back_guard_seen_at=0
  audiobook_back_guard_last_fire_at=0
  diag_last_log_at=0
  diag_loops=0
  diag_audiobook_loops=0
  diag_non_audiobook_loops=0
  diag_path_previews=0
  diag_marker_polls=0
  diag_marker_skips=0
  diag_position_reads=0
  diag_saves=0

  while :; do
    now_loop=$(date +%s 2>/dev/null || echo 0)
    diag_inc diag_loops
    loop_sleep=$IDLE_INTERVAL_SECONDS
    diag_maybe_log "$now_loop"
    path_preview=$(current_path_slot_preview)
    diag_inc diag_path_previews

    maybe_audiobook_back_guard "$now_loop" "$path_preview"

    if should_poll_book_title_marker "$path_preview" "$now_loop"; then
      diag_inc diag_marker_polls
      last_book_title_marker_poll_at=$now_loop
      book_title_seq=$(book_title_marker_seq 2>/dev/null || true)
      if [ -n "$book_title_seq" ] && [ "$book_title_seq" != "$last_book_title_seq" ]; then
        loop_sleep=$INTERVAL_SECONDS
        maybe_autostart_book_title "$book_title_seq" || log "book-title autostart failed seq=$book_title_seq"
        last_book_title_seq=$book_title_seq
        path_preview=$(current_path_slot_preview)
        diag_inc diag_path_previews
      fi
    else
      diag_inc diag_marker_skips
    fi
    if [ "$BACK_GUARD_ENABLED" = 1 ] &&
       ! path_preview_is_music "$path_preview" &&
       ! path_preview_is_audiobook "$path_preview" &&
       [ "$BACK_GUARD_IDLE_INTERVAL_SECONDS" -lt "$loop_sleep" ] 2>/dev/null; then
      loop_sleep=$BACK_GUARD_IDLE_INTERVAL_SECONDS
    fi

    if path_preview_is_audiobook "$path_preview"; then
      path=$(current_path)
    else
      path=
    fi
    case "$path" in
      [aA]:\\Audiobooks\\*)
        diag_inc diag_audiobook_loops
        loop_sleep=$INTERVAL_SECONDS
        now=$(date +%s 2>/dev/null || echo 0)
        if [ "$BOOK_TITLE_CONTEXT_SECONDS" -gt 0 ] 2>/dev/null; then
          book_title_context_until=$((now + BOOK_TITLE_CONTEXT_SECONDS))
        fi
        position_rc=0
        diag_inc diag_position_reads
        pos=$(position_ms) || position_rc=$?
        if [ "$position_rc" -ne 0 ]; then
          if [ "$position_rc" -ne 124 ]; then
            log "position helper unavailable rc=$position_rc; backing off"
            sleep "$HELPER_FAILURE_BACKOFF_SECONDS"
            last_path=$path
            continue
          fi
          helper_failures=$((helper_failures + 1))
          log "position helper timed out rc=$position_rc count=$helper_failures"
          if [ "$helper_failures" -ge "$HELPER_MAX_CONSECUTIVE_FAILURES" ]; then
            log "too many helper failures; stopping daemon"
            rm -f "$PID_FILE"
            exit 1
          fi
          sleep "$HELPER_FAILURE_BACKOFF_SECONDS"
          last_path=$path
          continue
        fi
        helper_failures=0
        case "$pos" in ''|*[!0-9]*) pos=0 ;; esac
        autostart_restore_active=0
        now_s=$(date '+%s')
        case "$book_title_autostart_until:$now_s" in
          *[!0-9:]* | :* | *:) autostart_restore_active=0 ;;
          *)
            [ "$book_title_autostart_until" -gt "$now_s" ] && autostart_restore_active=1
            ;;
        esac
        if [ "$autostart_restore_active" = 1 ]; then
          reset_key="$book_title_autostart_seq"
          if [ "$book_title_autostart_reset_key" != "$reset_key" ]; then
            log "book-title restore reset seq=$book_title_autostart_seq path=$path pos_ms=$pos"
            restored_path=
            completed_saved_path=
            clear_restore_failure_state
            last_saved_bucket=
            deferred_overwrite_path=
            book_title_autostart_reset_key=$reset_key
          fi
        fi

        if [ "$path" != "$last_path" ]; then
          track_index=$(catalog_field_for_path 2 "$path" || true)
          track_count=$(catalog_field_for_path 3 "$path" || true)
          log "audiobook path=$path pos_ms=$pos track=${track_index:-?}/${track_count:-?}"
          ensure_audiobook_play_mode || true
          restored_path=
          clear_restore_failure_state
          last_saved_bucket=
          deferred_overwrite_path=
        fi

        root=$(book_root_for_path "$path")
        record=$(existing_record_for_path "$path")
        if [ -f "$record" ] && [ "$(json_bool completed "$record")" = true ]; then
          if [ "$restored_path" != "$path" ]; then
            log "completed book start-over root=$root path=$path pos_ms=$pos"
          fi
          restored_path=$path
          completed_saved_path=
          clear_restore_failure_state
          clear_book_title_autostart
          last_saved_bucket=
          deferred_overwrite_path=
          completed_start_over_path=$path
        fi

        if [ "$restored_path" != "$path" ]; then
          if should_attempt_restore_for_position "$pos" "$autostart_restore_active"; then
            if [ "$autostart_restore_active" = 1 ] && [ "$pos" -gt "$RESTORE_ONLY_BEFORE_MS" ]; then
              log_book_title_restore_wait "$path" "$pos"
              last_path=$path
              sleep "$INTERVAL_SECONDS"
              continue
            fi
            should_try_restore=1
            if [ "$restore_failed_path" = "$path" ]; then
              retry_delay=$RESTORE_RETRY_AFTER_FAILURE_SECONDS
              if [ "$restore_failed_kind" = seek ]; then
                retry_delay=$(restore_retry_delay_seconds)
              fi
              retry_age=$((now_s - restore_failed_at))
              if [ "$retry_age" -lt "$retry_delay" ]; then
                should_try_restore=0
              fi
            fi
            if [ "$should_try_restore" -eq 1 ]; then
              track_restore_changed_path=0
              if should_skip_after_completed_restore "$path" "$pos" "$completed_saved_path"; then
                restored_path=$path
                clear_restore_failure_state
                clear_book_title_autostart
              elif maybe_restore_track "$path" "$pos"; then
                path_after_track_restore=$(current_path)
                if [ "$path_after_track_restore" != "$path" ]; then
                  path=$path_after_track_restore
                  track_restore_changed_path=1
                  diag_inc diag_position_reads
                  pos=$(position_ms) || pos=0
                  case "$pos" in ''|*[!0-9]*) pos=0 ;; esac
                  track_index=$(catalog_field_for_path 2 "$path" || true)
                  track_count=$(catalog_field_for_path 3 "$path" || true)
                  log "audiobook path=$path pos_ms=$pos track=${track_index:-?}/${track_count:-?}"
                fi
              else
                restored_path=
                note_track_restore_failure "$path" "$now_s"
                last_path=$path
                sleep "$INTERVAL_SECONDS"
                continue
              fi

              if [ "$track_restore_changed_path" = 1 ]; then
                log "restore settle after track restore path=$path pos_ms=$pos"
                last_path=$path
                sleep "$INTERVAL_SECONDS"
                continue
              fi

              if [ "$restored_path" = "$path" ] && [ "$completed_saved_path" != "$path" ]; then
                :
              elif maybe_restore "$path" "$pos"; then
                restored_path=$path
                completed_saved_path=$path
                clear_restore_failure_state
                clear_book_title_autostart
              else
                restored_path=
              fi
            fi
            diag_inc diag_position_reads
            pos=$(position_ms) || pos=0
            case "$pos" in ''|*[!0-9]*) pos=0 ;; esac
          fi
        fi

        if [ "$pos" -ge "$MIN_SAVE_MS" ]; then
          bucket=$((pos / SAVE_BUCKET_MS))
          if [ "$autostart_restore_active" = 1 ] && [ "$restored_path" != "$path" ] && [ "$pos" -le "$RESTORE_ONLY_BEFORE_MS" ]; then
            log_book_title_pre_restore_skip "$path" "$pos"
          elif should_skip_failed_restore_save "$path" "$pos"; then
            :
          elif [ "$completed_start_over_path" = "$path" ]; then
            save_position "$path" "$pos"
            diag_inc diag_saves
            last_saved_bucket=$bucket
            completed_start_over_path=
            deferred_overwrite_path=
          elif should_defer_new_track_save "$path" "$pos"; then
            if [ "$deferred_overwrite_path" != "$path" ]; then
              log "defer new-track save path=$path pos_ms=$pos"
              deferred_overwrite_path=$path
            fi
          elif [ "$bucket" != "$last_saved_bucket" ] || [ "$path" != "$last_path" ]; then
            save_position "$path" "$pos"
            diag_inc diag_saves
            last_saved_bucket=$bucket
            deferred_overwrite_path=
          fi
        fi
        ;;
      *)
        diag_inc diag_non_audiobook_loops
        if [ -n "$last_path" ]; then
          log "leave audiobook current=non-audiobook"
        fi
        restored_path=
        completed_saved_path=
        clear_restore_failure_state
        helper_failures=0
        last_saved_bucket=
        deferred_overwrite_path=
        completed_start_over_path=
        last_path=
        sleep "$loop_sleep"
        continue
        ;;
    esac

    last_path=$path
    sleep "$loop_sleep"
  done
}

if [ "${AUDIOBOOK_RESUME_DAEMON_SOURCE_ONLY:-0}" != 1 ]; then
  main "$@"
fi
