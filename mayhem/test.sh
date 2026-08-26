#!/usr/bin/env bash
#
# mayhem/test.sh — RUN RE/flex's OWN upstream test suite (prebuilt by mayhem/build.sh; never compiles).
#
# Upstream's suite is tests/Make's `all` list — test_bits test_ranges lorem streams test rtest ptest
# btest stest — plus lazytest (same directory, same style, just not wired into `all`). Each program is
# a known-answer/assertion suite: it prints FAILED/ERROR and exits non-zero on any mismatch (rtest
# asserts regex-match results, lorem cross-checks RE/flex against Boost.Regex + PCRE2 on real corpora,
# streams asserts encoding conversions, test asserts a regex matches a subject, ...). A neutered
# `exit(0)` library fails these assertions, so the oracle is behavioral, not exit-status-only.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"
SRC="${SRC:-/mayhem}"
cd "$SRC"

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

cd tests

# run_one <prog> <expected-output-marker> [args...]
# PASS requires BOTH exit 0 AND the program's known-answer output marker in the log — so a program
# neutered to exit(0) (printing nothing) fails, and a wrong-answer run (FAILED/ERROR) fails.
passed=0 failed=0
run_one() {
  local prog="$1" marker="$2"; shift 2
  if [ ! -x "./$prog" ]; then
    echo "test.sh: missing prebuilt test runner tests/$prog — mayhem/build.sh bug" >&2
    failed=$(( failed + 1 ))
    return
  fi
  if "./$prog" "$@" > "/tmp/$prog.log" 2>&1 && grep -qF -- "$marker" "/tmp/$prog.log"; then
    echo "PASS $prog"
    passed=$(( passed + 1 ))
  else
    echo "FAIL $prog (nonzero exit or expected output marker missing: $marker)"
    tail -20 "/tmp/$prog.log" >&2
    failed=$(( failed + 1 ))
  fi
}

# The full upstream suite (tests/Make `all` order) + lazytest, each with a known-answer marker.
run_one test_bits   "63 bits in alnum"
run_one test_ranges "elapsed real time"
run_one lorem       "Scanning lorem file stream with >>"
run_one streams     "File converted from UTF-16 to UTF-8"
run_one test        "Scan 1 'ababb'" '(a|b)*abb' 'ababb'
run_one rtest       "DONE"
run_one lazytest    "ALL OK"
run_one ptest       "DONE"
run_one btest       "DONE"
run_one stest       "DONE"

cd "$SRC"
emit_ctrf "reflex-upstream-suite" "$passed" "$failed" 0
