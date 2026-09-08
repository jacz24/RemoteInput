"""Value types shared across the probe.

Every type here is frozen. Functions build new values; nothing is mutated.
"""

from dataclasses import dataclass
from typing import Optional, Tuple


@dataclass(frozen=True)
class Evidence:
    """One matched name and where the probe saw it."""

    name: str
    source: str  # "module" | "import" | "bytescan"

    def __str__(self) -> str:
        return f"{self.name} ({self.source})"


@dataclass(frozen=True)
class Findings:
    """Raw observations about one target process and its binary."""

    modules: Tuple[str, ...] = ()
    imports: Tuple[str, ...] = ()
    scanned: Tuple[str, ...] = ()
    entitlements: Tuple[str, ...] = ()
    hardened: Optional[bool] = None


@dataclass(frozen=True)
class Verdict:
    """A conclusion the reader can act on."""

    topic: str
    label: str
    confidence: str  # "confirmed" | "likely" | "unknown" | "blocked"
    hook_target: str = ""
    effort: str = ""
    risk: str = ""
    evidence: Tuple[Evidence, ...] = ()
    notes: Tuple[str, ...] = ()


@dataclass(frozen=True)
class Target:
    """The process the probe looked at."""

    pid: int
    name: str
    binary_path: str
    arch: Tuple[str, ...] = ()
    translated: bool = False
