"""
scheduler_service.py
====================
Service layer for the Disk Scheduling Algorithm Visualizer.

Responsibilities:
  - Input validation
  - Subprocess invocation of compiled C binaries
  - JSON response parsing
  - Aggregated comparison across all algorithms

Binary path convention:
  backend/c_engine/<algorithm>   (relative to this file's grandparent dir)
"""

import json
import subprocess
import os
from pathlib import Path
from typing import Union

# ---------------------------------------------------------------------------
# Path resolution
# ---------------------------------------------------------------------------

# This file lives at:  backend/services/scheduler_service.py
# C engines live at:   backend/c_engine/<binary>
_SERVICE_DIR  = Path(__file__).resolve().parent          # backend/services/
_BACKEND_DIR  = _SERVICE_DIR.parent                      # backend/
_ENGINE_DIR   = _BACKEND_DIR / "c_engine"                # backend/c_engine/

# Supported algorithms and their binary names
ALGORITHMS = {
    "fcfs":  "fcfs",
    "sstf":  "sstf",
    "scan":  "scan",
    "cscan": "cscan",
}

# ---------------------------------------------------------------------------
# Validation helpers
# ---------------------------------------------------------------------------

class ValidationError(ValueError):
    """Raised when caller-supplied parameters are invalid."""


def _validate_inputs(
    algo: str,
    head: int,
    disk_size: int,
    direction: int,
    queue: list[int],
) -> None:
    """
    Validate all scheduler parameters and raise ValidationError on failure.

    Parameters
    ----------
    algo      : one of 'fcfs', 'sstf', 'scan', 'cscan'
    head      : initial head position  (0 <= head < disk_size)
    disk_size : total number of cylinders (> 0)
    direction : 0 = left, 1 = right
    queue     : list of cylinder request integers
    """
    if algo not in ALGORITHMS:
        raise ValidationError(
            f"Unknown algorithm '{algo}'. "
            f"Valid options: {', '.join(ALGORITHMS)}"
        )

    if not isinstance(disk_size, int) or disk_size <= 0:
        raise ValidationError("disk_size must be a positive integer.")

    if not isinstance(head, int) or head < 0:
        raise ValidationError("head must be a non-negative integer.")

    if head >= disk_size:
        raise ValidationError(
            f"head ({head}) must be less than disk_size ({disk_size})."
        )

    if direction not in (0, 1):
        raise ValidationError("direction must be 0 (left) or 1 (right).")

    if not isinstance(queue, list):
        raise ValidationError("queue must be a list of integers.")

    for i, req in enumerate(queue):
        if not isinstance(req, int):
            raise ValidationError(
                f"queue[{i}] is not an integer: {req!r}"
            )
        if req < 0 or req >= disk_size:
            raise ValidationError(
                f"queue[{i}] = {req} is out of range [0, {disk_size - 1}]."
            )

    if len(queue) > 256:
        raise ValidationError("Queue length exceeds maximum of 256 requests.")


def _binary_path(algo: str) -> Path:
    binary = ALGORITHMS[algo]

    # Windows fix → add .exe
    if os.name == "nt":
        binary += ".exe"

    return _ENGINE_DIR / binary


# ---------------------------------------------------------------------------
# Core runner
# ---------------------------------------------------------------------------

def run_algorithm(
    algo: str,
    head: int,
    disk_size: int,
    direction: int,
    queue: list[int],
) -> dict:
    """
    Execute a single disk-scheduling algorithm via its compiled C binary.

    Parameters
    ----------
    algo      : 'fcfs' | 'sstf' | 'scan' | 'cscan'
    head      : initial disk head position
    disk_size : number of cylinders on the disk
    direction : 0 = moving left, 1 = moving right
    queue     : list of cylinder requests (may be empty)

    Returns
    -------
    dict with keys:
        sequence  (list[int]) – ordered list of cylinders visited
        seek_time (int)       – total head movement distance
        algorithm (str)       – algo name echoed back
        error     (str)       – present only on failure
    """
    # ── Validate ────────────────────────────────────────────────────────────
    try:
        _validate_inputs(algo, head, disk_size, direction, queue)
    except ValidationError as exc:
        return _error_response(algo, str(exc))

    # ── Resolve binary ───────────────────────────────────────────────────────
    binary = _binary_path(algo)
    if not binary.exists():
        return _error_response(
            algo,
            f"Binary not found: {binary}. "
            f"Run compile.sh inside backend/c_engine/ first."
        )
    if not os.access(binary, os.X_OK):
        return _error_response(
            algo,
            f"Binary '{binary.name}' is not executable. "
            f"Run: chmod +x {binary}"
        )

    # ── Build argument string ────────────────────────────────────────────────
    queue_str = ",".join(str(r) for r in queue) if queue else ""

    cmd = [
        str(binary),
        str(head),
        str(disk_size),
        str(direction),
        queue_str,
    ]

    # ── Invoke subprocess ────────────────────────────────────────────────────
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=10,          # guard against infinite loops in bad binaries
            check=False,         # we inspect returncode manually
        )
    except subprocess.TimeoutExpired:
        return _error_response(algo, "Binary timed out after 10 seconds.")
    except OSError as exc:
        return _error_response(algo, f"Failed to launch binary: {exc}")

    # ── Parse stdout as JSON ─────────────────────────────────────────────────
    raw = result.stdout.strip()
    if not raw:
        stderr_hint = result.stderr.strip()
        return _error_response(
            algo,
            f"Binary produced no output. "
            + (f"stderr: {stderr_hint}" if stderr_hint else "")
        )

    try:
        payload = json.loads(raw)
    except json.JSONDecodeError as exc:
        return _error_response(
            algo,
            f"Binary returned invalid JSON: {exc}. Raw output: {raw[:200]}"
        )

    # ── Surface any error embedded in the binary's output ───────────────────
    if "error" in payload:
        return _error_response(algo, payload["error"])

    # ── Successful response ──────────────────────────────────────────────────
    return {
        "algorithm": algo,
        "sequence":  payload.get("sequence", []),
        "seek_time": payload.get("seek_time", 0),
    }


# ---------------------------------------------------------------------------
# Compare-all runner
# ---------------------------------------------------------------------------

def compare_all(
    head: int,
    disk_size: int,
    direction: int,
    queue: list[int],
) -> dict:
    """
    Run all four algorithms with identical inputs and return a unified result.

    Returns
    -------
    dict with keys:
        results  (dict[str, dict]) – keyed by algo name
        summary  (list[dict])      – sorted by seek_time ascending
        best     (str)             – algo with minimum seek_time
        worst    (str)             – algo with maximum seek_time
        inputs   (dict)            – echo of the inputs used
    """
    results: dict[str, dict] = {}

    for algo in ALGORITHMS:
        results[algo] = run_algorithm(algo, head, disk_size, direction, queue)

    # Build summary excluding errored results
    valid = [
        {"algorithm": algo, "seek_time": data["seek_time"]}
        for algo, data in results.items()
        if "error" not in data
    ]
    valid_sorted = sorted(valid, key=lambda x: x["seek_time"])

    best  = valid_sorted[0]["algorithm"]  if valid_sorted else None
    worst = valid_sorted[-1]["algorithm"] if valid_sorted else None

    return {
        "results": results,
        "summary": valid_sorted,
        "best":    best,
        "worst":   worst,
        "inputs":  {
            "head":      head,
            "disk_size": disk_size,
            "direction": direction,
            "queue":     queue,
        },
    }


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _error_response(algo: str, message: str) -> dict:
    """Uniform error dict returned on any failure path."""
    return {
        "algorithm": algo,
        "sequence":  [],
        "seek_time": 0,
        "error":     message,
    }
