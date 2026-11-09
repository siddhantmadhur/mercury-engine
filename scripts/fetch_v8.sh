#!/usr/bin/env bash
#
# Fetches the pinned V8 (third_party/v8/VERSION) with depot_tools and makes its
# Bazel files usable from Mercury. Does not build V8: Bazel builds it on demand.
# Idempotent: an up-to-date checkout and patch are skipped.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
V8_ROOT="$ROOT/third_party/v8"
VERSION="$(tr -d '[:space:]' < "$V8_ROOT/VERSION")"
DEPOT="$V8_ROOT/depot_tools"
STAMP="$V8_ROOT/.fetched_version"

if [[ -f "$STAMP" && "$(cat "$STAMP" 2>/dev/null || true)" == "$VERSION" ]]; then
  # The patch script is idempotent; run it so changes to the compatibility
  # patch apply without refetching V8.
  "$ROOT/scripts/v8_bazel_compat.py" "$V8_ROOT/v8"
  echo "V8 $VERSION already fetched"
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

"$ROOT/scripts/v8_bazel_compat.py" "$V8_ROOT/v8"

echo "$VERSION" > "$STAMP"
echo "Fetched and patched V8 $VERSION in $V8_ROOT/v8"
