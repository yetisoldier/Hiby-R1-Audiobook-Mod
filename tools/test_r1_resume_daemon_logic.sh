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

if [ "$failures" -ne 0 ]; then
  exit 1
fi
