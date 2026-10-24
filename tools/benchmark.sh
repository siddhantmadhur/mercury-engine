#!/usr/bin/env bash
# Benchmarks model_viewer maps with the profiler's bench mode.
#
#   tools/benchmark.sh [build-dir] [map...]
#
# Each map runs with a fixed 1/60 s time step (the flythrough is at the same
# place on the same frame every run) and no vsync. It prints one summary per
# map and saves it, with a screenshot taken right after the measured frames, under
# $BENCH_DIR (default: bench/<date>-<commit>).
#
#   WARMUP=300 FRAMES=1000   frames skipped, frames measured
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(cd "${1:-$root/build-release}" && pwd)"
shift || true
maps=("$@")
if [ ${#maps[@]} -eq 0 ]; then maps=(san_miguel sponza living_room cornell_original); fi

warmup="${WARMUP:-300}"
frames="${FRAMES:-1000}"
out="${BENCH_DIR:-$root/bench/$(date +%Y%m%d-%H%M%S)-$(git -C "$root" rev-parse --short HEAD)}"
mkdir -p "$out"
exe="$build/examples/model_viewer/model_viewer"

cd "$root/examples/model_viewer"
for map in "${maps[@]}"; do
  echo "== $map"
  MERCURY_FIXED_DT=1 MERCURY_VSYNC=0 \
  MERCURY_BENCH_WARMUP="$warmup" MERCURY_BENCH_FRAMES="$frames" \
  MERCURY_BENCH_OUT="$out/$map.txt" \
  MERCURY_BENCH_CAPTURE="$out/$map.png" \
    "$exe" --map "res://maps/$map.mmap" > "$out/$map.log" 2>&1 || true
  grep -E "Loaded .* in|Loaded .* from the cache|settled|probes from the cache" "$out/$map.log" || true
  cat "$out/$map.txt" 2>/dev/null || echo "(no summary; see $out/$map.log)"
done
echo "results in $out"
