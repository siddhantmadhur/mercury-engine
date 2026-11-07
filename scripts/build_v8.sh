#!/usr/bin/env bash
#
# Builds the pinned V8 (third_party/v8/VERSION) as a static monolith library.
# Takes ~30-60 min and ~15 GB of disk. Idempotent: finished steps are skipped.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
V8_ROOT="$ROOT/third_party/v8"
VERSION="$(tr -d '[:space:]' < "$V8_ROOT/VERSION")"
DEPOT="$V8_ROOT/depot_tools"
OUT="$V8_ROOT/v8/out/release"
LIB="$OUT/obj/libv8_monolith.a"

if [[ -f "$LIB" && "$(cat "$V8_ROOT/.built_version" 2>/dev/null || true)" == "$VERSION" ]]; then
  echo "V8 $VERSION already built: $LIB"
  exit 0
fi

if [[ ! -d "$DEPOT" ]]; then
  git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT"
fi
export PATH="$DEPOT:$PATH"

cd "$V8_ROOT"
if [[ ! -d v8 ]]; then
  fetch --no-history v8
fi
cd v8
git fetch --depth 1 origin "refs/tags/$VERSION:refs/tags/$VERSION" 2>/dev/null || true
git checkout "$VERSION"
gclient sync -D --no-history

# Chromium's build emits ELF CREL relocations when it links with lld, and GNU ld (which links
# the engine) cannot read them. Switch them off in the checkout (idempotent).
sed -i 's/if (is_linux \&\& use_lld \&\& current_cpu != "arm" \&\& current_cpu != "s390x") {/if (false \&\& is_linux \&\& use_lld \&\& current_cpu != "arm" \&\& current_cpu != "s390x") {/' \
  build/config/compiler/BUILD.gn

case "$(uname -m)" in
  arm64|aarch64) CPU=arm64 ;;
  *) CPU=x64 ;;
esac

gn gen "$OUT" --args="$(grep -v '^\s*$' "$V8_ROOT/args.gn") target_cpu=\"$CPU\""
ninja -C "$OUT" v8_monolith
echo "$VERSION" > "$V8_ROOT/.built_version"
echo "Built $LIB"
