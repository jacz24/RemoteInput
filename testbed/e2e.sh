#!/bin/sh
# End-to-end check: build the target and driver, launch the target with the library injected,
# pair, capture, inject input, assert, then tear down. macOS.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/testbed/out"
DYLIB_SRC="$ROOT/build/libRemoteInput.dylib"
DYLIB="${TMPDIR:-/tmp}/libRemoteInput.dylib"
LOG="$OUT/aut.log"

# /usr/bin/java is a SIP-protected Apple stub that strips DYLD_INSERT_LIBRARIES. Launch the real
# JDK binary, which is ad-hoc signed with the allow-dyld-environment-variables entitlement.
real_java() {
    [ -n "$JAVA_HOME" ] && [ -x "$JAVA_HOME/bin/java" ] && { echo "$JAVA_HOME/bin/java"; return; }
    for j in /opt/homebrew/opt/openjdk*/bin/java \
             /opt/homebrew/Cellar/openjdk*/*/libexec/openjdk.jdk/Contents/Home/bin/java \
             "$(/usr/libexec/java_home 2>/dev/null)/bin/java"; do
        [ -x "$j" ] && { echo "$j"; return; }
    done
    echo "/usr/bin/java"
}
JAVA="$(real_java)"

mkdir -p "$OUT"
cp "$DYLIB_SRC" "$DYLIB"                       # out of ~/Documents, or dyld hangs on a folder prompt
javac "$ROOT/testbed/TestClient.java" -d "$OUT"
cc -o "$OUT/driver" "$ROOT/testbed/driver.c"

# "-c" in argv makes the library activate (Plugin.cxx treats it as the client, not the launcher).
env DYLD_INSERT_LIBRARIES="$DYLIB" "$JAVA" -cp "$OUT" TestClient -c >"$LOG" 2>&1 &
AUT_PID=$!
trap 'kill "$AUT_PID" 2>/dev/null || true' EXIT

echo "target pid $AUT_PID; waiting for window + library..."
sleep 4
if ! kill -0 "$AUT_PID" 2>/dev/null; then
    echo "target exited early; log:" >&2
    cat "$LOG" >&2
    exit 1
fi

"$OUT/driver" "$AUT_PID" "$DYLIB" "$@"   # probe_modifiers.sh passes "modifiers"
