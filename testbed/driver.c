// End-to-end driver for the owned test target. It is the EIOS client side.
// It dlopen's the already-built libRemoteInput, pairs to the injected target by PID, captures
// the frame, and injects input. It asserts from the captured pixels, so it needs no reflection API.
//
// Build:  cc -o testbed/out/driver testbed/driver.c
// Run:    testbed/out/driver <pid> <dylib-path> [modifiers|focus]
//
// Checks: target dimensions, a red capture square, an injected mouse point (green), an injected key.
// "modifiers" and "focus" also inject the input of probe_modifiers.sh or probe_focus.sh after the checks.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

typedef void EIOS;

static void nap(int ms) {
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

#define BIND(var, name) do { \
    *(void**)(&var) = dlsym(h, name); \
    if (!var) { fprintf(stderr, "missing symbol %s\n", name); return 2; } \
} while (0)

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <pid> <dylib> [modifiers|focus]\n", argv[0]); return 2; }
    int pid = atoi(argv[1]);

    void* h = dlopen(argv[2], RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 2; }

    EIOS* (*PairClient)(int32_t);
    void (*GetTargetDimensions)(EIOS*, int32_t*, int32_t*);
    void (*SetImageFormat)(EIOS*, uint32_t);
    uint8_t* (*GetImageBuffer)(EIOS*);
    void (*UpdateImageBuffer)(EIOS*);
    void (*GainFocus)(EIOS*);
    void (*LoseFocus)(EIOS*);
    void (*SetMouseInputEnabled)(EIOS*, bool);
    void (*SetKeyboardInputEnabled)(EIOS*, bool);
    void (*MoveMouse)(EIOS*, int32_t, int32_t);
    void (*HoldMouse)(EIOS*, int32_t, int32_t, int32_t);
    void (*ReleaseMouse)(EIOS*, int32_t, int32_t, int32_t);
    void (*SendString)(EIOS*, const char*, int32_t, int32_t);

    BIND(PairClient, "EIOS_PairClient");
    BIND(GetTargetDimensions, "EIOS_GetTargetDimensions");
    BIND(SetImageFormat, "EIOS_SetImageFormat");
    BIND(GetImageBuffer, "EIOS_GetImageBuffer");
    BIND(UpdateImageBuffer, "EIOS_UpdateImageBuffer");
    BIND(GainFocus, "EIOS_GainFocus");
    BIND(LoseFocus, "EIOS_LoseFocus");
    BIND(SetMouseInputEnabled, "EIOS_SetMouseInputEnabled");
    BIND(SetKeyboardInputEnabled, "EIOS_SetKeyboardInputEnabled");
    BIND(MoveMouse, "EIOS_MoveMouse");
    BIND(HoldMouse, "EIOS_HoldMouse");
    BIND(ReleaseMouse, "EIOS_ReleaseMouse");
    BIND(SendString, "EIOS_SendString");

    // The library initializes on a detached thread after the JVM is up; wait for the pairing.
    EIOS* eios = NULL;
    for (int i = 0; i < 50 && !eios; ++i) { eios = PairClient(pid); if (!eios) nap(200); }
    if (!eios) { fprintf(stderr, "FAIL: could not pair to pid %d\n", pid); return 1; }
    printf("PASS: paired to pid %d\n", pid);

    int w = 0, hgt = 0;
    GetTargetDimensions(eios, &w, &hgt);
    printf("%s: dimensions %dx%d\n", (w == 765 && hgt == 503) ? "PASS" : "FAIL", w, hgt);

    SetImageFormat(eios, 1); // BGRA

    int fails = (w == 765 && hgt == 503) ? 0 : 1;

    #define PX(buf, x, y) ((buf) + (((y) * w + (x)) * 4))  // BGRA: [0]=B [1]=G [2]=R

    // Capture check: the red square at (50,50)-(90,90), and dark-gray background away from markers.
    UpdateImageBuffer(eios);
    uint8_t* buf = GetImageBuffer(eios);
    if (!buf) { fprintf(stderr, "FAIL: null image buffer\n"); return 1; }
    uint8_t* red = PX(buf, 60, 60);
    bool red_ok = red[2] > 200 && red[1] < 60 && red[0] < 60;
    printf("%s: capture red square B=%d G=%d R=%d\n", red_ok ? "PASS" : "FAIL", red[0], red[1], red[2]);
    fails += !red_ok;

    uint8_t* bg = PX(buf, 400, 400);
    bool bg_ok = bg[0] > 40 && bg[0] < 100 && bg[1] > 40 && bg[1] < 100 && bg[2] > 40 && bg[2] < 100;
    printf("%s: capture background B=%d G=%d R=%d\n", bg_ok ? "PASS" : "FAIL", bg[0], bg[1], bg[2]);
    fails += !bg_ok;

    // Input: enable, focus, inject a mouse point and a key, then re-capture and assert the markers.
    GainFocus(eios);
    SetMouseInputEnabled(eios, true);
    SetKeyboardInputEnabled(eios, true);

    MoveMouse(eios, 300, 200);
    HoldMouse(eios, 300, 200, 1);
    ReleaseMouse(eios, 300, 200, 1);
    SendString(eios, "k", 80, 80); // 'k' == 107, lands in the key box red channel
    nap(300); // let the target repaint

    UpdateImageBuffer(eios);
    buf = GetImageBuffer(eios);
    uint8_t* grn = PX(buf, 300, 200);
    bool grn_ok = grn[1] > 200 && grn[2] < 60 && grn[0] < 60;
    printf("%s: injected mouse marker B=%d G=%d R=%d\n", grn_ok ? "PASS" : "FAIL", grn[0], grn[1], grn[2]);
    fails += !grn_ok;

    uint8_t* keybox = PX(buf, 160, 60);
    bool key_ok = keybox[2] == 107; // red channel == 'k'
    printf("%s: injected key box red=%d (want 107)\n", key_ok ? "PASS" : "FAIL", keybox[2]);
    fails += !key_ok;

    // "modifiers" mode (Simba-AI #496): probe_modifiers.sh checks, in the target's log, the events this input makes.
    if (argc > 3 && strcmp(argv[3], "modifiers") == 0) {
        // A nap after each call: AWT's EventQueue merges a pending MOUSE_MOVED or MOUSE_DRAGGED into the next one.
        const int32_t drags[3][2] = { {1, 310}, {3, 320}, {2, 330} }; // button, x where the drag ends
        for (int i = 0; i < 3; ++i) {
            MoveMouse(eios, 300, 200);                          nap(50);
            HoldMouse(eios, 300, 200, drags[i][0]);             nap(50);
            MoveMouse(eios, drags[i][1], 205);                  nap(50);
            ReleaseMouse(eios, drags[i][1], 205, drags[i][0]);  nap(50);
        }
        MoveMouse(eios, w - 1, 250);  nap(50);
        MoveMouse(eios, w, 250);      nap(50);
        MoveMouse(eios, 300, 200);    nap(50);
        MoveMouse(eios, 300, hgt);
        nap(500); // let the target log the events before e2e.sh stops it
    }

    // "focus" mode: probe_focus.sh checks the age of a press that has to regain focus first.
    if (argc > 3 && strcmp(argv[3], "focus") == 0) {
        MoveMouse(eios, 310, 210);         nap(50);
        LoseFocus(eios);                   nap(50);
        HoldMouse(eios, 310, 210, 1);      nap(50);
        ReleaseMouse(eios, 310, 210, 1);
        nap(500);
    }

    printf(fails ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", fails);
    return fails ? 1 : 0;
}
