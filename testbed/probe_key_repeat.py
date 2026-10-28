"""Simba-Reflection #17's probe (Windows): RemoteInput's key repeat against a real keyboard's, with P14's two checks
(Simba-AI #484, a Mac probe) on the PC. It starts KeyLog.java, injects the DLL, pairs, and holds keys through the
plugin's exports:

  presses   10 presses of Escape, each 20-125 ms, 400 ms apart: none outlasts the repeat delay, so none repeats or
            attaches a thread (P14).
  holds     10 holds of the up arrow, each the repeat delay and 300 ms, 700 ms apart: each repeats and attaches one
            thread (the control of the thread count), and no key event comes in the 200 ms after a release (P14).
  rollover  hold A, hold B, release B, release A, 1 s apart. The operator's keyboard on Windows 11 (#17, 2026-10-23):
            B's press ends A's repeat; B repeats after its own delay; B's release ends the repeat while A is still down,
            and A posts nothing more until its release. A's and B's first repeats came 511 and 507 ms after their
            presses, at the OS's 500 ms.
  older     hold A, hold B, release A, release B, 1 s apart: the release of the older key leaves B's repeat alone.
  slow      the rollover again, with KeyLog holding the command thread the repeat interval after each key it posts, so
            B's press spans two of A's repeat times (#104). Where B counted as held before its task was the newest, A's
            task posted a repeat of B there, before B's delay; at real speed the window is two posts long.

The shape of the repeats (#105), over every hold: each key's repeats have deadlines at its press, the delay, then one each
interval, and the probe prints each interval and each repeat's lateness against its deadline, by arrival. The keyboard's
driver repeats on a kernel timer, which fires at the first tick of the OS's clock at or after each deadline; the tick is
the finest any process asked for (timeBeginPeriod), else 15.625 ms. The operator's keyboard at 33 ms and a tick of
15.625 ms (#17's key-log.tsv): 242 intervals of 30-33 ms (2 ticks) and 27 of 46-49 ms (3), a mean of 32.93 ms over 5.3 s,
so a deadline each whole 33 ms. The checks: each repeat at most a tick after its deadline and not before it, no interval
under 30 ms, and the share of 40 ms or more within 2 SE of the keyboard's at its tick, or none at a tick under which a
tick more than the interval stays under 40 ms. The tick must not change during the run.

usage: javac --release 11 -d testbed/out testbed/KeyLog.java
       python -I testbed/probe_key_repeat.py <java.exe> <libRemoteInput.dll>
JVM_OPTS adds JVM options (-Xcheck:jni). Exit 0 only when every check passes.
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
KEY_NAMES = {VK_ESCAPE: "Escape", VK_UP: "Up"}
# shortcut: a repeat ends its sleep on Windows's timer tick (15.6 ms unless a process raised the rate), so a first repeat
# may come up to a tick and the post after its delay; the keyboard's 507-511 ms fall inside. Measure again on a new timer.
# The check reads the plugin's own when: the arrival times carry each post's lag, about 0.3 ms either way.
FIRST_REPEAT_SLACK_MS = 20
# The operator's keyboard (#17's key-log.tsv): intervals of LONG_MS or more of all, at its tick; none under SHORT_MS.
KEYBOARD_LONG, KEYBOARD_INTERVALS, KEYBOARD_TICK_MS = 27, 269, 15.625
LONG_MS, SHORT_MS = 40, 30
# Each arrival carries its post's lag, about 0.3 ms either way (8217919).
LAG_MS = 1


def os_tick_ms():
    """The OS's clock tick in ms (NtQueryTimerResolution's current)."""
    least, most, current = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.c_ulong()
    ctypes.WinDLL("ntdll").NtQueryTimerResolution(ctypes.byref(least), ctypes.byref(most), ctypes.byref(current))
    return current.value / 10_000


def repeat_holds(events):
    """Each key's run of presses as (key, its press's arrival, its repeats' arrivals), in µs: a run ends at its key's
    release or at another key's press."""
    holds, held = [], None
    for kind, code, us, _ in events:
        if kind == PRESSED and held and held[0] == code:
            held[2].append(us)
        elif kind == PRESSED:
            held = (code, us, [])
            holds.append(held)
        elif kind == RELEASED and held and held[0] == code:
            held = None
    return holds


def repeat_report(events, delay_ms, speed_ms):
    """Each hold's key, its intervals between repeats and each repeat's lateness against its deadline, in ms by arrival."""
    return [(key, [(b - a) / 1000 for a, b in zip(reps, reps[1:])],
             [(us - press) / 1000 - delay_ms - k * speed_ms for k, us in enumerate(reps)])
            for key, press, reps in repeat_holds(events)]


def check_shape(report, tick_ms, speed_ms):
    """The faults of the repeats' shape against the keyboard's: a post at the first tick at or after each deadline."""
    faults = []
    lates = [late for _, _, lates in report for late in lates]
    off = [late for late in lates if not -LAG_MS <= late <= tick_ms + LAG_MS]
    if off:
        faults.append(f"{len(off)} of {len(lates)} repeats outside 0-{tick_ms:g} ms after their deadlines "
                      f"({min(off):.1f} to {max(off):.1f})")
    pairs = [(a, b) for _, gaps, _ in report for a, b in zip([None] + gaps, gaps)]
    short = [(a, b) for a, b in pairs if b < SHORT_MS]
    if short:
        after = sum(a is not None and a >= LONG_MS for a, _ in short)
        faults.append(f"{len(short)} interval(s) under {SHORT_MS} ms ({min(b for _, b in short):.1f} the least), "
                      f"{after} of them after one of {LONG_MS} ms or more")
    n, n_long = len(pairs), sum(b >= LONG_MS for _, b in pairs)
    if not n:
        faults.append("no intervals")
    elif abs(tick_ms - KEYBOARD_TICK_MS) < 0.1:
        p = (n_long + KEYBOARD_LONG) / (n + KEYBOARD_INTERVALS)
        se = (p * (1 - p) * (1 / n + 1 / KEYBOARD_INTERVALS)) ** 0.5
        if abs(n_long / n - KEYBOARD_LONG / KEYBOARD_INTERVALS) > 2 * se:
            faults.append(f"{n_long} of {n} intervals {LONG_MS} ms or more, outside 2 SE ({2 * se:.3f}) of the "
                          f"keyboard's {KEYBOARD_LONG} of {KEYBOARD_INTERVALS}")
    elif (speed_ms // tick_ms + 1) * tick_ms < LONG_MS and n_long:
        faults.append(f"{n_long} of {n} intervals {LONG_MS} ms or more, at a tick of {tick_ms:g} ms")
    return faults


def runs(codes):
    """[(code, count)] for each run of one code."""
    out = []
    for c in codes:
        if out and out[-1][0] == c:
            out[-1][1] += 1
        else:
            out.append([c, 1])
    return [tuple(r) for r in out]


def pressed_per_hold(events):
    """The KEY_PRESSED count of each hold of one key, a hold ending at its release."""
    counts, n = [], 0
    for kind, _, _, _ in events:
        if kind == PRESSED:
            n += 1
        elif kind == RELEASED:
            counts.append(n)
            n = 0
    return counts


def after_release(events, window_us=200_000):
    """Each press or KEY_TYPED within window_us after a release (P14's measure)."""
    late, released = [], None
    for kind, code, us, _ in events:
        if kind == RELEASED:
            released = us
        elif released is not None and us - released < window_us:
            late.append((kind, code))
    return late


def check_rollover(events, delay_ms, first_release):
    """The faults of a two-key hold (A, then B) whose first release is first_release: A repeats, B's press ends it, B
    repeats after its own delay; B's release ends the repeat, and A's leaves it alone."""
    faults = []
    presses = [(code, us) for kind, code, us, _ in events if kind == PRESSED]
    releases = [(code, us) for kind, code, us, _ in events if kind == RELEASED]
    pressed_runs = runs(c for c, _ in presses)
    if [c for c, _ in pressed_runs] != [VK_A, VK_B] or any(n < 2 for _, n in pressed_runs):
        faults.append(f"presses ran {pressed_runs}, expected A repeats, then B repeats, and no A after B")
    other = VK_A if first_release == VK_B else VK_B
    if [c for c, _ in releases] != [first_release, other]:
        faults.append(f"releases {[c for c, _ in releases]}, expected {chr(first_release)} then {chr(other)}")
    elif first_release == VK_B and presses and presses[-1][1] > releases[0][1]:
        faults.append(f"{sum(us > releases[0][1] for _, us in presses)} press(es) after B's release, while A was down")
    elif first_release == VK_A and not any(releases[0][1] < us < releases[1][1] for _, us in presses):
        faults.append("B's repeat ended at A's release")
    for key in (VK_A, VK_B):
        whens = [when for kind, code, _, when in events if kind == PRESSED and code == key]
        if len(whens) >= 2 and not delay_ms <= whens[1] - whens[0] <= delay_ms + FIRST_REPEAT_SLACK_MS:
            faults.append(f"{chr(key)}'s first repeat {whens[1] - whens[0]} ms after its press by the plugin's clock, "
                          f"expected {delay_ms}-{delay_ms + FIRST_REPEAT_SLACK_MS}")
    return faults


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
                events.append((int(f[1]), int(f[2]), int(f[5]), int(f[6])))  # id, key code, arrival in µs, when in ms
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

    def holds(key, n, hold_ms, gap_ms):
        for _ in range(n):
            lib.EIOS_HoldKey(eios, key)
            time.sleep(hold_ms() / 1000)
            lib.EIOS_ReleaseKey(eios, key)
            time.sleep(gap_ms / 1000)

    def two_keys(first_release):
        for call, key in [(lib.EIOS_HoldKey, VK_A), (lib.EIOS_HoldKey, VK_B), (lib.EIOS_ReleaseKey, first_release),
                          (lib.EIOS_ReleaseKey, VK_A if first_release == VK_B else VK_B)]:
            call(eios, key)
            time.sleep(1)

    rng = random.Random(14)  # ctl14.cxx's seed
    tick = os_tick_ms()
    presses, presses_threads = phase(lambda: holds(VK_ESCAPE, 10, lambda: rng.randint(20, 125), 400))
    held, held_threads = phase(lambda: holds(VK_UP, 10, lambda: delay + 300, 700))
    rollover, _ = phase(lambda: two_keys(VK_B))
    older, _ = phase(lambda: two_keys(VK_A))
    jvm.stdin.write(f"POST_MS {speed}\n")  # the phase's THREADS line, read after it, waits for it
    jvm.stdin.flush()
    slow, _ = phase(lambda: two_keys(VK_B))
    alive = jvm.poll() is None
    lib.EIOS_ReleaseClient(eios)
    jvm.kill()

    checks = [
        ("presses: one press each", pressed_per_hold(presses) == [1] * 10, f"pressed per press {pressed_per_hold(presses)}"),
        ("presses: no thread attached", presses_threads == 0, f"{presses_threads} thread(s)"),
        ("holds: each repeats, one thread each (control)",
         held_threads == 10 and len(pressed_per_hold(held)) == 10 and min(pressed_per_hold(held)) >= 2,
         f"{held_threads} thread(s), pressed per hold {pressed_per_hold(held)}"),
        ("presses and holds: no key event in the 200 ms after a release", not after_release(presses + held),
         f"{after_release(presses + held)}"),
    ]
    for name, events_of, first_release in [("rollover", rollover, VK_B), ("older", older, VK_A), ("slow", slow, VK_B)]:
        faults = check_rollover(events_of, delay, first_release)
        checks.append((f"{name}: the keyboard's order and first repeats", not faults, "; ".join(faults)))
    report = {name: repeat_report(events_of, delay, speed)
              for name, events_of in [("holds", held), ("rollover", rollover), ("older", older), ("slow", slow)]}
    shape = check_shape([hold for holds_of in report.values() for hold in holds_of], tick, speed)
    end_tick = os_tick_ms()
    checks.append(("repeats: the keyboard's shape, each at the first tick after its deadline", not shape, "; ".join(shape)))
    checks.append(("the OS's tick held through the run", end_tick == tick, f"{tick:g} ms, then {end_tick:g} ms"))

    print(f"java {version}, repeat delay {delay} ms, interval {speed} ms, the OS's tick {tick:g} ms")
    for name, holds_of in report.items():
        for key, gaps, lates in holds_of:
            print(f"  {name} {KEY_NAMES.get(key, chr(key))}: intervals {' '.join(f'{g:.1f}' for g in gaps)}"
                  f" | late {' '.join(f'{late:.1f}' for late in lates)}")
    for name, events_of in [("rollover", rollover), ("older", older), ("slow", slow)]:
        print(f"  {name} presses in runs: {[(chr(c), n) for c, n in runs(c for k, c, _, _ in events_of if k == PRESSED)]}")
    gaps = sorted(g for holds_of in report.values() for _, gaps_of, _ in holds_of for g in gaps_of)
    if gaps:
        print(f"  intervals: {len(gaps)}, {gaps[0]:.1f}-{gaps[-1]:.1f} ms (median {statistics.median(gaps):.1f}), "
              f"{sum(g < SHORT_MS for g in gaps)} under {SHORT_MS}, {sum(g >= LONG_MS for g in gaps)} of {LONG_MS} or more")
    for name, ok, detail in checks:
        print(f"{'PASS' if ok else 'FAIL'}: {name}" + ("" if ok else f" -- {detail}"))
    if not alive:
        print("FAIL: the JVM died")
    return 0 if alive and all(ok for _, ok, _ in checks) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
