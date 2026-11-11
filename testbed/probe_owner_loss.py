"""Simba-Reflection #254's probe (Windows): when the controller that holds a client dies, the client lets go of what it
held at once, with no command from anyone, and the next controller pairs and makes its input as on a fresh client.
Before the fix a killed Simba left each held key and button down and the key repeating (C-simba-killed-repeat), and a
controller killed inside a command left its answer for the next controller's first command.

OwnerLog.java in a hidden JVM, the DLL injected by this process. Each dead controller is a process of this probe's own
(this file with --controller), which pairs, makes its input and is killed by TerminateProcess; the probe kills no other
process. A step's events are the client's posts after its start.

  killed           a controller holds Shift, A, left, middle and right, A repeats, and the probe kills it: each held
                   input comes up once, no press comes after the first release, and the first release comes within
                   LAG_MOST_MS of the kill. The lag and the gaps between the releases are printed.
  idle             a controller that held nothing (one read, so the client knew it) is killed: nothing is posted
  replace          this process pairs: nothing reads held; a move and a left click; B held past its repeat delay and the
                   watch: B repeats, reads held, and comes up only at this process's release
  in-flight        a controller's hold of A is inside its post (OwnerLog's SLOW) when it is killed: A comes up once,
                   after the post
  after-in-flight  this process pairs again: its first read gives the repeat delay, and a click posts and reads as in
                   replace (the dead controller's answer is no answer to this one)

usage: javac --release 11 -d testbed/out testbed/OwnerLog.java
       python -I testbed/probe_owner_loss.py <java.exe> <libRemoteInput.dll>
Exit 0 only when each step posted and read exactly what it should.
"""
import ctypes
import os
import subprocess
import sys
import threading
import time

SETTLE = 0.3   # s: the EDT dispatches after the post
WATCH = 1.5   # s after a kill: many repeat intervals, so a repeat or a late release shows
SLOW_MS = 1500   # the in-flight post's length; the kill comes at its start
LAG_MOST_MS = 100   # the first release after a kill; the client wakes on the controller thread's end, not on a timer
VK_SHIFT, VK_A, VK_B = 0x10, 0x41, 0x42
MOUSE = {'500': 'click', '501': 'press', '502': 'release', '503': 'move', '504': 'enter', '505': 'exit', '506': 'drag'}
KEY = {'400': 'typed', '401': 'press', '402': 'release'}
KEY_NAMES = {VK_SHIFT: 'Shift', VK_A: 'A', VK_B: 'B'}


def bind(dll):
    lib = ctypes.CDLL(dll)
    lib.EIOS_Inject_PID.argtypes = [ctypes.c_int32]
    lib.EIOS_PairClient.argtypes = [ctypes.c_int32]
    lib.EIOS_PairClient.restype = ctypes.c_void_p
    lib.EIOS_MoveMouse.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
    for name in ('EIOS_HoldMouse', 'EIOS_ReleaseMouse'):
        getattr(lib, name).argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_int32]
    for name in ('EIOS_HoldKey', 'EIOS_ReleaseKey', 'EIOS_IsKeyHeld', 'EIOS_IsMouseButtonHeld'):
        getattr(lib, name).argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.EIOS_IsKeyHeld.restype = lib.EIOS_IsMouseButtonHeld.restype = ctypes.c_bool
    lib.EIOS_GetKeyboardRepeatDelay.argtypes = lib.EIOS_ReleaseClient.argtypes = [ctypes.c_void_p]
    lib.EIOS_GetKeyboardRepeatDelay.restype = ctypes.c_int32
    for name in ('EIOS_MoveMouse', 'EIOS_HoldMouse', 'EIOS_ReleaseMouse', 'EIOS_HoldKey', 'EIOS_ReleaseKey',
                 'EIOS_ReleaseClient'):
        getattr(lib, name).restype = None
    return lib


def pair(lib, pid):
    for _ in range(100):
        eios = lib.EIOS_PairClient(pid)
        if eios:
            return eios
        time.sleep(0.1)
    return None


def controller(dll, pid, mode):
    """A controller in a process of its own, which the probe kills: it pairs, makes mode's input, says READY and waits."""
    lib = bind(dll)
    eios = pair(lib, pid)
    if not eios:
        sys.exit(1)
    if mode == 'hold':
        lib.EIOS_MoveMouse(eios, 100, 100)
        for key in (VK_SHIFT, VK_A):
            lib.EIOS_HoldKey(eios, key)
        for button in (1, 2, 3):
            lib.EIOS_HoldMouse(eios, 0, 0, button)
    else:
        lib.EIOS_IsMouseButtonHeld(eios, 1)   # a command, so the client knows this controller
    print('READY', flush=True)
    if mode == 'in-flight':
        sys.stdin.readline()
        lib.EIOS_HoldKey(eios, VK_A)
    time.sleep(60)


def render(f):
    if f[0] == 'M':
        return f'{MOUSE[f[1]]} {f[2]}'
    if f[1] == '400':
        return f'typed {chr(int(f[3]))}'
    return f'{KEY[f[1]]} {KEY_NAMES.get(int(f[2]), f[2])}'


def main(java, dll):
    here = os.path.dirname(os.path.abspath(__file__))
    jvm = subprocess.Popen([java, '-cp', os.path.join(here, 'out'), 'OwnerLog'], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, text=True)
    children = []
    try:
        line = jvm.stdout.readline()
        pid, version = int(line.split()[1]), line.split()[2]
        events = []   # each post's fields: kind, id, button or key code, ..., when
        threading.Thread(target=lambda: [events.append(x.split()) for x in jvm.stdout], daemon=True).start()
        lib = bind(dll)
        threading.Thread(target=lib.EIOS_Inject_PID, args=(pid,), daemon=True).start()
        print(f'java {version}, pid {pid}, {dll}', flush=True)

        def spawn(mode):
            child = subprocess.Popen([sys.executable, '-I', os.path.abspath(__file__), '--controller', dll, str(pid), mode],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            children.append(child)
            if child.stdout.readline().strip() != 'READY':
                raise RuntimeError(f'the {mode} controller did not pair')
            return child

        def kill(child):
            at = time.time() * 1000
            child.kill()   # TerminateProcess: the controller runs no code of its own end
            child.wait()
            return at

        def wait_for(test, seconds):
            deadline = time.time() + seconds
            while not test():
                if time.time() > deadline:
                    raise RuntimeError('the client did not post what the step waits for')
                time.sleep(0.01)

        def names(start, end=None):
            return [render(f) for f in events[start:end]]

        failed, total = 0, 0

        def report(name, ok, got, expected='', note=''):
            nonlocal failed, total
            failed, total = failed + (not ok), total + 1
            print(f'{name:16} {"ok  " if ok else "FAIL"} posted [{", ".join(got)}]' + (f'; {note}' if note else '')
                  + ('' if ok else f'; expected {expected}'), flush=True)

        # killed
        mark = len(events)
        child = spawn('hold')
        wait_for(lambda: names(mark).count('press A') >= 2, 3)   # A's press and a repeat
        start = len(events)
        killed_at = kill(child)
        time.sleep(WATCH)
        got = events[start:]
        ups = [f for f in got if render(f).startswith('release')]
        held = ['release 1', 'release 2', 'release 3', 'release Shift', 'release A']
        first_up = got.index(ups[0]) if ups else len(got)
        presses_after = [render(f) for f in got[first_up:] if render(f).startswith('press')]
        lag = int(ups[0][-1]) - killed_at if ups else None
        gaps = [int(b[-1]) - int(a[-1]) for a, b in zip(ups, ups[1:])]
        report('killed', sorted(map(render, ups)) == sorted(held) and not presses_after and lag is not None
               and lag <= LAG_MOST_MS, [render(f) for f in got], f'each of {held} once, no press after the first, '
               f'the first within {LAG_MOST_MS} ms',
               f'first release {lag if lag is None else round(lag)} ms after the kill, gaps {gaps} ms')

        # idle
        child = spawn('idle')
        time.sleep(SETTLE)
        start = len(events)
        kill(child)
        time.sleep(WATCH)
        report('idle', not names(start), names(start), 'nothing')

        # replace
        eios = pair(lib, pid)
        if not eios:
            raise RuntimeError('this process did not pair')
        reads = [int(lib.EIOS_IsMouseButtonHeld(eios, b)) for b in (1, 2, 3)]
        reads += [int(lib.EIOS_IsKeyHeld(eios, k)) for k in (VK_SHIFT, VK_A)]
        delay = lib.EIOS_GetKeyboardRepeatDelay(eios)
        start = len(events)
        lib.EIOS_MoveMouse(eios, 150, 150)
        lib.EIOS_HoldMouse(eios, 0, 0, 1)
        lib.EIOS_ReleaseMouse(eios, 0, 0, 1)
        lib.EIOS_HoldKey(eios, VK_B)
        time.sleep(delay / 1000 + WATCH)
        b_held = int(lib.EIOS_IsKeyHeld(eios, VK_B))
        before_up = len(events)
        lib.EIOS_ReleaseKey(eios, VK_B)
        time.sleep(SETTLE)
        lib.EIOS_ReleaseClient(eios)
        click = ['move 0', 'press 1', 'release 1', 'click 1']
        got = names(start)
        b_part = names(start + 4, before_up)
        report('replace', reads == [0] * 5 and got[:4] == click and b_part.count('press B') >= 2
               and set(b_part) == {'press B', 'typed b'} and names(before_up) == ['release B'] and b_held == 1, got,
               f'reads 0 0 0 0 0, {click}, then B pressed and typed 2 times or more and released at the end, B held',
               f'reads {reads}, B held {b_held}')

        # in-flight
        child = spawn('in-flight')
        start = len(events)
        jvm.stdin.write(f'SLOW {SLOW_MS}\n')
        jvm.stdin.flush()
        time.sleep(SETTLE)   # OwnerLog's reader takes the line
        child.stdin.write('go\n')
        child.stdin.flush()
        wait_for(lambda: 'press A' in names(start), 3)
        kill(child)
        time.sleep(SLOW_MS / 1000 + WATCH)
        got = names(start)
        up = got.index('release A') if 'release A' in got else len(got)
        report('in-flight', got.count('release A') == 1 and got[up + 1:] == [] and set(got[:up]) <= {'press A', 'typed a'},
               got, 'A pressed (and repeated), then released once, and nothing after')

        # after-in-flight
        eios = pair(lib, pid)
        if not eios:
            raise RuntimeError('this process did not pair again')
        delay_again = lib.EIOS_GetKeyboardRepeatDelay(eios)
        start = len(events)
        lib.EIOS_MoveMouse(eios, 200, 150)
        lib.EIOS_HoldMouse(eios, 0, 0, 1)
        reads = [int(lib.EIOS_IsMouseButtonHeld(eios, 1))]
        lib.EIOS_ReleaseMouse(eios, 0, 0, 1)
        reads.append(int(lib.EIOS_IsMouseButtonHeld(eios, 1)))
        time.sleep(SETTLE)
        lib.EIOS_ReleaseClient(eios)
        report('after-in-flight', delay_again == delay and reads == [1, 0] and names(start) == click, names(start),
               f'the repeat delay {delay}, reads 1 0, {click}', f'repeat delay {delay_again}, reads {reads}')

        print(f'{"PASS" if not failed else "FAIL"}: {total - failed} of {total} steps posted and read what they should')
        return 1 if failed else 0
    finally:
        for child in children:
            child.kill()
        jvm.kill()


if __name__ == '__main__':
    if sys.argv[1] == '--controller':
        controller(sys.argv[2], int(sys.argv[3]), sys.argv[4])
    else:
        sys.exit(main(sys.argv[1], os.path.abspath(sys.argv[2])))
