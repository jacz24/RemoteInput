"""Command line entry point.

Examples:
    python3 -m client_probe --name runescape
    python3 -m client_probe --pid 4242
    python3 -m client_probe --pid 4242 --json > probe.json
    python3 -m client_probe --list
"""

import argparse
import dataclasses
import json
import sys
from typing import Optional, Tuple

from .classify import (
    classify_capture,
    classify_injection,
    classify_input,
    detect_static_linkage,
    detect_tamper_checks,
)
from .collect import binary_path, describe_target, find_processes, probe
from .model import Target
from .report import render


def _choose_target(args: argparse.Namespace) -> Optional[Target]:
    """Resolve the command line arguments to exactly one target."""
    if args.pid:
        path = binary_path(args.pid)
        name = path.rsplit("/", 1)[-1].rsplit("\\", 1)[-1] or str(args.pid)
        return describe_target(args.pid, name, path)

    matches = find_processes(args.name)
    if not matches:
        print(f"No running process matches '{args.name}'.", file=sys.stderr)
        print("Use --list to see what is running.", file=sys.stderr)
        return None

    if len(matches) > 1:
        print(f"'{args.name}' matches {len(matches)} processes:", file=sys.stderr)
        for match in matches:
            print(f"  {match.pid:>8}  {match.binary_path}", file=sys.stderr)
        print("Re-run with --pid to pick one.", file=sys.stderr)
        return None

    match = matches[0]
    return describe_target(match.pid, match.name, match.binary_path)


def _list_processes() -> int:
    matches = find_processes("")
    interesting = [
        match for match in matches
        if not match.binary_path.startswith(("/usr/", "/System/", "/sbin/", "/bin/"))
    ]
    for match in sorted(interesting, key=lambda item: item.name.lower()):
        print(f"{match.pid:>8}  {match.binary_path}")
    return 0


def _as_json(target: Target, verdicts: Tuple, static_notes, tamper_notes, problems) -> str:
    payload = {
        "target": dataclasses.asdict(target),
        "verdicts": [dataclasses.asdict(verdict) for verdict in verdicts],
        "static_linkage": list(static_notes),
        "integrity_checks": list(tamper_notes),
        "unread": list(problems),
    }
    return json.dumps(payload, indent=2)


def main(argv: Optional[list] = None) -> int:
    parser = argparse.ArgumentParser(
        prog="client_probe",
        description="Scope a RemoteInput port to a native game client.",
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--pid", type=int, help="probe this process id")
    group.add_argument("--name", help="probe the process whose path contains this text")
    group.add_argument("--list", action="store_true", help="list candidate processes")
    parser.add_argument("--json", action="store_true", help="print JSON instead of a report")

    args = parser.parse_args(argv)

    if args.list:
        return _list_processes()

    target = _choose_target(args)
    if target is None:
        return 1

    findings, problems = probe(target)
    verdicts = (
        classify_capture(findings),
        classify_input(findings),
        classify_injection(findings),
    )
    static_notes = detect_static_linkage(findings)
    tamper_notes = detect_tamper_checks(findings)

    if args.json:
        print(_as_json(target, verdicts, static_notes, tamper_notes, problems))
    else:
        print(render(target, verdicts, static_notes, tamper_notes, problems))
    return 0


if __name__ == "__main__":
    sys.exit(main())
