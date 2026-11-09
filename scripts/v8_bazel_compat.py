#!/usr/bin/env python3
"""Makes V8's own Bazel files usable from inside the Mercury build.

V8's Bazel support is written for building V8 in Google's/Chromium's CI:
  * MODULE.bazel registers a Linux-only LLVM toolchain and sysroot.
  * MODULE.bazel applies a root-only `single_version_override`.
  * bazel/defs.bzl links Chromium's in-tree libc++ into every V8 library.
    Mercury and its CMake-built dependencies use the host toolchain, so V8
    must do the same or the two C++ runtimes collide at the API boundary.

This script edits the disposable checkout fetched by scripts/fetch_v8.sh. It
is idempotent and deliberately fails if V8's files no longer match: that
means the pinned V8 version changed and the patch needs review.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"v8_bazel_compat: {message}", file=sys.stderr)
    raise SystemExit(1)


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        fail(f"expected one occurrence of {old!r} in {path}, found {count}")
    path.write_text(text.replace(old, new, 1))


def patch_module(root: Path) -> None:
    module = root / "MODULE.bazel"
    text = module.read_text()

    start = "# Define the local LLVM toolchain repository"
    end = 'register_toolchains("@llvm_toolchain//:cc_toolchain_k8")'
    begin = text.find(start)
    if begin < 0:
        # Already patched (or V8 changed).
        if 'register_toolchains("@llvm_toolchain' in text:
            fail("found llvm toolchain registration without its marker")
    else:
        finish = text.find(end, begin)
        if finish < 0:
            fail("could not find the llvm toolchain registration")
        finish += len(end)
        # Include the newline after the registration when present.
        if text[finish : finish + 1] == "\n":
            finish += 1
        text = text[:begin] + text[finish:]

    # single_version_override is only allowed in the root module.
    if "single_version_override(" in text:
        text = re.sub(
            r"\nsingle_version_override\(\n(?:.|\n)*?\n\)\n",
            "\n",
            text,
            count=1,
        )
        if "single_version_override(" in text:
            fail("could not remove single_version_override from MODULE.bazel")

    module.write_text(text)


def patch_defs(root: Path) -> None:
    defs = root / "bazel" / "defs.bzl"
    text = defs.read_text()

    # Use the host C++ standard library instead of Chromium's in-tree libc++.
    if '"@libcxx//:libc++"' in text:
        replace_once(
            defs,
            'deps = [":define_flags", "@libcxx//:libc++"],',
            'deps = [":define_flags"],',
        )

    # New Apple clang releases warn about constructs V8's CI compilers do not.
    if '"-Werror"' in text:
        replace_once(defs, '                "-Werror",\n', "")


def patch_build(root: Path) -> None:
    build = root / "BUILD.bazel"
    text = build.read_text()

    # shared/math.h includes "src/__support/...", and nested headers include
    # "hdr/..."; llvm-libc is a mini libc tree with its own include root.
    # Declare the whole header tree so the sandbox contains it.
    pattern = re.compile(
        r'hdrs = glob\(\[\s*"third_party/llvm-libc/src/[^\]]*?\]\),',
        re.S,
    )
    text, count = pattern.subn(
        'hdrs = glob(["third_party/llvm-libc/src/**/*.h"]),', text, count=1
    )
    if count == 0 and 'hdrs = glob(["third_party/llvm-libc/src/**/*.h"]),' not in text:
        fail("could not find the llvm-libc header glob in BUILD.bazel")
    build.write_text(text)


def main() -> None:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("third_party/v8/v8")
    if not (root / "MODULE.bazel").is_file():
        fail(f"{root} does not look like a V8 checkout; run scripts/fetch_v8.sh")
    patch_module(root)
    patch_defs(root)
    patch_build(root)
    print(f"v8_bazel_compat: patched {root}")


if __name__ == "__main__":
    main()
