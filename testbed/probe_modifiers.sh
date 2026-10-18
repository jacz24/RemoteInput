#!/bin/sh
# Probe for Simba-AI #496 (macOS): the down-mask each held button puts in a mouse event, and where the
# canvas ends. It runs e2e.sh with the driver in "modifiers" mode, then reads the events the target logged.
# A real AWT event carries BUTTON1/2/3_DOWN_MASK (1024/2048/4096) for a held left/middle/right button,
# and a component covers 0..w-1 x 0..h-1, so a move to x == w or y == h exits it.
# It tests the library in build/ as it is: build the commit under test first (cmake --build build).
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$ROOT/testbed/out/aut.log"

"$ROOT/testbed/e2e.sh" modifiers

echo
grep '^RECV' "$LOG"
echo

fails=0
check() { # $1 what, $2 a line the log must hold
    if grep -q "$2" "$LOG"; then echo "PASS: $1"; else echo "FAIL: $1"; fails=$((fails + 1)); fi
}
check "a left drag carries BUTTON1_DOWN_MASK (control)" '^RECV MOUSE_DRAGGED,(310,205),.*,modsEx=1024$'
check "a right drag carries BUTTON3_DOWN_MASK"          '^RECV MOUSE_DRAGGED,(320,205),.*,modsEx=4096$'
check "a middle drag carries BUTTON2_DOWN_MASK"         '^RECV MOUSE_DRAGGED,(330,205),.*,modsEx=2048$'
check "x == w-1 is inside (control)"                    '^RECV MOUSE_MOVED,(764,250),'
check "a move to x == w exits"                          '^RECV MOUSE_EXITED,(765,250),'
check "a move to y == h exits"                          '^RECV MOUSE_EXITED,(300,503),'

[ "$fails" -eq 0 ] && printf '\nPROBE PASSED\n' || printf '\n%d PROBE CHECK(S) FAILED\n' "$fails"
exit "$fails"
