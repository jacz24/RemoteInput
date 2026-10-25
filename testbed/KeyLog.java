import java.awt.AWTEvent;
import java.awt.Canvas;
import java.awt.EventQueue;
import java.awt.Frame;
import java.awt.Toolkit;
import java.awt.event.KeyEvent;
import java.util.Scanner;

// The client of probe_lost_events.py (Simba-Reflection #74, after #62's) and probe_key_repeat.py (#17): a hidden Frame
// with a Canvas, and a pushed EventQueue that prints each KeyEvent RemoteInput posts, with its arrival in µs
// (nanoTime: currentTimeMillis steps by the 15.6 ms timer tick on Windows). The queue sees each event before the focus
// manager, so a real mouse or keyboard at the PC cannot take one away.
public class KeyLog {
    public static void main(String[] args) throws Exception {
        Toolkit.getDefaultToolkit().getSystemEventQueue().push(new EventQueue() {
            @Override
            protected void dispatchEvent(AWTEvent e) {
                if (e instanceof KeyEvent) {
                    KeyEvent k = (KeyEvent) e;
                    System.out.println("KEY " + k.getID() + " " + k.getKeyCode() + " " + (int) k.getKeyChar() + " " + k.getKeyLocation()
                        + " " + System.nanoTime() / 1000);
                    System.out.flush();
                }
                super.dispatchEvent(e);
            }
        });
        // A THREADS line on stdin prints the name of a new, unstarted Thread: its number counts each anonymous thread and
        // each attach with no name since the last line, plus the line's own (P14, Simba-AI #484).
        Thread reader = new Thread(() -> {
            Scanner in = new Scanner(System.in);
            while (in.hasNextLine()) {
                if (in.nextLine().equals("THREADS")) {
                    System.out.println("THREADS " + new Thread().getName());
                    System.out.flush();
                }
            }
        }, "stdin");
        reader.setDaemon(true);
        reader.start();
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
