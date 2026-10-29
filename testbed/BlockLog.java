import java.awt.AWTEvent;
import java.awt.Canvas;
import java.awt.Dimension;
import java.awt.EventQueue;
import java.awt.Frame;
import java.awt.Toolkit;
import java.awt.event.KeyEvent;
import java.awt.event.MouseEvent;
import java.awt.event.MouseWheelEvent;
import java.util.Scanner;

// The client of probe_block.py (Simba-Reflection #13): a hidden Frame with a 200 x 200 Canvas, and a pushed EventQueue
// that prints each mouse, wheel and key event as it dispatches it, as KeyLog.java prints keys. The queue sees each event
// before the focus manager, so a hidden window still shows each key. Hidden, it takes no focus from the PC's user. A
// QUEUE line on stdin prints the system queue's class: still this file's own when RemoteInput pushed none.
public class BlockLog {
    public static void main(String[] args) throws Exception {
        Toolkit.getDefaultToolkit().getSystemEventQueue().push(new EventQueue() {
            @Override
            protected void dispatchEvent(AWTEvent e) {
                if (e instanceof MouseWheelEvent) {
                    MouseWheelEvent w = (MouseWheelEvent) e;
                    System.out.println("WHEEL " + w.getID() + " " + w.getX() + " " + w.getY() + " " + w.getWheelRotation());
                } else if (e instanceof MouseEvent) {
                    MouseEvent m = (MouseEvent) e;
                    System.out.println("MOUSE " + m.getID() + " " + m.getX() + " " + m.getY());
                } else if (e instanceof KeyEvent) {
                    KeyEvent k = (KeyEvent) e;
                    System.out.println("KEY " + k.getID() + " " + k.getKeyCode() + " " + (int) k.getKeyChar());
                }
                System.out.flush();
                super.dispatchEvent(e);
            }
        });
        Thread reader = new Thread(() -> {
            Scanner in = new Scanner(System.in);
            while (in.hasNextLine()) {
                if (in.nextLine().equals("QUEUE")) {
                    System.out.println("QUEUE " + Toolkit.getDefaultToolkit().getSystemEventQueue().getClass().getName());
                    System.out.flush();
                }
            }
        }, "stdin");
        reader.setDaemon(true);
        reader.start();
        Frame frame = new Frame("probe-13");
        // RemoteInput's has_focus calls hasFocus(): true skips the regain, which a hidden canvas cannot finish.
        frame.add(new Canvas() {
            @Override
            public boolean hasFocus() { return true; }

            @Override
            public Dimension getPreferredSize() { return new Dimension(200, 200); }
        });
        frame.pack();
        System.out.println("READY " + ProcessHandle.current().pid() + " " + System.getProperty("java.version"));
        System.out.flush();
        Thread.sleep(120_000);
        System.exit(0);
    }
}
