#!/usr/bin/env perl
#
# Unified setup script for Mercury Engine.
#
#   Linux  (Vulkan backend): compilers/build tools, the Vulkan loader + headers
#          + validation layers, GLFW's X11/Wayland dependencies, and DXC
#          (HLSL -> SPIR-V).
#   macOS  (MoltenVK backend): build tools (Bazelisk, cmake, ninja), Vulkan
#          loader + headers, MoltenVK, submodules, DXC and esbuild.
#
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
    install_bazelisk();
    run($^X, File::Spec->catfile($PROJECT_ROOT, 'scripts', 'install_esbuild.pl'));
    run($^X, File::Spec->catfile($PROJECT_ROOT, 'scripts', 'fetch_v8.pl'));

    print "\nDone. Next:\n";
    print "  git submodule update --init --recursive\n";
    print "  bazelisk build //mercury:mercury_player\n";
    print "  bazelisk test //tests/...\n";
    print "  bazelisk run //mercury:mercury_player -- examples/pong/pong.mproj\n";
}

sub install_apt {
    run('sudo', 'apt-get', 'update');
    run('sudo', 'apt-get', 'install', '-y',
        'build-essential', 'cmake', 'ninja-build', 'git', 'curl', 'pkg-config', 'python3',
        'libvulkan-dev', 'vulkan-validationlayers', 'vulkan-tools', 'spirv-tools',
        'libx11-dev', 'libxrandr-dev', 'libxinerama-dev', 'libxcursor-dev', 'libxi-dev',
        'libwayland-dev', 'libxkbcommon-dev', 'wayland-protocols');
}

sub install_pacman {
    run('sudo', 'pacman', '-S', '--needed', '--noconfirm',
        'base-devel', 'cmake', 'ninja', 'git', 'curl', 'pkgconf', 'python',
        'vulkan-icd-loader', 'vulkan-headers', 'vulkan-validation-layers', 'vulkan-tools',
        'spirv-tools', 'directx-shader-compiler',
        'libx11', 'libxrandr', 'libxinerama', 'libxcursor', 'libxi',
        'wayland', 'libxkbcommon', 'wayland-protocols');
}

sub install_bazelisk {
    if (command_exists('bazelisk')) {
        return;
    }

    my $machine = `uname -m`;
    chomp $machine;
    my %arch = (
        x86_64         => 'amd64',
        aarch64        => 'arm64',
        arm64          => 'arm64',
    );
    if (!exists $arch{$machine}) {
        print STDERR "No Bazelisk build for $machine\n";
        exit 1;
    }

    print "==> Installing Bazelisk into bin/ ...\n";
    my $bin = File::Spec->catdir($PROJECT_ROOT, 'bin');
    make_path($bin);
    my $target = File::Spec->catfile($bin, 'bazelisk');
    run('curl', '-fL',
        'https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-'
            . $arch{$machine},
        '-o', $target);
    chmod 0755, $target or die "cannot make $target executable: $!\n";
    print "Add $bin to PATH to use 'bazelisk'.\n";
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
    run('brew', 'install', 'bazelisk', 'cmake', 'ninja', 'pkg-config',
        'molten-vk', 'vulkan-loader', 'vulkan-headers');

    print "==> Updating git submodules...\n";
    run('git', '-C', $PROJECT_ROOT, 'submodule', 'update', '--init', '--recursive');

    install_dxc_macos();
    run($^X, File::Spec->catfile($PROJECT_ROOT, 'scripts', 'install_esbuild.pl'));

    # Fetch V8 sources (Bazel builds it on demand).
    run($^X, File::Spec->catfile($PROJECT_ROOT, 'scripts', 'fetch_v8.pl'));

    print "\n==> Setup complete! Next steps:\n";
    print "  bazelisk build //mercury:mercury_player\n";
    print "  bazelisk test //tests/...\n";
    print "  bazelisk run //mercury:mercury_player -- examples/pong/pong.mproj\n";
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
    my $sdk_ver = `curl -s https://vulkan.lunarg.com/sdk/latest/mac.txt`;
    if ($? != 0 || !defined $sdk_ver || $sdk_ver =~ /^\s*$/) {
        $sdk_ver = '1.4.363.0';
    } else {
        $sdk_ver =~ s/\A\s+//;
        $sdk_ver =~ s/\s+\z//;
    }

    my $tmp = tempdir(CLEANUP => 1);
    my $zip = File::Spec->catfile($tmp, 'vulkansdk.zip');

    print "Downloading Vulkan SDK $sdk_ver installer...\n";
    run('curl', '-fL',
        "https://sdk.lunarg.com/sdk/download/$sdk_ver/mac/vulkansdk-macos-$sdk_ver.zip",
        '-o', $zip);
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
