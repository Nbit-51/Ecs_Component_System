#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# build_and_test.sh — one-shot build, test, and benchmark script
#
# Usage:
#   ./scripts/build_and_test.sh [--asan] [--ubsan] [--bench] [--clean]
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build"

ASAN=0; UBSAN=0; RUN_BENCH=0; CLEAN=0
for arg in "$@"; do
  case $arg in
    --asan)  ASAN=1  ;;
    --ubsan) UBSAN=1 ;;
    --bench) RUN_BENCH=1 ;;
    --clean) CLEAN=1 ;;
    *) echo "Unknown option: $arg"; exit 1 ;;
  esac
done

cd "$ROOT"

# ── Fetch doctest if missing ──────────────────────────────────────────────────
bash scripts/fetch_doctest.sh

# ── Clean ────────────────────────────────────────────────────────────────────
if [[ $CLEAN -eq 1 ]]; then
  echo "» Cleaning build directory..."
  rm -rf "$BUILD_DIR"
fi

# ── CMake flags ───────────────────────────────────────────────────────────────
CMAKE_FLAGS=(
  "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
  "-DECS_BUILD_TESTS=ON"
  "-DECS_BUILD_BENCHMARKS=ON"
  "-DECS_BUILD_EXAMPLES=ON"
)
[[ $ASAN  -eq 1 ]] && CMAKE_FLAGS+=("-DECS_ENABLE_ASAN=ON")
[[ $UBSAN -eq 1 ]] && CMAKE_FLAGS+=("-DECS_ENABLE_UBSAN=ON")

# ── Configure ─────────────────────────────────────────────────────────────────
echo "» Configuring..."
cmake -B "$BUILD_DIR" "${CMAKE_FLAGS[@]}"

# ── Build ─────────────────────────────────────────────────────────────────────
echo "» Building ($(nproc) cores)..."
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

# ── Tests ─────────────────────────────────────────────────────────────────────
echo ""
echo "────────────────────────────────────────────────────"
echo " Running unit tests"
echo "────────────────────────────────────────────────────"
"$BUILD_DIR/test_ecs" --reporters=console --duration=true -v

# ── Benchmarks ────────────────────────────────────────────────────────────────
if [[ $RUN_BENCH -eq 1 ]]; then
  echo ""
  echo "────────────────────────────────────────────────────"
  echo " Running benchmarks"
  echo "────────────────────────────────────────────────────"
  "$BUILD_DIR/bench_ecs"
fi

echo ""
echo "✓ All done."
