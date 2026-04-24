#!/usr/bin/env bash
# =============================================================================
# compile.sh — Build all C disk scheduling engines
#
# Usage:
#   chmod +x compile.sh
#   ./compile.sh            # compile all
#   ./compile.sh --clean    # remove binaries then recompile
#   ./compile.sh --test     # compile + run quick sanity tests
#
# Output binaries: fcfs  sstf  scan  cscan  (same directory)
# =============================================================================

set -euo pipefail

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# ── Paths ─────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ── Compiler settings ─────────────────────────────────────────────────────────
CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -O2 -std=c11"

# ── Source → binary map ───────────────────────────────────────────────────────
declare -A TARGETS=(
    [fcfs.c]="fcfs"
    [sstf.c]="sstf"
    [scan.c]="scan"
    [cscan.c]="cscan"
)

# =============================================================================
# helpers
# =============================================================================

log_info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
log_ok()      { echo -e "${GREEN}[OK]${RESET}    $*"; }
log_warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
log_error()   { echo -e "${RED}[ERROR]${RESET} $*"; }
log_section() { echo -e "\n${BOLD}${CYAN}── $* ──${RESET}"; }

# Check that required compiler is present
check_compiler() {
    if ! command -v "$CC" &>/dev/null; then
        log_error "Compiler '$CC' not found."
        echo "  Install with:"
        echo "    Ubuntu/Debian : sudo apt-get install gcc"
        echo "    macOS         : xcode-select --install"
        echo "    Fedora/RHEL   : sudo dnf install gcc"
        exit 1
    fi
    log_info "Compiler : $($CC --version | head -n1)"
}

# Remove all compiled binaries
clean() {
    log_section "Cleaning binaries"
    for src in "${!TARGETS[@]}"; do
        bin="${TARGETS[$src]}"
        if [[ -f "$bin" ]]; then
            rm -f "$bin"
            log_info "Removed  : $bin"
        fi
    done
    log_ok "Clean complete."
}

# Compile a single source file
compile_one() {
    local src="$1"
    local bin="${TARGETS[$src]}"

    if [[ ! -f "$src" ]]; then
        log_error "Source not found: $SCRIPT_DIR/$src"
        return 1
    fi

    log_info "Compiling : $src  →  $bin"
    if $CC $CFLAGS -o "$bin" "$src" -lm 2>&1; then
        log_ok "Built     : ./$bin"
    else
        log_error "Failed    : $src"
        return 1
    fi
}

# Compile all sources
compile_all() {
    log_section "Compiling all schedulers"
    local failed=0

    for src in "${!TARGETS[@]}"; do
        compile_one "$src" || failed=$((failed + 1))
    done

    echo ""
    if [[ $failed -eq 0 ]]; then
        log_ok "All ${#TARGETS[@]} binaries compiled successfully."
    else
        log_error "$failed compilation(s) failed."
        exit 1
    fi
}

# =============================================================================
# sanity tests
# =============================================================================

# Run a binary and validate JSON output contains expected keys
run_test() {
    local label="$1"
    local cmd="$2"
    local expect_seq="$3"    # substring expected in sequence
    local expect_st="$4"     # exact seek_time value

    local output
    output=$(eval "$cmd" 2>/dev/null) || {
        log_error "FAIL [$label] — binary crashed or not found"
        return 1
    }

    # Must contain "sequence" and "seek_time" keys
    if ! echo "$output" | grep -q '"sequence"'; then
        log_error "FAIL [$label] — missing 'sequence' key"
        echo "       Output: $output"
        return 1
    fi
    if ! echo "$output" | grep -q '"seek_time"'; then
        log_error "FAIL [$label] — missing 'seek_time' key"
        echo "       Output: $output"
        return 1
    fi

    # Validate seek_time value if provided
    if [[ -n "$expect_st" ]]; then
        local actual_st
        actual_st=$(echo "$output" | grep -o '"seek_time": *[0-9]*' | grep -o '[0-9]*$')
        if [[ "$actual_st" != "$expect_st" ]]; then
            log_error "FAIL [$label] — seek_time expected=$expect_st got=$actual_st"
            echo "       Output: $output"
            return 1
        fi
    fi

    log_ok "PASS [$label]  →  $output"
}

run_all_tests() {
    log_section "Running sanity tests"

    local pass=0
    local fail=0

    # ── FCFS tests ────────────────────────────────────────────────
    # head=50, disk=200, dir=0, queue=82,170,43,140
    # Seek: |82-50|+|170-82|+|43-170|+|140-43| = 32+88+127+97 = 344
    run_test "FCFS basic"        "./fcfs 50 200 0 82,170,43,140"  "82"  "344" && pass=$((pass+1)) || fail=$((fail+1))

    # head=50, disk=200, dir=0, queue=82
    # Seek: |82-50| = 32
    run_test "FCFS single req"   "./fcfs 50 200 0 82"             "82"  "32"  && pass=$((pass+1)) || fail=$((fail+1))

    # Empty queue → seek_time must be 0
    run_test "FCFS empty queue"  "./fcfs 50 200 0 ''"             ""    "0"   && pass=$((pass+1)) || fail=$((fail+1))

    # ── SSTF tests ────────────────────────────────────────────────
    # head=50, disk=200, dir=0, queue=82,170,43,140
    # Nearest from 50: 43(7) → from 43: 82(39) → from 82: 140(58) → from 140: 170(30)
    # Seek: 7+39+58+30 = 134
    run_test "SSTF basic"        "./sstf 50 200 0 82,170,43,140"  "43"  "134" && pass=$((pass+1)) || fail=$((fail+1))

    # head=100, queue=same both sides equidistant tie-break
    run_test "SSTF single req"   "./sstf 100 200 0 100"           "100" "0"   && pass=$((pass+1)) || fail=$((fail+1))

    # Empty queue
    run_test "SSTF empty queue"  "./sstf 50 200 0 ''"             ""    "0"   && pass=$((pass+1)) || fail=$((fail+1))

    # ── SCAN tests ────────────────────────────────────────────────
    # head=50, disk=200, dir=1 (right), queue=82,170,43,140
    # Right: 82,140,170 → boundary 199 → left (descending): 43
    # Seek: 32+58+30+29 + (199-170) + (199-43) = 32+58+30+29+29+156 = 334
    # Actually: 50→82(32)→140(58)→170(30)→199(29) then 199→43(156) = 305
    run_test "SCAN right"        "./scan 50 200 1 82,170,43,140"  "199" ""    && pass=$((pass+1)) || fail=$((fail+1))

    # head=50, dir=0 (left), queue=82,170,43,140
    # Left: 43 → boundary 0 → right (ascending): 82,140,170
    run_test "SCAN left"         "./scan 50 200 0 82,170,43,140"  "0"   ""    && pass=$((pass+1)) || fail=$((fail+1))

    # Empty queue
    run_test "SCAN empty queue"  "./scan 50 200 1 ''"             ""    "0"   && pass=$((pass+1)) || fail=$((fail+1))

    # ── C-SCAN tests ──────────────────────────────────────────────
    # head=50, disk=200, dir=1 (right), queue=82,170,43,140
    # Right: 82,140,170 → boundary 199 → jump to 0 → ascending: 43
    run_test "CSCAN right"       "./cscan 50 200 1 82,170,43,140" "199" ""    && pass=$((pass+1)) || fail=$((fail+1))

    # head=50, dir=0 (left), queue=82,170,43,140
    # Left (desc): 43 → boundary 0 → jump to 199 → desc: 170,140,82
    run_test "CSCAN left"        "./cscan 50 200 0 82,170,43,140" "0"   ""    && pass=$((pass+1)) || fail=$((fail+1))

    # Empty queue
    run_test "CSCAN empty queue" "./cscan 50 200 1 ''"            ""    "0"   && pass=$((pass+1)) || fail=$((fail+1))

    # ── Error handling tests ───────────────────────────────────────
    # Negative head → must output JSON with "error" key
    output=$(./fcfs -1 200 0 82,170 2>/dev/null || true)
    if echo "$output" | grep -q '"error"'; then
        log_ok "PASS [FCFS negative head]  →  error caught"
        pass=$((pass+1))
    else
        log_error "FAIL [FCFS negative head]  →  $output"
        fail=$((fail+1))
    fi

    # Out-of-range request (250 >= disk_size 200)
    output=$(./fcfs 50 200 0 82,250 2>/dev/null || true)
    if echo "$output" | grep -q '"error"'; then
        log_ok "PASS [FCFS out-of-range]   →  error caught"
        pass=$((pass+1))
    else
        log_error "FAIL [FCFS out-of-range]   →  $output"
        fail=$((fail+1))
    fi

    # Summary
    echo ""
    echo -e "${BOLD}Test Results: ${GREEN}$pass passed${RESET} / ${RED}$fail failed${RESET}"
    [[ $fail -gt 0 ]] && exit 1
    return 0
}

# =============================================================================
# entry point
# =============================================================================

main() {
    echo -e "${BOLD}${CYAN}"
    echo "╔══════════════════════════════════════════════╗"
    echo "║   Disk Scheduler — C Engine Build System     ║"
    echo "╚══════════════════════════════════════════════╝"
    echo -e "${RESET}"

    check_compiler

    local do_clean=0
    local do_test=0

    for arg in "$@"; do
        case "$arg" in
            --clean)  do_clean=1 ;;
            --test)   do_test=1  ;;
            --help|-h)
                echo "Usage: $0 [--clean] [--test]"
                echo "  --clean  Remove binaries before compiling"
                echo "  --test   Run sanity tests after compilation"
                exit 0
                ;;
            *)
                log_warn "Unknown argument: $arg (ignored)"
                ;;
        esac
    done

    [[ $do_clean -eq 1 ]] && clean

    compile_all

    [[ $do_test -eq 1 ]] && run_all_tests

    log_section "Build complete"
    echo -e "Binaries ready in: ${BOLD}$SCRIPT_DIR/${RESET}"
    echo -e "  ${GREEN}./fcfs${RESET}   ./sstf   ./scan   ./cscan"
    echo ""
    echo -e "Example:"
    echo -e "  ${CYAN}./fcfs 50 200 0 82,170,43,140${RESET}"
}

main "$@"