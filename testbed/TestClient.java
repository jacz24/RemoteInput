// Owned application-under-test for RemoteInput.
// It copies nothing from RuneLite. It only exposes the three surfaces RemoteInput touches,
// so the whole pipeline can be exercised against a target we own:
//   1. Reflection read  -> public static STATE, with a nested object (Reflection.cxx walks fields by name).
//   2. Input injection   -> key/mouse listeners record the last event into STATE and draw it (Injection/).
//   3. Frame capture      -> a Canvas drawn through a BufferStrategy, i.e. the software blit the Mac hook grabs (Hooks/).
//
// Field map for the driver (class TestClient, default package):
//   STATE.tick                int, increments every frame
//   STATE.lastKeyCode         int   STATE.lastKeyChar  char
//   STATE.mouseX STATE.mouseY STATE.mouseButton  int
//   STATE.nested.value        int, starts at 42
// Known pixels for a capture assert: a red square fills (50,50)-(90,90) on a dark-gray background.
//
// Pass "-c" as a program argument: on Mac the library in Plugin.cxx activates only when the
// process argv holds a "-c" token (otherwise it treats the process as the RuneLite launcher and stays inert).

import java.awt.*;
import java.awt.event.*;
import java.awt.image.BufferedImage;

public class TestClient {
    public static final class Nested {
        public volatile int value = 42;
    }

    public static final class GameState {
        public volatile int tick = 0;
        public volatile int lastKeyCode = -1;
        public volatile char lastKeyChar = 0;
        public volatile int mouseX = -1;
        public volatile int mouseY = -1;
        public volatile int mouseButton = -1;
        public final Nested nested = new Nested();
    }

    public static final GameState STATE = new GameState();

    public static void main(String[] args) {
        System.out.println("main start, pid=" + ProcessHandle.current().pid() + ", headless=" + java.awt.GraphicsEnvironment.isHeadless());
        System.out.flush();
        final int w = 765, h = 503; // a fixed, RuneLite-like client size

        Frame frame = new Frame("RemoteInput Test Client");
        Canvas canvas = new Canvas();
        canvas.setSize(w, h);
        frame.add(canvas);
        frame.pack();
        frame.setResizable(false);
        frame.addWindowListener(new WindowAdapter() {
            @Override public void windowClosing(WindowEvent e) { System.exit(0); }
        });
        frame.setVisible(true);
        System.out.println("window shown: " + frame.isShowing() + " size " + canvas.getWidth() + "x" + canvas.getHeight());
        System.out.flush();

        canvas.setFocusable(true);
        canvas.requestFocus();
        canvas.addKeyListener(new KeyAdapter() {
            @Override public void keyPressed(KeyEvent e) {
                STATE.lastKeyCode = e.getKeyCode();
                STATE.lastKeyChar = e.getKeyChar();
                System.out.println("RECV key code=" + e.getKeyCode() + " char='" + e.getKeyChar() + "'");
                System.out.flush();
            }
        });
        // Log every mouse event the canvas gets, with its extended modifiers (probe_modifiers.sh reads these lines).
        Toolkit.getDefaultToolkit().addAWTEventListener(e -> {
            MouseEvent m = (MouseEvent) e;
            if (m.getSource() == canvas) {
                System.out.println("RECV " + m.paramString() + ",modsEx=" + m.getModifiersEx());
                System.out.flush();
            }
        }, AWTEvent.MOUSE_EVENT_MASK | AWTEvent.MOUSE_MOTION_EVENT_MASK);
        canvas.addMouseListener(new MouseAdapter() {
            @Override public void mousePressed(MouseEvent e) {
                STATE.mouseX = e.getX();
                STATE.mouseY = e.getY();
                STATE.mouseButton = e.getButton();
            }
        });
        canvas.addMouseMotionListener(new MouseMotionAdapter() {
            @Override public void mouseMoved(MouseEvent e) {
                STATE.mouseX = e.getX();
                STATE.mouseY = e.getY();
            }
        });

        // Render like RuneLite's software mode: draw a BufferedImage, then blit it onto the canvas.
        // That drawImage is the software blit the Mac hook (JavaNativeBlit) intercepts; a BufferStrategy
        // flip is not, so the hook would never fire and the capture would stay black.
        BufferedImage img = new BufferedImage(w, h, BufferedImage.TYPE_INT_RGB);
        // A software image->image blit, which goes through sun.java2d.loops.Blit.Blit -- the primitive
        // RemoteInput captures. A blit to the on-screen canvas is GPU-backed on macOS and bypasses it.
        BufferedImage offscreen = new BufferedImage(w, h, BufferedImage.TYPE_INT_RGB);

        while (true) {
            STATE.tick++;
            Graphics g = img.createGraphics();
            try {
                g.setColor(Color.DARK_GRAY);
                g.fillRect(0, 0, w, h);
                g.setColor(Color.RED);
                g.fillRect(50, 50, 40, 40); // known pixels for a capture assert
                // Input markers: prove injected input from the captured frame alone (no reflection needed).
                if (STATE.mouseX >= 0) {
                    g.setColor(Color.GREEN);
                    g.fillRect(STATE.mouseX - 5, STATE.mouseY - 5, 11, 11); // a green square at the injected mouse point
                }
                g.setColor(new Color(STATE.lastKeyChar & 0xFF, 0, 128)); // red channel == last injected key char
                g.fillRect(150, 50, 20, 20);
                g.setColor(Color.WHITE);
                g.drawString("tick=" + STATE.tick, 10, 20);
                g.drawString("nested.value=" + STATE.nested.value, 10, 40);
                char c = STATE.lastKeyChar >= 32 ? STATE.lastKeyChar : ' ';
                g.drawString("key=" + STATE.lastKeyCode + " '" + c + "'", 10, 110);
                g.drawString("mouse=(" + STATE.mouseX + "," + STATE.mouseY + ") btn=" + STATE.mouseButton, 10, 125);
            } finally {
                g.dispose();
            }
            Graphics og = offscreen.getGraphics();
            og.drawImage(img, 0, 0, null); // software image->image blit -> the Mac hook captures the frame here
            og.dispose();
            Graphics cg = canvas.getGraphics();
            if (cg != null) {
                cg.drawImage(img, 0, 0, null); // on-screen display (GPU-backed on macOS; not the capture path)
                cg.dispose();
            }
            Toolkit.getDefaultToolkit().sync();
            try { Thread.sleep(33); } catch (InterruptedException ignored) { return; }
        }
    }
}
