#!/bin/sh
set -eu

APP="/usr/bin/r1_audiobook_app"
if [ ! -x "$APP" ]; then
  echo "r1_audiobook_app is missing: $APP" >&2
  exit 1
fi

# Note: Do NOT kill -STOP hiby_player here. hiby_player is the process
# that called system() to launch this script. Stopping it would hang
# the system() call and kill our app. Instead, our app takes over the
# framebuffer by writing directly to /dev/fb0, which overwrites
# hiby_player's output. hiby_player is mostly idle on the launcher
# screen so framebuffer contention is minimal.

attempt=0
max_attempts=${AUDIOBOOK_LAUNCH_MAX_RESTARTS:-3}
case "$max_attempts" in ''|*[!0-9]*|0) max_attempts=3 ;; esac

EXIT_CODE=1
while [ "$attempt" -lt "$max_attempts" ]; do
  "$APP" "$@" && EXIT_CODE=0 && break
  attempt=$((attempt + 1))
  sleep 1
done

if [ "$EXIT_CODE" -ne 0 ]; then
  echo "r1_audiobook_app exited repeatedly after $max_attempts attempts" >&2
fi
exit $EXIT_CODE