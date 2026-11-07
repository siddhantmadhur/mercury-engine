#!/usr/bin/env bash
#
# Downloads the pinned esbuild binary (the TypeScript -> JavaScript bundler used for
# game scripts) into bin/esbuild/. Node is not needed. The npm tarball is checked
# against the SHA-256 written below.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ESBUILD_VERSION="0.28.2"

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)  PLATFORM=linux-x64;    SHA256=9573bb2233aab0f9ea7647d5cca9726113cc1768de61d66b17267f4db84488f6 ;;
  Linux-aarch64) PLATFORM=linux-arm64;  SHA256=a96dbfa41d3ef5dbd1ef22b1c10d5187be9267e86093a870f06402a7ec931596 ;;
  Darwin-x86_64) PLATFORM=darwin-x64;   SHA256=abb6a7a895aaf2cc0df36fecc3bf0042479ffec51a643a056d54e9109f27db55 ;;
  Darwin-arm64)  PLATFORM=darwin-arm64; SHA256=1980cde09749094452b20d36ff267585ccb3f72749c7bc97291cd9996ccf5a2a ;;
  *) echo "No prebuilt esbuild for $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

DEST="${PROJECT_ROOT}/bin/esbuild"
if [[ -x "${DEST}/esbuild" ]] && [[ "$("${DEST}/esbuild" --version)" == "${ESBUILD_VERSION}" ]]; then
  echo "esbuild ${ESBUILD_VERSION} already installed: ${DEST}/esbuild"
  exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT
echo "Downloading esbuild ${ESBUILD_VERSION} (${PLATFORM}) into bin/esbuild ..."
curl -fL "https://registry.npmjs.org/@esbuild/${PLATFORM}/-/${PLATFORM}-${ESBUILD_VERSION}.tgz" \
  -o "${TMP}/esbuild.tgz"
if command -v sha256sum >/dev/null 2>&1; then
  ACTUAL="$(sha256sum "${TMP}/esbuild.tgz" | cut -d' ' -f1)"
else
  ACTUAL="$(shasum -a 256 "${TMP}/esbuild.tgz" | cut -d' ' -f1)"
fi
if [[ "${ACTUAL}" != "${SHA256}" ]]; then
  echo "esbuild checksum mismatch: expected ${SHA256}, got ${ACTUAL}" >&2
  exit 1
fi
tar -xzf "${TMP}/esbuild.tgz" -C "${TMP}"
mkdir -p "${DEST}"
install -m 755 "${TMP}/package/bin/esbuild" "${DEST}/esbuild"
"${DEST}/esbuild" --version
