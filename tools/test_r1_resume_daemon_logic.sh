#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DAEMON="$SCRIPT_DIR/r1_audiobook_resume_daemon.sh"

failures=0

ok() {
  printf 'OK   %s\n' "$1"
}

fail() {
  printf 'FAIL %s\n' "$1" >&2
  failures=$((failures + 1))
}

assert_eq() {
  test_label=$1
  expected=$2
  actual=$3
  if [ "$expected" = "$actual" ]; then
    ok "$test_label"
  else
    fail "$test_label expected=[$expected] actual=[$actual]"
  fi
}

assert_true() {
  test_label=$1
  shift
  if "$@"; then
    ok "$test_label"
  else
    fail "$test_label"
  fi
}

assert_false() {
  test_label=$1
  shift
  if "$@"; then
    fail "$test_label"
  else
    ok "$test_label"
  fi
}

AUDIOBOOK_RESUME_DAEMON_SOURCE_ONLY=1
. "$DAEMON"

log() {
  :
}

audio_hex='61003a005c0041007500640069006f0062006f006f006b0073005c0042006f006f006b005c00300031002e006d00700033000000'
audio_upper_hex='41003a005c0041007500640069006f0062006f006f006b0073005c0042006f006f006b005c00300031002e006d00700033000000'
audio_rootless_hex='5c0041007500640069006f0062006f006f006b0073005c0042006f006f006b005c00300031002e006d00700033000000'
music_hex='61003a005c004d0075007300690063005c004100720074006900730074005c00300031002e006d00700033000000'

assert_true "path slot recognizes audiobook lowercase drive" path_slot_hex_is_audiobook "$audio_hex"
assert_true "path slot recognizes audiobook uppercase drive" path_slot_hex_is_audiobook "$audio_upper_hex"
assert_true "path slot recognizes rootless audiobook path" path_slot_hex_is_audiobook "$audio_rootless_hex"
assert_false "path slot ignores music path" path_slot_hex_is_audiobook "$music_hex"
assert_true "path preview recognizes audiobook path" path_preview_is_audiobook 'a:\Audiobooks\Book\01.mp3'
assert_true "path preview recognizes rootless audiobook path" path_preview_is_audiobook '\Audiobooks\Book\01.mp3'
assert_false "path preview ignores music path" path_preview_is_audiobook 'a:\Music\Artist\01.mp3'
assert_true "path preview recognizes music path" path_preview_is_music 'a:\Music\Artist\01.mp3'
assert_false "path preview music ignores audiobook path" path_preview_is_music 'a:\Audiobooks\Book\01.mp3'

BOOK_TITLE_AUTOSTART_ENABLED=1
BOOK_TITLE_CONTEXT_SECONDS=300
BOOK_TITLE_MARKER_IDLE_POLL_SECONDS=5
BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS=15
book_title_context_until=0
last_book_title_marker_poll_at=0
assert_true "marker poll runs immediately for music after start" should_poll_book_title_marker 'a:\Music\Artist\01.mp3' 100
last_book_title_marker_poll_at=100
assert_false "marker poll throttles while music is active" should_poll_book_title_marker 'a:\Music\Artist\01.mp3' 109
assert_true "marker poll resumes after music throttle interval" should_poll_book_title_marker 'a:\Music\Artist\01.mp3' 115
last_book_title_marker_poll_at=100
assert_true "marker poll remains fast for audiobook context" should_poll_book_title_marker 'a:\Audiobooks\Book\01.mp3' 101
book_title_context_until=200
assert_true "marker poll remains fast during recent audiobook context" should_poll_book_title_marker '' 101
assert_false "marker poll throttles music even during recent audiobook context" should_poll_book_title_marker 'a:\Music\Artist\01.mp3' 109
book_title_context_until=0
BOOK_TITLE_AUTOSTART_ENABLED=0
assert_false "marker poll obeys autostart disable switch" should_poll_book_title_marker 'a:\Audiobooks\Book\01.mp3' 101
BOOK_TITLE_AUTOSTART_ENABLED=1

CURRENT_PATH_HEX_CACHE=
CURRENT_PATH_VALUE_CACHE=
assert_eq "path slot decodes audiobook path" 'a:\Audiobooks\Book\01.mp3' "$(current_path_from_hex "$audio_hex")"
assert_eq "path slot normalizes rootless audiobook path" 'a:\Audiobooks\Book\01.mp3' "$(current_path_from_hex "$audio_rootless_hex")"

sleep_track_switch_poll() {
  :
}

BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS=5
BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE=4
BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES=20
assert_eq "geometry first row" "0 1" "$(direct_track_geometry 1)"
assert_eq "geometry last visible row" "0 5" "$(direct_track_geometry 5)"
assert_eq "geometry first scrolled row" "1 2" "$(direct_track_geometry 6)"
assert_eq "geometry later visible row after scroll" "1 5" "$(direct_track_geometry 9)"

BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS=9
assert_eq "geometry clamps visible rows" "1 2" "$(direct_track_geometry 6)"

BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS=5
BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES=bad
assert_eq "geometry sanitizes bad max swipes" "1 5" "$(direct_track_geometry 9)"

BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES=0
assert_false "geometry rejects over max swipes" direct_track_geometry 9

BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES=20
BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED=1
BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED=1
BOOK_TITLE_DIRECT_TRACK_RETURN_DELAY_SECONDS=0
BOOK_TITLE_DIRECT_TRACK_SWIPE_SETTLE_SECONDS=0
TRACK_SWITCH_SETTLE_SECONDS=1
TRACK_SWITCH_POLL_US=1000000
book_title_autostart_until=$(( $(date +%s) + 60 ))
saved_path='a:\Audiobooks\Author\Book\09.mp3'
selected=0
swipe_taps=0
row_tapped=0
back_taps=0

touch_back_to_track_list() {
  back_taps=$((back_taps + 1))
  return 0
}

touch_track_swipe_up() {
  swipe_taps=$((swipe_taps + 1))
  return 0
}

touch_track_row() {
  row_tapped=$1
  selected=1
  return 0
}

current_path() {
  if [ "$selected" = 1 ]; then
    printf '%s\n' "$saved_path"
  else
    printf '%s\n' 'a:\Audiobooks\Author\Book\01.mp3'
  fi
}

book_root_for_path() {
  printf '%s\n' 'a:\Audiobooks\Author\Book'
}

catalog_field_for_path() {
  case "$1" in
    2) printf '%s\n' 9 ;;
    3) printf '%s\n' 12 ;;
    *) printf '\n' ;;
  esac
}

assert_true "direct track select reaches saved path" book_title_direct_track_select 1 9 "$saved_path"
assert_eq "direct track select returns to list once" "1" "$back_taps"
assert_eq "direct track select swipes once" "1" "$swipe_taps"
assert_eq "direct track select taps row five" "5" "$row_tapped"

saved_path='a:\Audiobooks\Author\Book\15.mp3'
near_path='a:\Audiobooks\Author\Book\14.mp3'
selected_path='a:\Audiobooks\Author\Book\01.mp3'
selected=0
retry_phase=0
swipe_taps=0
row_taps=
back_taps=0

touch_back_to_track_list() {
  back_taps=$((back_taps + 1))
  return 0
}

touch_track_swipe_up() {
  swipe_taps=$((swipe_taps + 1))
  return 0
}

touch_track_row() {
  row_taps="${row_taps}${1},"
  if [ "$retry_phase" = 0 ]; then
    selected_path=$near_path
    retry_phase=1
  else
    case "$1" in
      4) selected_path=$saved_path ;;
      *) selected_path=$near_path ;;
    esac
  fi
  selected=1
  return 0
}

current_path() {
  if [ "$selected" = 1 ]; then
    printf '%s\n' "$selected_path"
  else
    printf '%s\n' 'a:\Audiobooks\Author\Book\01.mp3'
  fi
}

book_root_for_path() {
  printf '%s\n' 'a:\Audiobooks\Author\Book'
}

catalog_field_for_path() {
  case "$2" in
    "$saved_path") printf '%s\n' 15 ;;
    "$near_path") printf '%s\n' 14 ;;
    *) printf '%s\n' 1 ;;
  esac
}

assert_true "direct track select retries adjacent row after near miss" book_title_direct_track_select 1 15 "$saved_path"
assert_eq "direct track select near miss backs to list twice" "2" "$back_taps"
assert_eq "direct track select near miss taps original and retry rows" "3,4," "$row_taps"

saved_path='a:\Audiobooks\Author\Book\15.mp3'
near_path='a:\Audiobooks\Author\Book\14.mp3'
selected_path='a:\Audiobooks\Author\Book\01.mp3'
selected=0
retry_phase=0
swipe_taps=0
row_taps=
back_taps=0

touch_track_row() {
  row_taps="${row_taps}${1},"
  selected_path=$near_path
  selected=1
  return 0
}

set +e
book_title_direct_track_select 1 15 "$saved_path"
status=$?
set -e
assert_eq "direct track select blocks key fallback after unresolved near miss" "2" "$status"
assert_eq "unresolved near miss still avoids extra key fallback rows only" "3,4," "$row_taps"

saved_path='a:\Audiobooks\Author\Book\15.mp3'
selected_path='a:\Audiobooks\Author\Book\12.mp3'
selected=0
row_tapped=0
back_taps=0

touch_back_to_track_list() {
  back_taps=$((back_taps + 1))
  return 0
}

touch_track_row() {
  row_tapped=$1
  selected_path=$saved_path
  selected=1
  return 0
}

current_path() {
  if [ "$selected" = 1 ]; then
    printf '%s\n' "$selected_path"
  else
    printf '%s\n' 'a:\Audiobooks\Author\Book\12.mp3'
  fi
}

book_root_for_path() {
  printf '%s\n' 'a:\Audiobooks\Author\Book'
}

catalog_field_for_path() {
  case "$2" in
    "$saved_path") printf '%s\n' 15 ;;
    *) printf '%s\n' 12 ;;
  esac
}

assert_true "visible track select reaches saved forward row" book_title_visible_track_select 12 15 "$saved_path"
assert_eq "visible track select backs to list once" "1" "$back_taps"
assert_eq "visible track select taps saved visible row" "4" "$row_tapped"

saved_path='a:\Audiobooks\Author\Book\15.mp3'
near_path='a:\Audiobooks\Author\Book\14.mp3'
selected_path='a:\Audiobooks\Author\Book\12.mp3'
selected=0
row_tapped=0
back_taps=0

touch_track_row() {
  row_tapped=$1
  selected_path=$near_path
  selected=1
  return 0
}

set +e
book_title_visible_track_select 12 15 "$saved_path"
status=$?
set -e
assert_eq "visible track select blocks key fallback after near miss" "2" "$status"
assert_eq "visible track select near miss still taps expected row" "4" "$row_tapped"

TRACK_RESTORE_NEAR_MISS_TRANSPORT_ENABLED=1
TRACK_RESTORE_NEAR_MISS_MAX_STEPS=4
BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED=1
BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS=20
PLAY_MODE_TARGET=3
selected_index_num=16
transport_next_calls=0
transport_prev_calls=0
row_taps=

path_for_index() {
  printf 'a:\\Audiobooks\\Author\\Book\\%02d.mp3\n' "$1"
}

saved_path=$(path_for_index 16)

touch_track_row() {
  row_taps="${row_taps}${1},"
  selected_index_num=24
  return 0
}

current_path() {
  path_for_index "$selected_index_num"
}

book_root_for_path() {
  printf '%s\n' 'a:\Audiobooks\Author\Book'
}

catalog_field_for_path() {
  case "$1" in
    2)
      path_leaf=${2##*\\}
      printf '%s\n' "${path_leaf%.mp3}"
      ;;
    *) printf '\n' ;;
  esac
}

play_mode_value() {
  printf '%s\n' 3
}

track_next() {
  transport_next_calls=$((transport_next_calls + 1))
  selected_index_num=$((selected_index_num + 1))
  return 0
}

track_prev() {
  transport_prev_calls=$((transport_prev_calls + 1))
  selected_index_num=$((selected_index_num - 1))
  return 0
}

touch_track_row 1
assert_true "direct-start verifier recovers delayed row-tap overshoot" book_title_verify_selected_track "$saved_path" 16 1 "test direct-start verifier" "$saved_path"
assert_eq "direct-start verifier uses expanded recovery limit" "0:8:16" "$transport_next_calls:$transport_prev_calls:$selected_index_num"
assert_eq "direct-start verifier only tapped probe row" "1," "$row_taps"

BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED=0
selected=0
swipe_taps=0
row_tapped=0
back_taps=0
assert_false "direct track select obeys disable switch" book_title_direct_track_select 1 9 "$saved_path"
assert_eq "disabled direct track select has no touches" "0:0:0" "$back_taps:$swipe_taps:$row_tapped"

RESTORE_ENABLED=1
BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED=0
BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED=1
pid_mem_first_catalog_path() {
  fail "preplay direct start scanned memory while disabled"
  return 1
}
assert_false "preplay direct start obeys select disable switch" book_title_direct_start_saved_track 123 456 789

RESTORE_REWIND_MS=0
assert_eq "restore target exact by default" "270200" "$(restore_target_ms 270200)"
RESTORE_REWIND_MS=5000
assert_eq "restore target applies smart rewind" "265200" "$(restore_target_ms 270200)"
RESTORE_REWIND_MS=999999
assert_eq "restore target clamps before zero" "0" "$(restore_target_ms 270200)"
RESTORE_REWIND_MS=bad
assert_eq "restore target ignores bad rewind setting" "270200" "$(restore_target_ms 270200)"
RESTORE_REWIND_MS=0

RESTORE_ENABLED=1
TRACK_RESTORE_ENABLED=1
TRACK_RESTORE_KEY_FALLBACK_ENABLED=0
RESTORE_MIN_MS=10000
RESTORE_ONLY_BEFORE_MS=15000
book_title_autostart_until=$(( $(date +%s) + 60 ))
autostart_restore_active=1
selected_path='a:\Audiobooks\Author\Book\01.mp3'
near_path='a:\Audiobooks\Author\Book\14.mp3'
saved_path='a:\Audiobooks\Author\Book\15.mp3'
test_record=$(mktemp)
visible_after_direct_file=$(mktemp)
direct_near_miss_file=$(mktemp)
: >"$test_record"

existing_record_for_path() {
  printf '%s\n' "$test_record"
}

record_saved_path_for_current() {
  printf '%s\n' "$saved_path"
}

same_book_root() {
  return 0
}

json_bool() {
  printf '%s\n' false
}

json_number() {
  case "$1" in
    position_ms) printf '%s\n' 270200 ;;
    *) printf '\n' ;;
  esac
}

current_path() {
  printf '%s\n' "$selected_path"
}

catalog_field_for_path() {
  case "$2" in
    "$saved_path") printf '%s\n' 15 ;;
    "$near_path") printf '%s\n' 14 ;;
    *) printf '%s\n' 1 ;;
  esac
}

book_title_direct_track_select() {
  printf '%s\n' called >"$direct_near_miss_file"
  selected_path=$near_path
  return 2
}

book_title_visible_track_select() {
  printf '%s\n' "$1:$2:$3" >"$visible_after_direct_file"
  selected_path=$saved_path
  return 0
}

assert_true "track restore recovers direct near miss through visible rows" maybe_restore_track 'a:\Audiobooks\Author\Book\01.mp3' 1021
direct_near_miss_called=$(cat "$direct_near_miss_file" 2>/dev/null || true)
assert_eq "track restore reaches direct near-miss selector" "called" "$direct_near_miss_called"
visible_after_direct_args=$(cat "$visible_after_direct_file" 2>/dev/null || true)
assert_eq "visible fallback receives actual near-miss index" "14:15:$saved_path" "$visible_after_direct_args"
rm -f "$test_record" "$visible_after_direct_file" "$direct_near_miss_file"

RESTORE_ENABLED=1
TRACK_RESTORE_ENABLED=1
TRACK_RESTORE_KEY_FALLBACK_ENABLED=0
TRACK_RESTORE_NEAR_MISS_TRANSPORT_ENABLED=1
TRACK_RESTORE_NEAR_MISS_MAX_STEPS=4
PLAY_MODE_TARGET=3
RESTORE_MIN_MS=10000
RESTORE_ONLY_BEFORE_MS=15000
book_title_autostart_until=$(( $(date +%s) + 60 ))
autostart_restore_active=1
test_record=$(mktemp)
: >"$test_record"
start_path='a:\Audiobooks\Author\Book\01.mp3'
near_path='a:\Audiobooks\Author\Book\13.mp3'
mid_path='a:\Audiobooks\Author\Book\14.mp3'
saved_path='a:\Audiobooks\Author\Book\15.mp3'
selected_path=$start_path
transport_next_calls=0
transport_prev_calls=0

existing_record_for_path() {
  printf '%s\n' "$test_record"
}

record_saved_path_for_current() {
  printf '%s\n' "$saved_path"
}

book_root_for_path() {
  printf '%s\n' 'a:\Audiobooks\Author\Book'
}

same_book_root() {
  return 0
}

json_bool() {
  printf '%s\n' false
}

json_number() {
  case "$1" in
    position_ms) printf '%s\n' 270200 ;;
    *) printf '\n' ;;
  esac
}

current_path() {
  printf '%s\n' "$selected_path"
}

catalog_field_for_path() {
  case "$2" in
    "$start_path") printf '%s\n' 1 ;;
    "$near_path") printf '%s\n' 13 ;;
    "$mid_path") printf '%s\n' 14 ;;
    "$saved_path") printf '%s\n' 15 ;;
    *) printf '\n' ;;
  esac
}

book_title_direct_track_select() {
  selected_path=$mid_path
  return 2
}

book_title_visible_track_select() {
  selected_path=$near_path
  return 2
}

play_mode_value() {
  printf '%s\n' 3
}

track_next() {
  transport_next_calls=$((transport_next_calls + 1))
  case "$transport_next_calls" in
    1) selected_path=$mid_path ;;
    2) selected_path=$saved_path ;;
    *) selected_path=$saved_path ;;
  esac
  return 0
}

track_prev() {
  transport_prev_calls=$((transport_prev_calls + 1))
  return 1
}

assert_true "track restore uses near-miss transport fallback" maybe_restore_track "$start_path" 1021
assert_eq "near-miss transport advances to saved track" "$saved_path" "$selected_path"
assert_eq "near-miss transport uses expected next count" "2:0" "$transport_next_calls:$transport_prev_calls"
rm -f "$test_record"

RESTORE_ENABLED=1
TRACK_RESTORE_ENABLED=1
TRACK_RESTORE_KEY_FALLBACK_ENABLED=0
TRACK_RESTORE_NEAR_MISS_TRANSPORT_ENABLED=1
TRACK_RESTORE_NEAR_MISS_MAX_STEPS=4
PLAY_MODE_TARGET=3
RESTORE_MIN_MS=10000
RESTORE_ONLY_BEFORE_MS=15000
book_title_autostart_until=$(( $(date +%s) + 60 ))
autostart_restore_active=1
test_record=$(mktemp)
: >"$test_record"
start_path='a:\Audiobooks\Author\Book\01.mp3'
near_path='a:\Audiobooks\Author\Book\13.mp3'
mid_path='a:\Audiobooks\Author\Book\14.mp3'
saved_path='a:\Audiobooks\Author\Book\15.mp3'
selected_path=$start_path
transport_next_calls=0
transport_prev_calls=0

play_mode_value() {
  printf '%s\n' 1
}

track_next() {
  transport_next_calls=$((transport_next_calls + 1))
  return 1
}

track_prev() {
  transport_prev_calls=$((transport_prev_calls + 1))
  return 1
}

assert_false "near-miss transport refuses non-audiobook play mode" maybe_restore_track "$start_path" 1021
assert_eq "non-audiobook mode prevents transport taps" "0:0:$near_path" "$transport_next_calls:$transport_prev_calls:$selected_path"
rm -f "$test_record"

PLAY_MODE_ENFORCE_ENABLED=1
PLAY_MODE_TARGET=3
PLAY_MODE_MAX_TAPS=4
PLAY_MODE_TOUCH_X=49
PLAY_MODE_TOUCH_Y=730
PLAY_MODE_SETTLE_SECONDS=0
mode_state=0
play_mode_taps=0

play_mode_value() {
  printf '%s\n' "$mode_state"
}

play_mode_screen_ready() {
  return 0
}

touch_generated_tap() {
  label=$1
  x=$2
  y=$3
  if [ "$label:$x:$y" != "play-mode:49:730" ]; then
    fail "play mode guard used unexpected tap $label:$x:$y"
    return 1
  fi
  play_mode_taps=$((play_mode_taps + 1))
  case "$mode_state" in
    1) mode_state=2 ;;
    2) mode_state=0 ;;
    0) mode_state=3 ;;
    3) mode_state=1 ;;
    *) mode_state=9 ;;
  esac
  return 0
}

assert_true "play mode guard reaches sequential playback" ensure_audiobook_play_mode
assert_eq "play mode guard taps through cycle" "1:3" "$play_mode_taps:$mode_state"

mode_state=3
play_mode_taps=0
assert_true "play mode guard leaves sequential playback alone" ensure_audiobook_play_mode
assert_eq "sequential playback needs no taps" "0:3" "$play_mode_taps:$mode_state"

play_mode_screen_ready() {
  return 1
}

mode_state=2
play_mode_taps=0
assert_false "play mode guard blocks when screen not ready" ensure_audiobook_play_mode
assert_eq "screen guard prevents play mode tap" "0:2" "$play_mode_taps:$mode_state"

if [ "$failures" -ne 0 ]; then
  exit 1
fi
