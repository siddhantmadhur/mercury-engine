#!/usr/bin/env bash
#
# Builds the pinned V8 (third_party/v8/VERSION) as a static monolith library
# with GN. This is the fallback for hosts where V8's Bazel build cannot be
# used; the normal Bazel build does not run this script.
#
# Takes ~30-60 min and ~15 GB of disk. Idempotent: finished steps are skipped.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
V8_ROOT="$ROOT/third_party/v8"
VERSION="$(tr -d '[:space:]' < "$V8_ROOT/VERSION")"
OUT="$V8_ROOT/v8/out/release"
LIB="$OUT/obj/libv8_monolith.a"

if [[ -f "$LIB" && "$(cat "$V8_ROOT/.built_version" 2>/dev/null || true)" == "$VERSION" ]]; then
  echo "V8 $VERSION already built: $LIB"
  exit 0
fi

# Fetch sources (and the Bazel compatibility patch) first.
"$ROOT/scripts/fetch_v8.sh"

export PATH="$V8_ROOT/depot_tools:$PATH"
cd "$V8_ROOT/v8"

case "$(uname -s)" in
  Linux)
    # Chromium's build emits ELF CREL relocations when it links with lld, and
    # GNU ld (which links the engine) cannot read them. Switch them off in the
    # checkout (idempotent).
    sed -i 's/if (is_linux \&\& use_lld \&\& current_cpu != "arm" \&\& current_cpu != "s390x") {/if (false \&\& is_linux \&\& use_lld \&\& current_cpu != "arm" \&\& current_cpu != "s390x") {/' \
      build/config/compiler/BUILD.gn
    GN_ARGS="$(cat "$V8_ROOT/args.gn")"
    ;;
  Darwin)
    # use_sysroot/use_glib are Linux-only GN args.
    GN_ARGS="$(grep -v -E '^[[:space:]]*(use_sysroot|use_glib)[[:space:]]*=' "$V8_ROOT/args.gn")"
    ;;
  *)
    GN_ARGS="$(grep -v -E '^[[:space:]]*(use_sysroot|use_glib)[[:space:]]*=' "$V8_ROOT/args.gn")"
    ;;
esac

case "$(uname -m)" in
  arm64|aarch64) CPU=arm64 ;;
  *) CPU=x64 ;;
esac

gn gen "$OUT" --args="$GN_ARGS
target_cpu=\"$CPU\""
ninja -C "$OUT" v8_monolith
echo "$VERSION" > "$V8_ROOT/.built_version"
echo "Built $LIB"
