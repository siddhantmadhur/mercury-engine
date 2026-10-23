#!/usr/bin/env bash
#
# Installs everything needed to build Mercury on macOS (MoltenVK Vulkan backend):
# build tools (cmake, ninja), Vulkan loader + headers, MoltenVK, submodules,
# and DXC (DirectX Shader Compiler for HLSL -> SPIR-V).
#
# Run from anywhere in the repository.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "==> Setting up Mercury Engine on macOS..."

# 1. Check for Homebrew
if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew not found. Please install Homebrew from https://brew.sh first." >&2
  exit 1
fi

echo "==> Installing build tools and Vulkan dependencies via Homebrew..."
brew install cmake ninja molten-vk vulkan-loader vulkan-headers

# 2. Update submodules
echo "==> Updating git submodules..."
git -C "${PROJECT_ROOT}" submodule update --init --recursive

# 3. Check for or install DXC
install_dxc() {
  if command -v dxc >/dev/null 2>&1; then
    echo "dxc found on PATH: $(command -v dxc)"
    return
  fi
  if [[ -n "${VULKAN_SDK:-}" && -x "${VULKAN_SDK}/bin/dxc" ]]; then
    echo "dxc found in Vulkan SDK: ${VULKAN_SDK}/bin/dxc"
    return
  fi
  if [[ -x "${PROJECT_ROOT}/bin/vulkansdk/macOS/bin/dxc" ]]; then
    echo "dxc already installed: ${PROJECT_ROOT}/bin/vulkansdk/macOS/bin/dxc"
    return
  fi

  echo "==> Fetching DXC from LunarG Vulkan SDK for macOS..."
  SDK_VER="$(curl -s https://vulkan.lunarg.com/sdk/latest/mac.txt || echo "1.4.363.0")"
  TMP_DIR="$(mktemp -d)"
  trap 'rm -rf "${TMP_DIR}"' EXIT

  echo "Downloading Vulkan SDK ${SDK_VER} installer..."
  curl -fL "https://sdk.lunarg.com/sdk/download/${SDK_VER}/mac/vulkansdk-macos-${SDK_VER}.zip" \
    -o "${TMP_DIR}/vulkansdk.zip"

  unzip -q "${TMP_DIR}/vulkansdk.zip" -d "${TMP_DIR}/extracted"
  INSTALLER="$(find "${TMP_DIR}/extracted" -name "vulkansdk-macOS-*" -type f -perm +111 | head -n 1)"

  if [[ -n "${INSTALLER}" && -x "${INSTALLER}" ]]; then
    echo "Installing core SDK components into bin/vulkansdk..."
    "${INSTALLER}" install \
      --root "${PROJECT_ROOT}/bin/vulkansdk" \
      --accept-licenses --default-answer --confirm-command
  else
    echo "Could not execute SDK installer; please install LunarG Vulkan SDK manually." >&2
    exit 1
  fi
}

install_dxc

echo
echo "==> Setup complete! Next steps:"
echo "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug"
echo "  cmake --build build"
