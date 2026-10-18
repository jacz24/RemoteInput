#!/bin/sh
# Build and launch the owned application-under-test with libRemoteInput injected (macOS).
# Usage: testbed/run.sh
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/testbed/out"
DYLIB_SRC="$ROOT/build/libRemoteInput.dylib"

[ -f "$DYLIB_SRC" ] || { echo "Missing $DYLIB_SRC -- build the library first (cmake --build build)." >&2; exit 1; }

# /usr/bin/java is a SIP-protected Apple stub; macOS strips DYLD_INSERT_LIBRARIES from it, so the
# library never loads. Launch the real JDK binary, which is ad-hoc signed with the
# allow-dyld-environment-variables entitlement and does honor the injection.
real_java() {
    [ -n "$JAVA_HOME" ] && [ -x "$JAVA_HOME/bin/java" ] && { echo "$JAVA_HOME/bin/java"; return; }
    for j in /opt/homebrew/opt/openjdk*/bin/java \
             /opt/homebrew/Cellar/openjdk*/*/libexec/openjdk.jdk/Contents/Home/bin/java \
             "$(/usr/libexec/java_home 2>/dev/null)/bin/java"; do
        [ -x "$j" ] && { echo "$j"; return; }
    done
    echo "/usr/bin/java" # last resort: the SIP stub, injection will NOT work
}
JAVA="$(real_java)"

# The library must not load from under ~/Documents: dyld holds its open() for a folder-access
# prompt and the process hangs before main. Copy it to the temp dir and inject from there.
DYLIB="${TMPDIR:-/tmp}/libRemoteInput.dylib"
cp "$DYLIB_SRC" "$DYLIB"

mkdir -p "$OUT"
javac "$ROOT/testbed/TestClient.java" -d "$OUT"

# The trailing "-c" is required, not cosmetic: Plugin.cxx activates the library only when the
# process argv holds a "-c" token (else it treats the process as the launcher and stays inert).
exec env DYLD_INSERT_LIBRARIES="$DYLIB" "$JAVA" -cp "$OUT" TestClient -c
