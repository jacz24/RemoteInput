"""Simba-Reflection #17's probe (Windows): RemoteInput's key repeat against a real keyboard's, with P14's two checks
(Simba-AI #484, a Mac probe) on the PC. It starts KeyLog.java, injects the DLL, pairs, and holds keys through the
plugin's exports:

  presses   10 presses of Escape, each 20-125 ms, 400 ms apart: none outlasts the repeat delay, so none attaches a
            thread (P14).
  holds     10 holds of the up arrow, each the repeat delay and 300 ms, 700 ms apart: each repeats and attaches one
            thread (the control of the thread count), and no key event comes in the 200 ms after a release (P14).
  rollover  hold A, hold B, release B, release A, 1 s apart. The operator's keyboard on Windows 11 (#17, 2026-10-23):
            B's press ends A's repeat; B repeats after its own delay; B's release ends the repeat while A is still down,
            and A posts nothing more until its release. A's and B's first repeats came 511 and 507 ms after their
            presses, at the OS's 500 ms.

usage: javac --release 11 -d testbed/out testbed/KeyLog.java
       python -I testbed/probe_key_repeat.py <java.exe> <libRemoteInput.dll>
Exit 0 only when every check passes.
"""
import ctypes
import os
import random
import statistics
import subprocess
import sys
import threading
import time

PRESSED, TYPED, RELEASED = 401, 400, 402
VK_ESCAPE, VK_UP, VK_A, VK_B = 0x1B, 0x26, 0x41, 0x42
# shortcut: a repeat task sleeps on Windows's 15.6 ms timer tick, so the bound on a first repeat is the delay plus a tick
# and the post (the real keyboard's 507-511 ms fall inside); measure the bound again if the fork raises the timer's rate.
FIRST_REPEAT_SLACK_MS = 20


def runs(codes):
    """[(code, count)] for each run of one code."""
    out = []
    for c in codes:
        if out and out[-1][0] == c:
            out[-1][1] += 1
        else:
            out.append([c, 1])
    return [tuple(r) for r in out]


def after_release(events, window_us=200_000):
    """Each press or KEY_TYPED within window_us after a release (P14's measure)."""
    late, released = [], None
    for kind, code, us in events:
        if kind == RELEASED:
            released = us
        elif released is not None and us - released < window_us:
            late.append((kind, code))
    return late


def check_rollover(events, delay_ms):
    """The faults of the rollover phase against the real keyboard's order and the repeat delay."""
    faults = []
    presses = [(code, us) for kind, code, us in events if kind == PRESSED]
    releases = [(code, us) for kind, code, us in events if kind == RELEASED]
    if [c for c, _ in runs(c for c, _ in presses)] != [VK_A, VK_B] or any(n < 2 for _, n in runs(c for c, _ in presses)):
        faults.append(f"presses ran {runs(c for c, _ in presses)}, expected A repeats, then B repeats, and no A after B")
    if [c for c, _ in releases] != [VK_B, VK_A]:
        faults.append(f"releases {[c for c, _ in releases]}, expected B then A")
    if presses and releases and presses[-1][1] > releases[0][1]:
        faults.append(f"{sum(us > releases[0][1] for _, us in presses)} press(es) after B's release, while A was down")
    for key, name in [(VK_A, "A"), (VK_B, "B")]:
        times = [us for c, us in presses if c == key]
        if len(times) >= 2:
            first = (times[1] - times[0]) / 1000
            if not delay_ms <= first <= delay_ms + FIRST_REPEAT_SLACK_MS:
                faults.append(f"{name}'s first repeat {first:.1f} ms after its press, expected {delay_ms}-{delay_ms + FIRST_REPEAT_SLACK_MS}")
    return faults


def timings(events):
    """Each key's first-repeat delay and its later intervals, in ms, for the report."""
    out = {}
    for key in sorted({c for k, c, _ in events if k == PRESSED}):
        times = [us for k, c, us in events if k == PRESSED and c == key]
        gaps = [(b - a) / 1000 for a, b in zip(times, times[1:])]
        if gaps:
            out[chr(key) if 32 < key < 127 else hex(key)] = (gaps[0], min(gaps[1:] or [0]), statistics.median(gaps[1:] or [0]), max(gaps[1:] or [0]))
    return out


def main(java, dll):
    here = os.path.dirname(os.path.abspath(__file__))
    jvm = subprocess.Popen([java, *os.environ.get("JVM_OPTS", "").split(), "-cp", os.path.join(here, "out"), "KeyLog"],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in jvm.stdout:  # -Xcheck:jni warns before main runs
        if line.startswith("READY"):
            break
    else:
        sys.exit("JVM did not start")
    pid, version = int(line.split()[1]), line.split()[2]
    events, threads = [], []

    def read():
        for l in jvm.stdout:
            f = l.split()
            if f[:1] == ["KEY"]:
                events.append((int(f[1]), int(f[2]), int(f[5])))  # id, key code, arrival in µs
            elif f[:1] == ["THREADS"]:
                threads.append(int(f[1].rsplit("-", 1)[1]))
    threading.Thread(target=read, daemon=True).start()

    lib = ctypes.CDLL(os.path.abspath(dll))
    lib.EIOS_Inject_PID.argtypes = [ctypes.c_int32]
    lib.EIOS_PairClient.argtypes = [ctypes.c_int32]
    lib.EIOS_PairClient.restype = ctypes.c_void_p
    lib.EIOS_ReleaseClient.argtypes = [ctypes.c_void_p]
    lib.EIOS_HoldKey.argtypes = lib.EIOS_ReleaseKey.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.EIOS_GetKeyboardRepeatDelay.argtypes = lib.EIOS_GetKeyboardSpeed.argtypes = [ctypes.c_void_p]

    threading.Thread(target=lib.EIOS_Inject_PID, args=(pid,), daemon=True).start()
    for _ in range(100):
        if jvm.poll() is not None:
            sys.exit(f"java {version} exited 0x{jvm.returncode & 0xFFFFFFFF:08X} before the pairing")
        eios = lib.EIOS_PairClient(pid)
        if eios:
            break
        time.sleep(0.1)
    else:
        sys.exit(f"java {version} not paired")
    delay, speed = lib.EIOS_GetKeyboardRepeatDelay(eios), lib.EIOS_GetKeyboardSpeed(eios)

    def thread_number():
        n = len(threads)
        jvm.stdin.write("THREADS\n")
        jvm.stdin.flush()
        while len(threads) == n:
            time.sleep(0.01)
        return threads[-1]

    def phase(steps):
        """Run steps, then give the phase's events and the anonymous threads it made (the THREADS line's own left out)."""
        time.sleep(0.5)
        start, first = len(events), thread_number()
        steps()
        time.sleep(0.5)  # the EDT prints after the post
        return events[start:], thread_number() - first - 1

    def hold(key, ms):
        lib.EIOS_HoldKey(eios, key)
        time.sleep(ms / 1000)
        lib.EIOS_ReleaseKey(eios, key)

    rng = random.Random(14)
    presses, presses_threads = phase(lambda: [(hold(VK_ESCAPE, rng.randint(20, 125)), time.sleep(0.4)) for _ in range(10)])
    holds, holds_threads = phase(lambda: [(hold(VK_UP, delay + 300), time.sleep(0.7)) for _ in range(10)])

    def rollover():
        for call, key in [(lib.EIOS_HoldKey, VK_A), (lib.EIOS_HoldKey, VK_B), (lib.EIOS_ReleaseKey, VK_B), (lib.EIOS_ReleaseKey, VK_A)]:
            call(eios, key)
            time.sleep(1)
    two_keys, _ = phase(rollover)
    alive = jvm.poll() is None
    lib.EIOS_ReleaseClient(eios)
    jvm.kill()

    count = lambda evs, kind: sum(k == kind for k, _, _ in evs)
    checks = [
        ("presses: 10 pressed, 10 released", (count(presses, PRESSED), count(presses, RELEASED)) == (10, 10),
         f"{count(presses, PRESSED)} pressed, {count(presses, RELEASED)} released"),
        ("presses: no thread attached", presses_threads == 0, f"{presses_threads} thread(s)"),
        ("holds: each repeats, one thread each (control)", holds_threads == 10 and count(holds, PRESSED) >= 20,
         f"{holds_threads} thread(s), {count(holds, PRESSED)} pressed"),
        ("presses and holds: no key event in the 200 ms after a release", not after_release(presses + holds),
         f"{after_release(presses + holds)}"),
    ]
    faults = check_rollover(two_keys, delay)
    checks.append(("rollover: the real keyboard's order and first repeats", not faults, "; ".join(faults)))

    print(f"java {version}, repeat delay {delay} ms, interval {speed} ms")
    for key, (first, lo, med, hi) in timings(two_keys).items():
        print(f"  rollover {key}: first repeat {first:.1f} ms, then each {lo:.1f}-{hi:.1f} ms (median {med:.1f})")
    print(f"  rollover presses in runs: {[(chr(c), n) for c, n in runs(c for k, c, _ in two_keys if k == PRESSED)]}")
    for name, ok, detail in checks:
        print(f"{'PASS' if ok else 'FAIL'}: {name}" + ("" if ok else f" -- {detail}"))
    if not alive:
        print("FAIL: the JVM died")
    return 0 if alive and all(ok for _, ok, _ in checks) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
