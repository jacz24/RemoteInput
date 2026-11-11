import java.awt.AWTEvent;
import java.awt.Canvas;
import java.awt.EventQueue;
import java.awt.Frame;
import java.awt.Toolkit;
import java.awt.event.MouseEvent;
import java.util.Scanner;

// The client of probe_same_point.py (Simba-Reflection #252): a hidden Frame with a 400 x 300 Canvas, and a pushed
// EventQueue that prints each MouseEvent twice: POST as it is posted (RemoteInput calls the system queue's postEvent;
// AWT's own input reaches this queue past that method), before the queue can coalesce two moves, and DISPATCH as it is
// dispatched, from either source. Hidden, the window takes no focus and no OS mouse input from the PC's user. A SIZE w h
// line on stdin resizes the canvas on the EDT, as a client's layout does, and prints SIZED.
public class MoveLog {
    public static void main(String[] args) throws Exception {
        Toolkit.getDefaultToolkit().getSystemEventQueue().push(new EventQueue() {
            @Override
            public void postEvent(AWTEvent e) {
                print("POST", e);
                super.postEvent(e);
            }

            @Override
            protected void dispatchEvent(AWTEvent e) {
                print("DISPATCH", e);
                super.dispatchEvent(e);
            }
        });
        Canvas canvas = new Canvas() {
            // RemoteInput's has_focus calls hasFocus(): true skips the regain, which a hidden canvas cannot finish.
            @Override
            public boolean hasFocus() { return true; }
        };
        Frame frame = new Frame("probe-252");
        frame.setLayout(null);   // no layout: only a SIZE line sizes the canvas
        frame.add(canvas);
        frame.pack();
        canvas.setBounds(0, 0, 400, 300);
        Thread reader = new Thread(() -> {
            Scanner in = new Scanner(System.in);
            while (in.hasNextLine()) {
                String[] cmd = in.nextLine().split(" ");
                if (cmd[0].equals("SIZE")) {
                    try {
                        EventQueue.invokeAndWait(() -> canvas.setSize(Integer.parseInt(cmd[1]), Integer.parseInt(cmd[2])));
                    } catch (Exception ex) {
                        throw new RuntimeException(ex);
                    }
                    System.out.println("SIZED " + canvas.getWidth() + " " + canvas.getHeight());
                    System.out.flush();
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

    // The fields gate 10 compares (Simba-Reflection tools/gate10.py): x, y, button, click count and the extended mask.
    static synchronized void print(String where, AWTEvent e) {
        if (e instanceof MouseEvent) {
            MouseEvent m = (MouseEvent) e;
            System.out.println(where + " " + m.getID() + " " + m.getX() + " " + m.getY() + " " + m.getButton() + " "
                + m.getClickCount() + " " + m.getModifiersEx());
            System.out.flush();
        }
    }
}
