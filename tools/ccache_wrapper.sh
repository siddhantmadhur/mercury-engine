#!/usr/bin/env bash
# Compiler launcher used by rules_foreign_cc dependency builds.
#
# .bazelrc exports CCACHE_DISABLE=1 to every Bazel action so the
# rules_cc_autoconf probe actions stay deterministic. Dependency builds opt into
# ccache on purpose, so clear that variable and hand the compile to ccache.
#
# If ccache is not installed (CI images, fresh machines) this falls through to
# the real compiler, so the wrapper is safe to use unconditionally. Bazel
# actions get a minimal PATH, hence the explicit Homebrew locations.
unset CCACHE_DISABLE

if command -v ccache >/dev/null 2>&1; then
    exec ccache "$@"
fi

for candidate in /opt/homebrew/bin/ccache /usr/local/bin/ccache /usr/bin/ccache; do
    if [ -x "$candidate" ]; then
        exec "$candidate" "$@"
    fi
done

exec "$@"
