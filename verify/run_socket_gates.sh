#!/bin/sh
# verify/run_socket_gates.sh -- POSIX (Linux / WSL) socket backend gates.
#
#   BIN=bin/XinYueC_Static bash verify/run_socket_gates.sh
#   BIN=/abs/path/to/XinYueC_Static TIMEOUT_S=300 bash verify/run_socket_gates.sh
#
# This is the POSIX counterpart of verify\run_socket_gates.bat.  It runs the
# SAME seven suites, the SAME negative control, the SAME binary-freshness
# check, and the SAME exit-code discipline -- but with no PowerShell and no
# vcvars64.bat dependency.
#
# [Suites] (identical to the Windows gate)
#   xsocket-stall            socket read-stall / backpressure / reconnect
#   xsocket-pipe             local stream transport
#   udp-directed             UDP directed send/receive
#   udp-bind-family          UDP dual-stack bind
#   xlink-client             XLink client state machine + reconnect
#   xmqtt-tcp-api            XMqtt TCP server API
#   xconsole-shell-backend   console shell SSH/Telnet backends
#
# [Rules]
#   - NEGATIVE CONTROL FIRST: run `--test __gate_selfcheck_must_fail__` and
#     REQUIRE it to be judged FAIL.  If a deliberately failing command looks
#     green, the gate cannot trust any result below -> abort with exit 3.
#     (This is ordered before the freshness check on purpose; T-13 says the
#     negative control goes first.  The Windows gate does freshness first.)
#   - Binary freshness: any .c/.h under Src/, Drive/, Test/ or the top-level
#     main.c newer than BIN means we would be testing an OLD build -> exit 2.
#   - Every suite runs under a wall-clock timeout (`timeout` from coreutils).
#     Exit 124 means "timed out" and is counted as FAIL.  If `timeout` is not
#     available the script prints a clear downgrade notice and runs the suites
#     without a wall-clock guard.
#   - Exit codes are compared as INTEGERS via the shell's own `$?` (no process
#     object whose ExitCode can come back NULL like the Windows PowerShell
#     Start-Process -PassThru trap).
#   - Summary: any FAIL -> exit 1; all green -> exit 0.
#
#   Exit codes: 0 all green / 1 suite(s) failed / 2 precondition (missing or
#   stale binary) / 3 gate broken (negative control not detected).
#
# PURE ASCII ONLY -- this file is a shell script read as-is on every POSIX
# platform; keeping comments ASCII avoids locale-dependent surprises.
# ===========================================================================

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

# BIN: test host path.  Default matches the CMake runtime output directory
# (root CMakeLists.txt sets CMAKE_RUNTIME_OUTPUT_DIRECTORY to <repo>/bin for a
# native Linux build, so the ELF host is bin/XinYueC_Static -- no .exe).
BIN=${BIN:-bin/XinYueC_Static}
# Wall-clock seconds per suite; mirrors the Windows gate's 180000 ms.
TIMEOUT_S=${TIMEOUT_S:-180}

SUITES="xsocket-stall xsocket-pipe udp-directed udp-bind-family xlink-client xmqtt-tcp-api xconsole-shell-backend"

FAILED=0
RAN=0
FAILEDNAMES=

echo "============================================================"
echo " POSIX socket backend gates"
echo " ROOT      = $ROOT"
echo " BIN       = $BIN"
echo " TIMEOUT_S = $TIMEOUT_S"
echo "============================================================"

# ------------------------------------------------------------------ timeout
TIMEOUT_BIN=
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_BIN=timeout
else
    echo "[WARN] 'timeout' (coreutils) not found."
    echo "[WARN] Suites will run WITHOUT a wall-clock guard; a hung suite"
    echo "[WARN] will hang this gate.  Install coreutils to fix that."
fi

# --------------------------------------------------------------- precondition
if [ ! -e "$BIN" ]; then
    echo "[ERROR] test host not found: $BIN"
    echo "        Build it first on Linux/WSL, e.g.:"
    echo "          cmake -S . -B <build> -G Ninja"
    echo "          cmake --build <build> --target XinYueC_Static"
    echo "        ...then pass BIN=<build-output>/XinYueC_Static"
    exit 2
fi
if [ ! -x "$BIN" ]; then
    echo "[ERROR] test host is not executable: $BIN"
    exit 2
fi

# ------------------------------------------------------------- run one command
# run_step <display-name> <suite-flag>
# Sets FAILED / RAN / FAILEDNAMES; always returns 0 so the gate keeps going.
run_step() {
    _name=$1
    _flag=$2
    RAN=$((RAN + 1))
    echo
    echo "---- [step $RAN] $_name ----"
    if [ -n "$TIMEOUT_BIN" ]; then
        $TIMEOUT_BIN "$TIMEOUT_S" "$BIN" --test "$_flag"
    else
        "$BIN" --test "$_flag"
    fi
    _rc=$?
    if [ "$_rc" -eq 124 ]; then
        echo "[FAIL] $_name timed out after ${TIMEOUT_S}s"
        FAILED=$((FAILED + 1))
        FAILEDNAMES="$FAILEDNAMES [$_name:timeout]"
    elif [ "$_rc" -ne 0 ]; then
        echo "[FAIL] $_name exit=$_rc"
        FAILED=$((FAILED + 1))
        FAILEDNAMES="$FAILEDNAMES [$_name]"
    else
        echo "[OK] $_name"
    fi
    return 0
}

# ------------------------------------------------------ negative control FIRST
echo
echo "[gate] negative-control self-check (must FAIL) ..."
if [ -n "$TIMEOUT_BIN" ]; then
    $TIMEOUT_BIN "$TIMEOUT_S" "$BIN" --test __gate_selfcheck_must_fail__
else
    "$BIN" --test __gate_selfcheck_must_fail__
fi
_self_rc=$?
echo "[gate] selfcheck exit=$_self_rc (non-zero expected)"
if [ "$_self_rc" -eq 0 ]; then
    echo
    echo "[FATAL] The gate can NOT detect a failing command (negative control"
    echo "        passed).  Every result below would be meaningless -- aborting."
    exit 3
fi
echo "[gate] negative control behaved as expected (gate can detect failures)"

# ------------------------------------------------------------------ freshness
# Any source newer than the binary -> we would test a stale build.
_stale=
if [ -d Src ]; then _stale=$(find Src -type f \( -name '*.c' -o -name '*.h' \) -newer "$BIN" -print -quit 2>/dev/null); fi
if [ -z "$_stale" ] && [ -d Drive ]; then _stale=$(find Drive -type f \( -name '*.c' -o -name '*.h' \) -newer "$BIN" -print -quit 2>/dev/null); fi
if [ -z "$_stale" ] && [ -d Test ]; then _stale=$(find Test -type f \( -name '*.c' -o -name '*.h' \) -newer "$BIN" -print -quit 2>/dev/null); fi
if [ -z "$_stale" ] && [ -f main.c ]; then _stale=$(find main.c -newer "$BIN" -print -quit 2>/dev/null); fi
if [ -n "$_stale" ]; then
    echo "[STALE] binary is older than: $_stale"
    echo "[ERROR] $BIN is OLDER than the sources -- rebuild first"
    echo "        (this repo's ninja has no header dependency tracking; a"
    echo "         full clean rebuild is required after public-header changes)"
    exit 2
fi
echo "[OK] binary is newer than the sources"

# --------------------------------------------------------------------- suites
for _suite in $SUITES; do
    run_step "$_suite" "$_suite"
done

# -------------------------------------------------------------------- summary
echo
echo "============================================================"
if [ "$FAILED" -eq 0 ]; then
    echo " SOCKET GATES ALL GREEN - ran $RAN suites"
    echo "============================================================"
    exit 0
fi
echo " SOCKET GATES FAILED $FAILED of $RAN suites"
echo " failing:$FAILEDNAMES"
echo "============================================================"
exit 1
