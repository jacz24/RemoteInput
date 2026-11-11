"""Simba-Reflection #252's probe (Windows): a move to the cursor's own point posts nothing, held button or not, as the
Windows peer drops a WM_MOUSEMOVE to its last point (awt_Component.cpp, WmMouseMove: it posts a move or a drag only
when the component or the point changed; Robot's same-point step of gate 10, #207, posts none). Its entry and exit come
from the OS's tracking of the cursor, not from a move, so a resize under a cursor that stays put posts none here (the
fork's shortcut in move_mouse): the canvas keeps its last event's inside state until a move to a new point.

MoveLog.java runs in its own JVM, hidden, so no OS mouse input reaches it. The probe injects the DLL, pairs, and makes
each step through the plugin's exports; a step's events are the POST lines (each event RemoteInput posts, before the
queue coalesces two moves) after its start. A DISPATCH line that no POST line matches is an event of the OS (a real
cursor, a window's move): the probe reports those apart and fails on none. Two sessions, the canvas 400 x 300 at first:

  the first position from the constructor (no point yet):
  first          the first move, to (100, 100) inside: the entry and a move
  same           two moves to (100, 100): nothing
  move           (120, 110): a move
  exit           (500, 100), outside: the exit
  same-outside   (500, 100): nothing
  enter          (130, 120), a new point inside: the entry and a move (gate 10's hover)
  resize         the canvas 450 x 350, then (130, 120): nothing, the point stays inside
  resize-out     the canvas 100 x 100, then (130, 120): nothing (no exit: the shortcut)
  resize-in      the canvas 400 x 300, then (130, 120): nothing (no entry: the shortcut)
  after-resize   (135, 120): a move, as the canvas never had the exit
  press          the left button down: the press
  held-same      (135, 120) held: nothing (the fork's rule before #252)
  drag           (140, 125) held: a drag (Windows: button 0, count 0)
  drag-same      (140, 125) held: nothing
  release        the left button up: the release, no click after a drag
  shift-same     Shift down, then (140, 125): nothing (the key's own events are no mouse events)
  shift-move     (150, 130): a move with Shift's mask (64)

  the first position read from the real cursor (a script's MouseXY before its first move, include/click.simba):
  read-first     EIOS_GetMousePosition, then a move to the point it gave: nothing

usage: javac --release 11 -d testbed/out testbed/MoveLog.java
       python -I testbed/probe_same_point.py <java.exe> <libRemoteInput.dll>
Exit 0 only when each step posted exactly its events.
"""
import ctypes
import os
import subprocess
import sys
import threading
import time
from collections import Counter

SETTLE = 0.3   # s: the EDT dispatches after the post
SIZE_WAIT = 5   # s: the longest wait for the canvas's resize
KIND = {'500': 'CLICKED', '501': 'PRESSED', '502': 'RELEASED', '503': 'MOVED', '504': 'ENTERED', '505': 'EXITED',
        '506': 'DRAGGED'}
LEFT, SHIFT = 1, 0x10   # Simba's left button; VK_SHIFT


def session(java, dll, steps):
    """MoveLog in a new JVM, the DLL injected and paired, then each step of steps(lib, eios, move, size): the number of
    steps that failed and of steps."""
    here = os.path.dirname(os.path.abspath(__file__))
    jvm = subprocess.Popen([java, '-cp', os.path.join(here, 'out'), 'MoveLog'], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, text=True)
    try:
        line = jvm.stdout.readline()
        pid, version = int(line.split()[1]), line.split()[2]
        lines = []
        threading.Thread(target=lambda: [lines.append(x.split()) for x in jvm.stdout], daemon=True).start()

        lib = ctypes.CDLL(dll)
        lib.EIOS_Inject_PID.argtypes = [ctypes.c_int32]
        lib.EIOS_PairClient.argtypes = [ctypes.c_int32]
        lib.EIOS_PairClient.restype = ctypes.c_void_p
        lib.EIOS_ReleaseClient.argtypes = [ctypes.c_void_p]
        lib.EIOS_MoveMouse.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
        lib.EIOS_GetMousePosition.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32)]
        for name in ('EIOS_HoldMouse', 'EIOS_ReleaseMouse'):
            getattr(lib, name).argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_int32]
        for name in ('EIOS_HoldKey', 'EIOS_ReleaseKey'):
            getattr(lib, name).argtypes = [ctypes.c_void_p, ctypes.c_int32]
        threading.Thread(target=lib.EIOS_Inject_PID, args=(pid,), daemon=True).start()
        eios = None
        for _ in range(100):
            if jvm.poll() is not None:
                break
            eios = lib.EIOS_PairClient(pid)
            if eios:
                break
            time.sleep(0.1)
        if not eios:
            print(f'FAIL: java {version} not paired')
            return 1, 1
        print(f'java {version}, pid {pid}, {dll}', flush=True)

        def move(*points):
            return lambda: [lib.EIOS_MoveMouse(eios, x, y) for x, y in points]

        def size(w, h):
            def resize():
                start = len(lines)
                jvm.stdin.write(f'SIZE {w} {h}\n')
                jvm.stdin.flush()
                deadline = time.time() + SIZE_WAIT
                while not any(g[0] == 'SIZED' for g in lines[start:]):
                    if time.time() > deadline:
                        raise RuntimeError(f'the canvas did not take {w} x {h}')
                    time.sleep(0.01)
            return resize

        failed, steps = 0, steps(lib, eios, move, size)
        for name, actions, want in steps:
            start = len(lines)
            for action in actions:
                action()
            time.sleep(SETTLE)
            got = lines[start:]
            render = lambda where: [' '.join([KIND.get(g[1], g[1])] + g[2:]) for g in got if g[0] == where]
            posted = render('POST')
            os_events = list((Counter(render('DISPATCH')) - Counter(posted)).elements())
            ok = posted == want
            failed += not ok
            print(f'{name:13} {"ok  " if ok else "FAIL"} posted [{", ".join(posted)}]'
                  + ('' if ok else f', expected [{", ".join(want)}]')
                  + (f'; OS events, apart: [{", ".join(os_events)}]' if os_events else ''), flush=True)
        lib.EIOS_ReleaseKey(eios, SHIFT)
        lib.EIOS_ReleaseClient(eios)
        return failed, len(steps)
    finally:
        jvm.kill()


def constructed(lib, eios, move, size):
    return [
        ('first', [move((100, 100))], ['ENTERED 100 100 0 0 0', 'MOVED 100 100 0 0 0']),
        ('same', [move((100, 100), (100, 100))], []),
        ('move', [move((120, 110))], ['MOVED 120 110 0 0 0']),
        ('exit', [move((500, 100))], ['EXITED 500 100 0 0 0']),
        ('same-outside', [move((500, 100))], []),
        ('enter', [move((130, 120))], ['ENTERED 130 120 0 0 0', 'MOVED 130 120 0 0 0']),
        ('resize', [size(450, 350), move((130, 120))], []),
        ('resize-out', [size(100, 100), move((130, 120))], []),
        ('resize-in', [size(400, 300), move((130, 120))], []),
        ('after-resize', [move((135, 120))], ['MOVED 135 120 0 0 0']),
        ('press', [lambda: lib.EIOS_HoldMouse(eios, 0, 0, LEFT)], ['PRESSED 135 120 1 1 1024']),
        ('held-same', [move((135, 120))], []),
        ('drag', [move((140, 125))], ['DRAGGED 140 125 0 0 1024']),
        ('drag-same', [move((140, 125))], []),
        ('release', [lambda: lib.EIOS_ReleaseMouse(eios, 0, 0, LEFT)], ['RELEASED 140 125 1 1 0']),
        ('shift-same', [lambda: lib.EIOS_HoldKey(eios, SHIFT), move((140, 125))], []),
        ('shift-move', [move((150, 130))], ['MOVED 150 130 0 0 64']),
    ]


def read_first(lib, eios, move, size):
    def read_then_move():
        x, y = ctypes.c_int32(), ctypes.c_int32()
        lib.EIOS_GetMousePosition(eios, ctypes.byref(x), ctypes.byref(y))
        print(f'  the first read gave ({x.value}, {y.value}), {"inside" if 0 <= x.value < 400 and 0 <= y.value < 300 else "outside"}'
              ' the canvas', flush=True)
        move((x.value, y.value))()
    return [('read-first', [read_then_move], [])]


def main(java, dll):
    runs = [session(java, dll, constructed), session(java, dll, read_first)]
    failed, total = sum(r[0] for r in runs), sum(r[1] for r in runs)
    print(f'{"PASS" if not failed else "FAIL"}: {total - failed} of {total} steps posted their events')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], os.path.abspath(sys.argv[2])))
