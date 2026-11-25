#!/usr/bin/env bash
#
# mayhem/test.sh — RUN the VibeOS functional self-test built by mayhem/build.sh.
# exit 0 = pass. Upstream ships no host-runnable tests (it is a bare-metal aarch64 OS run in
# QEMU), so /mayhem/vibeos_selftest (mayhem/harness/selftest*.c, normal flags) checks known
# answers of the fuzzed subsystems on the committed seeds: FAT32 reads of named files with
# exact contents, VFS create/write/rename/delete round-trips, ELF load + relocation values,
# the DTB memory node, ARP replies and checksums, and the kernel's strtok_r/snprintf.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

BIN="$SRC/vibeos_selftest"
if [ ! -x "$BIN" ]; then
  echo "missing $BIN — mayhem/build.sh did not build the self-test" >&2
  emit_ctrf "vibeos-selftest" 0 1 0
  exit 1
fi
out="$("$BIN" 2>&1)"; rc=$?
printf '%s\n' "$out"
passed=$(printf '%s\n' "$out" | grep -c '^PASS ' || true)
failed=$(printf '%s\n' "$out" | grep -c '^FAIL ' || true)
# A crash or early exit loses the remaining checks: count it as a failure.
if [ "$rc" -ne 0 ] && [ "$failed" -eq 0 ]; then failed=1; fi
# Every check must have run (the self-test prints one line per check).
EXPECTED=41
if [ $((passed + failed)) -lt "$EXPECTED" ]; then failed=$((failed + EXPECTED - passed - failed)); fi
emit_ctrf "vibeos-selftest" "$passed" "$failed" 0
