"""
app.py
======
Flask application entry-point for the Disk Scheduling Algorithm Visualizer.

Routes
------
POST /run-algorithm   – run a single algorithm
POST /compare-all     – run all four algorithms and return comparison data
GET  /health          – liveness check

Run
---
    python backend/app.py
    # or via gunicorn in production:
    gunicorn -w 2 -b 0.0.0.0:5000 "backend.app:create_app()"
"""

import sys
import os
import webbrowser
from pathlib import Path

# ---------------------------------------------------------------------------
# Make sure 'backend/' is on sys.path so relative imports work whether the
# file is run as  `python backend/app.py`  or  `python -m backend.app`
# ---------------------------------------------------------------------------
_BACKEND_DIR = Path(__file__).resolve().parent
if str(_BACKEND_DIR) not in sys.path:
    sys.path.insert(0, str(_BACKEND_DIR))

from flask import Flask, jsonify, request, Response
from flask_cors import CORS

from services.scheduler_service import (
    run_algorithm,
    compare_all,
    ALGORITHMS,
    ValidationError,
)

# ---------------------------------------------------------------------------
# App factory
# ---------------------------------------------------------------------------

def create_app() -> Flask:
    app = Flask(__name__)

    # Allow all origins so the plain file:// frontend can talk to Flask.
    # Restrict origins in production via the CORS_ORIGINS env var:
    #   CORS_ORIGINS="https://your-domain.com" python backend/app.py
    allowed_origins = os.environ.get("CORS_ORIGINS", "*")
    CORS(app, resources={r"/*": {"origins": allowed_origins}})

    # ── Register blueprints / routes ─────────────────────────────────────
    _register_routes(app)

    return app


# ---------------------------------------------------------------------------
# Route registration
# ---------------------------------------------------------------------------

def _register_routes(app: Flask) -> None:

    # ── Health check ────────────────────────────────────────────────────────
    @app.get("/health")
    def health() -> Response:
        """Quick liveness probe — useful for Docker / load-balancer checks."""
        return jsonify({
            "status":     "ok",
            "algorithms": list(ALGORITHMS.keys()),
        })

    # ── Run single algorithm ─────────────────────────────────────────────────
    @app.post("/run-algorithm")
    def run_algo() -> Response:
        """
        Execute one disk-scheduling algorithm.

        Request body (JSON)
        -------------------
        {
            "algorithm"  : "fcfs" | "sstf" | "scan" | "cscan",
            "head"       : <int>,
            "disk_size"  : <int>,
            "direction"  : 0 | 1,
            "queue"      : [<int>, ...]
        }

        Response (JSON)
        ---------------
        Success:
            { "algorithm": "fcfs", "sequence": [...], "seek_time": 344 }
        Error:
            { "algorithm": "fcfs", "sequence": [], "seek_time": 0,
              "error": "..." }
        """
        body, parse_err = _parse_json_body()
        if parse_err:
            return parse_err

        # ── Extract fields ──────────────────────────────────────────────────
        try:
            algo      = _require_str(body,  "algorithm")
            head      = _require_int(body,  "head")
            disk_size = _require_int(body,  "disk_size")
            direction = _require_int(body,  "direction")
            queue     = _require_int_list(body, "queue")
        except _FieldError as exc:
            return _bad_request(str(exc))

        # ── Delegate to service ─────────────────────────────────────────────
        result = run_algorithm(algo, head, disk_size, direction, queue)

        # If the service surfaced an error, return 422 so the frontend
        # can distinguish a processing error from a network error.
        if "error" in result:
            return jsonify(result), 422

        return jsonify(result), 200

    # ── Compare all algorithms ───────────────────────────────────────────────
    @app.post("/compare-all")
    def compare() -> Response:
        """
        Run all four algorithms with the same inputs and return a
        side-by-side comparison.

        Request body (JSON)
        -------------------
        {
            "head"      : <int>,
            "disk_size" : <int>,
            "direction" : 0 | 1,
            "queue"     : [<int>, ...]
        }

        Response (JSON)
        ---------------
        {
            "results"  : { "fcfs": {...}, "sstf": {...}, ... },
            "summary"  : [ { "algorithm": "sstf", "seek_time": 134 }, ... ],
            "best"     : "sstf",
            "worst"    : "fcfs",
            "inputs"   : { "head": 50, "disk_size": 200, ... }
        }
        """
        body, parse_err = _parse_json_body()
        if parse_err:
            return parse_err

        try:
            head      = _require_int(body,  "head")
            disk_size = _require_int(body,  "disk_size")
            direction = _require_int(body,  "direction")
            queue     = _require_int_list(body, "queue")
        except _FieldError as exc:
            return _bad_request(str(exc))

        result = compare_all(head, disk_size, direction, queue)
        return jsonify(result), 200

    # ── 404 handler ─────────────────────────────────────────────────────────
    @app.errorhandler(404)
    def not_found(exc) -> Response:
        return jsonify({"error": "Endpoint not found."}), 404

    # ── 405 handler ─────────────────────────────────────────────────────────
    @app.errorhandler(405)
    def method_not_allowed(exc) -> Response:
        return jsonify({"error": "Method not allowed."}), 405

    # ── Generic server error handler ─────────────────────────────────────────
    @app.errorhandler(500)
    def internal_error(exc) -> Response:
        return jsonify({"error": "Internal server error.", "detail": str(exc)}), 500


# ---------------------------------------------------------------------------
# Request-parsing helpers
# ---------------------------------------------------------------------------

class _FieldError(ValueError):
    """Raised when a required request field is missing or has wrong type."""


def _parse_json_body():
    """
    Attempt to parse the request body as JSON.
    Returns (body_dict, None) on success or (None, error_response) on failure.
    """
    if not request.is_json:
        return None, _bad_request("Content-Type must be application/json.")
    try:
        body = request.get_json(force=True, silent=False)
    except Exception:
        body = None

    if body is None or not isinstance(body, dict):
        return None, _bad_request("Request body must be a JSON object.")

    return body, None


def _require_str(body: dict, key: str) -> str:
    val = body.get(key)
    if val is None:
        raise _FieldError(f"Missing required field: '{key}'.")
    if not isinstance(val, str):
        raise _FieldError(f"Field '{key}' must be a string.")
    val = val.strip().lower()
    if not val:
        raise _FieldError(f"Field '{key}' must not be empty.")
    return val


def _require_int(body: dict, key: str) -> int:
    val = body.get(key)
    if val is None:
        raise _FieldError(f"Missing required field: '{key}'.")
    # Accept JSON numbers (int or float that is whole)
    if isinstance(val, bool):
        raise _FieldError(f"Field '{key}' must be an integer, not boolean.")
    if isinstance(val, float):
        if not val.is_integer():
            raise _FieldError(f"Field '{key}' must be a whole number.")
        val = int(val)
    if not isinstance(val, int):
        raise _FieldError(f"Field '{key}' must be an integer.")
    return val


def _require_int_list(body: dict, key: str) -> list:
    val = body.get(key)
    if val is None:
        # Treat missing queue as empty — valid (zero movement)
        return []
    if not isinstance(val, list):
        raise _FieldError(f"Field '{key}' must be an array of integers.")
    result = []
    for i, item in enumerate(val):
        if isinstance(item, bool):
            raise _FieldError(f"'{key}[{i}]' must be an integer, not boolean.")
        if isinstance(item, float):
            if not item.is_integer():
                raise _FieldError(f"'{key}[{i}]' must be a whole number.")
            item = int(item)
        if not isinstance(item, int):
            raise _FieldError(f"'{key}[{i}]' must be an integer.")
        result.append(item)
    return result


def _bad_request(message: str) -> Response:
    return jsonify({"error": message}), 400


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    port  = int(os.environ.get("PORT", 5000))
    debug = os.environ.get("FLASK_DEBUG", "1") == "1"

    app = create_app()

    print(f"\n  Disk Scheduler API  →  http://127.0.0.1:{port}")
    print(f"  Debug mode          :  {debug}")
    print(f"  Endpoints           :  POST /run-algorithm  |  POST /compare-all  |  GET /health\n")

    # 🔥 AUTO OPEN FRONTEND
    frontend_path = os.path.abspath("../frontend/index.html")
    webbrowser.open(f"file://{frontend_path}")

    app.run(host="0.0.0.0", port=port, debug=debug)
