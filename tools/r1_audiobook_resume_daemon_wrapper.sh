#!/bin/sh
set -eu

BASE_DIR=${AUDIOBOOK_BASE_DIR:-/usr/data/audiobooks}
DAEMON_BIN=${AUDIOBOOK_RESUME_DAEMON_BIN:-$BASE_DIR/bin/r1_audiobook_resume_daemon}
DAEMON_FALLBACK=${AUDIOBOOK_RESUME_DAEMON_FALLBACK:-$BASE_DIR/bin/r1_audiobook_resume_daemon_shell.sh}

# Keep the risky direct-open helper disabled by default unless the caller
# explicitly opts in. The arm-window logic in the compiled daemon does not
# depend on this flag.
: "${AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED:=0}"
export AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED

if [ -x "$DAEMON_BIN" ]; then
  exec "$DAEMON_BIN" "$@"
fi

if [ -x "$DAEMON_FALLBACK" ]; then
  exec "$DAEMON_FALLBACK" "$@"
fi

printf '%s\n' "r1_audiobook_resume_daemon wrapper: missing both $DAEMON_BIN and $DAEMON_FALLBACK" >&2
exit 127
