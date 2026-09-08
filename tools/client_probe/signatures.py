"""What the probe looks for.

Edit this file to teach the probe about a new toolkit or graphics API.
Nothing else needs to change.

Each entry maps a label to the symbol names and the module name fragments
that prove the client uses it. Module fragments match case-insensitively
against the full path of a loaded module.
"""

from typing import Dict, Tuple

# ---------------------------------------------------------------- capture

CAPTURE_SYMBOLS: Dict[str, Tuple[str, ...]] = {
    "Vulkan": (
        "vkQueuePresentKHR",
        "vkCreateSwapchainKHR",
        "vkGetSwapchainImagesKHR",
        "vkAcquireNextImageKHR",
        "vkCmdCopyImage",
    ),
    "Metal": (
        "MTLCreateSystemDefaultDevice",
        "presentDrawable",
        "nextDrawable",
        "CAMetalLayer",
    ),
    "Direct3D": (
        "D3D11CreateDeviceAndSwapChain",
        "D3D12CreateDevice",
        "Direct3DCreate9",
        "CreateDXGIFactory",
    ),
    "OpenGL": (
        "wglSwapBuffers",
        "glXSwapBuffers",
        "CGLFlushDrawable",
        "glReadPixels",
        "glGenBuffers",
        "eglSwapBuffers",
    ),
}

CAPTURE_MODULES: Dict[str, Tuple[str, ...]] = {
    "Vulkan": ("libvulkan", "vulkan-1.dll", "moltenvk"),
    "Metal": ("metal.framework", "metalkit.framework"),
    "Direct3D": ("d3d9.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll"),
    "OpenGL": ("libgl.so", "opengl32.dll", "opengl.framework", "libegl"),
}

# The function to hook for each capture path, and the cost of doing it.
CAPTURE_PLAN: Dict[str, Tuple[str, str]] = {
    "Vulkan": (
        "vkQueuePresentKHR  (then copy the swapchain image to a staging buffer)",
        "2-4 weeks",
    ),
    "Metal": (
        "-[MTLCommandBuffer presentDrawable:]  or  -[CAMetalLayer nextDrawable]",
        "2-4 weeks",
    ),
    "Direct3D": (
        "IDXGISwapChain::Present  or  IDirect3DDevice9::EndScene",
        "1-2 weeks (RemoteInput already has a Direct-X renderer to copy)",
    ),
    "OpenGL": (
        "wglSwapBuffers / glXSwapBuffers / CGLFlushDrawable, then read back via PBO",
        "days (RemoteInput already has this code)",
    ),
}

# Priority order. The first label with evidence wins.
CAPTURE_PRIORITY: Tuple[str, ...] = ("Vulkan", "Metal", "Direct3D", "OpenGL")

# ------------------------------------------------------------------ input

INPUT_SYMBOLS: Dict[str, Tuple[str, ...]] = {
    "Raw input": (
        "GetRawInputData",
        "RegisterRawInputDevices",
        "GetAsyncKeyState",
        "GetKeyboardState",
        "DirectInput8Create",
        "XInputGetState",
        "IOHIDManagerCreate",
        "IOHIDDeviceOpen",
        "CGEventSourceKeyState",
        "SDL_SetRelativeMouseMode",
        "SDL_GetRelativeMouseState",
        "libevdev_new_from_fd",
        "XIQueryDevice",
    ),
    "Toolkit": (
        "SDL_PollEvent",
        "SDL_PumpEvents",
        "SDL_PushEvent",
        "SDL_WaitEvent",
        "glfwPollEvents",
        "glfwSetKeyCallback",
        "glfwSetCursorPosCallback",
        "gtk_main",
    ),
    "Message loop": (
        "PeekMessageW",
        "PeekMessageA",
        "GetMessageW",
        "DispatchMessageW",
        "TranslateMessage",
        "DefWindowProcW",
        "XNextEvent",
        "XPending",
        "xcb_poll_for_event",
        "nextEventMatchingMask",
        "sendEvent:",
    ),
}

INPUT_MODULES: Dict[str, Tuple[str, ...]] = {
    "Toolkit": ("libsdl", "sdl2.dll", "libglfw", "glfw3.dll", "libgtk"),
    # IOKit.framework is deliberately absent. Nearly every macOS app loads it
    # for reasons unrelated to input, so it would be a misleading signal.
    # Raw input on macOS is proved by its symbols, not by a loaded module.
    "Raw input": ("dinput8.dll", "xinput", "hidapi"),
    "Message loop": ("user32.dll", "libx11", "libxcb", "appkit.framework"),
}

# Labels where a missing module really does suggest static linkage. System
# APIs such as AppKit and IOKit are never linked statically, so a missing
# module there proves nothing.
STATIC_LINK_CANDIDATES: Tuple[str, ...] = ("Toolkit", "Vulkan", "OpenGL")

# How to drive input for each path, and the cost.
INPUT_PLAN: Dict[str, Tuple[str, str, str]] = {
    "Toolkit": (
        "Hook SDL_PollEvent / SDL_PumpEvents and push your own SDL_Event",
        "days",
        "low",
    ),
    "Message loop": (
        "PostMessage the target window (Windows) or CGEventPostToPid (macOS)",
        "days",
        "medium",
    ),
    "Raw input": (
        "Hook GetRawInputData / GetAsyncKeyState / IOHIDManager inside the process",
        "weeks, and it breaks on client updates",
        "high",
    ),
}

INPUT_PRIORITY: Tuple[str, ...] = ("Toolkit", "Message loop", "Raw input")

# ------------------------------------------------------------- anti-tamper

TAMPER_SYMBOLS: Tuple[str, ...] = (
    "IsDebuggerPresent",
    "CheckRemoteDebuggerPresent",
    "NtQueryInformationProcess",
    "ptrace",
    "csops",
    "task_for_pid",
    "mach_vm_read",
    "dyld_image_count",
)

# macOS entitlement that must be present for a dylib injection to survive
# the hardened runtime.
LIBRARY_VALIDATION_OPT_OUT = "com.apple.security.cs.disable-library-validation"


def all_symbols() -> Tuple[str, ...]:
    """Every name the byte scan should look for, without duplicates."""
    seen = []
    for table in (CAPTURE_SYMBOLS, INPUT_SYMBOLS):
        for names in table.values():
            seen.extend(names)
    seen.extend(TAMPER_SYMBOLS)
    return tuple(dict.fromkeys(seen))
