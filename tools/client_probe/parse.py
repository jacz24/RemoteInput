"""Pure parsers for the output of the platform tools.

Each function takes text and returns a tuple of names. No function here
runs a command, touches the disk, or keeps state.
"""

import re
from typing import Iterable, Tuple

# A path that sits at the end of a line, after at least one space.
# Non-greedy plus an end anchor keeps paths that contain spaces intact.
_TRAILING_PATH = re.compile(r"\s(/.+?)\s*$")


_MODULE_SUFFIXES = (".dylib", ".dll", ".so")


def _unique(values: Iterable[str]) -> Tuple[str, ...]:
    """Keep the first occurrence of each value and drop the rest."""
    return tuple(dict.fromkeys(v for v in values if v))


def is_module(path: str) -> bool:
    """Decide whether a mapped file is executable code rather than data.

    A process maps fonts, caches, string tables and icon archives as well as
    libraries. Only the libraries tell us which APIs the client uses.
    """
    lowered = path.lower()
    name = lowered.rsplit("/", 1)[-1].rsplit("\\", 1)[-1]

    if "/resources/" in lowered:
        return False
    if any(suffix in name for suffix in _MODULE_SUFFIXES):
        return True
    if ".framework/" in lowered or ".bundle/" in lowered:
        return "." not in name
    return "." not in name


def _keep_modules(paths: Iterable[str]) -> Tuple[str, ...]:
    return _unique(path for path in paths if is_module(path))


def parse_vmmap(text: str) -> Tuple[str, ...]:
    """Read module paths out of `vmmap <pid>` output (macOS)."""
    paths = (
        match.group(1)
        for match in (_TRAILING_PATH.search(line) for line in text.splitlines())
        if match
    )
    return _keep_modules(paths)


def parse_proc_maps(text: str) -> Tuple[str, ...]:
    """Read module paths out of /proc/<pid>/maps (Linux)."""
    paths = []
    for line in text.splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) < 6:
            continue
        path = fields[5].strip()
        if path.startswith("/"):
            paths.append(path)
    return _keep_modules(paths)


def parse_otool_l(text: str) -> Tuple[str, ...]:
    """Read linked library paths out of `otool -L <binary>` output (macOS)."""
    paths = []
    for line in text.splitlines():
        if not line.startswith((" ", "\t")):
            continue  # the first line repeats the binary path
        path = line.strip().split(" (compatibility")[0].strip()
        if path:
            paths.append(path)
    return _unique(paths)


def parse_nm_undefined(text: str) -> Tuple[str, ...]:
    """Read symbol names out of `nm -u <binary>` output (macOS)."""
    names = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or "(for architecture" in stripped:
            continue
        name = stripped.split()[-1]
        names.append(name[1:] if name.startswith("_") else name)
    return _unique(names)


def parse_objdump_dynamic(text: str) -> Tuple[str, ...]:
    """Read symbol names out of `objdump -T <binary>` output (Linux)."""
    names = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.endswith(":"):
            continue
        names.append(stripped.split()[-1])
    return _unique(names)


def parse_powershell_modules(text: str) -> Tuple[str, ...]:
    """Read module paths out of the PowerShell module listing (Windows)."""
    return _unique(line.strip() for line in text.splitlines())


_ARCH_ALIASES = {"x86-64": "x86_64", "aarch64": "arm64", "x86_64h": "x86_64"}
_FAT_ARCH = re.compile(r"\(for architecture ([^)]+)\)")


def parse_file_arch(text: str) -> Tuple[str, ...]:
    """Read the architectures out of `file <binary>` output.

    The injected library must be built for the same architecture as the
    target, so this drives which artifact to ship.
    """
    fat = _FAT_ARCH.findall(text)
    if fat:
        return _unique(_ARCH_ALIASES.get(name.strip(), name.strip()) for name in fat)

    for line in text.splitlines():
        if "Mach-O" in line:
            name = line.split()[-1]
            return (_ARCH_ALIASES.get(name, name),)
        if "ELF" in line:
            fields = [field.strip() for field in line.split(",")]
            if len(fields) > 1:
                name = fields[1]
                return (_ARCH_ALIASES.get(name, name),)

    return ()


def scan_bytes_for_symbols(blob: bytes, names: Iterable[str]) -> Tuple[str, ...]:
    """Find which of `names` appear as plain ASCII inside a binary.

    This is a heuristic. It catches a statically linked toolkit that exports
    no symbols, which the import tables miss. It can also report a name that
    only sits in a string table and is never called, so treat a hit as a
    lead rather than as proof.
    """
    found = [name for name in names if name.encode("ascii", "ignore") in blob]
    return _unique(found)
