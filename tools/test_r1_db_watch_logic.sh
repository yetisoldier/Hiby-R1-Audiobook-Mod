#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WATCHER="$REPO_ROOT/tools/r1_audiobook_db_watch.sh"

fail() {
  echo "FAIL  $*" >&2
  exit 1
}

ok() {
  echo "OK   $*"
}

assert_contains() {
  file=$1
  needle=$2
  label=$3
  if ! grep -Fq "$needle" "$file"; then
    echo "--- $file ---" >&2
    cat "$file" >&2 || true
    fail "$label missing: $needle"
  fi
  ok "$label"
}

assert_not_contains() {
  file=$1
  needle=$2
  label=$3
  if grep -Fq "$needle" "$file"; then
    echo "--- $file ---" >&2
    cat "$file" >&2 || true
    fail "$label unexpectedly found: $needle"
  fi
  ok "$label"
}

new_fixture() {
  tmp=$(mktemp -d)
  mkdir -p "$tmp/base" "$tmp/sd/Audiobooks/Author/Book" "$tmp/sd/Music"
  printf 'db\n' >"$tmp/primary.db"
  printf 'db\n' >"$tmp/mirror.db"
  printf 'audio\n' >"$tmp/sd/Audiobooks/Author/Book/01.mp3"
  printf '%s\n' "$tmp"
}

run_watcher_fixture() {
  tmp=$1
  timeout_seconds=$2
  set +e
  timeout "$timeout_seconds" env \
    AUDIOBOOK_BASE_DIR="$tmp/base" \
    AUDIOBOOK_DB_MAINT_HELPER="$tmp/helper.sh" \
    AUDIOBOOK_DB_PATH="$tmp/primary.db" \
    AUDIOBOOK_SD_ROOT="$tmp/sd" \
    AUDIOBOOK_DB_MIRROR_PATHS="$tmp/mirror.db" \
    AUDIOBOOK_DB_BOOT_DELAY_SECONDS=0 \
    AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS=4 \
    AUDIOBOOK_DB_STABLE_POLL_SECONDS=1 \
    AUDIOBOOK_DB_INTERVAL_SECONDS=60 \
    AUDIOBOOK_DB_STABLE_SECONDS=1 \
    AUDIOBOOK_DB_ZERO_AUDIO_RETRY_TIMEOUT_SECONDS=3 \
    AUDIOBOOK_DB_ZERO_AUDIO_RETRY_POLL_SECONDS=1 \
    AUDIOBOOK_DB_LOCKED_DB_RETRY_TIMEOUT_SECONDS=4 \
    AUDIOBOOK_DB_LOCKED_DB_RETRY_POLL_SECONDS=1 \
    sh "$WATCHER" >"$tmp/stdout.log" 2>"$tmp/stderr.log"
  rc=$?
  set -e
  case "$rc" in
    0|124|143) return 0 ;;
    *)
      cat "$tmp/stdout.log" >&2 || true
      cat "$tmp/stderr.log" >&2 || true
      fail "watcher exited unexpectedly with rc=$rc"
      ;;
  esac
}

test_zero_audio_defer_avoids_mirror_copy() {
  tmp=$(new_fixture)
  trap 'rm -rf "$tmp"' EXIT
  cat >"$tmp/helper.sh" <<'EOF'
#!/bin/sh
echo "audiobook tracks: 0"
exit 0
EOF
  chmod 755 "$tmp/helper.sh"

  run_watcher_fixture "$tmp" 8
  log="$tmp/base/db-maint.log"
  assert_contains "$log" "defer-zero-audiobooks" "zero-row result is deferred when audiobook files exist"
  assert_contains "$log" "mirror-skip reason=boot db=$tmp/mirror.db primary-transient rc=20" "transient zero-row primary is not mirrored"
  assert_contains "$log" "zero-audiobook-retry-still-empty" "zero-row retry continues instead of accepting empty catalog"
  assert_not_contains "$log" "mirror-copy reason=boot" "zero-row boot result was not copied to mirrors"
  rm -rf "$tmp"
  trap - EXIT
}

test_locked_db_retries_then_mirrors_success() {
  tmp=$(new_fixture)
  trap 'rm -rf "$tmp"' EXIT
  cat >"$tmp/helper.sh" <<'EOF'
#!/bin/sh
state_file="${AUDIOBOOK_BASE_DIR:-.}/helper-count"
count=$(cat "$state_file" 2>/dev/null || echo 0)
count=$((count + 1))
echo "$count" >"$state_file"
if [ "$count" -eq 1 ]; then
  echo "r1_audiobook_db_maint: prepare load existing audiobooks: database is locked" >&2
  exit 1
fi
echo "audiobook tracks: 1"
exit 0
EOF
  chmod 755 "$tmp/helper.sh"

  run_watcher_fixture "$tmp" 8
  log="$tmp/base/db-maint.log"
  assert_contains "$log" "failed-locked reason=boot role=primary" "locked helper failure is classified as transient"
  assert_contains "$log" "locked-db-retry-start reason=boot" "locked DB retry starts"
  assert_contains "$log" "done reason=boot-db-unlocked role=primary" "locked DB retry reaches successful primary repair"
  assert_contains "$log" "mirror-copy reason=boot-db-unlocked" "successful retry is mirrored"
  rm -rf "$tmp"
  trap - EXIT
}

test_zero_retry_survives_locked_retry() {
  tmp=$(new_fixture)
  trap 'rm -rf "$tmp"' EXIT
  cat >"$tmp/helper.sh" <<'EOF'
#!/bin/sh
state_file="${AUDIOBOOK_BASE_DIR:-.}/helper-count"
count=$(cat "$state_file" 2>/dev/null || echo 0)
count=$((count + 1))
echo "$count" >"$state_file"
if [ "$count" -eq 2 ]; then
  echo "r1_audiobook_db_maint: prepare load existing audiobooks: database is locked" >&2
  exit 1
fi
echo "audiobook tracks: 0"
exit 0
EOF
  chmod 755 "$tmp/helper.sh"

  run_watcher_fixture "$tmp" 8
  log="$tmp/base/db-maint.log"
  assert_contains "$log" "defer-zero-audiobooks" "zero-row result is deferred before nested lock"
  assert_contains "$log" "zero-audiobook-retry-locked" "zero-row retry keeps going after locked DB"
  assert_contains "$log" "zero-audiobook-retry-still-empty" "zero-row retry continues after nested lock clears"
  assert_not_contains "$log" "mirror-copy reason=boot" "nested zero/locked retry did not mirror empty result"
  rm -rf "$tmp"
  trap - EXIT
}

test_clean_sd_db_promotes_over_stale_primary() {
  tmp=$(new_fixture)
  trap 'rm -rf "$tmp"' EXIT
  printf 'stale-primary\n' >"$tmp/primary.db"
  printf 'clean-sd\n' >"$tmp/sd/usrlocal_media.db"
  cat >"$tmp/helper.sh" <<'EOF'
#!/bin/sh
db=
needs=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --db)
      db=$2
      shift 2
      ;;
    --needs-maintenance)
      needs=1
      shift
      ;;
    *)
      shift
      ;;
  esac
done
if [ "$needs" = 1 ]; then
  if grep -q stale "$db" 2>/dev/null; then
    echo "audiobook rows MEDIA_TABLE: 99"
    echo "audiobook files: 1"
    echo "audiobook files missing from DB: 1"
    echo "needs maintenance: yes"
    exit 10
  fi
  echo "audiobook rows MEDIA_TABLE: 1"
  echo "audiobook files: 1"
  echo "audiobook files missing from DB: 0"
  echo "needs maintenance: no"
  exit 0
fi
echo "audiobook tracks: 1"
exit 0
EOF
  chmod 755 "$tmp/helper.sh"

  set +e
  timeout 5 env \
    AUDIOBOOK_BASE_DIR="$tmp/base" \
    AUDIOBOOK_DB_MAINT_HELPER="$tmp/helper.sh" \
    AUDIOBOOK_DB_PATH="$tmp/primary.db" \
    AUDIOBOOK_SD_ROOT="$tmp/sd" \
    AUDIOBOOK_DB_MIRROR_PATHS="$tmp/mirror.db $tmp/sd/usrlocal_media.db" \
    AUDIOBOOK_DB_BOOT_DELAY_SECONDS=0 \
    AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS=1 \
    AUDIOBOOK_DB_STABLE_POLL_SECONDS=1 \
    AUDIOBOOK_DB_INTERVAL_SECONDS=60 \
    AUDIOBOOK_DB_STABLE_SECONDS=1 \
    sh "$WATCHER" >"$tmp/stdout.log" 2>"$tmp/stderr.log"
  rc=$?
  set -e
  case "$rc" in
    0|124|143) ;;
    *)
      cat "$tmp/stdout.log" >&2 || true
      cat "$tmp/stderr.log" >&2 || true
      fail "watcher exited unexpectedly with rc=$rc"
      ;;
  esac

  log="$tmp/base/db-maint.log"
  assert_contains "$log" "check-needs-maint role=primary-promote-check db=$tmp/primary.db" "stale primary is detected before promotion"
  assert_contains "$log" "primary-copy reason=boot source=$tmp/sd/usrlocal_media.db primary=$tmp/primary.db" "clean SD DB is promoted to primary"
  assert_contains "$log" "done reason=boot-promoted-sd role=primary" "promoted primary is maintained"
  assert_contains "$tmp/primary.db" "clean-sd" "primary DB now matches clean SD DB"
  assert_contains "$tmp/mirror.db" "clean-sd" "other mirror receives promoted DB"
  rm -rf "$tmp"
  trap - EXIT
}

test_zero_audio_defer_avoids_mirror_copy
test_locked_db_retries_then_mirrors_success
test_zero_retry_survives_locked_retry
test_clean_sd_db_promotes_over_stale_primary
