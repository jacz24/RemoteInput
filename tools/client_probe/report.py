"""Render the findings as text a reader can paste into a ticket."""

import platform
from datetime import datetime
from typing import Tuple

from .model import Target, Verdict

_WIDTH = 74
_RULE = "=" * _WIDTH
_THIN = "-" * _WIDTH


def _header(target: Target) -> str:
    stamp = datetime.now().strftime("%Y-%m-%d %H:%M")
    host = f"{platform.system()} {platform.release()} / {platform.machine()}"
    build = ", ".join(target.arch) if target.arch else "unknown"
    if target.translated:
        build += "  (running under Rosetta)"
    return "\n".join(
        [
            _RULE,
            f" RemoteInput port probe — {target.name} (pid {target.pid})",
            f" {target.binary_path or 'binary path unknown'}",
            f" target build : {build}",
            f" host         : {host} / {stamp}",
            _RULE,
        ]
    )


def _architecture_note(target: Target) -> Tuple[str, ...]:
    """Warn when the library must be built for a different architecture."""
    if not target.arch:
        return ()

    host = "arm64" if platform.machine() in ("arm64", "aarch64") else "x86_64"
    if target.translated:
        return (
            f"The client is {', '.join(target.arch)} and runs under Rosetta on an "
            f"{host} host. Build and inject the x86_64 library "
            f"(libRemoteInput-x86_64.dylib), not the {host} one.",
        )
    if host not in target.arch:
        return (
            f"The client is {', '.join(target.arch)} but the host is {host}. "
            f"Build the library for {target.arch[0]}.",
        )
    return ()


def _summary(verdicts: Tuple[Verdict, ...]) -> str:
    lines = ["", "VERDICT", _THIN]
    for verdict in verdicts:
        lines.append(f"  {verdict.topic:<16} {verdict.label:<34} {verdict.confidence}")
    return "\n".join(lines)


def _detail(verdict: Verdict) -> str:
    lines = ["", f"{verdict.topic.upper()} — {verdict.label} ({verdict.confidence})", _THIN]

    if verdict.hook_target:
        lines.append(f"  Hook target : {verdict.hook_target}")
    if verdict.effort:
        lines.append(f"  Effort      : {verdict.effort}")
    if verdict.risk:
        lines.append(f"  Risk        : {verdict.risk}")

    if verdict.evidence:
        lines.append("  Evidence    :")
        for item in verdict.evidence[:12]:
            lines.append(f"                {item}")
        if len(verdict.evidence) > 12:
            lines.append(f"                ... and {len(verdict.evidence) - 12} more")

    for note in verdict.notes:
        lines.append(f"  Note        : {note}")

    return "\n".join(lines)


def _warnings(title: str, notes: Tuple[str, ...]) -> str:
    if not notes:
        return ""
    lines = ["", title.upper(), _THIN]
    lines.extend(f"  - {note}" for note in notes)
    return "\n".join(lines)


def _next_steps(capture: Verdict, user_input: Verdict, injection: Verdict) -> str:
    """Turn the three verdicts into an ordered plan."""
    steps = []

    if injection.confidence == "blocked":
        steps.append(
            "Injection is blocked first. Solve this before any capture work, or "
            "commit to the out-of-process design (ScreenCaptureKit plus "
            "CGEventPostToPid) and skip injection altogether."
        )
    else:
        steps.append(
            "Injection looks possible. Reuse Injection/Injector_*.cxx and "
            "Echo/ without change — neither knows anything about Java."
        )

    if capture.label == "OpenGL":
        steps.append(
            "Capture is the cheap path. Point the existing PBO read-back code in "
            "NativeHooks_*.cxx at the client swap function instead of the JDK one."
        )
    elif capture.label in ("Vulkan", "Metal"):
        steps.append(
            f"Capture needs a new {capture.label} backend. This is the largest "
            f"single item on the schedule. Budget three to four weeks."
        )
    elif capture.label == "Direct3D":
        steps.append(
            "Capture can start from the existing Direct-X renderer in "
            "NativeHooks_Windows.cxx, which already hooks Present and EndScene."
        )
    else:
        steps.append(
            "Capture is unscoped. Run the probe again while the client draws "
            "frames, and with sudo, before you estimate anything."
        )

    if user_input.risk == "high":
        steps.append(
            "Input is the schedule risk. The client bypasses window messages, so "
            "budget weeks and expect breakage on every client update."
        )
    elif user_input.label == "Toolkit":
        steps.append(
            "Input is the cheap path. Hook the toolkit event pump and push your "
            "own events. This is the closest match to the AWT trick used today."
        )
    elif user_input.label == "Message loop":
        steps.append(
            "Input can be driven from outside the process. No injection needed "
            "for input alone."
        )

    steps.append(
        "Drop the 24 REFLECT_* commands in EIOSTypes.hxx. A C++ client keeps no "
        "field metadata, so reflection would mean chasing struct offsets."
    )
    steps.append(
        "Keep EIOS.cxx as it is. It has zero Java references, so existing Simba "
        "scripts keep working against the new backend."
    )

    lines = ["", "WHAT TO DO NEXT", _THIN]
    lines.extend(f"  {index}. {step}" for index, step in enumerate(steps, start=1))
    return "\n".join(lines)


def render(
    target: Target,
    verdicts: Tuple[Verdict, ...],
    static_notes: Tuple[str, ...],
    tamper_notes: Tuple[str, ...],
    problems: Tuple[str, ...],
) -> str:
    """Build the full report."""
    capture, user_input, injection = verdicts

    parts = [
        _header(target),
        _summary(verdicts),
        _detail(capture),
        _detail(user_input),
        _detail(injection),
        _warnings("architecture", _architecture_note(target)),
        _warnings("static linkage", static_notes),
        _warnings("integrity checks", tamper_notes),
        _warnings("what the probe could not read", problems),
        _next_steps(capture, user_input, injection),
        "",
    ]
    return "\n".join(part for part in parts if part)
