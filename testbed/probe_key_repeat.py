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
  long      hold A 4 s, then D 4 s, 2 s apart, as the operator held them (#142): the fold below.
  slow     the rollover again, with KeyLog holding the command thread the repeat interval after each key it posts, so
            B's press spans two of A's repeat times (#104). Where B counted as held before its task was the newest, A's
            task posted a repeat of B there, before B's delay; at real speed the window is two posts long.

The shape of the repeats (#105), over every hold: each key's repeats have deadlines at its press, the delay, then one each
interval, and the probe prints each interval and each repeat's lateness against its deadline, by arrival. The keyboard's
repeats come at the first point at or after each deadline of a grid of about 15.75 ms (#142; #105 took it for the OS's
default tick, 15.625 ms), whatever tick another process asked for. The operator's keyboard at 33 ms (#17's real-keyboard
log of 2026-10-23, since written over): 242 intervals of 30-33 ms (2 steps) and 27 of 46-49 ms (3), a mean of 32.93 ms
over 5.3 s, so a deadline each whole 33 ms; again at a global tick of 1 ms (2026-10-28, 3 holds, Simba-Reflection's
docs/live/2026-10-28-key-repeat-tick1ms.tsv): 175 of 30-33 ms and 19 of 45-48 ms. The checks: each repeat at most a grid
step after its deadline and not before it, no interval under SHORT_MS, and the share of 40 ms or more within 2 SE of the
keyboard's at its grid, or of the share another grid gives.

Where the repeats fall on the grid (#142): each hold's repeats folded at a period, the phases' mean resultant length (1 when
every repeat falls at one point), each hold its own phase and the holds weighted by their repeats. The long phase's two
holds, folded so, against the keyboard's long holds: at 15.625 ms, at most theirs; at the best period, within theirs, and
the period too. A grid of a fixed phase scores about 0.99 at its period, as the plugin's of #105 did at 15.625 ms.
--keyboard logs the operator's own holds in KeyLog's shown window, each event's arrival in µs and the OS's tick at it, and
prints their fold; --tick1 holds a 1 ms tick in this process for the log.

usage: javac --release 11 -d testbed/out testbed/KeyLog.java
       python -I testbed/probe_key_repeat.py <java.exe> <libRemoteInput.dll>
       python -I testbed/probe_key_repeat.py <java.exe> --keyboard <log.tsv> [--tick1]
JVM_OPTS adds JVM options (-Xcheck:jni). Exit 0 only when every check passes.
"""
import ctypes
import math
import os
import random
import statistics
import subprocess
import sys
import threading
import time

PRESSED, TYPED, RELEASED = 401, 400, 402
VK_ESCAPE, VK_UP, VK_A, VK_B, VK_D = 0x1B, 0x26, 0x41, 0x42, 0x44
KEY_NAMES = {VK_ESCAPE: "Escape", VK_UP: "Up"}
# The plugin's grid (InputOutput.cxx's wait_for_tick, #142): each step GRID_MS, and up to GRID_STEP_MS either way.
GRID_MS, GRID_STEP_MS = 15.75, 0.52
# A first repeat comes at the first grid point at or after its delay (#105), up to a step and the post after it; the
# keyboard's 499-511 ms fall inside.
# The check reads the plugin's own when: the arrival times carry each post's lag, about 0.3 ms either way.
FIRST_REPEAT_SLACK_MS = 20
# The operator's keyboard (#17's log of 2026-10-23): intervals of LONG_MS or more of all, at its grid. Its least, 29.71 ms
# (2026-10-30, by µs), is two steps of a grid whose phase moves: none under SHORT_MS.
KEYBOARD_LONG, KEYBOARD_INTERVALS, KEYBOARD_GRID_MS = 27, 269, GRID_MS
LONG_MS, SHORT_MS = 40, 29.5
# The operator's keyboard at a global tick of 1 ms, its 6 holds of 2 s or more (2026-10-28, 2026-10-30), each folded alone
# (#142): at most 0.44 at 15.625 ms; at its best period, 15.69-15.84 ms, 0.57-0.96, and 0.97 for a hold of 0.6 s. A best
# period over a hold of 3.5 s is known to about 0.035 ms (the fold's peak is 0.07 ms wide): its range widens by 0.04.
KEYBOARD_AT_DEFAULT_MOST, KEYBOARD_BEST_MS, KEYBOARD_AT_BEST = 0.44, (15.65, 15.88), (0.57, 0.97)
# Each arrival carries its post's lag, about 0.3 ms either way (8217919).
LAG_MS = 1


def os_ticks_ms():
    """The OS's default tick and the tick it runs now, the finest any process asked for, in ms (NtQueryTimerResolution)."""
    coarsest, finest, current = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.c_ulong()
    if ctypes.WinDLL("ntdll").NtQueryTimerResolution(ctypes.byref(coarsest), ctypes.byref(finest), ctypes.byref(current)):
        sys.exit("NtQueryTimerResolution failed")
    return coarsest.value / 10_000, current.value / 10_000


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


def concentration(holds_ms, period_ms):
    """The repeats' phases at period_ms: each hold's mean resultant length, weighted by its repeats (#142)."""
    resultant = lambda t: math.hypot(sum(math.cos(2 * math.pi * x / period_ms) for x in t),
                                     sum(math.sin(2 * math.pi * x / period_ms) for x in t))
    return sum(resultant(t) for t in holds_ms) / sum(map(len, holds_ms))


def best_period(holds_ms, lo_ms=15.0, hi_ms=17.0, step_ms=0.001):
    """The period in lo_ms-hi_ms at which the repeats concentrate most: #105's and #142's scan."""
    return max((lo_ms + k * step_ms for k in range(round((hi_ms - lo_ms) / step_ms) + 1)),
               key=lambda p: concentration(holds_ms, p))


def fold_line(name, holds_ms):
    best = best_period(holds_ms)
    return (f"  {name}: {sum(map(len, holds_ms))} repeats, {concentration(holds_ms, 15.625):.2f} at 15.625 ms, "
            f"{concentration(holds_ms, best):.2f} at the best, {best:.3f} ms")


def check_fold(holds_ms):
    """The faults of the holds' fold against the keyboard's long holds (#142)."""
    at_default, best = concentration(holds_ms, 15.625), best_period(holds_ms)
    at_best, faults = concentration(holds_ms, best), []
    if at_default > KEYBOARD_AT_DEFAULT_MOST:
        faults.append(f"{at_default:.2f} at 15.625 ms, over the keyboard's {KEYBOARD_AT_DEFAULT_MOST}")
    if not KEYBOARD_BEST_MS[0] <= best <= KEYBOARD_BEST_MS[1]:
        faults.append(f"the best period {best:.3f} ms, outside the keyboard's {KEYBOARD_BEST_MS[0]}-{KEYBOARD_BEST_MS[1]}")
    if not KEYBOARD_AT_BEST[0] <= at_best <= KEYBOARD_AT_BEST[1]:
        faults.append(f"{at_best:.2f} at the best, outside the keyboard's {KEYBOARD_AT_BEST[0]}-{KEYBOARD_AT_BEST[1]}")
    return faults


def check_shape(report, tick_ms, speed_ms):
    """The faults of the repeats' shape against the keyboard's: a post at the first tick at or after each deadline, so
    each interval is the ticks the setting spans or one more, the longer in frac(setting / tick) of them."""
    faults = []
    lates = [late for _, _, of_hold in report for late in of_hold]
    step_ms = tick_ms + GRID_STEP_MS
    off = [late for late in lates if not -LAG_MS <= late <= step_ms + LAG_MS]
    if off:
        faults.append(f"{len(off)} of {len(lates)} repeats outside 0-{step_ms:g} ms after their deadlines "
                      f"({min(off):.1f} to {max(off):.1f})")
    shorter = speed_ms // tick_ms * tick_ms
    short_ms = min(SHORT_MS, shorter - LAG_MS)
    gap_pairs = [(before, gap) for _, gaps, _ in report for before, gap in zip([None] + gaps, gaps)]
    short = [(before, gap) for before, gap in gap_pairs if gap < short_ms]
    if short:
        after = sum(before is not None and before >= LONG_MS for before, _ in short)
        faults.append(f"{len(short)} interval(s) under {short_ms:g} ms ({min(gap for _, gap in short):.1f} the least), "
                      f"{after} of them after one of {LONG_MS} ms or more")
    n, n_long = len(gap_pairs), sum(gap >= LONG_MS for _, gap in gap_pairs)
    longer = (speed_ms / tick_ms) % 1 if shorter + tick_ms >= LONG_MS else 0
    if not n:
        faults.append("no intervals")
    elif abs(tick_ms - KEYBOARD_GRID_MS) < 0.1:
        p = (n_long + KEYBOARD_LONG) / (n + KEYBOARD_INTERVALS)
        se = (p * (1 - p) * (1 / n + 1 / KEYBOARD_INTERVALS)) ** 0.5
        if abs(n_long / n - KEYBOARD_LONG / KEYBOARD_INTERVALS) > 2 * se:
            faults.append(f"{n_long} of {n} intervals {LONG_MS} ms or more, outside 2 SE ({2 * se:.3f}) of the "
                          f"keyboard's {KEYBOARD_LONG} of {KEYBOARD_INTERVALS}")
    elif abs(n_long / n - longer) > 2 * (longer * (1 - longer) / n) ** 0.5:
        faults.append(f"{n_long} of {n} intervals {LONG_MS} ms or more, where a tick of {tick_ms:g} ms gives "
                      f"{longer:.3f} of them")
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
    default_tick, now_tick = os_ticks_ms()
    presses, presses_threads = phase(lambda: holds(VK_ESCAPE, 10, lambda: rng.randint(20, 125), 400))
    held, held_threads = phase(lambda: holds(VK_UP, 10, lambda: delay + 300, 700))
    rollover, _ = phase(lambda: two_keys(VK_B))
    older, _ = phase(lambda: two_keys(VK_A))
    long, _ = phase(lambda: [holds(key, 1, lambda: 4000, 2000) for key in (VK_A, VK_D)])
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
              for name, events_of in [("holds", held), ("rollover", rollover), ("older", older), ("long", long), ("slow", slow)]}
    shape = check_shape([hold for holds_of in report.values() for hold in holds_of], GRID_MS, speed)
    checks.append(("repeats: the keyboard's shape, each at the first grid point after its deadline", not shape,
                   "; ".join(shape)))
    long_ms = [[us / 1000 for us in reps] for _, _, reps in repeat_holds(long)]
    fold = check_fold(long_ms) if len(long_ms) == 2 else [f"{len(long_ms)} long holds, expected 2"]
    checks.append(("long: the keyboard's fold, at 15.625 ms and at the best period", not fold, "; ".join(fold)))

    print(f"java {version}, repeat delay {delay} ms, interval {speed} ms, the OS's default tick {default_tick:g} ms "
          f"(now {now_tick:g} ms)")
    for name, holds_of in report.items():
        for key, gaps, lates in holds_of:
            print(f"  {name} {KEY_NAMES.get(key, chr(key))}: intervals {' '.join(f'{g:.1f}' for g in gaps)}"
                  f" | late {' '.join(f'{late:.1f}' for late in lates)}")
    for name, events_of in [("rollover", rollover), ("older", older), ("slow", slow)]:
        print(f"  {name} presses in runs: {[(chr(c), n) for c, n in runs(c for k, c, _, _ in events_of if k == PRESSED)]}")
    for (key, _, _), reps in zip(repeat_holds(long), long_ms):
        print(fold_line(f"long {chr(key)}", [reps]))
    if long_ms:
        print(fold_line("long, both", long_ms))
    gaps = sorted(g for holds_of in report.values() for _, gaps_of, _ in holds_of for g in gaps_of)
    if gaps:
        print(f"  intervals: {len(gaps)}, {gaps[0]:.1f}-{gaps[-1]:.1f} ms (median {statistics.median(gaps):.1f}), "
              f"{sum(g < SHORT_MS for g in gaps)} under {SHORT_MS}, {sum(g >= LONG_MS for g in gaps)} of {LONG_MS} or more")
    for name, ok, detail in checks:
        print(f"{'PASS' if ok else 'FAIL'}: {name}" + ("" if ok else f" -- {detail}"))
    if not alive:
        print("FAIL: the JVM died")
    return 0 if alive and all(ok for _, ok, _ in checks) else 1


def keyboard(java, log_path, tick1):
    """The operator's holds in KeyLog's shown window, logged to log_path with the OS's tick at each event, and their fold."""
    if tick1:
        ctypes.WinDLL("winmm").timeBeginPeriod(1)
    here = os.path.dirname(os.path.abspath(__file__))
    jvm = subprocess.Popen([java, "-cp", os.path.join(here, "out"), "KeyLog", "show"], stdout=subprocess.PIPE, text=True)
    events = []
    with open(log_path, "w") as log:
        log.write("us\tid\tkey_code\tkey_char\tlocation\ttick_ms\n")
        for line in jvm.stdout:
            f = line.split()
            if f[:1] == ["READY"]:
                print(f"java {f[2]}, the OS's tick now {os_ticks_ms()[1]:g} ms: click the window, then hold a letter "
                      f"4 s, twice or more, 2 s apart; it closes in 2 min")
            elif f[:1] == ["KEY"]:
                log.write("\t".join([f[5], f[1], f[2], f[3], f[4], f"{os_ticks_ms()[1]:g}"]) + "\n")
                events.append((int(f[1]), int(f[2]), int(f[5]), int(f[6])))
    holds = [(code, [us / 1000 for us in reps]) for code, _, reps in repeat_holds(events) if len(reps) >= 2]
    for code, reps in holds:
        print(fold_line(chr(code), [reps]))
    if holds:
        print(fold_line("all", [reps for _, reps in holds]))
    return 0


if __name__ == "__main__":
    sys.exit(keyboard(sys.argv[1], sys.argv[3], "--tick1" in sys.argv) if sys.argv[2] == "--keyboard"
             else main(sys.argv[1], sys.argv[2]))
