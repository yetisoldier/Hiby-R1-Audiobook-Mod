#!/bin/sh

killall    hiby_player    &>/dev/null
killall -9 hiby_player    &>/dev/null

if [ -f "/usr/bin/batd" ]; then
killall    batd    &>/dev/null
killall -9 batd    &>/dev/null
/usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt &
fi

while true; do
  # Remove any stale launch flag
  rm -f /tmp/.r1_audiobook_launch 2>/dev/null

  # Launch hiby_player
  /usr/bin/hiby_player

  # Check if hiby_player exited because user tapped Audiobooks
  if [ -f /tmp/.r1_audiobook_launch ]; then
    # User tapped Audiobooks - launch our app
    rm -f /tmp/.r1_audiobook_launch 2>/dev/null
    /usr/bin/r1_audiobook_launch.sh 2>/dev/null
    # When app exits, loop back and relaunch hiby_player
    continue
  fi

  # Normal exit - reboot as stock behavior
  sleep 1
  reboot
done