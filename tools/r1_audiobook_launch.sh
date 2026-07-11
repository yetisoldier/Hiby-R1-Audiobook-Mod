#!/bin/sh
set -eu

APP="/usr/bin/r1_audiobook_app"
if [ ! -x "$APP" ]; then
  echo "r1_audiobook_app is missing: $APP" >&2
  exit 1
fi

attempt=0
max_attempts=${AUDIOBOOK_LAUNCH_MAX_RESTARTS:-5}
case "$max_attempts" in ''|*[!0-9]*|0) max_attempts=5 ;; esac

while [ "$attempt" -lt "$max_attempts" ]; do
  "$APP" "$@" && exit 0
  attempt=$((attempt + 1))
  sleep 1
done

echo "r1_audiobook_app exited repeatedly after $max_attempts attempts" >&2
exit 1
