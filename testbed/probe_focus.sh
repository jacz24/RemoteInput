#!/bin/sh
# Probe (macOS): a press that must regain focus first carries the time it is posted, not the time
# hold_mouse started. It runs e2e.sh with the driver in "focus" mode (LoseFocus, then a left click),
# then reads the age on arrival that the target logs for the press. gain_focus waits 200 ms or more,
# so a press stamped before it arrives that much late; one stamped after arrives within a few ms.
# It tests the library in build/ as it is: build the commit under test first (cmake --build build).
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$ROOT/testbed/out/aut.log"

"$ROOT/testbed/e2e.sh" focus

echo
grep '^RECV MOUSE_PRESSED' "$LOG"
echo

# The last press is the one after LoseFocus; e2e's own click comes first.
age=$(grep '^RECV MOUSE_PRESSED,(310,210),' "$LOG" | tail -1 | sed -n 's/.*,age=\([0-9-]*\),.*/\1/p')
if [ -n "$age" ] && [ "$age" -lt 100 ]; then
    printf 'PASS: the press after a focus loss arrives %s ms after its time\n\nPROBE PASSED\n' "$age"
    exit 0
fi
printf 'FAIL: the press after a focus loss arrives %s ms after its time (want < 100)\n\nPROBE FAILED\n' "${age:-?}"
exit 1
