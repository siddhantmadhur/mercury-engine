#!/usr/bin/env bash
#
# Installs everything needed to build Mercury on Linux (Vulkan backend):
# compilers/build tools, the Vulkan loader + headers + validation layers,
# GLFW's X11/Wayland dependencies, and DXC (HLSL -> SPIR-V).
#
# Supports Debian/Ubuntu (apt) and Arch (pacman). Run from anywhere; uses sudo
# for package installs.
#
# DXC: Arch packages it (directx-shader-compiler). Debian/Ubuntu do not, so the
# official Linux release is downloaded into bin/dxc/ (git-ignored), which
# CMake searches automatically. If you have the LunarG Vulkan SDK installed and
# VULKAN_SDK set, its dxc is used instead and this download is skipped.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DXC_VERSION="v1.9.2609"
DXC_ARCHIVE="linux_dxc_2026_09_28.x86_x64.tar.gz"

install_apt() {
  sudo apt-get update
  sudo apt-get install -y \
    build-essential cmake ninja-build git curl \
    libvulkan-dev vulkan-validationlayers vulkan-tools spirv-tools \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols pkg-config
}

install_pacman() {
  sudo pacman -S --needed --noconfirm \
    base-devel cmake ninja git curl \
    vulkan-icd-loader vulkan-headers vulkan-validation-layers vulkan-tools \
    spirv-tools directx-shader-compiler \
    libx11 libxrandr libxinerama libxcursor libxi \
    wayland libxkbcommon wayland-protocols pkgconf
}

install_dxc() {
  if command -v dxc >/dev/null 2>&1; then
    echo "dxc found on PATH: $(command -v dxc)"
    return
  fi
  if [[ -n "${VULKAN_SDK:-}" && -x "${VULKAN_SDK}/bin/dxc" ]]; then
    echo "dxc found in Vulkan SDK: ${VULKAN_SDK}/bin/dxc"
    return
  fi
  if [[ -x "${PROJECT_ROOT}/bin/dxc/bin/dxc" ]]; then
    echo "dxc already downloaded: ${PROJECT_ROOT}/bin/dxc/bin/dxc"
    return
  fi
  if [[ "$(uname -m)" != "x86_64" ]]; then
    echo "No prebuilt dxc for $(uname -m); install the LunarG Vulkan SDK" \
         "(https://vulkan.lunarg.com) and set VULKAN_SDK." >&2
    exit 1
  fi

  echo "Downloading dxc ${DXC_VERSION} into bin/dxc ..."
  mkdir -p "${PROJECT_ROOT}/bin/dxc"
  curl -fL \
    "https://github.com/microsoft/DirectXShaderCompiler/releases/download/${DXC_VERSION}/${DXC_ARCHIVE}" \
    | tar -xz -C "${PROJECT_ROOT}/bin/dxc"
  "${PROJECT_ROOT}/bin/dxc/bin/dxc" --version
}

if command -v apt-get >/dev/null 2>&1; then
  install_apt
elif command -v pacman >/dev/null 2>&1; then
  install_pacman
else
  echo "Unsupported distro: install the packages listed in this script" \
       "manually." >&2
  exit 1
fi

install_dxc

echo
echo "Done. Next:"
echo "  git submodule update --init --recursive"
echo "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug"
echo "  cmake --build build"
