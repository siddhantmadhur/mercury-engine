# V8

Mercury embeds V8 to run TypeScript game scripts. V8 is built from source, pinned to the
version in `VERSION`, and is not committed (everything here except `VERSION`, `args.gn` and
this file is git-ignored).

Run `scripts/build_v8.sh` (Linux/macOS) or `scripts/build_v8.ps1` (Windows). The build takes about
30-60 minutes and ~15 GB of disk. The output is
`v8/out/release/obj/libv8_monolith.a` (`v8_monolith.lib` on Windows).

`use_sysroot = false` makes the bundled clang use the host libstdc++, matching the engine.
The GN args in `args.gn` must not be changed unless the engine's compile definitions change with
them (pointer compression, the sandbox and i18n all change V8's ABI).
