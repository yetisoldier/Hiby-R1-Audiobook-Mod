#!/bin/sh

killall    hiby_player    &>/dev/null
killall -9 hiby_player    &>/dev/null

if [ -f "/usr/bin/batd" ]; then
killall    batd    &>/dev/null
killall -9 batd    &>/dev/null
/usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt &
fi

while true; do
  # Launch hiby_player. When user taps Audiobooks, the binary patch
  # calls execve() which replaces hiby_player with r1_audiobook_app
  # instantly. When our app exits, this loop relaunches hiby_player.
  /usr/bin/hiby_player

  # Normal exit (crash or quit) - reboot as stock behavior
  sleep 1
  reboot
done