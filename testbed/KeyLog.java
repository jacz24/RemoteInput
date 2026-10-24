import java.awt.AWTEvent;
import java.awt.Canvas;
import java.awt.EventQueue;
import java.awt.Frame;
import java.awt.Toolkit;
import java.awt.event.KeyEvent;

// The client of probe_lost_events.py (Simba-Reflection #74, after #62's): a hidden Frame with a Canvas, and a pushed
// EventQueue that prints each KeyEvent RemoteInput posts. The queue sees each event before the focus manager, so a real
// mouse or keyboard at the PC cannot take one away.
public class KeyLog {
    public static void main(String[] args) throws Exception {
        Toolkit.getDefaultToolkit().getSystemEventQueue().push(new EventQueue() {
            @Override
            protected void dispatchEvent(AWTEvent e) {
                if (e instanceof KeyEvent) {
                    KeyEvent k = (KeyEvent) e;
                    System.out.println("KEY " + k.getID() + " " + k.getKeyCode() + " " + (int) k.getKeyChar() + " " + k.getKeyLocation());
                    System.out.flush();
                }
                super.dispatchEvent(e);
            }
        });
        Frame frame = new Frame("probe-74");
        // RemoteInput's has_focus calls hasFocus(): true skips the regain, which a hidden canvas cannot finish.
        frame.add(new Canvas() {
            @Override
            public boolean hasFocus() { return true; }
        });
        frame.pack();
        System.out.println("READY " + ProcessHandle.current().pid() + " " + System.getProperty("java.version"));
        System.out.flush();
        Thread.sleep(60_000);
        System.exit(0);
    }
}
