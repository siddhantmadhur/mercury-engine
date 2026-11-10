# V8

Mercury embeds V8 to run TypeScript game scripts. V8 is pinned to the version in
`VERSION` and is not committed (everything here except `VERSION`, `args.gn`,
`BUILD.bazel`, `patches/` and this file is git-ignored).

## Normal build (Bazel)

Run `scripts/fetch_v8.sh`. It uses depot_tools to fetch the pinned V8 and applies
`scripts/v8_bazel_compat.py`, a small patch that lets V8's own Bazel files build
inside Mercury's toolchain (host libc++ instead of Chromium's, no root-only
module overrides, no Linux-only LLVM toolchain).

Then Bazel builds V8 on demand:

```sh
bazelisk build -c opt //mercury:mercury_player
```

The first V8 build takes 30-60 minutes. V8's Bazel build enables pointer
compression by default; `.bazelrc` disables it to match the ABI the engine and
the TypeScript runtime were written against.

## GN fallback

`scripts/build_v8.sh` still builds the GN monolith
(`v8/out/release/obj/libv8_monolith.a`) for hosts where V8's Bazel build cannot
be used. Select it with:

```sh
bazelisk build //mercury:mercury_player --define=v8_backend=gn
```

The GN args in `args.gn` must not be changed unless the engine's compile
definitions change with them (pointer compression, the sandbox and i18n all
change V8's ABI). `use_sysroot` and `use_glib` are Linux-only and are filtered
out on macOS.
