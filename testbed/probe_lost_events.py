"""Simba-Reflection #74's probe (Windows): RemoteInput posts every key event a script asks for, and reports one it
cannot build. Each session starts KeyLog.java in a new JVM, injects the DLL, pairs, and types through the plugin's exports:

  text     '[\\]' through key_send (Simba's Target.KeySend), then send_string: each press, KEY_TYPED and release,
           in order (#62's text; #74 lost a session's first event, the press of '[').
  pending  b'\\xffab' through key_send: Java's KeyEvent rejects the KEY_TYPED of 0xFF (CHAR_UNDEFINED), so its
           exception is pending on the command thread at the next post. Every later event must arrive, and the
           plugin must print one lost event on stderr.
  vm       RemoteVM.FindClass of a class the JVM lacks, which leaves NoClassDefFoundError pending on the command
           thread (RemoteVM keeps it for the script), then '[\\]' through key_send: every event, no report.

usage: javac --release 11 -d testbed/out testbed/KeyLog.java
       python -I testbed/probe_lost_events.py <java.exe> <libRemoteInput.dll> [sessions]
JVM_OPTS adds JVM options (-Xcheck:jni). Exit 0 only when every session got every event and the one report.
Each session runs in its own Python process, so the plugin's stderr is a pipe from its start.
"""
import ctypes
import json
import os
import re
import subprocess
import sys
import threading
import time

TEXT = b"[\\]"
PENDING = b"\xffab"
PRESSED, TYPED, RELEASED = 401, 400, 402


def expected(text, typed=True):
    """The (id, char) of each event of text, as the plugin posts it: press, KEY_TYPED, release. A byte over 127 is a
    signed char, so 0xFF's char is 0xFFFF."""
    events = []
    for b in text:
        char = b if b < 128 else 0xFF00 | b
        events += [(PRESSED, char)] + ([(TYPED, char)] if typed and char != 0xFFFF else []) + [(RELEASED, char)]
    return events


def session(java, dll):
    here = os.path.dirname(os.path.abspath(__file__))
    jvm = subprocess.Popen([java, *os.environ.get("JVM_OPTS", "").split(), "-cp", os.path.join(here, "out"), "KeyLog"],
                           stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    other = []
    for line in jvm.stdout:  # -Xcheck:jni warns before main runs
        if line.startswith("READY"):
            break
        other.append(line.rstrip())
    else:
        return {"error": f"JVM did not start: {other}"}
    pid, version = int(line.split()[1]), line.split()[2]
    events = []

    def read():
        for l in jvm.stdout:
            if l.startswith("KEY "):
                events.append(tuple(map(int, l.split()[1:4:2])))  # id and char
            else:
                other.append(l.rstrip())
    threading.Thread(target=read, daemon=True).start()

    lib = ctypes.CDLL(dll)
    lib.EIOS_Inject_PID.argtypes = [ctypes.c_int32]
    lib.EIOS_PairClient.argtypes = [ctypes.c_int32]
    lib.EIOS_PairClient.restype = ctypes.c_void_p
    lib.EIOS_ReleaseClient.argtypes = [ctypes.c_void_p]
    lib.SimbaPluginTarget_KeySend.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32, ctypes.POINTER(ctypes.c_int32)]
    lib.EIOS_SendString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32, ctypes.c_int32]

    threading.Thread(target=lib.EIOS_Inject_PID, args=(pid,), daemon=True).start()
    for _ in range(100):
        if jvm.poll() is not None:
            return {"error": f"java {version} exited 0x{jvm.returncode & 0xFFFFFFFF:08X} before the pairing"}
        eios = lib.EIOS_PairClient(pid)
        if eios:
            break
        time.sleep(0.1)
    else:
        return {"error": f"java {version} not paired"}

    def key_send(text):
        sleeps = (ctypes.c_int32 * (len(text) * 4))(*([20] * (len(text) * 4)))  # 4 sleep times a character
        lib.SimbaPluginTarget_KeySend(eios, text, len(text), sleeps)

    def leave_pending_then_type():
        # The plugin's Lape methods take (Params, Result): Params[0] the RemoteVM variable, then each argument's address.
        vm, eios_var, name, cls = ctypes.c_void_p(), ctypes.c_void_p(eios), ctypes.c_char_p(b"no/such/Class74"), ctypes.c_void_p()
        lib.Pascal_RemoteVM_Init((ctypes.c_void_p * 2)(ctypes.addressof(vm), ctypes.addressof(eios_var)), None)
        lib.Pascal_RemoteVM_FindClass((ctypes.c_void_p * 2)(ctypes.addressof(vm), ctypes.addressof(name)), ctypes.byref(cls))
        key_send(TEXT)

    result = {"version": version}
    for name, call in [("key_send", lambda: key_send(TEXT)), ("send_string", lambda: lib.EIOS_SendString(eios, TEXT, 20, 20)),
                       ("pending", lambda: key_send(PENDING)), ("vm", leave_pending_then_type)]:
        start = len(events)
        print(f"PHASE {name}", file=sys.stderr, flush=True)
        call()
        time.sleep(1)  # the EDT prints after the post
        result[name] = events[start:]
    result["alive"] = jvm.poll() is None
    result["jni_warnings"] = sum("JNI call made" in l for l in other)
    lib.EIOS_ReleaseClient(eios)
    jvm.kill()
    return result


def check(result, stderr):
    """The faults of one session, empty when it got every event and the one report."""
    if "error" in result:
        return [result["error"]]
    faults = [f"{name}: got {result[name]}, expected {want}" for name, want in
              [("key_send", expected(TEXT)), ("send_string", expected(TEXT)), ("pending", expected(PENDING)), ("vm", expected(TEXT))]
              if [tuple(e) for e in result[name]] != want]
    reports = {phase: re.findall(r"RemoteInput: (\d+) input event", text) for phase, text in
               re.findall(r"PHASE (\w+)\n(.*?)(?=PHASE |\Z)", stderr, re.S)}
    if reports != {"key_send": [], "send_string": [], "pending": ["1"], "vm": []}:
        faults.append(f"reports by phase {reports}, expected one lost event in pending")
    if not result["alive"]:
        faults.append("the JVM died")
    return faults


if __name__ == "__main__":
    if sys.argv[1] == "--session":
        print(json.dumps(session(sys.argv[2], os.path.abspath(sys.argv[3]))))
        sys.exit(0)
    java, dll, runs = sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1
    failed = 0
    for i in range(1, runs + 1):
        child = subprocess.run([sys.executable, "-I", __file__, "--session", java, dll], capture_output=True, text=True, timeout=120)
        lines = child.stdout.strip().splitlines()
        result = json.loads(lines[-1]) if lines else {"error": f"no result: {child.stderr.strip()}"}
        faults = check(result, child.stderr)
        failed += bool(faults)
        print(f"session {i}: java {result.get('version', '?')}, JNI warnings {result.get('jni_warnings', '?')}: "
              + ("ok" if not faults else "FAIL\n  " + "\n  ".join(faults)), flush=True)
    print(f"{'PASS' if not failed else 'FAIL'}: {runs - failed} of {runs} sessions got every event")
    sys.exit(1 if failed else 0)
