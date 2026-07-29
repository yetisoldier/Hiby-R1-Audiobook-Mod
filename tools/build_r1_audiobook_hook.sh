#!/bin/sh
# POSIX equivalent of tools/build_r1_audiobook_hook.ps1, for building the hook
# on macOS or Linux. Flags, sources and defines are kept deliberately identical
# to the PowerShell script — if you change one, change both.
#
#   ./tools/build_r1_audiobook_hook.sh [output.so]
#
# Requires `zig` on PATH (brew install zig). The PowerShell script pins Zig
# under .deps/zig on Windows; here we use whatever is installed, so check
# `zig version` if a build starts behaving oddly.
set -e

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-"$repo_root/build/libaudiobook_hook.so"}
mkdir -p "$(dirname "$out")"

command -v zig >/dev/null 2>&1 || {
    echo "error: zig not found on PATH (brew install zig)" >&2
    exit 1
}

# Zig ships the kernel headers the hook needs (linux/input.h and friends) under
# its bundled libc tree; locate it rather than assuming a layout.
zig_lib=$(zig env | sed -n 's/.*"lib_dir"[^"]*"\([^"]*\)".*/\1/p')
[ -n "$zig_lib" ] || zig_lib=$(zig env | grep -o '/[^"]*lib' | head -1)
kernel_include="$zig_lib/libc/include/any-linux-any"

set -- \
    audiobook_app/hook.c \
    audiobook_app/ui.c \
    audiobook_app/render.c \
    audiobook_app/font.c \
    audiobook_app/library.c \
    audiobook_app/scan.c \
    audiobook_app/tags.c \
    audiobook_app/mp4_audio.c \
    audiobook_app/cover.c \
    audiobook_app/pngdec.c \
    audiobook_app/bookmark_sd.c \
    audiobook_app/storage_guard.c \
    audiobook_app/player.c \
    audiobook_app/wsola.c \
    audiobook_app/sqlite3.c

cd "$repo_root"

# See the .ps1 for why each of these matters: glibc 2.22 hard-float to match
# hiby_player, hidden visibility so our SQLite does not shadow the player's,
# shared+PIC because this is LD_PRELOADed.
zig cc \
    -target mipsel-linux-gnueabihf.2.22 \
    -shared -fPIC -fvisibility=hidden -fno-common -Os -s \
    -I "$kernel_include" \
    -I vendor \
    -I vendor/libjpeg \
    -DSQLITE_THREADSAFE=2 \
    -DSQLITE_DEFAULT_MEMSTATUS=0 \
    -DSQLITE_OMIT_LOAD_EXTENSION=1 \
    -DSQLITE_ENABLE_FTS5=1 \
    -DSQLITE_OMIT_DEPRECATED=1 \
    -DSQLITE_TEMP_STORE=2 \
    "$@" \
    -lpthread -ldl -lm \
    -o "$out"

ls -la "$out"
