"""Simba-Reflection #13's probe (Windows): with the block on, RemoteInput's filter drops the OS's input to the game's
windows, and its own input passes; the block holds only while the script that set it holds the client and lives.

BlockLog.java runs in its own JVM, hidden, so it takes no focus from the PC's user. The real input is a message posted
to its canvas's window: the queue the OS posts real input to, which the filter reads, with no move of the real cursor
(java.awt.Robot moves it). AWT gives a posted move the real cursor's place, so a move at RemoteInput's exact point is
RemoteInput's, any other move or any wheel notch real. A posted key reaches no window without the OS's focus (AWT's
SendKeyEventToFocusOwner), so the probe shows the mouse and the wheel; the key messages take the same filter.

Each holder is a process of its own (--holder) that loads the DLL as Simba does, injects, pairs, and sets the block,
as a script: the probe can kill one, as a kill of Simba. The phases, in one JVM:
  open      no block: real input and RemoteInput's arrive;
  blocked   holder A blocks: no real input, RemoteInput's arrives;
  lifted    A unblocks: real input arrives;
  ended     A blocks, then its process exits with no release (a script's end): real input arrives;
  newholder holder B pairs (the next run's script; the DLL is in already), the flags still off from A: real input
            arrives (no holder inherits a block), and RemoteInput's;
  blockedB  B blocks: no real input, RemoteInput's arrives;
  killed    the probe kills B with its block on (a kill of Simba): real input arrives;
  released  holder C pairs, blocks, then releases the client: real input arrives. Last: a release ends RemoteInput's
            service in the client (ControlCenter::terminate);
  queue     the system queue is still BlockLog's own: RemoteInput pushed none.

usage: javac --release 11 -d testbed/out testbed/BlockLog.java
       python -I testbed/probe_block.py <java.exe> <libRemoteInput.dll>
Exit 0 only when every phase passes.
"""
import ctypes
import os
import subprocess
import sys
import threading
import time
from ctypes import wintypes

WM_MOUSEMOVE, WM_MOUSEWHEEL = 0x0200, 0x020A
SETTLE = 0.5   # s: the EDT prints after the post
BOT_WAIT = 3   # s: the longest wait for RemoteInput's move
BOT_POINTS = range(40, 50)   # RemoteInput's moves go to (n, n)


def holder(dll, pid):
    """A script's side: each stdin line a command, OK after each; the main thread pairs and calls, as a script's."""
    lib = ctypes.CDLL(dll)
    lib.EIOS_Inject_PID.argtypes = [ctypes.c_int32]
    lib.EIOS_PairClient.argtypes = [ctypes.c_int32]
    lib.EIOS_PairClient.restype = ctypes.c_void_p
    lib.EIOS_ReleaseClient.argtypes = [ctypes.c_void_p]
    lib.EIOS_MoveMouse.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
    for name in ('EIOS_SetMouseInputEnabled', 'EIOS_SetKeyboardInputEnabled'):
        getattr(lib, name).argtypes = [ctypes.c_void_p, ctypes.c_bool]
    eios = None
    for line in sys.stdin:
        cmd = line.split()
        if cmd[0] in ('inject', 'pair'):
            if cmd[0] == 'inject':   # a client with the DLL already (a second script) is paired alone, as Simba's
                threading.Thread(target=lib.EIOS_Inject_PID, args=(pid,), daemon=True).start()
            for _ in range(100):
                eios = lib.EIOS_PairClient(pid)
                if eios:
                    break
                time.sleep(0.1)
            print('OK' if eios else 'FAIL not paired', flush=True)
            continue
        if cmd[0] in ('block', 'open'):
            lib.EIOS_SetMouseInputEnabled(eios, cmd[0] == 'open')
            lib.EIOS_SetKeyboardInputEnabled(eios, cmd[0] == 'open')
        elif cmd[0] == 'bot':
            lib.EIOS_MoveMouse(eios, int(cmd[1]), int(cmd[1]))
        elif cmd[0] == 'release':
            lib.EIOS_ReleaseClient(eios)
        print('OK', flush=True)


class Holder:
    def __init__(self, dll, pid):
        self.p = subprocess.Popen([sys.executable, '-I', __file__, '--holder', dll, str(pid)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True)

    def __call__(self, cmd):
        self.p.stdin.write(cmd + '\n')
        self.p.stdin.flush()
        reply = self.p.stdout.readline().strip()
        if reply != 'OK':
            raise RuntimeError(f'{cmd}: {reply or "the holder died"}')


u = ctypes.WinDLL('user32', use_last_error=True)
u.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def window_of(pid, cls, parent=None):
    """The first window of class Cls of process Pid, a child of Parent with one."""
    found = []

    def each(h, _):
        p, name = wintypes.DWORD(), ctypes.create_unicode_buffer(64)
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        u.GetClassNameW(h, name, 64)
        if p.value == pid and name.value == cls:
            found.append(h)
        return True
    u.EnumChildWindows(parent, EnumProc(each), 0) if parent else u.EnumWindows(EnumProc(each), 0)
    return found[0] if found else None


def post_real(canvas):
    """Three moves and a wheel notch, as the OS posts them to the window."""
    for x in (10, 20, 30):
        u.PostMessageW(canvas, WM_MOUSEMOVE, 0, (x << 16) | x)
    u.PostMessageW(canvas, WM_MOUSEWHEEL, (-120 & 0xFFFF) << 16, (10 << 16) | 10)


def main(java, dll):
    here = os.path.dirname(os.path.abspath(__file__))
    jvm = subprocess.Popen([java, '-cp', os.path.join(here, 'out'), 'BlockLog'], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, text=True)
    line = jvm.stdout.readline()
    pid = int(line.split()[1])
    print(f'java {line.split()[2]}, pid {pid}', flush=True)
    lines = []
    threading.Thread(target=lambda: [lines.append(x.split()) for x in jvm.stdout], daemon=True).start()
    canvas = None
    for _ in range(50):
        frame = window_of(pid, 'SunAwtFrame')
        canvas = frame and window_of(pid, 'SunAwtCanvas', frame)
        if canvas:
            break
        time.sleep(0.1)
    results = []

    def phase(name, real_ok, bot=None, before=None):
        """Post real input (and RemoteInput's move to bot, bot) after Before runs: whether each arrived as it must."""
        if before:
            before()
        start = len(lines)
        # RemoteInput's first move enters the canvas (504), each next one moves in it (503): each at (n, n), n of
        # BOT_POINTS, where no posted move lands (AWT takes the frame's border off each). It goes first, and the real
        # input after it arrives: EventQueue coalesces the moves that wait for one component, and keeps the newest.
        is_bot = lambda g: g[0] == 'MOUSE' and g[2] == g[3] and int(g[2]) in BOT_POINTS
        if bot:
            bot[0](f'bot {bot[1]}')
        deadline = time.time() + BOT_WAIT
        while bot and time.time() < deadline and not any(is_bot(g) and g[2] == str(bot[1]) for g in lines[start:]):
            time.sleep(0.05)
        post_real(canvas)
        time.sleep(SETTLE)
        got = lines[start:]
        real = sum(1 for g in got if g[0] == 'WHEEL' or (g[0] == 'MOUSE' and g[1] == '503' and not is_bot(g)))
        botted = bot is not None and any(is_bot(g) and g[2] == str(bot[1]) for g in got)
        ok = (real > 0) == real_ok and (bot is None or botted)
        results.append(ok)
        print(f'{name}: real input {"arrived" if real else "dropped"} ({real} events)'
              + (f", RemoteInput's {'arrived' if botted else 'MISSING'}" if bot else '') + f': {"ok" if ok else "FAIL"}', flush=True)

    try:
        a = Holder(dll, pid)
        a('inject')
        phase('open', True, (a, 40))
        phase('blocked', False, (a, 41), lambda: a('block'))
        phase('lifted', True, (a, 42), lambda: a('open'))

        def end(h):
            h.p.stdin.close()   # the script's end: its process exits, with no release
            h.p.wait()
        phase('ended', True, None, lambda: (a('block'), end(a)))
        b = Holder(dll, pid)
        b('pair')   # the next run's script: the client has the DLL already
        phase('newholder', True, (b, 43))
        phase('blockedB', False, (b, 44), lambda: b('block'))
        phase('killed', True, None, lambda: (b.p.kill(), b.p.wait()))
        c = Holder(dll, pid)
        c('pair')
        phase('released', True, None, lambda: (c('block'), c('release')))   # last: a release ends the client's service
        end(c)
        jvm.stdin.write('QUEUE\n')
        jvm.stdin.flush()
        time.sleep(SETTLE)
        queue = next((g[1] for g in lines if g[0] == 'QUEUE'), None)
        results.append(queue == 'BlockLog$1')
        print(f'queue: the system queue is {queue}: {"ok" if results[-1] else "FAIL"}', flush=True)
    except RuntimeError as e:
        results.append(False)
        print(f'FAIL {e}', flush=True)
    finally:
        jvm.kill()
    print(f'{"PASS" if all(results) else "FAIL"}: {sum(results)} of {len(results)} phases')
    return 0 if all(results) else 1


if __name__ == '__main__':
    if sys.argv[1] == '--holder':
        holder(sys.argv[2], int(sys.argv[3]))
    else:
        sys.exit(main(sys.argv[1], os.path.abspath(sys.argv[2])))
