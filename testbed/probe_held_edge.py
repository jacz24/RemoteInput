"""Simba-Reflection #253's probe (Windows): a held button's move is a drag on either side of the canvas's edge, as the
Windows peer's capture of the mouse sends each move to the canvas; the exit and the entry each come before their drag
(gate 10's held-edge steps, java.awt.Robot). No drag posts a click, and a release outside lands at its own point.

probe_same_point.py's session: MoveLog.java in a hidden JVM, the DLL injected and paired, each step through the plugin's
exports, its events the POST lines. The canvas is 400 x 300; Simba 2's buttons: 1 left, 2 middle, 3 right.

  first          a move to (100, 100): the entry and a move
  edge-*         the buttons held, then (500, 100) outside and back to (100, 100), released last pressed first
  out-*          the same, released at (500, 100), then back to (100, 100): the entry and a move, no button
  for left, middle, right, and both (left, then right)

usage: javac --release 11 -d testbed/out testbed/MoveLog.java
       python -I testbed/probe_held_edge.py <java.exe> <libRemoteInput.dll>
Exit 0 only when each step posted exactly its events.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # -I leaves the script's folder off the path
from probe_same_point import session   # noqa: E402

MASK = {1: 1024, 2: 2048, 3: 4096}   # BUTTON1_DOWN_MASK, BUTTON2_DOWN_MASK, BUTTON3_DOWN_MASK
CASES = {'left': (1,), 'middle': (2,), 'right': (3,), 'both': (1, 3)}


def held(lib, eios, move, buttons, outside):
    """The actions and the events of one case: the presses at (100, 100), the exit and the drag at (500, 100), then
    the entry and the drag back at (100, 100) and the releases, or the releases outside and the entry and a move."""
    actions, want, mask = [], [], 0
    for b in buttons:
        mask |= MASK[b]
        actions.append(lambda b=b: lib.EIOS_HoldMouse(eios, 0, 0, b))
        want.append(f'PRESSED 100 100 {b} 1 {mask}')
    actions.append(move((500, 100)))
    want += [f'EXITED 500 100 0 0 {mask}', f'DRAGGED 500 100 0 0 {mask}']
    back = [move((100, 100))]
    if not outside:
        actions += back
        want += [f'ENTERED 100 100 0 0 {mask}', f'DRAGGED 100 100 0 0 {mask}']
    release_x = 500 if outside else 100
    for b in reversed(buttons):
        mask &= ~MASK[b]
        actions.append(lambda b=b: lib.EIOS_ReleaseMouse(eios, 0, 0, b))
        want.append(f'RELEASED {release_x} 100 {b} 1 {mask}')
    if outside:
        actions += back
        want += ['ENTERED 100 100 0 0 0', 'MOVED 100 100 0 0 0']
    return actions, want


def steps(lib, eios, move, size):
    return [('first', [move((100, 100))], ['ENTERED 100 100 0 0 0', 'MOVED 100 100 0 0 0'])] + [
        (f'{kind}-{name}', *held(lib, eios, move, buttons, kind == 'out'))
        for kind in ('edge', 'out') for name, buttons in CASES.items()]


def main(java, dll):
    failed, total = session(java, dll, steps)
    print(f'{"PASS" if not failed else "FAIL"}: {total - failed} of {total} steps posted their events')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], os.path.abspath(sys.argv[2])))
