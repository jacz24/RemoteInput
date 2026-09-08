"""Turn raw observations into conclusions the reader can act on.

Every function here is pure: it reads a Findings value and returns a new
Verdict. Nothing is mutated.
"""

from typing import Dict, Tuple

from .model import Evidence, Findings, Verdict
from .signatures import (
    CAPTURE_MODULES,
    CAPTURE_PLAN,
    CAPTURE_PRIORITY,
    CAPTURE_SYMBOLS,
    INPUT_MODULES,
    INPUT_PLAN,
    INPUT_PRIORITY,
    INPUT_SYMBOLS,
    LIBRARY_VALIDATION_OPT_OUT,
    STATIC_LINK_CANDIDATES,
    TAMPER_SYMBOLS,
)


def _module_hits(modules: Tuple[str, ...], fragments: Tuple[str, ...]) -> Tuple[Evidence, ...]:
    """Find loaded modules whose path contains one of the fragments."""
    hits = []
    for module in modules:
        lowered = module.lower()
        for fragment in fragments:
            if fragment in lowered:
                hits.append(Evidence(module, "module"))
                break
    return tuple(hits)


def _symbol_hits(findings: Findings, names: Tuple[str, ...]) -> Tuple[Evidence, ...]:
    """Find wanted symbols in the import table, then in the byte scan."""
    hits = []
    for name in names:
        if name in findings.imports:
            hits.append(Evidence(name, "import"))
        elif name in findings.scanned:
            hits.append(Evidence(name, "bytescan"))
    return tuple(hits)


def _gather(
    findings: Findings,
    label: str,
    symbols: Dict[str, Tuple[str, ...]],
    modules: Dict[str, Tuple[str, ...]],
) -> Tuple[Tuple[Evidence, ...], Tuple[Evidence, ...]]:
    """Collect the module evidence and the symbol evidence for one label."""
    from_modules = _module_hits(findings.modules, modules.get(label, ()))
    from_symbols = _symbol_hits(findings, symbols.get(label, ()))
    return from_modules, from_symbols


def _confidence(from_modules: Tuple[Evidence, ...], from_symbols: Tuple[Evidence, ...]) -> str:
    if from_modules and from_symbols:
        return "confirmed"
    return "likely"


def _strength(from_modules: Tuple[Evidence, ...], from_symbols: Tuple[Evidence, ...]) -> int:
    """Rank the evidence. A loaded module outranks a byte scan hit.

    A binary can carry the symbol names of an API it never calls. Chromium
    clients name every Vulkan entry point while they present through Metal.
    Only a loaded module proves the client really uses the API.
    """
    if from_modules and from_symbols:
        return 3
    if from_modules:
        return 2
    if any(hit.source == "import" for hit in from_symbols):
        return 2
    return 1 if from_symbols else 0


def _best_label(
    findings: Findings,
    priority: Tuple[str, ...],
    symbols: Dict[str, Tuple[str, ...]],
    modules: Dict[str, Tuple[str, ...]],
):
    """Pick the label with the strongest evidence, breaking ties by priority."""
    best = None
    for label in priority:
        from_modules, from_symbols = _gather(findings, label, symbols, modules)
        score = _strength(from_modules, from_symbols)
        if score == 0:
            continue
        if best is None or score > best[0]:
            best = (score, label, from_modules, from_symbols)
    return best


def classify_capture(findings: Findings) -> Verdict:
    """Decide which graphics API the client presents frames with."""
    best = _best_label(findings, CAPTURE_PRIORITY, CAPTURE_SYMBOLS, CAPTURE_MODULES)
    if best:
        _, label, from_modules, from_symbols = best
        hook_target, effort = CAPTURE_PLAN[label]
        return Verdict(
            topic="Screen capture",
            label=label,
            confidence=_confidence(from_modules, from_symbols),
            hook_target=hook_target,
            effort=effort,
            risk="low" if label in ("OpenGL", "Direct3D") else "medium",
            evidence=from_modules + from_symbols,
        )

    return Verdict(
        topic="Screen capture",
        label="Unknown",
        confidence="unknown",
        notes=(
            "No graphics API was detected. The process may not have drawn a frame yet, "
            "or the probe could not read its module list. Try again with sudo.",
        ),
    )


def classify_input(findings: Findings) -> Verdict:
    """Decide how the client reads mouse and keyboard input."""
    _, raw_symbols = _gather(findings, "Raw input", INPUT_SYMBOLS, INPUT_MODULES)
    uses_raw = bool(raw_symbols)

    best = _best_label(findings, INPUT_PRIORITY, INPUT_SYMBOLS, INPUT_MODULES)
    if best:
        _, label, from_modules, from_symbols = best
        hook_target, effort, risk = INPUT_PLAN[label]
        notes = []
        if uses_raw and label != "Raw input":
            names = ", ".join(hit.name for hit in raw_symbols)
            notes.append(
                f"Raw input calls are also present ({names}). Window messages alone "
                f"will not reach the client. Plan for the raw input path."
            )
            risk = "high"

        return Verdict(
            topic="Input",
            label=label,
            confidence=_confidence(from_modules, from_symbols),
            hook_target=hook_target,
            effort=effort,
            risk=risk,
            evidence=from_modules + from_symbols,
            notes=tuple(notes),
        )

    return Verdict(
        topic="Input",
        label="Unknown",
        confidence="unknown",
        notes=(
            "No input path was detected. The client may link its toolkit statically. "
            "Check the static linkage warnings below.",
        ),
    )


def classify_injection(findings: Findings) -> Verdict:
    """Decide whether a dylib injection can survive the target's code signature."""
    if findings.hardened is None:
        return Verdict(
            topic="Injection",
            label="Not checked",
            confidence="likely",
            notes=("The code signature was not read. This check runs on macOS only.",),
        )

    if not findings.hardened:
        return Verdict(
            topic="Injection",
            label="Open",
            confidence="likely",
            effort="none — reuse Injector_Darwin.cxx as is",
            risk="low",
            notes=("The binary does not use the hardened runtime, so dlopen should succeed.",),
        )

    if LIBRARY_VALIDATION_OPT_OUT in findings.entitlements:
        return Verdict(
            topic="Injection",
            label="Hardened, library validation disabled",
            confidence="likely",
            effort="none — reuse Injector_Darwin.cxx as is",
            risk="low",
            notes=(f"The target carries {LIBRARY_VALIDATION_OPT_OUT}, so an unsigned dylib loads.",),
        )

    return Verdict(
        topic="Injection",
        label="Hardened runtime, library validation on",
        confidence="blocked",
        risk="high",
        notes=(
            "macOS will refuse to dlopen an unsigned library into this process.",
            "Options: sign the dylib with the same team identifier as the target, "
            "or capture out of process with ScreenCaptureKit and drive input with "
            "CGEventPostToPid, which needs no injection at all.",
        ),
    )


def detect_static_linkage(findings: Findings) -> Tuple[str, ...]:
    """Warn where a symbol was found but its library was never loaded."""
    notes = []
    tables = (
        (INPUT_SYMBOLS, INPUT_MODULES),
        (CAPTURE_SYMBOLS, CAPTURE_MODULES),
    )
    for symbols, modules in tables:
        for label, names in symbols.items():
            if label not in STATIC_LINK_CANDIDATES:
                continue
            found = [name for name in names if name in findings.scanned]
            if not found:
                continue
            if _module_hits(findings.modules, modules.get(label, ())):
                continue
            notes.append(
                f"{label}: found {', '.join(found)} in the binary, but no matching "
                f"library is loaded. It is probably linked statically, so you must "
                f"locate the function by byte signature rather than by name."
            )
    return tuple(notes)


def detect_tamper_checks(findings: Findings) -> Tuple[str, ...]:
    """List any integrity or debugger checks the client appears to make."""
    hits = _symbol_hits(findings, TAMPER_SYMBOLS)
    if not hits:
        return ()
    names = ", ".join(hit.name for hit in hits)
    return (
        f"The client references {names}. It may notice an injected module or a "
        f"patched function. Design the out-of-process capture fallback early.",
    )
