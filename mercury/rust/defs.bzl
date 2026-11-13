"""Starlark helpers for integrating Rust feature modules into Mercury via cxx."""

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_rust//rust:defs.bzl", "rust_library")


def _mercury_cxx_bridge_gen_impl(ctx):
    src = ctx.file.src
    out_h = ctx.outputs.out_h
    out_cc = ctx.outputs.out_cc
    codegen = ctx.executable._codegen

    ctx.actions.run_shell(
        inputs = [src],
        outputs = [out_h, out_cc],
        tools = [codegen],
        command = """
set -e
SRC="$1"
OUT_H="$2"
OUT_CC="$3"
CODEGEN="$4"
if [ ! -s "$SRC" ] || ! grep -q "cxx::bridge" "$SRC"; then
    printf '// No #[cxx::bridge] module in %s\\n#pragma once\\n' "$SRC" > "$OUT_H"
    printf '// No #[cxx::bridge] module in %s\\n' "$SRC" > "$OUT_CC"
else
    "$CODEGEN" "$SRC" -o "$OUT_H" -o "$OUT_CC"
fi
""",
        arguments = [src.path, out_h.path, out_cc.path, codegen.path],
        mnemonic = "MercuryCxxBridge",
        progress_message = "Generating cxx bridge for %s" % src.short_path,
    )


_mercury_cxx_bridge_gen = rule(
    implementation = _mercury_cxx_bridge_gen_impl,
    attrs = {
        "src": attr.label(allow_single_file = [".rs"], mandatory = True),
        "out_h": attr.output(mandatory = True),
        "out_cc": attr.output(mandatory = True),
        "_codegen": attr.label(
            default = Label("@cxx.rs//:codegen"),
            executable = True,
            cfg = "exec",
        ),
    },
)


def mercury_rust_feature(
        name,
        src,
        deps = [],
        cc_deps = [],
        crate_name = None,
        edition = "2024",
        **kwargs):
    """Defines a Rust feature module (and optional cxx bridge) linked into Mercury.

    Works seamlessly when `src` is empty, contains pure Rust (using
    `mercury_kernel::prelude::*`), or defines a `#[cxx::bridge]` module.
    """
    rust_library(
        name = name,
        srcs = [src],
        crate_root = src,
        crate_name = crate_name or name,
        edition = edition,
        deps = deps + [
            "//mercury/rust:mercury_kernel",
            "@cxx.rs//:cxx",
        ],
        **kwargs
    )

    bridge_name = "%s_bridge" % name

    _mercury_cxx_bridge_gen(
        name = "%s/generated" % bridge_name,
        src = src,
        out_h = src + ".h",
        out_cc = src + ".cc",
        **kwargs
    )

    native.alias(
        name = "%s/header" % bridge_name,
        actual = src + ".h",
        **kwargs
    )

    native.alias(
        name = "%s/source" % bridge_name,
        actual = src + ".cc",
        **kwargs
    )

    cc_library(
        name = "%s/include" % bridge_name,
        hdrs = [src + ".h"],
        **kwargs
    )

    cc_library(
        name = bridge_name,
        srcs = [src + ".cc"],
        deps = cc_deps + [
            "@cxx.rs//:core",
            ":%s/include" % bridge_name,
        ],
        linkstatic = True,
        **kwargs
    )
