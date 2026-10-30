import java.awt.AWTEvent;
import java.awt.Canvas;
import java.awt.EventQueue;
import java.awt.Frame;
import java.awt.Toolkit;
import java.awt.event.KeyEvent;
import java.util.Scanner;

// The client of probe_lost_events.py (Simba-Reflection #74, after #62's) and probe_key_repeat.py (#17): a hidden Frame
// (shown with the argument show, for a real keyboard, #142)
// with a Canvas, and a pushed EventQueue that prints each KeyEvent RemoteInput posts, with its arrival in µs
// (nanoTime: currentTimeMillis steps by the 15.6 ms timer tick on Windows) and its when, the poster's clock in ms. The
// queue sees each event before the focus manager, so a real mouse or keyboard at the PC cannot take one away.
public class KeyLog {
    // A POST_MS line on stdin holds the thread that posted the first key event that many ms after each key event it posts
    // (#104): the command thread, as each repeat task attaches a thread of its own. The pushed queue is the system queue.
    static volatile int postMs;
    static volatile Thread commandThread;

    public static void main(String[] args) throws Exception {
        Toolkit.getDefaultToolkit().getSystemEventQueue().push(new EventQueue() {
            @Override
            public void postEvent(AWTEvent e) {
                super.postEvent(e);
                if (e instanceof KeyEvent) {
                    if (commandThread == null) {
                        commandThread = Thread.currentThread();
                    }
                    if (postMs > 0 && Thread.currentThread() == commandThread) {
                        try {
                            Thread.sleep(postMs);
                        } catch (InterruptedException ignored) {
                        }
                    }
                }
            }

            @Override
            protected void dispatchEvent(AWTEvent e) {
                if (e instanceof KeyEvent) {
                    KeyEvent k = (KeyEvent) e;
                    System.out.println("KEY " + k.getID() + " " + k.getKeyCode() + " " + (int) k.getKeyChar() + " " + k.getKeyLocation()
                        + " " + System.nanoTime() / 1000 + " " + k.getWhen());
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
                String line = in.nextLine();
                if (line.equals("THREADS")) {
                    System.out.println("THREADS " + new Thread().getName());
                    System.out.flush();
                } else if (line.startsWith("POST_MS ")) {
                    postMs = Integer.parseInt(line.substring("POST_MS ".length()));
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
        if (args.length > 0 && args[0].equals("show")) {   // probe_key_repeat.py --keyboard: the operator's own keys
            frame.setSize(400, 200);
            frame.setVisible(true);
        }
        System.out.println("READY " + ProcessHandle.current().pid() + " " + System.getProperty("java.version"));
        System.out.flush();
        Thread.sleep(120_000);   // probe_key_repeat.py's phases take about 51 s
        System.exit(0);
    }
}
