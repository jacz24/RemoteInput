"""Tests for the pure parse and classify layers of client_probe.

The collect layer shells out to platform tools and is not covered here.
Everything below is a pure function: text or structured input, value output.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from client_probe.classify import (
    classify_capture,
    classify_injection,
    classify_input,
    detect_static_linkage,
)
from client_probe.model import Findings
from client_probe.parse import (
    parse_file_arch,
    parse_nm_undefined,
    parse_objdump_dynamic,
    parse_otool_l,
    parse_powershell_modules,
    parse_proc_maps,
    parse_vmmap,
    scan_bytes_for_symbols,
)


VMMAP_SAMPLE = """\
==== Non-writable regions for process 4242
REGION TYPE                 START - END
__TEXT  184994000-1849e7000  [ 332K ] r-x/r-x SM=COW  /usr/lib/libobjc.A.dylib
__TEXT  1849e7000-184a1c000  [ 212K ] r-x/r-x SM=COW  /usr/lib/libvulkan.1.dylib
__TEXT  184ad0000-184ad4000  [  16K ] r-x/r-x SM=COW  /opt/game/libSDL2-2.0.0.dylib
__DATA  184b29000-184b4a000  [ 132K ] rw-/rw- SM=COW  /usr/lib/libobjc.A.dylib
"""

PROC_MAPS_SAMPLE = """\
55d3f0a00000-55d3f0a21000 r-xp 00000000 08:01 1234  /opt/game/client
7f2b1c000000-7f2b1c021000 r-xp 00000000 08:01 5678  /usr/lib/libGL.so.1
7f2b1d000000-7f2b1d021000 r-xp 00000000 08:01 9012  /usr/lib/libSDL2-2.0.so.0
7f2b1e000000-7f2b1e021000 rw-p 00000000 00:00 0
"""

OTOOL_SAMPLE = """\
/opt/game/client:
\t/usr/lib/libSDL2-2.0.0.dylib (compatibility version 1.0.0, current version 1.0.0)
\t/System/Library/Frameworks/Metal.framework/Versions/A/Metal (compatibility version 1.0.0)
"""

NM_SAMPLE = """\

/opt/game/client (for architecture arm64):
_SDL_PollEvent
_SDL_PumpEvents
_vkQueuePresentKHR
_malloc
"""

OBJDUMP_SAMPLE = """\
DYNAMIC SYMBOL TABLE:
0000000000000000      DF *UND*  0000000000000000  GLIBC_2.2.5 malloc
0000000000000000      DF *UND*  0000000000000000  glXSwapBuffers
0000000000000000      DF *UND*  0000000000000000  XNextEvent
"""

POWERSHELL_SAMPLE = """\
C:\\Windows\\System32\\ntdll.dll
C:\\Windows\\System32\\opengl32.dll
C:\\Program Files\\Game\\SDL2.dll
"""


class TestParsers(unittest.TestCase):
    def test_vmmap_returns_unique_module_paths(self):
        result = parse_vmmap(VMMAP_SAMPLE)
        self.assertIn("/usr/lib/libvulkan.1.dylib", result)
        self.assertIn("/opt/game/libSDL2-2.0.0.dylib", result)
        self.assertEqual(1, result.count("/usr/lib/libobjc.A.dylib"))

    def test_vmmap_drops_mapped_data_files(self):
        """vmmap lists fonts, caches and string tables. Those are not modules."""
        noisy = (
            "__TEXT 1-2 [ 1K ] r-x/r-x SM=COW  /usr/lib/libobjc.A.dylib\n"
            "__DATA 3-4 [ 1K ] r--/r-- SM=COW  /System/Library/Fonts/Assets.car\n"
            "__DATA 5-6 [ 1K ] r--/r-- SM=COW  /usr/share/icu/icutz44l.dat\n"
            "__DATA 7-8 [ 1K ] r--/r-- SM=COW  /System/Library/X.framework/Resources/Menus.loctable\n"
            "__TEXT 9-a [ 1K ] r-x/r-x SM=COW  /System/Library/Frameworks/Metal.framework/Versions/A/Metal\n"
        )
        result = parse_vmmap(noisy)
        self.assertIn("/usr/lib/libobjc.A.dylib", result)
        self.assertIn("/System/Library/Frameworks/Metal.framework/Versions/A/Metal", result)
        self.assertNotIn("/System/Library/Fonts/Assets.car", result)
        self.assertNotIn("/usr/share/icu/icutz44l.dat", result)
        self.assertEqual(2, len(result))

    def test_proc_maps_keeps_only_file_backed_paths(self):
        result = parse_proc_maps(PROC_MAPS_SAMPLE)
        self.assertIn("/usr/lib/libGL.so.1", result)
        self.assertIn("/usr/lib/libSDL2-2.0.so.0", result)
        self.assertEqual(3, len(result))

    def test_otool_skips_the_header_line(self):
        result = parse_otool_l(OTOOL_SAMPLE)
        self.assertEqual(2, len(result))
        self.assertNotIn("/opt/game/client:", result)

    def test_nm_strips_underscore_and_arch_header(self):
        result = parse_nm_undefined(NM_SAMPLE)
        self.assertIn("SDL_PollEvent", result)
        self.assertIn("vkQueuePresentKHR", result)
        self.assertNotIn("_SDL_PollEvent", result)

    def test_objdump_takes_the_last_column(self):
        result = parse_objdump_dynamic(OBJDUMP_SAMPLE)
        self.assertIn("glXSwapBuffers", result)
        self.assertIn("XNextEvent", result)
        self.assertNotIn("DYNAMIC", result)

    def test_powershell_modules_drop_blank_lines(self):
        result = parse_powershell_modules(POWERSHELL_SAMPLE)
        self.assertEqual(3, len(result))
        self.assertIn("C:\\Program Files\\Game\\SDL2.dll", result)

    def test_byte_scan_finds_ascii_symbol_names(self):
        blob = b"\x00\x01junkSDL_PollEvent\x00padGetRawInputData\x00"
        result = scan_bytes_for_symbols(blob, ("SDL_PollEvent", "GetRawInputData", "glfwPollEvents"))
        self.assertIn("SDL_PollEvent", result)
        self.assertIn("GetRawInputData", result)
        self.assertNotIn("glfwPollEvents", result)


class TestArchitecture(unittest.TestCase):
    """The injected library must match the target architecture.

    The official macOS client ships x86_64 only and runs under Rosetta on
    Apple Silicon, so an arm64 build of the library cannot load into it.
    """

    def test_single_architecture(self):
        text = "/path/osclient: Mach-O 64-bit executable x86_64"
        self.assertEqual(("x86_64",), parse_file_arch(text))

    def test_universal_binary_lists_both(self):
        text = (
            "/path/app: Mach-O universal binary with 2 architectures\n"
            "/path/app (for architecture x86_64): Mach-O 64-bit executable x86_64\n"
            "/path/app (for architecture arm64): Mach-O 64-bit executable arm64\n"
        )
        self.assertEqual(("x86_64", "arm64"), parse_file_arch(text))

    def test_elf_binary(self):
        text = "/path/client: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV)"
        self.assertEqual(("x86_64",), parse_file_arch(text))

    def test_unrecognised_output_is_empty(self):
        self.assertEqual((), parse_file_arch("nonsense"))


def _findings(modules=(), imports=(), scanned=(), entitlements=(), hardened=None):
    return Findings(
        modules=tuple(modules),
        imports=tuple(imports),
        scanned=tuple(scanned),
        entitlements=tuple(entitlements),
        hardened=hardened,
    )


class TestCaptureClassifier(unittest.TestCase):
    def test_vulkan_module_and_symbol_gives_confirmed_vulkan(self):
        verdict = classify_capture(
            _findings(modules=("/usr/lib/libvulkan.1.dylib",), imports=("vkQueuePresentKHR",))
        )
        self.assertEqual("Vulkan", verdict.label)
        self.assertEqual("confirmed", verdict.confidence)
        self.assertIn("vkQueuePresentKHR", verdict.hook_target)

    def test_opengl_only_gives_opengl_with_swap_target(self):
        verdict = classify_capture(
            _findings(modules=("/usr/lib/libGL.so.1",), imports=("glXSwapBuffers",))
        )
        self.assertEqual("OpenGL", verdict.label)
        self.assertIn("glXSwapBuffers", verdict.hook_target)

    def test_vulkan_wins_when_both_vulkan_and_opengl_appear(self):
        verdict = classify_capture(
            _findings(
                modules=("/usr/lib/libvulkan.1.dylib", "/usr/lib/libGL.so.1"),
                imports=("vkQueuePresentKHR", "glXSwapBuffers"),
            )
        )
        self.assertEqual("Vulkan", verdict.label)

    def test_no_evidence_gives_unknown(self):
        verdict = classify_capture(_findings())
        self.assertEqual("Unknown", verdict.label)
        self.assertEqual("unknown", verdict.confidence)

    def test_loaded_module_beats_a_byte_scan_only_match(self):
        """A real Metal app whose binary merely mentions Vulkan must read Metal.

        Chromium-based clients carry Vulkan symbol names as strings even when
        they present through Metal. Module evidence must outrank a byte scan.
        """
        verdict = classify_capture(
            _findings(
                modules=("/System/Library/Frameworks/Metal.framework/Versions/A/Metal",),
                imports=("MTLCreateSystemDefaultDevice",),
                scanned=("vkQueuePresentKHR", "vkCreateSwapchainKHR"),
            )
        )
        self.assertEqual("Metal", verdict.label)
        self.assertEqual("confirmed", verdict.confidence)


class TestInputClassifier(unittest.TestCase):
    def test_sdl_symbols_give_low_risk_toolkit_path(self):
        verdict = classify_input(_findings(imports=("SDL_PollEvent", "SDL_PumpEvents")))
        self.assertEqual("Toolkit", verdict.label)
        self.assertEqual("low", verdict.risk)

    def test_raw_input_gives_high_risk(self):
        verdict = classify_input(_findings(imports=("GetRawInputData", "RegisterRawInputDevices")))
        self.assertEqual("Raw input", verdict.label)
        self.assertEqual("high", verdict.risk)

    def test_message_loop_only_gives_medium_risk(self):
        verdict = classify_input(_findings(imports=("PeekMessageW", "DispatchMessageW")))
        self.assertEqual("Message loop", verdict.label)

    def test_raw_input_escalates_risk_even_beside_a_toolkit(self):
        verdict = classify_input(_findings(imports=("SDL_PollEvent", "GetAsyncKeyState")))
        self.assertEqual("high", verdict.risk)
        self.assertTrue(any("raw" in note.lower() for note in verdict.notes))


class TestInjectionClassifier(unittest.TestCase):
    def test_hardened_runtime_without_opt_out_blocks_injection(self):
        verdict = classify_injection(_findings(hardened=True, entitlements=()))
        self.assertEqual("blocked", verdict.confidence)

    def test_library_validation_disabled_allows_injection(self):
        verdict = classify_injection(
            _findings(
                hardened=True,
                entitlements=("com.apple.security.cs.disable-library-validation",),
            )
        )
        self.assertEqual("likely", verdict.confidence)

    def test_unsigned_binary_is_open(self):
        verdict = classify_injection(_findings(hardened=False))
        self.assertEqual("likely", verdict.confidence)


class TestStaticLinkage(unittest.TestCase):
    def test_symbol_without_matching_module_is_flagged(self):
        notes = detect_static_linkage(_findings(modules=("/usr/lib/libc.dylib",), scanned=("SDL_PollEvent",)))
        self.assertTrue(any("static" in note.lower() for note in notes))

    def test_symbol_with_matching_module_is_not_flagged(self):
        notes = detect_static_linkage(
            _findings(modules=("/opt/game/libSDL2-2.0.0.dylib",), scanned=("SDL_PollEvent",))
        )
        self.assertEqual((), notes)


if __name__ == "__main__":
    unittest.main(verbosity=2)
