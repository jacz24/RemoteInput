"""The impure layer: run platform tools and read files.

Every function degrades to an empty result when a tool is missing or a
permission is denied. The probe reports what it could not read rather
than failing outright.
"""

import os
import platform
import re
import subprocess
from typing import List, Optional, Tuple

from .model import Findings, Target
from .parse import (
    parse_file_arch,
    parse_nm_undefined,
    parse_objdump_dynamic,
    parse_otool_l,
    parse_powershell_modules,
    parse_proc_maps,
    parse_vmmap,
)
from .signatures import all_symbols

# Directories that hold operating system libraries rather than game code.
_SYSTEM_PREFIXES = (
    "/usr/", "/system/", "/lib/", "/lib64/", "/opt/x11/",
    "c:\\windows\\", "c:\\program files\\windowsapps\\",
)

_MAX_SCAN_BYTES = 400 * 1024 * 1024


def _run(command: List[str], timeout: int = 30) -> str:
    """Run a command and return its output. Return "" on any failure."""
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
        return (result.stdout or "") + (result.stderr or "")
    except (OSError, subprocess.SubprocessError):
        return ""


def _powershell(script: str) -> str:
    return _run(["powershell", "-NoProfile", "-Command", script])


# ------------------------------------------------------------- processes


def find_processes(name_fragment: str) -> Tuple[Target, ...]:
    """List running processes whose name or path contains the fragment."""
    system = platform.system()
    wanted = name_fragment.lower()
    found = []

    if system == "Windows":
        text = _powershell(
            "Get-Process | Where-Object {$_.Path} | "
            "ForEach-Object {\"$($_.Id)`t$($_.Path)\"}"
        )
        rows = (line.split("\t", 1) for line in text.splitlines() if "\t" in line)
    else:
        text = _run(["ps", "-Ao", "pid=,comm="])
        rows = (line.strip().split(None, 1) for line in text.splitlines() if line.strip())

    for row in rows:
        if len(row) != 2:
            continue
        pid_text, path = row[0].strip(), row[1].strip()
        if wanted not in path.lower():
            continue
        if not pid_text.isdigit():
            continue
        found.append(Target(int(pid_text), os.path.basename(path), path))

    return tuple(found)


def binary_path(pid: int) -> str:
    """Find the executable behind a pid."""
    system = platform.system()
    if system == "Linux":
        try:
            return os.readlink(f"/proc/{pid}/exe")
        except OSError:
            return ""
    if system == "Windows":
        return _powershell(f"(Get-Process -Id {pid}).Path").strip()
    return _run(["ps", "-p", str(pid), "-o", "comm="]).strip()


# --------------------------------------------------------------- modules


def loaded_modules(pid: int) -> Tuple[Tuple[str, ...], str]:
    """List the modules a process has loaded.

    Returns the paths and a note describing any problem.
    """
    system = platform.system()

    if system == "Linux":
        try:
            with open(f"/proc/{pid}/maps", "r", encoding="utf-8", errors="ignore") as handle:
                return parse_proc_maps(handle.read()), ""
        except OSError as error:
            return (), f"Could not read /proc/{pid}/maps: {error}"

    if system == "Windows":
        text = _powershell(
            f"(Get-Process -Id {pid}).Modules | ForEach-Object {{$_.FileName}}"
        )
        modules = parse_powershell_modules(text)
        if not modules:
            return (), "PowerShell returned no modules. Run this terminal as Administrator."
        return modules, ""

    text = _run(["vmmap", str(pid)])
    modules = parse_vmmap(text)
    if modules:
        return modules, ""

    lsof = _run(["lsof", "-p", str(pid), "-Fn"])
    fallback = tuple(
        line[1:] for line in lsof.splitlines()
        if line.startswith("n/") and ("." in os.path.basename(line))
    )
    if fallback:
        return fallback, "vmmap was denied, so this list comes from lsof and may be short."
    return (), f"Could not read the module list. Try: sudo python3 -m client_probe --pid {pid}"


# --------------------------------------------------------------- symbols


def imported_symbols(binary: str) -> Tuple[Tuple[str, ...], str]:
    """List the symbols a binary imports from other libraries."""
    if not binary or not os.path.exists(binary):
        return (), "The binary path could not be resolved, so imports were not read."

    system = platform.system()
    if system == "Darwin":
        text = _run(["nm", "-u", binary])
        symbols = parse_nm_undefined(text)
        linked = parse_otool_l(_run(["otool", "-L", binary]))
        return symbols + linked, ""

    if system == "Linux":
        text = _run(["objdump", "-T", binary])
        if text:
            return parse_objdump_dynamic(text), ""
        return parse_objdump_dynamic(_run(["nm", "-D", "--undefined-only", binary])), ""

    return (), "Windows import tables are not read directly. The byte scan below covers this."


def read_for_scan(paths: Tuple[str, ...]) -> Tuple[bytes, str]:
    """Read binaries into memory for the ASCII symbol scan."""
    chunks = []
    total = 0
    skipped = 0
    for path in paths:
        try:
            size = os.path.getsize(path)
            if total + size > _MAX_SCAN_BYTES:
                skipped += 1
                continue
            with open(path, "rb") as handle:
                chunks.append(handle.read())
            total += size
        except OSError:
            skipped += 1

    note = f"{skipped} file(s) could not be scanned." if skipped else ""
    return b"".join(chunks), note


def game_modules(modules: Tuple[str, ...]) -> Tuple[str, ...]:
    """Keep only the modules that ship with the game, not with the OS."""
    return tuple(
        module for module in modules
        if not module.lower().startswith(_SYSTEM_PREFIXES)
    )


# ----------------------------------------------------------- code signing


def binary_arch(binary: str) -> Tuple[str, ...]:
    """Read the architectures a binary was built for."""
    if not binary or not os.path.exists(binary):
        return ()
    return parse_file_arch(_run(["file", binary]))


def is_translated(pid: int) -> bool:
    """Say whether a process runs under Rosetta 2 (macOS on Apple Silicon).

    A translated process is x86_64 no matter what the host CPU is, so the
    injected library must be x86_64 as well.
    """
    if platform.system() != "Darwin":
        return False

    import ctypes
    import ctypes.util

    p_translated = 0x00020000
    ctl_kern, kern_proc, kern_proc_pid = 1, 14, 1

    try:
        libc = ctypes.CDLL(ctypes.util.find_library("c"))
        mib = (ctypes.c_int * 4)(ctl_kern, kern_proc, kern_proc_pid, pid)
        buffer = ctypes.create_string_buffer(648)
        length = ctypes.c_size_t(len(buffer))
        if libc.sysctl(mib, 4, buffer, ctypes.byref(length), None, 0) != 0:
            return False
        # kinfo_proc.kp_proc.p_flag sits at offset 0x20 of the struct.
        flags = int.from_bytes(buffer.raw[0x20:0x24], "little")
        return bool(flags & p_translated)
    except (OSError, ValueError, AttributeError):
        return False


def describe_target(pid: int, name: str, path: str) -> Target:
    """Build a Target with its architecture filled in."""
    binary = path or binary_path(pid)
    return Target(
        pid=pid,
        name=name or os.path.basename(binary) or str(pid),
        binary_path=binary,
        arch=binary_arch(binary),
        translated=is_translated(pid),
    )


def codesign_info(binary: str) -> Tuple[Optional[bool], Tuple[str, ...]]:
    """Read the hardened runtime flag and the entitlements (macOS only)."""
    if platform.system() != "Darwin" or not binary:
        return None, ()

    details = _run(["codesign", "-d", "--verbose=2", binary])
    if "code object is not signed" in details.lower():
        return False, ()

    flags = re.search(r"flags=0x[0-9a-fA-F]+\(([^)]*)\)", details)
    hardened = bool(flags and "runtime" in flags.group(1))

    plist = _run(["codesign", "-d", "--entitlements", ":-", binary])
    entitlements = tuple(re.findall(r"<key>([^<]+)</key>", plist))
    return hardened, entitlements


# ------------------------------------------------------------------ probe


def probe(target: Target) -> Tuple[Findings, Tuple[str, ...]]:
    """Gather everything the classifiers need about one target."""
    problems = []

    modules, module_note = loaded_modules(target.pid)
    if module_note:
        problems.append(module_note)

    binary = target.binary_path or binary_path(target.pid)
    imports, import_note = imported_symbols(binary)
    if import_note:
        problems.append(import_note)

    scan_targets = tuple(dict.fromkeys((binary,) + game_modules(modules))) if binary else game_modules(modules)
    blob, scan_note = read_for_scan(scan_targets)
    if scan_note:
        problems.append(scan_note)

    from .parse import scan_bytes_for_symbols

    scanned = scan_bytes_for_symbols(blob, all_symbols())
    hardened, entitlements = codesign_info(binary)

    findings = Findings(
        modules=modules,
        imports=imports,
        scanned=scanned,
        entitlements=entitlements,
        hardened=hardened,
    )
    return findings, tuple(problems)
