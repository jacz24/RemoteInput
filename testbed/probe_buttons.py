"""Simba-Reflection #205's probe (Windows): each mouse button number acts on one button slot, and a stop releases every
held button, the right one too. Simba 2 sends 1 left, 2 middle and 3 right (MouseButtonToInt at Simba 146e4d67e3:
LEFT 1, RIGHT 3, MIDDLE 2, SCROLL_UP 4, SCROLL_DOWN 5); 0 is Simba 1's right, an alias the fork keeps. Before the fix
the held-state check took 1, 2 and 3 while the stop released 0, 1 and 2, so a held right button stayed down.

MoveLog.java in a hidden JVM, the DLL injected and paired, each step through the plugin's exports; a step's events are
the POST lines, its reads the EIOS_IsMouseButtonHeld answers (number:held). The canvas is 400 x 300.

  first          a move to (100, 100): the entry and a move
  left-1 ...     a move to a new point (so each press counts 1), the button pressed twice and released twice: one
                 press, one release and its click; held after each press, not after each release
  right-0        the same by 0, the alias, read by 0 and by 3
  left-right     left, then right, held: the press masks 1024 and 5120; released right first
  three          left, middle and right held: 1024, 3072, 7168
  stop           EIOS_ReleaseClient with the three held: a release and its click of each, the right one last
  re-pair        a new pair of the same client: no button reads held, nothing posted
  right-click    a right press and release: both posted
  no-button      4, 5 (Simba's SCROLL_UP and SCROLL_DOWN) and -1 pressed, read and released: nothing, and no slot
                 held after

usage: javac --release 11 -d testbed/out testbed/MoveLog.java
       python -I testbed/probe_buttons.py <java.exe> <libRemoteInput.dll>
Exit 0 only when each step posted exactly its events and read exactly its states.
"""
import ctypes
import os
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # -I leaves the script's folder off the path
from probe_same_point import KIND, SETTLE   # noqa: E402

MASK = {1: 1024, 2: 2048, 3: 4096}   # BUTTON1_DOWN_MASK, BUTTON2_DOWN_MASK, BUTTON3_DOWN_MASK


def bind(dll):
    lib = ctypes.CDLL(dll)
    lib.EIOS_Inject_PID.argtypes = [ctypes.c_int32]
    lib.EIOS_PairClient.argtypes = [ctypes.c_int32]
    lib.EIOS_PairClient.restype = ctypes.c_void_p
    lib.EIOS_MoveMouse.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
    for name in ('EIOS_HoldMouse', 'EIOS_ReleaseMouse'):
        getattr(lib, name).argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_int32]
    lib.EIOS_IsMouseButtonHeld.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.EIOS_IsMouseButtonHeld.restype = ctypes.c_bool
    lib.EIOS_ReleaseClient.argtypes = [ctypes.c_void_p]
    for name in ('EIOS_MoveMouse', 'EIOS_HoldMouse', 'EIOS_ReleaseMouse', 'EIOS_ReleaseClient'):
        getattr(lib, name).restype = None   # an action returns None; a read returns its line
    return lib


def actions(lib, eios):
    return (lambda x, y: lambda: lib.EIOS_MoveMouse(eios, x, y),
            lambda b: lambda: lib.EIOS_HoldMouse(eios, 0, 0, b),
            lambda b: lambda: lib.EIOS_ReleaseMouse(eios, 0, 0, b),
            lambda b: lambda: f'{b}:{int(lib.EIOS_IsMouseButtonHeld(eios, b))}')


def click(x, b):
    """A press and release of Java button b at (x, 100) after a move there, no other button held."""
    return [f'MOVED {x} 100 0 0 0', f'PRESSED {x} 100 {b} 1 {MASK[b]}', f'RELEASED {x} 100 {b} 1 0',
            f'CLICKED {x} 100 {b} 1 0']


def first_pair(lib, eios):
    move, hold, up, held = actions(lib, eios)
    steps = [('first', [move(100, 100)], ['ENTERED 100 100 0 0 0', 'MOVED 100 100 0 0 0'], [])]
    for x, (name, n, b) in zip((120, 140, 160), (('left-1', 1, 1), ('middle-2', 2, 2), ('right-3', 3, 3))):
        steps.append((name, [move(x, 100), hold(n), held(n), hold(n), held(n), up(n), held(n), up(n), held(n)],
                      click(x, b), [f'{n}:1', f'{n}:1', f'{n}:0', f'{n}:0']))
    steps += [
        ('right-0', [move(180, 100), hold(0), held(0), held(3), hold(0), up(0), held(0), held(3), up(0)],
         click(180, 3), ['0:1', '3:1', '0:0', '3:0']),
        ('left-right', [move(200, 100), hold(1), hold(3), held(1), held(3), up(3), held(3), held(1), up(1), held(1)],
         ['MOVED 200 100 0 0 0', 'PRESSED 200 100 1 1 1024', 'PRESSED 200 100 3 1 5120', 'RELEASED 200 100 3 1 1024',
          'CLICKED 200 100 3 1 1024', 'RELEASED 200 100 1 1 0', 'CLICKED 200 100 1 1 0'],
         ['1:1', '3:1', '3:0', '1:1', '1:0']),
        ('three', [move(220, 100), hold(1), hold(2), hold(3), held(1), held(2), held(3)],
         ['MOVED 220 100 0 0 0', 'PRESSED 220 100 1 1 1024', 'PRESSED 220 100 2 1 3072', 'PRESSED 220 100 3 1 7168'],
         ['1:1', '2:1', '3:1']),
        ('stop', [lambda: lib.EIOS_ReleaseClient(eios)],
         ['RELEASED 220 100 1 1 6144', 'CLICKED 220 100 1 1 6144', 'RELEASED 220 100 2 1 4096',
          'CLICKED 220 100 2 1 4096', 'RELEASED 220 100 3 1 0', 'CLICKED 220 100 3 1 0'], []),
    ]
    return steps


def second_pair(lib, eios):
    move, hold, up, held = actions(lib, eios)
    return [
        ('re-pair', [held(1), held(2), held(3), held(0)], [], ['1:0', '2:0', '3:0', '0:0']),
        ('right-click', [move(240, 100), hold(3), up(3)], click(240, 3), []),
        # last: before the fix a number past the slots wrote outside them and posted an event Java may refuse
        ('no-button', [a(n) for n in (4, 5, -1) for a in (hold, held, up)] + [held(1), held(2), held(3)], [],
         ['4:0', '5:0', '-1:0', '1:0', '2:0', '3:0']),
    ]


def main(java, dll):
    here = os.path.dirname(os.path.abspath(__file__))
    jvm = subprocess.Popen([java, '-cp', os.path.join(here, 'out'), 'MoveLog'], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, text=True)
    try:
        line = jvm.stdout.readline()
        pid, version = int(line.split()[1]), line.split()[2]
        lines = []
        threading.Thread(target=lambda: [lines.append(x.split()) for x in jvm.stdout], daemon=True).start()
        lib = bind(dll)
        threading.Thread(target=lib.EIOS_Inject_PID, args=(pid,), daemon=True).start()
        print(f'java {version}, pid {pid}, {dll}', flush=True)

        failed = total = 0
        for pair_steps in (first_pair, second_pair):
            eios = None
            for _ in range(100):
                if jvm.poll() is not None:
                    break
                eios = lib.EIOS_PairClient(pid)
                if eios:
                    break
                time.sleep(0.1)
            if not eios:
                print(f'FAIL: java {version} not paired (exit {jvm.poll()})')
                return 1
            for name, acts, want, want_reads in pair_steps(lib, eios):
                start = len(lines)
                reads = [r for r in (a() for a in acts) if r is not None]
                time.sleep(SETTLE)
                posted = [' '.join([KIND.get(g[1], g[1])] + g[2:]) for g in lines[start:] if g[0] == 'POST']
                ok = posted == want and reads == want_reads
                failed, total = failed + (not ok), total + 1
                print(f'{name:12} {"ok  " if ok else "FAIL"} posted [{", ".join(posted)}] read [{" ".join(reads)}]'
                      + ('' if ok else f', expected [{", ".join(want)}] read [{" ".join(want_reads)}]'), flush=True)
        if jvm.poll() is None:   # a client that died waits for no command
            lib.EIOS_ReleaseClient(eios)
        print(f'{"PASS" if not failed else "FAIL"}: {total - failed} of {total} steps posted and read their states')
        return 1 if failed else 0
    finally:
        jvm.kill()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], os.path.abspath(sys.argv[2])))
