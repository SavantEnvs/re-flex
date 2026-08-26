#!/usr/bin/env bash
#
# mayhem/build.sh — build RE/flex fuzz targets + the upstream test suite.
#
#   /mayhem/fuzz_range           sanitized + libFuzzer  -> target `range`  (Unicode/POSIX class tables + reflex::convert)
#   /mayhem/fuzz_reflex          sanitized + libFuzzer  -> target `reflex` (Pattern/Matcher engine harness)
#   /mayhem/fuzz_*-standalone    sanitized, run-once reproducers (not Mayhemfile targets)
#   tests/{test_bits,test_ranges,lorem,streams,test,rtest,ptest,btest,stest,lazytest}
#                                normal flags           -> upstream known-answer test suite, run by mayhem/test.sh
#
# Order matters: the upstream test binaries statically link a NORMAL-flags libreflex.a, so they are
# built (and smoke-run by upstream's own Make rules) FIRST; then lib/src are cleaned and rebuilt with
# $SANITIZER_FLAGS for the fuzz targets. Uses upstream's quick Make files (no autotools, no network).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}"
: "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "${SRC:-/mayhem}"
SRC="${SRC:-/mayhem}"

# ── 1) Upstream test suite, NORMAL flags (clean, independent build) ─────────────────────────────
# Build the plain library + reflex, then every test program from tests/Make's `all` list plus
# lazytest. Upstream's rules also RUN each test as it builds — that's upstream behavior; test.sh
# re-runs the prebuilt binaries and does the counting.
make -C lib  -f Make clean >/dev/null
make -C src  -f Make clean >/dev/null
make -C tests -f Make clean >/dev/null
make -C lib -f Make -j"$MAYHEM_JOBS" libreflex.a CPP="$CXX" CMFLAGS="$COVERAGE_FLAGS"
make -C src -f Make -j"$MAYHEM_JOBS" reflex CPP="$CXX" CMFLAGS="$COVERAGE_FLAGS"
mkdir -p bin && cp -f src/reflex bin/reflex
make -C tests -f Make CPP="$CXX" CXX="$CXX -std=gnu++11" \
     INCPCRE2=/usr/include LIBPCRE2=-lpcre2-8 \
     INCBOOST=/usr/include LIBBOOST=-lboost_regex \
     all lazytest

# ── 2) Sanitized rebuild of the library (fuzzed code is instrumented) ────────────────────────
# COVERAGE: $SANITIZER_FLAGS is ASan+UBSan only — it carries NO SanitizerCoverage. Without
# -fsanitize=fuzzer-no-link the libreflex objects get no edge counters, so libFuzzer runs BLIND over
# the actual fuzzed code (targets execute fine but Mayhem records edges_covered=0 forever, §6.2 item
# 11). `-fsanitize=fuzzer` at LINK supplies only the runtime; instrumentation is a per-TU COMPILE
# flag, so it must appear both here and on the harness compiles below.
FUZZ_COV="-fsanitize=fuzzer-no-link"

make -C lib -f Make clean >/dev/null
make -C src -f Make clean >/dev/null
make -C lib -f Make -j"$MAYHEM_JOBS" libreflex.a CPP="$CXX" CMFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS $FUZZ_COV"

# ── 3) libFuzzer harnesses + standalone reproducers ────────────────────────────────────────────
# The standalone driver is LLVM's run-once main (C). Compile it as a C object FIRST with $CC —
# clang++ would mangle its LLVMFuzzerTestOneInput reference and miss the harness's extern "C" def.
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o /tmp/standalone_main.o
for h in range reflex; do
  # shellcheck disable=SC2086
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $FUZZ_COV $LIB_FUZZING_ENGINE -std=gnu++11 -Iinclude \
       mayhem/fuzz_$h.cpp lib/libreflex.a -o "$SRC/fuzz_$h"
  # shellcheck disable=SC2086
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $FUZZ_COV -std=gnu++11 -Iinclude \
       mayhem/fuzz_$h.cpp /tmp/standalone_main.o lib/libreflex.a -o "$SRC/fuzz_$h-standalone"
done

echo "build.sh: built /mayhem/fuzz_{range,reflex}(+-standalone) and the upstream test suite in tests/"
