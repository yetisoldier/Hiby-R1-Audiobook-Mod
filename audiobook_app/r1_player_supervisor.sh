#!/bin/sh
# r1_player_supervisor.sh — replaces stock /usr/bin/hiby_player.sh
#
# LD_PRELOAD in-process audiobook hook supervisor.
#
# Boot → supervisor launches stock hiby_player with LD_PRELOAD set to
# libaudiobook_hook.so. The hook .so installs in-process trampolines
# (Hook A: hgl_fb_display suppressor, Hook B: tile-cave trampoline →
# hook_b). When the user taps the Audiobooks tile, hook_b runs inside
# hiby_player's process, taking over the framebuffer while hiby_player
# stays alive (keeping fb0 + DMA active). No process killing, no fb DMA
# conflict, no touch-screen death.
#
# The supervisor only handles crashes: if hiby_player exits, count and
# reboot after MAX_CRASHES consecutive crashes.

PLAYER="/usr/bin/hiby_player"
HOOK_LIB="/usr/lib/libaudiobook_hook.so"
CRASH_COUNT=0
MAX_CRASHES=5

while true; do
    if [ -f "$HOOK_LIB" ]; then
        LD_PRELOAD="$HOOK_LIB" "$PLAYER" &
    else
        "$PLAYER" &
    fi
    HP_PID=$!
    wait "$HP_PID" 2>/dev/null
    CRASH_COUNT=$((CRASH_COUNT + 1))
    if [ "$CRASH_COUNT" -ge "$MAX_CRASHES" ]; then
        reboot
    fi
    sleep 1
done