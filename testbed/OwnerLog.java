import java.awt.AWTEvent;
import java.awt.Canvas;
import java.awt.EventQueue;
import java.awt.Frame;
import java.awt.Toolkit;
import java.awt.event.FocusEvent;
import java.awt.event.KeyEvent;
import java.awt.event.MouseEvent;
import java.util.Scanner;

// The client of probe_owner_loss.py (Simba-Reflection #254): a hidden Frame with a 400 x 300 Canvas, and a pushed
// EventQueue that prints each MouseEvent (M) and KeyEvent (K) as it is posted, with its when, the poster's clock in ms.
// Hidden, the window takes no focus and no OS input from the PC's user. A SLOW ms line on stdin holds the thread that
// posts the next KeyEvent ms after its line: RemoteInput's command thread, so the command stays in flight. An UNFOCUS
// line makes the canvas lack focus until a FOCUS_GAINED is posted, as RemoteInput's regain posts one.
public class OwnerLog {
    static volatile int slowMs;
    static volatile boolean focused = true;

    public static void main(String[] args) throws Exception {
        Toolkit.getDefaultToolkit().getSystemEventQueue().push(new EventQueue() {
            @Override
            public void postEvent(AWTEvent e) {
                print(e);
                if (e.getID() == FocusEvent.FOCUS_GAINED) {
                    focused = true;
                }
                if (e instanceof KeyEvent && slowMs > 0) {
                    int ms = slowMs;
                    slowMs = 0;
                    try {
                        Thread.sleep(ms);
                    } catch (InterruptedException ignored) {
                    }
                }
                super.postEvent(e);
            }
        });
        Canvas canvas = new Canvas() {
            // RemoteInput's has_focus calls hasFocus(): true skips the regain, which a hidden canvas cannot finish alone.
            @Override
            public boolean hasFocus() { return focused; }
        };
        Frame frame = new Frame("probe-254");
        frame.setLayout(null);
        frame.add(canvas);
        frame.pack();
        canvas.setBounds(0, 0, 400, 300);
        Thread reader = new Thread(() -> {
            Scanner in = new Scanner(System.in);
            while (in.hasNextLine()) {
                String[] cmd = in.nextLine().split(" ");
                if (cmd[0].equals("SLOW")) {
                    slowMs = Integer.parseInt(cmd[1]);
                } else if (cmd[0].equals("UNFOCUS")) {
                    focused = false;
                }
            }
        }, "stdin");
        reader.setDaemon(true);
        reader.start();
        System.out.println("READY " + ProcessHandle.current().pid() + " " + System.getProperty("java.version"));
        System.out.flush();
        Thread.sleep(120_000);
        System.exit(0);
    }

    static synchronized void print(AWTEvent e) {
        if (e instanceof MouseEvent) {
            MouseEvent m = (MouseEvent) e;
            System.out.println("M " + m.getID() + " " + m.getButton() + " " + m.getX() + " " + m.getY() + " "
                + m.getModifiersEx() + " " + m.getWhen());
        } else if (e instanceof KeyEvent) {
            KeyEvent k = (KeyEvent) e;
            System.out.println("K " + k.getID() + " " + k.getKeyCode() + " " + (int) k.getKeyChar() + " "
                + k.getModifiersEx() + " " + k.getWhen());
        } else {
            return;
        }
        System.out.flush();
    }
}
