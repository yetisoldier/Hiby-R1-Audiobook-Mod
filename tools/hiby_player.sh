#!/bin/sh

killall    hiby_player    &>/dev/null
killall -9 hiby_player    &>/dev/null

if [ -f "/usr/bin/batd" ]; then
  killall    batd    &>/dev/null
  killall -9 batd    &>/dev/null
  /usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt &
fi

FLAG="/tmp/.r1_audiobook_launch"
APP="/usr/bin/r1_audiobook_app"
CRASH_COUNT=0
MAX_CRASHES=5

# Determine if usleep is available; fall back to sleep 0.1 (BusyBox supports
# fractional seconds). If neither works, use sleep 1.
if command -v usleep >/dev/null 2>&1; then
  POLL_SLEEP="usleep 100000"
else
  POLL_SLEEP="sleep 0.1"
fi

while true; do
  rm -f "$FLAG"
  /usr/bin/hiby_player &
  HP_PID=$!

  # Poll for flag file while hiby_player runs
  while kill -0 $HP_PID 2>/dev/null; do
    if [ -f "$FLAG" ]; then
      # Audiobooks tile tapped - kill hiby_player cleanly
      kill -TERM $HP_PID 2>/dev/null
      # Wait up to 2 seconds for clean exit
      i=0
      while kill -0 $HP_PID 2>/dev/null && [ $i -lt 20 ]; do
        $POLL_SLEEP
        i=$((i + 1))
      done
      # Force kill if still alive
      kill -9 $HP_PID 2>/dev/null
      wait $HP_PID 2>/dev/null
      rm -f "$FLAG"
      # Clear framebuffer to prevent stale frame
      dd if=/dev/zero of=/dev/fb0 bs=960 count=800 2>/dev/null
      # Launch audiobook app (foreground)
      if [ -x "$APP" ]; then
        "$APP"
      fi
      # Clear framebuffer after app exits
      dd if=/dev/zero of=/dev/fb0 bs=960 count=800 2>/dev/null
      CRASH_COUNT=0
      break  # loop back to relaunch hiby_player
    fi
    $POLL_SLEEP
  done

  # If we get here, hiby_player exited without flag
  wait $HP_PID 2>/dev/null
  rm -f "$FLAG"

  CRASH_COUNT=$((CRASH_COUNT + 1))
  if [ $CRASH_COUNT -ge $MAX_CRASHES ]; then
    # Repeated crashes - reboot as stock recovery
    sleep 1
    reboot
  fi

  # Brief pause before relaunching
  sleep 1
done