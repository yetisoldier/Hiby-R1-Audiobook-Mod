#!/bin/sh

# Device-side destructive transport test. Run only from a detached worker:
# it intentionally stops the active ADB gadget, then exercises the direct
# FunctionFS fallback from the installed/bind-mounted common helper.

RESULT=/tmp/r1-direct-adb-test.result
rm -f "$RESULT"
sleep 1

. /usr/bin/r1_usb_gadget_common.sh || {
    echo 91 >"$RESULT"
    exit 91
}

if ! usb_lock; then
    echo 90 >"$RESULT"
    exit 90
fi
trap usb_unlock EXIT INT TERM

usb_log "direct-fallback-test-start"
usb_stop_adb_gadget
rc=$?
if [ "$rc" -eq 0 ]; then
    usb_start_adb_direct
    rc=$?
fi

echo "$rc" >"$RESULT"
usb_log "direct-fallback-test-result rc=$rc"
exit "$rc"
