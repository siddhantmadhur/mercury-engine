#!/usr/bin/env perl
#
# Unified setup script for Mercury Engine.
#
#   Linux  (Vulkan backend): compilers/build tools (CMake, Ninja, ccache, the
#          Rust toolchain), the Vulkan loader + headers + validation layers,
#          GLFW's X11/Wayland dependencies, and DXC (HLSL -> SPIR-V).
#   macOS  (MoltenVK backend): build tools (CMake, Ninja, ccache, the Rust
#          toolchain), Vulkan loader + headers, MoltenVK, and DXC.
#
# The build itself is plain CMake + Ninja: see README.md and CMakePresets.json.
# Windows is not supported yet: this script prints "OS not supported".
#
# Run from anywhere in the repository.

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Find qw(find);
use File::Path qw(make_path);
use File::Spec;
use File::Temp qw(tempdir);

if ($^O ne 'linux' && $^O ne 'darwin') {
    print STDERR "OS not supported\n";
    exit 1;
}

my (undef, $SCRIPT_DIR, undef) = File::Spec->splitpath(File::Spec->rel2abs($0));
my $PROJECT_ROOT = abs_path(File::Spec->catdir($SCRIPT_DIR, '..'));
die "cannot resolve project root\n" if !defined $PROJECT_ROOT;
my $DXC_VERSION = 'v1.9.2609';
my $DXC_ARCHIVE = 'linux_dxc_2026_09_28.x86_x64.tar.gz';
# Downloads are pinned and checked against these SHA-256 digests before
# anything is unpacked or run. Update the version and the digest together.
my $DXC_SHA256 = '96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1';
my $VULKAN_SDK_VERSION = '1.4.363.0';
my $VULKAN_SDK_MAC_SHA256 = '163431e0203c8215286a17cf393643ce6b951f77d1b4f74ab30821a1f0beb09b';

if ($^O eq 'linux') {
    setup_linux();
} else {
    setup_macos();
}

sub setup_linux {
    if (command_exists('apt-get')) {
        install_apt();
    } elsif (command_exists('pacman')) {
        install_pacman();
    } else {
        print STDERR "Unsupported distro: install the packages listed in this script manually.\n";
        exit 1;
    }

    install_dxc_linux();
    install_rust();
    install_cxxbridge();
    fetch_rust_deps();

    print "\nDone. Next:\n";
    print "  git submodule update --init --recursive\n";
    print "  cmake --preset release\n";
    print "  cmake --build --preset release\n";
    print "  cmake --build --preset check     # build and run the tests\n";
    print "  ./build/release/bin/mercury_player examples/pong/pong.mproj\n";
    print "  ./build/release/bin/cpp_hello\n";
}

sub install_apt {
    run('sudo', 'apt-get', 'update');
    run('sudo', 'apt-get', 'install', '-y',
        'build-essential', 'cmake', 'ninja-build', 'ccache', 'git', 'curl',
        'pkg-config', 'python3',
        'libvulkan-dev', 'vulkan-validationlayers', 'vulkan-tools', 'spirv-tools',
        'libx11-dev', 'libxrandr-dev', 'libxinerama-dev', 'libxcursor-dev', 'libxi-dev',
        'libwayland-dev', 'libxkbcommon-dev', 'wayland-protocols',
        'libwxgtk3.2-dev');
}

sub install_pacman {
    run('sudo', 'pacman', '-S', '--needed', '--noconfirm',
        'base-devel', 'cmake', 'ninja', 'ccache', 'git', 'curl', 'pkgconf',
        'python', 'rustup',
        'vulkan-icd-loader', 'vulkan-headers', 'vulkan-validation-layers',
        'vulkan-tools', 'spirv-tools', 'directx-shader-compiler',
        'libx11', 'libxrandr', 'libxinerama', 'libxcursor', 'libxi',
        'wayland', 'libxkbcommon', 'wayland-protocols',
        'wxwidgets-gtk3');
}

sub install_rust {
    my $cargo = command_exists('cargo');
    if ($cargo) {
        # rustup installs shims even before a toolchain is selected, so ask
        # cargo whether it can actually run.
        if (system($cargo, '--version') == 0) {
            print "cargo found: $cargo\n";
            return;
        }
        if (command_exists('rustup')) {
            print "==> Installing the stable Rust toolchain...\n";
            run('rustup', 'toolchain', 'install', 'stable', '--profile', 'minimal');
            run('rustup', 'default', 'stable');
            return;
        }
    }

    print "==> Installing the Rust toolchain with rustup...\n";
    run('sh', '-c',
        'curl --proto "=https" --tlsv1.2 -sSf https://sh.rustup.rs | '
            . 'sh -s -- -y --profile minimal --default-toolchain stable');
    my $cargo_bin = File::Spec->catdir($ENV{HOME}, '.cargo', 'bin');
    $ENV{PATH} = $cargo_bin . ':' . ($ENV{PATH} // '');
    for my $tool ('cargo', 'rustc') {
        my $path = File::Spec->catfile($cargo_bin, $tool);
        die "rustup did not install $tool\n" unless -x $path;
    }
}

sub install_cxxbridge {
    my $found = command_exists('cxxbridge');
    if ($found) {
        my $version = `$found --version`;
        chomp $version if defined $version;
        if (defined $version && $version =~ /1\.0\.205\s*$/) {
            print "cxxbridge found: $found\n";
            return;
        }
        print "Replacing " . ($version // 'an older cxxbridge') . "...\n";
    }

    my $cargo = command_exists('cargo');
    if (!$cargo) {
        my $cargo_bin = File::Spec->catfile($ENV{HOME}, '.cargo', 'bin', 'cargo');
        $cargo = $cargo_bin if -x $cargo_bin;
    }
    die "cargo was not found; install the Rust toolchain first\n" unless $cargo;

    print "==> Installing cxxbridge-cmd (the C++ side of Mercury's Rust modules)...\n";
    run($cargo, 'install', 'cxxbridge-cmd', '--version', '1.0.205', '--locked');
}

sub fetch_rust_deps {
    my $cargo = command_exists('cargo');
    if (!$cargo) {
        my $cargo_bin = File::Spec->catfile($ENV{HOME}, '.cargo', 'bin', 'cargo');
        $cargo = $cargo_bin if -x $cargo_bin;
    }
    return unless $cargo;
    my $manifest = File::Spec->catfile($PROJECT_ROOT, 'Cargo.toml');
    return unless -f $manifest;

    print "==> Fetching Rust dependencies...\n";
    run($cargo, 'fetch', '--manifest-path', $manifest);
}

sub install_dxc_linux {
    my $dxc = command_exists('dxc');
    if ($dxc) {
        print "dxc found on PATH: $dxc\n";
        return;
    }

    my $vulkan_sdk = $ENV{VULKAN_SDK} // '';
    if ($vulkan_sdk ne '' && -x File::Spec->catfile($vulkan_sdk, 'bin', 'dxc')) {
        print "dxc found in Vulkan SDK: " . File::Spec->catfile($vulkan_sdk, 'bin', 'dxc') . "\n";
        return;
    }

    my $local = File::Spec->catfile($PROJECT_ROOT, 'bin', 'dxc', 'bin', 'dxc');
    if (-x $local) {
        print "dxc already downloaded: $local\n";
        return;
    }

    my $machine = `uname -m`;
    chomp $machine;
    if ($machine ne 'x86_64') {
        print STDERR "No prebuilt dxc for $machine; install the LunarG Vulkan SDK"
            . " (https://vulkan.lunarg.com) and set VULKAN_SDK.\n";
        exit 1;
    }

    print "Downloading dxc $DXC_VERSION into bin/dxc ...\n";
    my $dest = File::Spec->catdir($PROJECT_ROOT, 'bin', 'dxc');
    make_path($dest);

    my $tmp = tempdir(CLEANUP => 1);
    my $archive = File::Spec->catfile($tmp, $DXC_ARCHIVE);
    run('curl', '-fL',
        'https://github.com/microsoft/DirectXShaderCompiler/releases/download/'
            . $DXC_VERSION . '/' . $DXC_ARCHIVE,
        '-o', $archive);
    verify_sha256($archive, $DXC_SHA256);
    run('tar', '-xzf', $archive, '-C', $dest);
    run($local, '--version');
}

sub setup_macos {
    print "==> Setting up Mercury Engine on macOS...\n";

    if (!command_exists('brew')) {
        print STDERR "Homebrew not found. Please install Homebrew from https://brew.sh first.\n";
        exit 1;
    }

    print "==> Installing build tools and Vulkan dependencies via Homebrew...\n";
    run('brew', 'install', 'cmake', 'ninja', 'ccache', 'pkg-config',
        'molten-vk', 'vulkan-loader', 'vulkan-headers', 'wxwidgets');

    print "==> Updating git submodules...\n";
    run('git', '-C', $PROJECT_ROOT, 'submodule', 'update', '--init', '--recursive');

    install_rust();
    install_cxxbridge();
    fetch_rust_deps();
    install_dxc_macos();

    print "\n==> Setup complete! Next steps:\n";
    print "  cmake --preset release\n";
    print "  cmake --build --preset release\n";
    print "  cmake --build --preset check     # build and run the tests\n";
    print "  ./build/release/bin/mercury_player examples/pong/pong.mproj\n";
    print "  ./build/release/bin/cpp_hello\n";
}

sub install_dxc_macos {
    my $dxc = command_exists('dxc');
    if ($dxc) {
        print "dxc found on PATH: $dxc\n";
        return;
    }

    my $vulkan_sdk = $ENV{VULKAN_SDK} // '';
    if ($vulkan_sdk ne '' && -x File::Spec->catfile($vulkan_sdk, 'bin', 'dxc')) {
        print "dxc found in Vulkan SDK: " . File::Spec->catfile($vulkan_sdk, 'bin', 'dxc') . "\n";
        return;
    }

    my $installed = File::Spec->catfile($PROJECT_ROOT, 'bin', 'vulkansdk', 'macOS', 'bin', 'dxc');
    if (-x $installed) {
        print "dxc already installed: $installed\n";
        return;
    }

    print "==> Fetching DXC from LunarG Vulkan SDK for macOS...\n";
    my $sdk_ver = $VULKAN_SDK_VERSION;

    my $tmp = tempdir(CLEANUP => 1);
    my $zip = File::Spec->catfile($tmp, 'vulkansdk.zip');

    print "Downloading Vulkan SDK $sdk_ver installer...\n";
    run('curl', '-fL',
        "https://sdk.lunarg.com/sdk/download/$sdk_ver/mac/vulkansdk-macos-$sdk_ver.zip",
        '-o', $zip);
    verify_sha256($zip, $VULKAN_SDK_MAC_SHA256);
    run('unzip', '-q', $zip, '-d', File::Spec->catdir($tmp, 'extracted'));

    my $installer;
    find({
        wanted => sub {
            return if defined $installer;
            return unless -f $_ && -x _;
            return unless m{(?:^|/)vulkansdk-macOS-[^/]*$};
            $installer = $File::Find::name;
        },
        no_chdir => 1,
    }, File::Spec->catdir($tmp, 'extracted'));

    if (defined $installer) {
        print "Installing core SDK components into bin/vulkansdk...\n";
        run($installer, 'install',
            '--root', File::Spec->catdir($PROJECT_ROOT, 'bin', 'vulkansdk'),
            '--accept-licenses', '--default-answer', '--confirm-command');
    } else {
        print STDERR "Could not execute SDK installer; please install LunarG Vulkan SDK manually.\n";
        exit 1;
    }
}

# Dies unless the file's SHA-256 digest matches, so a tampered or swapped
# download is never extracted or executed.
sub verify_sha256 {
    my ($file, $expected) = @_;
    my $tool = command_exists('sha256sum') ? ['sha256sum']
             : command_exists('shasum')    ? ['shasum', '-a', '256']
             : undef;
    die "need sha256sum or shasum to verify downloads\n" unless $tool;
    open(my $fh, '-|', @$tool, $file) or die "cannot run @$tool: $!\n";
    my $line = <$fh>;
    close $fh;
    my ($actual) = defined $line ? ($line =~ /^([0-9a-fA-F]{64})/) : ();
    die "could not compute the SHA-256 of $file\n" unless defined $actual;
    if (lc($actual) ne lc($expected)) {
        die "SHA-256 mismatch for $file\n  expected $expected\n  got      $actual\n";
    }
    print "SHA-256 verified: " . File::Spec->abs2rel($file) . "\n";
}

sub command_exists {
    my ($command) = @_;
    for my $dir (split /:/, $ENV{PATH} // '') {
        $dir = '.' if $dir eq '';
        my $path = File::Spec->catfile($dir, $command);
        return $path if -x $path && !-d $path;
    }
    return undef;
}

sub run {
    my (@cmd) = @_;
    system(@cmd);
    die "command failed (@cmd): $!\n" if $? != 0;
}
