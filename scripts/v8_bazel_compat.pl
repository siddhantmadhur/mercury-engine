#!/usr/bin/env perl
#
# Makes V8's own Bazel files usable from inside the Mercury build.
#
# V8's Bazel support is written for building V8 in Google's/Chromium's CI:
#   * MODULE.bazel registers a Linux-only LLVM toolchain and sysroot.
#   * MODULE.bazel applies a root-only `single_version_override`.
#   * bazel/defs.bzl links Chromium's in-tree libc++ into every V8 library.
#     Mercury and its CMake-built dependencies use the host toolchain, so V8
#     must do the same or the two C++ runtimes collide at the API boundary.
#
# This script edits the disposable checkout fetched by scripts/fetch_v8.pl. It
# is idempotent and deliberately fails if V8's files no longer match: that
# means the pinned V8 version changed and the patch needs review.

use strict;
use warnings;
use File::Spec;

my $root = defined $ARGV[0] ? $ARGV[0] : File::Spec->catdir('third_party', 'v8', 'v8');

sub fail {
    my ($message) = @_;
    print STDERR "v8_bazel_compat: $message\n";
    exit 1;
}

sub read_file {
    my ($path) = @_;
    open my $fh, '<:encoding(UTF-8)', $path
        or fail("cannot read $path: $!");
    local $/;
    my $text = <$fh>;
    close $fh;
    return defined $text ? $text : '';
}

sub write_file {
    my ($path, $text) = @_;
    open my $fh, '>:encoding(UTF-8)', $path
        or fail("cannot write $path: $!");
    print {$fh} $text;
    close $fh
        or fail("cannot write $path: $!");
}

sub replace_once {
    my ($path, $old, $new) = @_;
    my $text = read_file($path);
    my $count = () = $text =~ /\Q$old\E/g;
    if ($count != 1) {
        fail("expected one occurrence of '$old' in $path, found $count");
    }
    $text =~ s/\Q$old\E/$new/;
    write_file($path, $text);
}

sub patch_module {
    my ($root) = @_;
    my $module = File::Spec->catfile($root, 'MODULE.bazel');
    my $text = read_file($module);

    my $start = '# Define the local LLVM toolchain repository';
    my $end = 'register_toolchains("@llvm_toolchain//:cc_toolchain_k8")';
    my $begin = index($text, $start);
    if ($begin < 0) {
        # Already patched (or V8 changed).
        if (index($text, 'register_toolchains("@llvm_toolchain') >= 0) {
            fail('found llvm toolchain registration without its marker');
        }
    } else {
        my $finish = index($text, $end, $begin);
        fail('could not find the llvm toolchain registration') if $finish < 0;
        $finish += length($end);
        # Include the newline after the registration when present.
        $finish++ if substr($text, $finish, 1) eq "\n";
        $text = substr($text, 0, $begin) . substr($text, $finish);
    }

    # single_version_override is only allowed in the root module.
    if (index($text, 'single_version_override(') >= 0) {
        $text =~ s/\nsingle_version_override\(\n.*?\n\)\n/\n/s;
        if (index($text, 'single_version_override(') >= 0) {
            fail('could not remove single_version_override from MODULE.bazel');
        }
    }

    write_file($module, $text);
}

sub patch_defs {
    my ($root) = @_;
    my $defs = File::Spec->catfile($root, 'bazel', 'defs.bzl');
    my $text = read_file($defs);

    # Use the host C++ standard library instead of Chromium's in-tree libc++.
    if (index($text, '"@libcxx//:libc++"') >= 0) {
        replace_once(
            $defs,
            'deps = [":define_flags", "@libcxx//:libc++"],',
            'deps = [":define_flags"],',
        );
    }

    # New Apple clang releases warn about constructs V8's CI compilers do not.
    $text = read_file($defs);
    if (index($text, '"-Werror"') >= 0) {
        replace_once($defs, '                "-Werror",' . "\n", '');
    }
}

sub patch_build {
    my ($root) = @_;
    my $build = File::Spec->catfile($root, 'BUILD.bazel');
    my $text = read_file($build);

    # shared/math.h includes "src/__support/...", and nested headers include
    # "hdr/..."; llvm-libc is a mini libc tree with its own include root.
    # Declare the whole header tree so the sandbox contains it.
    my $replacement = 'hdrs = glob(["third_party/llvm-libc/src/**/*.h"]),';
    my $count = ($text =~ s{hdrs = glob\(\[\s*"third_party/llvm-libc/src/[^\]]*?\]\),}{$replacement}s);
    if ($count == 0 && index($text, $replacement) < 0) {
        fail('could not find the llvm-libc header glob in BUILD.bazel');
    }
    write_file($build, $text);
}

sub main {
    if (!-f File::Spec->catfile($root, 'MODULE.bazel')) {
        fail("$root does not look like a V8 checkout; run scripts/fetch_v8.pl");
    }
    patch_module($root);
    patch_defs($root);
    patch_build($root);
    print "v8_bazel_compat: patched $root\n";
}

main();
