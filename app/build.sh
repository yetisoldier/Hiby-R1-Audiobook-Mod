#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
APP_DIR="$ROOT/app"
OUT_DIR="$ROOT/build"
OUT_BIN="$OUT_DIR/r1_audiobook_app"
ZIG="${ZIG:-/home/yetisoldier/tools/zig/zig}"
CC="${CC:-$ZIG}"
SQLITE_VERSION="${SQLITE_VERSION:-3530200}"
SQLITE_DIR="$ROOT/.deps/sqlite/sqlite-amalgamation-$SQLITE_VERSION"
SQLITE_URL="https://sqlite.org/2026/sqlite-amalgamation-$SQLITE_VERSION.zip"
FAAD_DIR="$ROOT/.deps/faad2/faad2-master"
FAAD_URL="https://github.com/knik0/faad2/archive/refs/heads/master.zip"

ensure_sqlite() {
  if [ -f "$SQLITE_DIR/sqlite3.c" ]; then
    return 0
  fi
  mkdir -p "$ROOT/.deps/sqlite"
  python3 - "$SQLITE_URL" "$ROOT/.deps/sqlite/sqlite-amalgamation-$SQLITE_VERSION.zip" "$SQLITE_DIR" <<'PY'
import sys, urllib.request, zipfile, pathlib
url, zip_path, out_dir = sys.argv[1:4]
zip_path = pathlib.Path(zip_path)
out_dir = pathlib.Path(out_dir)
if not zip_path.exists():
    urllib.request.urlretrieve(url, zip_path)
with zipfile.ZipFile(zip_path) as zf:
    zf.extractall(out_dir.parent)
PY
}

ensure_faad2() {
  if [ -f "$FAAD_DIR/libfaad/decoder.c" ] && [ -f "$FAAD_DIR/frontend/mp4read.c" ]; then
    return 0
  fi
  mkdir -p "$ROOT/.deps/faad2"
  python3 - "$FAAD_URL" "$ROOT/.deps/faad2/faad2-master.zip" "$FAAD_DIR" <<'PY'
import sys, urllib.request, zipfile, pathlib, shutil
url, zip_path, out_dir = sys.argv[1:4]
zip_path = pathlib.Path(zip_path)
out_dir = pathlib.Path(out_dir)
if not zip_path.exists():
    urllib.request.urlretrieve(url, zip_path)
with zipfile.ZipFile(zip_path) as zf:
    zf.extractall(out_dir.parent)
src_root = out_dir.parent / "faad2-master"
if src_root != out_dir:
    if out_dir.exists():
        shutil.rmtree(out_dir)
    src_root.rename(out_dir)
PY
}

mkdir -p "$OUT_DIR"
ensure_sqlite
ensure_faad2

COMMON_FLAGS="-std=c11 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections -fno-strict-aliasing -D_FILE_OFFSET_BITS=64"
FAAD_WARN_FLAGS="-Wno-unused-parameter -Wno-missing-field-initializers"
DEFS="-D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -D_LARGEFILE64_SOURCE -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_JSON1 -DPACKAGE_VERSION=\"faad2-master\""
INCLUDES="-I$APP_DIR/src -I$APP_DIR/assets -I$SQLITE_DIR -I$FAAD_DIR/include -I$FAAD_DIR/libfaad -I$FAAD_DIR/frontend"
FAAD_SOURCES="$(find "$FAAD_DIR/libfaad" -maxdepth 1 -name '*.c' | sort | tr '\n' ' ') $FAAD_DIR/frontend/mp4read.c $FAAD_DIR/frontend/unicode_support.c"
SOURCES="
$SQLITE_DIR/sqlite3.c
$FAAD_SOURCES
$APP_DIR/src/common.c
$APP_DIR/src/alsa.c
$APP_DIR/src/decoder.c
$APP_DIR/src/m4b_decoder.c
$APP_DIR/src/main.c
$APP_DIR/src/ui.c
$APP_DIR/src/db.c
$APP_DIR/src/scanner.c
$APP_DIR/src/player.c
$APP_DIR/src/queue.c
$APP_DIR/src/resume.c
$APP_DIR/src/ipc.c
$APP_DIR/src/config.c
$APP_DIR/src/touch.c
$APP_DIR/src/fb.c
$APP_DIR/src/font.c
$APP_DIR/src/cover.c
"

exec "$CC" cc \
  -target mipsel-linux-musleabi \
  $COMMON_FLAGS \
  -static -s \
  $DEFS \
  $FAAD_WARN_FLAGS \
  $INCLUDES \
  $SOURCES \
  -ldl -lm -lpthread \
  -o "$OUT_BIN"
