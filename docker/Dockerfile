# Build environment for Mercury Engine CI (Linux / Vulkan backend).
#
# Everything scripts/setup_project.pl would install on Ubuntu is baked in here
# so CI jobs do not spend time on apt-get/rustup/cargo install. Keep this in
# sync with install_apt() in scripts/setup_project.pl.
#
# The image is built and pushed to the GitLab container registry by the
# `build-image-job` in .gitlab-ci.yml whenever something under docker/ changes.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG LLVM_VERSION=20
ARG CXXBRIDGE_VERSION=1.0.205

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl wget gnupg \
        build-essential cmake ninja-build ccache git perl python3 pkg-config \
        libvulkan-dev vulkan-validationlayers vulkan-tools spirv-tools \
        libglfw3-dev \
        libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
        libwayland-dev libxkbcommon-dev wayland-protocols \
        libwxgtk3.2-dev \
    && rm -rf /var/lib/apt/lists/*

# clang-format (the format job pins the LLVM 20 release from apt.llvm.org).
RUN wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
        | gpg --dearmor -o /usr/share/keyrings/apt.llvm.org.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/apt.llvm.org.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-${LLVM_VERSION} main" \
        > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends clang-format-${LLVM_VERSION} \
    && ln -sf /usr/bin/clang-format-${LLVM_VERSION} /usr/local/bin/clang-format \
    && rm -rf /var/lib/apt/lists/*

# clang + libc++: Filament (third_party/filament) refuses to configure with GCC on
# Linux and must be built with clang and libc++ (see its BUILDING.md). Everything
# else is built with the same toolchain so the C++ standard library matches.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        clang-${LLVM_VERSION} libc++-${LLVM_VERSION}-dev libc++abi-${LLVM_VERSION}-dev \
    && ln -sf /usr/bin/clang-${LLVM_VERSION} /usr/local/bin/clang \
    && ln -sf /usr/bin/clang++-${LLVM_VERSION} /usr/local/bin/clang++ \
    && rm -rf /var/lib/apt/lists/*
ENV CC=clang \
    CXX=clang++ \
    CXXFLAGS=-stdlib=libc++

# Rust toolchain + cxxbridge (the C++ side of Mercury's Rust modules).
ENV RUSTUP_HOME=/usr/local/rustup \
    CARGO_HOME=/usr/local/cargo \
    PATH=/usr/local/cargo/bin:$PATH
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
        | sh -s -- -y --profile minimal --default-toolchain stable \
    && cargo install cxxbridge-cmd --version ${CXXBRIDGE_VERSION} --locked \
    && rm -rf ${CARGO_HOME}/registry ${CARGO_HOME}/git \
    && chmod -R a+rwX ${RUSTUP_HOME} ${CARGO_HOME}
