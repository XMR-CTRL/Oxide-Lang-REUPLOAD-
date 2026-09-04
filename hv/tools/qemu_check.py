#!/usr/bin/env python3
"""qemu_check.py — run the built multiboot2 hypervisor kernel ELF under QEMU
and post-process the log to detect triple-faults.

Implements "Tier 1 — 1b" of the road map: run the kernel under
`-d int,cpu_reset` and automatically catch a triple-fault signature in
qemu.log, while confirming the expected hypervisor boot markers arrived on
the serial port.

Usage:
    python3 hv/tools/qemu_check.py [path/to/kernel.elf]

Inputs:
    argv[1]   path to the kernel ELF (default: hv/build_out/kernel.elf)
    $QEMU_BIN optional override for the `qemu-system-x86_64` executable

Exit codes:
    0  PASS  (no triple-fault signature AND all expected serial markers seen)
    1  FAIL  (triple-fault signature detected, or missing serial markers)
    2  ERROR (QEMU binary not found / could not run — boot test not executed)
    3  ERROR (bad usage — non-existent kernel ELF path)

Only Python stdlib is used (subprocess, sys, os, re, pathlib).
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Default kernel ELF location relative to the repo root. The script may be
# invoked from anywhere (CI usually runs it from the repo root), so the
# default is resolved relative to this file's location.
_DEFAULT_KERNEL_ELF = "hv/build_out/kernel.elf"

# QEMU invocation. `-serial stdio` wires the guest UART to QEMU's stdout/
# stderr; we capture that separately from the qemu.log that the `-D` flag
# writes. `-no-reboot` turns a triple-fault reboot into a clean QEMU exit so
# the test is deterministic and a crashing kernel doesn't loop forever.
_QEMU_ARG_TEMPLATE = [
    "-kernel", "{elf}",
    "-serial", "stdio",
    "-display", "none",
    "-no-reboot",
    "-d", "int,cpu_reset",
    "-D", "{qemu_log}",
]

# Expected boot markers on the serial port. Matched case-insensitively as a
# substring. "VM CALL" and "VMCALL" are alternative spellings of the same
# event; finding either satisfies the "VMCALL" marker.
_EXPECTED_MARKERS: Sequence[Tuple[str, Tuple[str, ...]]] = (
    ("VMXON ok",  ("VMXON ok",)),
    ("vmlaunch",  ("vmlaunch",)),
    ("VM exit",   ("VM exit",)),
    ("CPUID",     ("CPUID",)),
    ("VMCALL",    ("VM CALL", "VMCALL")),
    ("HLT",       ("HLT",)),
)

# ---------------------------------------------------------------------------
# Triple-fault detection in qemu.log
# ---------------------------------------------------------------------------
#
# With `-d int,cpu_reset`, QEMU emits three line families we care about:
#
#   1. Per-instruction execution lines, e.g.
#        "       0x...: say .. <mnemonic>"
#        "IN: <symbol>"
#      These indicate the CPU resumed normally — "forward progress".
#
#   2. Interrupt/exception delivery events. The exact phrasing varies across
#      QEMU versions:
#        "check_exception old: 0x.. new 0x.."     (older QEMU)
#        "CPU_GET_INT: ... exception=.. "         (newer TCG)
#        "v=.. e=.. ..."                           (TUI int dump)
#      We match any of these as a candidate exception event. We also match
#      the symbolic vector names (#DB, #GP, #PF, #DF, #UD, #TS, #SS, #NP).
#
#   3. `cpu_reset` lines, e.g.
#        "CPU_RESET: ..." or "cpu_reset"
#
# A triple fault manifests as the CPU delivering exception N, then (because
# the new handler faults too) the double-fault (#DF, vector 8), and then —
# with no handler recovering — a third fault triggers a `cpu_reset`. With
# `-no-reboot` QEMU exits instead of rebooting, so a clean exit right after a
# stretch of exception delivery is also suspicious.
#
# Heuristic (line-buffered, regex-per-line):
#   (a) Find every `cpu_reset` line.
#   (b) For each reset, scan a window of ~_RESET_WINDOW lines around it for
#       two exception events with NO intervening "progress" line.
#   (c) If there's no reset but QEMU exited non-zero AND we saw two
#       back-to-back exceptions anywhere, also call it a triple-fault
#       (covers `-no-reboot` exit on a real triple-fault).
#   (d) Defensive: ≥4 exceptions in a row with zero progress AND any reset
#       present → also a triple-fault.
#
# This is deliberately permissive — we prefer a false FAIL (triple-fault
# suspected) over silently passing a genuinely crashing build. Reviewers can
# always inspect qemu.log for a definitive answer.

# A "made forward progress" line: an instruction execution trace line or an
# `IN:` (translation-block-entry) line.
_RE_INSN_PROGRESS = re.compile(
    r"^\s*0x[0-9A-Fa-f]+\s*:\s*say\b"
    r"|^\s*IN:\s"
)

# An exception / interrupt delivery event across QEMU -d int phrasings.
_RE_EXCEPTION_EVENT = re.compile(
    r"check_exception"
    r"|CPU_GET_INT"
    r"|\bv=\d+"
    r"|exception=0x[0-9A-Fa-f]+"
    r"|new_exception=0x[0-9A-Fa-f]+"
    r"|#[A-Za-z]{2}\b"              # matches #DB #GP #PF #DF #UD #TS #SS #NP ...
)

# The cpu_reset event.
_RE_CPU_RESET = re.compile(r"cpu_reset", re.IGNORECASE)

# Window (lines) around a cpu_reset that we still consider "adjacent" for the
# back-to-back exception heuristic.
_RESET_WINDOW = 64


# ---------------------------------------------------------------------------
# Environment helpers
# ---------------------------------------------------------------------------


def _resolve_qemu(env: "os._Environ[str]") -> Optional[str]:
    """Return the qemu-system-x86_64 binary path or None if not found."""
    explicit = env.get("QEMU_BIN")
    if explicit:
        # Accept either an absolute path to the file or a name found on PATH.
        if os.path.isfile(explicit) or shutil.which(explicit):
            return explicit
        return None
    return shutil.which("qemu-system-x86_64")


def _resolve_kernel(argv: Sequence[str]) -> Tuple[Optional[Path], Optional[str]]:
    """Resolve the kernel ELF path. Returns (path, error_message).

    Tries the cwd-relative path first, then the repo-root-relative path
    (this file lives at <repo>/hv/tools/, so parents[2] is the repo root)."""
    raw: str = argv[1] if len(argv) > 1 and argv[1] else _DEFAULT_KERNEL_ELF
    p = Path(raw)
    if not p.is_absolute():
        cwd_relative = Path.cwd() / p
        if cwd_relative.is_file():
            return cwd_relative, None
        script_relative = Path(__file__).resolve().parents[2] / p
        if script_relative.is_file():
            return script_relative, None
        return None, f"kernel ELF not found: {cwd_relative}"
    if not p.is_file():
        return None, f"kernel ELF not found: {p}"
    return p.resolve(), None


# ---------------------------------------------------------------------------
# Log post-processing
# ---------------------------------------------------------------------------


def _classify_log_line(line: str) -> str:
    """Classify a single qemu.log line into one of:
       'reset' | 'exception' | 'progress' | ''
    Pure per-line regex; no cross-line state. Ordering matters: cpu_reset and
    exception lines occasionally contain instruction text too, so we check
    reset/exception before progress."""
    if _RE_CPU_RESET.search(line):
        return "reset"
    if _RE_EXCEPTION_EVENT.search(line):
        return "exception"
    if _RE_INSN_PROGRESS.search(line):
        return "progress"
    return ""


def _find_back_to_back_exceptions(
    lines: List[str], start: int, end: int
) -> Optional[Tuple[int, int]]:
    """Within lines[start:end], look for two exception-event lines separated
    by no 'progress' line. Returns (i, j) indices of the two exception lines
    if found, else None.
    Non-progress / non-exception lines (noise) are ignored, so a sequence
    `exception noise exception` still counts as back-to-back."""
    last_exc: Optional[int] = None
    for i in range(start, end):
        klass = _classify_log_line(lines[i])
        if klass == "exception":
            if last_exc is not None:
                return last_exc, i
            last_exc = i
        elif klass == "progress":
            last_exc = None  # CPU resumed → reset the run
        # else: noisy line, ignore
    return None


def detect_triple_fault(qemu_log_text: str, qemu_exit_code: Optional[int]) -> bool:
    """Return True if a triple-fault signature is present in qemu_log_text.

    See the module-level comment for the full heuristic."""
    lines = qemu_log_text.splitlines()
    if not lines:
        return False

    n = len(lines)
    reset_positions = [i for i, ln in enumerate(lines) if _RE_CPU_RESET.search(ln)]

    # Case 1: back-to-back exceptions adjacent (within window) to a cpu_reset.
    for r in reset_positions:
        lo = max(0, r - _RESET_WINDOW)
        hi = min(n, r + _RESET_WINDOW + 1)
        if _find_back_to_back_exceptions(lines, lo, hi) is not None:
            return True

    # Case 2: no reset, but QEMU exited non-zero and we saw two back-to-back
    # exceptions anywhere (common with `-no-reboot` on real triple-faults).
    if qemu_exit_code is not None and qemu_exit_code != 0:
        if _find_back_to_back_exceptions(lines, 0, n) is not None:
            return True

    # Case 3: defensive — a long run of unhandled exceptions with no progress
    # at all AND a reset somewhere in the log almost always means we
    # fault-looped to a triple-fault.
    if reset_positions:
        exc_in_a_row = 0
        best_run = 0
        for ln in lines:
            klass = _classify_log_line(ln)
            if klass == "exception":
                exc_in_a_row += 1
                if exc_in_a_row > best_run:
                    best_run = exc_in_a_row
            elif klass == "progress":
                exc_in_a_row = 0
        if best_run >= 4:
            return True

    return False


def parse_serial_markers(serial_text: str) -> List[str]:
    """Return the list of expected markers actually present in serial_text
    (case-insensitive substring match)."""
    if not serial_text:
        return []
    found: List[str] = []
    for label, alternatives in _EXPECTED_MARKERS:
        if any(re.search(re.escape(alt), serial_text, re.IGNORECASE)
               for alt in alternatives):
            found.append(label)
    return found


# ---------------------------------------------------------------------------
# QEMU driver
# ---------------------------------------------------------------------------


def _build_qemu_command(
    qemu_bin: str, kernel_elf: Path, qemu_log_path: Path
) -> List[str]:
    """Substitute the ELF and qemu.log paths into the template argv list."""
    filled = [
        arg.format(elf=str(kernel_elf), qemu_log=str(qemu_log_path))
        for arg in _QEMU_ARG_TEMPLATE
    ]
    return [qemu_bin, *filled]


def run_qemu(
    qemu_bin: str,
    kernel_elf: Path,
    cwd: Path,
    qemu_log_path: Path,
    timeout: int = 60,
) -> Tuple[Optional[int], str, str]:
    """Run QEMU and return (returncode, stdout, stderr).

    On a spawn failure (qemu binary missing / not executable) returns
    (None, "", "<error message>"). On timeout returns (None, <partial out>,
    <partial err> + timeout note)."""
    cmd = _build_qemu_command(qemu_bin, kernel_elf, qemu_log_path)
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(cwd),
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except FileNotFoundError:
        return None, "", f"qemu binary not found: {qemu_bin}"
    except PermissionError:
        return None, "", f"qemu binary not executable: {qemu_bin}"
    except subprocess.TimeoutExpired as e:
        out = e.stdout or ""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        err = e.stderr or ""
        if isinstance(err, bytes):
            err = err.decode("utf-8", "replace")
        return None, out, err + f"\n[qemu timed out after {timeout}s]"
    return proc.returncode, proc.stdout or "", proc.stderr or ""


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def main(argv: Sequence[str]) -> int:
    # --- resolve kernel ELF -------------------------------------------------
    kernel_elf, err = _resolve_kernel(argv)
    if kernel_elf is None:
        print(f"qemu_hv: ERROR: {err}", file=sys.stderr)
        return 3
    kernel_elf = Path(kernel_elf)

    # --- resolve QEMU -------------------------------------------------------
    qemu = _resolve_qemu(os.environ)
    if qemu is None:
        msg = (
            "QEMU_BIN not found, cannot run hv boot test "
            "(set QEMU_BIN or install qemu-system-x86_64)"
        )
        print(f"qemu_hv: ERROR: {msg}", file=sys.stderr)
        return 2

    # --- prepare output paths ----------------------------------------------
    # Drop qemu.log / serial.log next to the kernel ELF so CI can scrape them.
    out_dir = kernel_elf.parent
    qemu_log_path = out_dir / "qemu.log"
    serial_log_path = out_dir / "serial.log"

    print(f"qemu_hv: running {qemu} with kernel={kernel_elf}")
    print(f"qemu_hv: qemu.log  -> {qemu_log_path}")
    print(f"qemu_hv: serial.log-> {serial_log_path}")

    rc, stdout, stderr = run_qemu(qemu, kernel_elf, out_dir, qemu_log_path)
    if rc is None:
        # QEMU failed to spawn entirely (or timed out hard before producing rc).
        print(f"qemu_hv: ERROR: {stderr.strip()}", file=sys.stderr)
        return 2

    # QEMU routes the guest UART to stdout with `-serial stdio`; some builds
    # route to stderr, so fold both together as the "serial" stream.
    serial_text = (stdout or "") + (stderr or "")
    try:
        serial_log_path.write_text(serial_text, encoding="utf-8", errors="replace")
    except OSError as e:
        print(f"qemu_hv: WARN: could not write serial.log: {e}", file=sys.stderr)

    # --- read qemu.log ----------------------------------------------------
    qemu_log_text = ""
    if qemu_log_path.is_file():
        try:
            qemu_log_text = qemu_log_path.read_text(
                encoding="utf-8", errors="replace"
            )
        except OSError as e:
            print(f"qemu_hv: WARN: could not read qemu.log: {e}", file=sys.stderr)

    # --- post-process -----------------------------------------------------
    triple_fault = detect_triple_fault(qemu_log_text, rc)
    markers = parse_serial_markers(serial_text)

    all_markers_present = len(markers) == len(_EXPECTED_MARKERS)
    passed = (not triple_fault) and all_markers_present

    # --- structured report ------------------------------------------------
    status = "PASS" if passed else "FAIL"
    tf_str = "yes" if triple_fault else "no"
    markers_str = ",".join(markers) if markers else "(none)"
    print(f"qemu_hv: {status} (triple_fault={tf_str}, markers={markers_str})")

    # --- diagnostics for reviewers ---------------------------------------
    if triple_fault:
        print(
            f"qemu_hv: triple-fault signature detected "
            f"(see {qemu_log_path} for QEMU int/cpu_reset log)",
            file=sys.stderr,
        )
    if not all_markers_present:
        missing = [label for label, _ in _EXPECTED_MARKERS if label not in markers]
        print(
            f"qemu_hv: missing serial markers: {', '.join(missing)} "
            f"(see {serial_log_path})",
            file=sys.stderr,
        )
    # A non-zero exit code is not by itself a failure — isa-debug-exit
    # returns non-zero on a clean halt too. Only note it, don't fail on it.
    if rc != 0 and not triple_fault:
        print(f"qemu_hv: note: qemu exited with code {rc}", file=sys.stderr)

    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
