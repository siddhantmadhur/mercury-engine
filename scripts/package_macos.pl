#!/usr/bin/env perl
#
# package_macos.pl - builds a self-contained, publishable Mercury .app bundle.
#
# The bundle contains everything the game needs: the player, the MoltenVK
# driver and the Vulkan loader it links against, and the project packed into
# .mpack archives (the same format the editor's Build Game writes). Nothing in
# the app points back at the engine checkout.
#
#   ./scripts/package_macos.pl --project examples/pong/pong.mproj
#   open Builds/Pong.app
#
# Typical flags:
#   --identity "Developer ID Application: Your Name (TEAMID)"   sign for
#           distribution (default is an ad-hoc signature)
#   --zip   also write a ditto-compatible .zip next to the app
#   --packs DIR   reuse .mpack files from an editor "Build Game" folder
#   --skip-build  reuse the binaries already in bazel-bin
#   --target editor   package mercury_editor instead of the player
#
# macOS only; run from anywhere in the repository.

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(basename dirname);
use File::Copy qw(copy);
use File::Path qw(make_path remove_tree);
use File::Spec;
use File::Temp qw(tempdir);
use Getopt::Long qw(GetOptions);
use JSON::PP ();
use Compress::Zlib qw(compress crc32);

if ($^O ne 'darwin') {
    print STDERR "package_macos.pl only runs on macOS\n";
    exit 1;
}

my (undef, $SCRIPT_DIR, undef) = File::Spec->splitpath(File::Spec->rel2abs($0));
my $REPO = abs_path(File::Spec->catdir($SCRIPT_DIR, '..'));
die "cannot resolve the repository root\n" unless defined $REPO;

my %opt = (
    target       => 'player',
    version      => '1.0.0',
    min_macos    => '26.0',
    pack_size_mb => 256,
    identity     => '-',
);
my $want_zip = 0;
my $no_sign = 0;
my $skip_build = 0;
my $verbose = 0;

GetOptions(
    'project=s'      => \$opt{project},
    'target=s'       => \$opt{target},
    'name=s'         => \$opt{name},
    'display-name=s' => \$opt{display_name},
    'bundle-id=s'    => \$opt{bundle_id},
    'version=s'      => \$opt{version},
    'min-macos=s'    => \$opt{min_macos},
    'output=s'       => \$opt{output},
    'icon=s'         => \$opt{icon},
    'identity=s'     => \$opt{identity},
    'no-sign'        => \$no_sign,
    'packs=s'        => \$opt{packs},
    'pack-size-mb=i' => \$opt{pack_size_mb},
    'skip-build'     => \$skip_build,
    'zip'            => \$want_zip,
    'verbose|v'      => \$verbose,
    'help|h'         => sub { usage(0) },
) or usage(1);

usage(1) if @ARGV;
if ($opt{target} ne 'player' && $opt{target} ne 'editor') {
    die "--target must be player or editor\n";
}

if (defined $opt{name} && $opt{name} =~ m{[/:]}) {
    die "--name may not contain '/' or ':'\n";
}

# The player needs a project; the editor does not.
if ($opt{target} eq 'player' && !$opt{project}) {
    usage(1);
}

sub usage {
    my ($code) = @_;
    my $name = basename($0);
    print <<"USAGE";
usage: $name --project PATH/TO/game.mproj [options]

  --project PATH        the .mproj to ship (player)
  --target player|editor
                        what to bundle (default: player)
  --name NAME           bundle and executable name (default: project name)
  --display-name NAME   name shown in Finder (default: --name)
  --bundle-id ID        CFBundleIdentifier (default: dev.smadhur.<name>)
  --version VERSION     CFBundleShortVersionString (default: 1.0.0)
  --min-macos VERSION   LSMinimumSystemVersion (default: 26.0)
  --output DIR          where to write the .app (default: Builds/)
  --icon PATH           .icns, or a .png to convert with sips/iconutil
  --identity IDENTITY   codesign identity; "-" signs ad-hoc (default)
  --no-sign             do not sign the bundle
  --packs DIR           reuse an editor "Build Game" output instead of packing
  --pack-size-mb N      split game.mpack past N MB; 0 keeps one file (256)
  --skip-build          do not run Bazel first
  --zip                 also write a .zip for distribution
  --verbose             show every command
  -h, --help            this help
USAGE
    exit($code // 0);
}

sub log_line  { print "@_\n" }
sub detail    { print "    @_\n" if $verbose }
sub fail      { print STDERR "@_\n"; exit 1 }

sub run {
    my @cmd = @_;
    detail(join(' ', map { /\s/ ? "'$_'" : $_ } @cmd));
    system(@cmd);
    return $? == 0;
}

sub run_or_die {
    my @cmd = @_;
    run(@cmd) or fail("command failed: @cmd");
}

# Runs a command but swallows its stdout (sips echoes every input path).
sub run_quiet {
    my @cmd = @_;
    detail(join(' ', @cmd));
    capture(@cmd);
    return $? == 0;
}

sub capture {
    my @cmd = @_;
    open(my $fh, '-|', @cmd) or fail("cannot run @cmd: $!");
    local $/;
    my $out = <$fh>;
    close $fh;
    return defined $out ? $out : '';
}

sub which {
    my ($name) = @_;
    for my $dir (split /:/, $ENV{PATH} // '') {
        my $path = File::Spec->catfile($dir, $name);
        return $path if -x $path && !-d $path;
    }
    return undef;
}

sub slugify {
    my ($text) = @_;
    $text //= '';
    $text = lc $text;
    $text =~ s/[^a-z0-9]+/-/g;
    $text =~ s/^-+|-+$//g;
    $text = 'game' if $text eq '';
    return $text;
}

# --------------------------------------------------------------------------
# The project
# --------------------------------------------------------------------------

my $project_root;
my $project_name;

sub project_name_from_file {
    my ($path) = @_;
    open(my $fh, '<', $path) or return undef;
    my $section = '';
    my $name;
    while (my $line = <$fh>) {
        if ($line =~ /^\s*\[([^\]]+)\]/) {
            $section = $1;
            next;
        }
        if ($section eq 'project' && $line =~ /^\s*name\s*=\s*"([^"]*)"/) {
            $name = $1;
            last;
        }
    }
    close $fh;
    return $name;
}

if ($opt{target} eq 'player') {
    my $project = abs_path($opt{project});
    fail("no such project: $opt{project}") unless defined $project && -f $project;
    $project_root = dirname($project);
    $project_name =
        project_name_from_file($project) // basename($project_root);
} else {
    $project_name = $opt{display_name} // 'Mercury Editor';
}

my $name = $opt{name} // ($opt{target} eq 'editor' ? 'Mercury Editor' : $project_name);
my $slug = slugify($name);
my $bundle_id = $opt{bundle_id} // "dev.smadhur.$slug";
my $output_dir = File::Spec->rel2abs($opt{output} // File::Spec->catdir($REPO, 'Builds'));
make_path($output_dir);
$output_dir = abs_path($output_dir);
my $app_dir = File::Spec->catdir($output_dir, "$name.app");

my $bazel_target =
    $opt{target} eq 'editor' ? '//editor:mercury_editor' : '//mercury:mercury_player';

log_line("Packaging $name.app");
log_line("  project : $project_root") if $opt{target} eq 'player';
log_line("  output  : $app_dir");

# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------

my $bazel_bin = File::Spec->catfile($REPO, 'bazel-bin');

unless ($skip_build) {
    my $bazel = which('bazelisk') // which('bazel');
    fail("Bazelisk was not found; install it with ./scripts/setup_project.pl")
        unless $bazel;
    log_line("Building $bazel_target (Release)");
    # The deployment target must match LSMinimumSystemVersion below. Bazel
    # otherwise stamps the installed SDK version (e.g. 27.0) into the binary
    # and macOS refuses to launch it on older systems.
    run_or_die($bazel, 'build', '-c', 'opt',
        "--macos_minimum_os=$opt{min_macos}", $bazel_target);
}

my $source_exe =
    $opt{target} eq 'editor'
    ? File::Spec->catfile($bazel_bin, 'editor', 'mercury_editor')
    : File::Spec->catfile($bazel_bin, 'mercury', 'mercury_player');

# bazel-bin is a symlink; resolve it so install_name_tool rewrites the real file
# in the output tree rather than failing on the link.
my $real_exe = abs_path($source_exe);
fail("the build did not produce $source_exe") unless defined $real_exe && -f $real_exe;

# --------------------------------------------------------------------------
# Bundle skeleton
# --------------------------------------------------------------------------

remove_tree($app_dir) if -e $app_dir;
my $contents = File::Spec->catdir($app_dir, 'Contents');
my $macos    = File::Spec->catdir($contents, 'MacOS');
my $res      = File::Spec->catdir($contents, 'Resources');
my $fw       = File::Spec->catdir($contents, 'Frameworks');
make_path($macos, $res, $fw);

my $exe = File::Spec->catfile($macos, $slug);
copy($real_exe, $exe) or fail("cannot copy $real_exe: $!");
chmod 0755, $exe or fail("cannot chmod $exe: $!");

my $icon_file;
if ($opt{icon}) {
    my $icon = abs_path($opt{icon});
    fail("no such icon: $opt{icon}") unless defined $icon && -f $icon;
    $icon_file = install_icon($icon);
}

write_info_plist();
open(my $pkg, '>', File::Spec->catfile($contents, 'PkgInfo'))
    or fail("cannot write PkgInfo: $!");
print $pkg "APPL????\n";
close $pkg;

# --------------------------------------------------------------------------
# Packs
# --------------------------------------------------------------------------

if ($opt{target} eq 'player') {
    my $max_bytes = $opt{pack_size_mb} > 0 ? $opt{pack_size_mb} * 1024 * 1024 : 0;
    if ($opt{packs}) {
        my $packs = abs_path($opt{packs});
        fail("no such pack folder: $opt{packs}") unless defined $packs && -d $packs;
        copy_packs($packs);
    } else {
        pack_game($res, $max_bytes);
        pack_engine($res, $max_bytes);
    }
}

# --------------------------------------------------------------------------
# Dependencies, signature, output
# --------------------------------------------------------------------------

write_vulkan_icd();
bundle_dependencies();

unless ($no_sign) {
    sign_bundle();
}

verify_bundle();

if ($want_zip) {
    my $zip = File::Spec->catfile($output_dir, "$name-$opt{version}-macos.zip");
    unlink $zip;
    log_line("Writing $zip");
    run_or_die('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', $app_dir, $zip);
}

my $size = capture('du', '-sh', $app_dir);
$size =~ s/^\s+//;
$size =~ s/\s.*//s;
log_line("");
log_line("Built $app_dir ($size)");
log_line("Run it with: open \"$app_dir\"");

# --------------------------------------------------------------------------
# Info.plist and icon
# --------------------------------------------------------------------------

sub xml_escape {
    my ($text) = @_;
    $text =~ s/&/&amp;/g;
    $text =~ s/</&lt;/g;
    $text =~ s/>/&gt;/g;
    $text =~ s/"/&quot;/g;
    return $text;
}

sub write_info_plist {
    my $bundle_version = $opt{version} =~ /^(\d+(?:\.\d+)*)$/ ? $1 : '1';
    my $display = xml_escape($opt{display_name} // $name);
    my $icon_entry = $icon_file
        ? "    <key>CFBundleIconFile</key>\n    <string>" . xml_escape($icon_file) . "</string>\n"
        : '';
    my $category = $opt{target} eq 'player'
        ? "public.app-category.games" : "public.app-category.developer-tools";
    my $plist = <<"PLIST";
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>@{[ xml_escape($slug) ]}</string>
    <key>CFBundleIdentifier</key>
    <string>@{[ xml_escape($bundle_id) ]}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>$display</string>
    <key>CFBundleDisplayName</key>
    <string>$display</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>@{[ xml_escape($opt{version}) ]}</string>
    <key>CFBundleVersion</key>
    <string>$bundle_version</string>
    <key>LSMinimumSystemVersion</key>
    <string>@{[ xml_escape($opt{min_macos}) ]}</string>
    <key>LSApplicationCategoryType</key>
    <string>$category</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
$icon_entry</dict>
</plist>
PLIST
    my $path = File::Spec->catfile($app_dir, 'Contents', 'Info.plist');
    open(my $fh, '>', $path) or fail("cannot write $path: $!");
    print $fh $plist;
    close $fh;
    run_or_die('plutil', '-lint', '-s', $path);
}

sub install_icon {
    my ($icon) = @_;
    fail("no such icon: $icon") unless -f $icon;
    my $dest = File::Spec->catfile($res, 'AppIcon.icns');
    if ($icon =~ /\.icns$/i) {
        copy($icon, $dest) or fail("cannot copy $icon: $!");
        return 'AppIcon';
    }
    fail("the icon must be a .icns or a .png") unless $icon =~ /\.png$/i;
    fail("sips and iconutil are required to convert a PNG icon")
        unless which('sips') && which('iconutil');

    my $tmp = tempdir(CLEANUP => 1);
    my $iconset = File::Spec->catdir($tmp, 'AppIcon.iconset');
    make_path($iconset);
    for my $base (16, 32, 128, 256, 512) {
        for my $scale (1, 2) {
            my $size = $base * $scale;
            my $suffix = $scale == 1 ? '' : '@2x';
            my $out = File::Spec->catfile($iconset, "icon_${base}x${base}${suffix}.png");
            run_quiet('sips', '-s', 'format', 'png', '-z', $size, $size, $icon,
                '--out', $out)
                or fail("sips could not convert $icon");
        }
    }
    run_or_die('iconutil', '-c', 'icns', $iconset, '-o', $dest);
    return 'AppIcon';
}

# --------------------------------------------------------------------------
# .mpack writing (mirrors mercury/pack/mpack.cc)
# --------------------------------------------------------------------------

use constant {
    MPAK_HEADER_SIZE  => 32,
    MIDX_HEADER_SIZE  => 32,
    MIDX_ENTRY_SIZE   => 48,
    MPAK_ALIGNMENT    => 16,
    MPAK_VERSION      => 1,
    MIN_BUCKETS       => 16,
};

sub hash_path {
    use integer;
    my $h = 14695981039346656037;
    for my $c (unpack('C*', $_[0])) {
        $h = ($h ^ $c) * 1099511628211;
    }
    # Give back the unsigned 64-bit pattern so bit operations behave.
    return unpack('Q<', pack('Q<', $h));
}

sub align_up {
    my ($value) = @_;
    return int(($value + MPAK_ALIGNMENT - 1) / MPAK_ALIGNMENT) * MPAK_ALIGNMENT;
}

sub is_source_file {
    my ($path) = @_;
    return $path =~ /\.(?:cc|cpp|cxx|c|hh|hpp|h|tmp)$/i;
}

sub is_skipped_at_root {
    my ($name) = @_;
    return 1 if $name eq 'CMakeLists.txt' || $name eq 'MODULE.bazel'
        || $name eq 'MODULE.bazel.lock' || $name eq 'WORKSPACE'
        || $name eq 'WORKSPACE.bazel' || $name eq 'Builds'
        || $name =~ /^bazel-/ || $name =~ /\.app$/;
    return 0;
}

# $files: listref of [entry_path, source_path]. Sorted by entry path.
sub write_pack {
    my ($dir, $base, $files, $max_bytes) = @_;
    # The C++ writer keeps entries sorted by path; do the same so packs are
    # reproducible regardless of directory iteration order.
    my @sorted = sort { $a->[0] cmp $b->[0] } @$files;

    my @entries;
    my @archives;
    my $archive = 0;
    my $out;
    my $path;

    my $open_archive = sub {
        close $out if $out;
        $path = File::Spec->catfile($dir, sprintf('%s%s', $base,
            $archive == 0 ? '.mpack' : sprintf('_%03d.mpack', $archive)));
        open($out, '+>', $path) or fail("cannot write $path: $!");
        binmode $out;
        print {$out} "\0" x MPAK_HEADER_SIZE;
        push @archives, $path;
        return MPAK_HEADER_SIZE;
    };

    my $pos = $open_archive->();
    my $archive_has_data = 0;
    my $total_in = 0;
    my $total_out = 0;

    for my $item (@sorted) {
        my ($entry_path, $source) = @$item;
        fail("invalid pack path \"$entry_path\"")
            unless $entry_path ne '' && $entry_path !~ m{^/} && $entry_path !~ m{/$}
                && $entry_path !~ /[\\:]/
                && $entry_path !~ m{(?:^|/)\.\.?(?:/|$)};

        open(my $in, '<', $source) or fail("cannot read $source: $!");
        binmode $in;
        local $/;
        my $data = <$in>;
        close $in;
        $data = '' unless defined $data;
        $total_in += length($data);

        my $crc = crc32($data);
        my $stored = $data;
        my $compression = 0;
        if (length($data) > 0 && length($data) <= 0xFFFFFFFF) {
            my $compressed = compress($data, 9);
            if (defined $compressed
                && length($compressed) <= length($data) * 0.95) {
                $stored = $compressed;
                $compression = 1;
            }
        }

        my $offset = align_up($pos);
        if ($max_bytes && $archive_has_data && $offset + length($stored) > $max_bytes) {
            $archive++;
            $pos = $open_archive->();
            $offset = align_up($pos);
            $archive_has_data = 0;
        }
        print $out "\0" x ($offset - $pos) if $offset > $pos;
        print $out $stored;
        $total_out += length($stored);

        push @entries, {
            path        => $entry_path,
            hash        => hash_path($entry_path),
            archive     => $archive,
            offset      => $offset,
            stored_size => length($stored),
            size        => length($data),
            compression => $compression,
            crc32       => $crc,
        };
        $pos = $offset + length($stored);
        $archive_has_data = 1;
    }
    close $out;

    my $archive_count = scalar @archives;
    for my $i (0 .. $#archives) {
        open(my $fh, '+<', $archives[$i]) or fail("cannot reopen $archives[$i]: $!");
        binmode $fh;
        seek($fh, 0, 0);
        print $fh 'MPAK', pack('V', MPAK_VERSION), pack('v', $i),
            pack('v', $archive_count), "\0" x 16;
        close $fh;
    }

    my $bucket_count = MIN_BUCKETS;
    $bucket_count *= 2 while $bucket_count < @entries * 2;
    my @buckets = (0) x $bucket_count;

    my $strings = '';
    my $records = '';
    for my $i (0 .. $#entries) {
        my $e = $entries[$i];
        my $b = $e->{hash} & ($bucket_count - 1);
        for (;;) {
            if ($buckets[$b] == 0) {
                $buckets[$b] = $i + 1;
                last;
            }
            $b = ($b + 1) & ($bucket_count - 1);
        }
        $records .= pack('Q<', $e->{hash});
        $records .= pack('V', length($strings));
        $records .= pack('v', length($e->{path}));
        $records .= pack('v', $e->{archive});
        $records .= pack('Q<', $e->{offset});
        $records .= pack('Q<', $e->{stored_size});
        $records .= pack('Q<', $e->{size});
        $records .= pack('C', $e->{compression}) . pack('C', 0) . pack('v', 0);
        $records .= pack('V', $e->{crc32});
        $strings .= $e->{path};
    }
    die "internal error: bad index record size\n"
        unless length($records) == @entries * MIDX_ENTRY_SIZE;

    my $index = 'MIDX' . pack('V', MPAK_VERSION) . pack('V', $archive_count)
        . pack('V', scalar @entries) . pack('V', $bucket_count) . pack('V', 0)
        . pack('Q<', length($strings))
        . pack('V*', @buckets) . $records . $strings;

    my $index_path = File::Spec->catfile($dir, "$base.mindex");
    open(my $fh, '>', $index_path) or fail("cannot write $index_path: $!");
    binmode $fh;
    print $fh $index;
    close $fh;

    log_line(sprintf("  %s.mpack: %d files, %s -> %s (%d archive%s)",
        $base, scalar @entries, bytes($total_in), bytes($total_out),
        $archive_count, $archive_count == 1 ? '' : 's'));
    return 1;
}

sub bytes {
    my ($n) = @_;
    return sprintf('%.0f B', $n) if $n < 1024;
    return sprintf('%.1f KB', $n / 1024) if $n < 1024 * 1024;
    return sprintf('%.1f MB', $n / (1024 * 1024)) if $n < 1024 * 1024 * 1024;
    return sprintf('%.2f GB', $n / (1024 * 1024 * 1024));
}

# Every project file the editor's Build Game would pack.
sub collect_game_files {
    my ($root) = @_;
    my @files;
    my $output = abs_path($output_dir) // $output_dir;

    my $walk;
    $walk = sub {
        my ($dir, $rel, $depth) = @_;
        opendir(my $dh, $dir) or fail("cannot read $dir: $!");
        my @names = sort grep { $_ ne '.' && $_ ne '..' } readdir $dh;
        closedir $dh;
        for my $name (@names) {
            next if $name =~ /^\./;
            next if $depth == 0 && is_skipped_at_root($name);
            my $path = File::Spec->catfile($dir, $name);
            my $entry = length($rel) ? "$rel/$name" : $name;
            if (-d $path) {
                my $canonical = abs_path($path) // $path;
                next if $canonical eq $output;
                next if $canonical eq (abs_path($app_dir) // $app_dir);
                $walk->($path, $entry, $depth + 1);
            } elsif (-f $path) {
                next if is_source_file($name);
                push @files, [$entry, $path];
            }
        }
    };
    $walk->($root, '', 0);
    return \@files;
}

sub pack_game {
    my ($dest, $max_bytes) = @_;
    fail("the project folder does not exist: $project_root")
        unless -d $project_root;
    my $files = collect_game_files($project_root);
    fail("no files to pack in $project_root") unless @$files;
    log_line("Packing the project into game.mpack");
    write_pack($dest, 'game', $files, $max_bytes);
}

sub pack_engine {
    my ($dest, $max_bytes) = @_;
    my @files;
    my $ui = File::Spec->catdir($REPO, 'mercury', 'ui', 'resources');
    fail("the engine's UI resources are missing ($ui)")
        unless -d $ui;
    my $walk;
    $walk = sub {
        my ($dir, $rel) = @_;
        opendir(my $dh, $dir) or fail("cannot read $dir: $!");
        my @names = sort grep { $_ ne '.' && $_ ne '..' } readdir $dh;
        closedir $dh;
        for my $name (@names) {
            next if $name =~ /^\./;
            my $path = File::Spec->catfile($dir, $name);
            my $entry = length($rel) ? "$rel/$name" : $name;
            if (-d $path) {
                $walk->($path, $entry);
            } elsif (-f $path) {
                push @files, ["ui/$entry", $path];
            }
        }
    };
    $walk->($ui, '');

    my $spirv = File::Spec->catdir($REPO, 'bazel-bin', 'mercury', 'shaders');
    opendir(my $dh, $spirv) or fail("no compiled shaders in $spirv");
    my @spv = sort grep { /\.spv$/ && -f File::Spec->catfile($spirv, $_) } readdir $dh;
    closedir $dh;
    fail("no compiled shaders in $spirv") unless @spv;
    push @files, ["shaders/$_", File::Spec->catfile($spirv, $_)] for @spv;

    log_line("Packing the engine's UI theme and shaders into engine.mpack");
    write_pack($dest, 'engine', \@files, $max_bytes);
}

sub copy_packs {
    my ($dir) = @_;
    my @packs = (glob(File::Spec->catfile($dir, '*.mpack')),
                 glob(File::Spec->catfile($dir, '*.mindex')));
    fail("no .mpack or .mindex files in $dir") unless @packs;
    for my $pack (@packs) {
        copy($pack, File::Spec->catfile($res, basename($pack)))
            or fail("cannot copy $pack: $!");
    }
    log_line("Copied " . scalar(@packs) . " pack files from $dir");
}

# --------------------------------------------------------------------------
# MoltenVK: the Vulkan loader needs an ICD manifest, so ship one
# --------------------------------------------------------------------------

sub find_file {
    my (@candidates) = @_;
    for my $path (@candidates) {
        return $path if defined $path && -f $path;
    }
    return undef;
}

sub write_vulkan_icd {
    my $icd_json = find_file(
        $ENV{VULKAN_SDK} ? File::Spec->catfile($ENV{VULKAN_SDK}, 'etc', 'vulkan', 'icd.d', 'MoltenVK_icd.json') : undef,
        '/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json',
        '/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json',
        glob('/opt/homebrew/Cellar/molten-vk/*/etc/vulkan/icd.d/MoltenVK_icd.json'),
    );
    fail("MoltenVK's ICD manifest was not found; install it with "
        . "'brew install molten-vk' or set VULKAN_SDK") unless $icd_json;

    my $manifest = JSON::PP->new->decode(slurp($icd_json));
    my $library = $manifest->{ICD}{library_path};
    fail("$icd_json has no ICD.library_path") unless defined $library;

    my $moltenvk = $library =~ m{^/}
        ? $library
        : File::Spec->rel2abs($library, dirname($icd_json));
    $moltenvk = verify_path($moltenvk);
    fail("MoltenVK was not found next to $icd_json") unless defined $moltenvk && -f $moltenvk;

    my $bundled = copy_dependency($moltenvk);
    my $relative = '../../../Frameworks/' . basename($bundled);
    $manifest->{ICD}{library_path} = $relative;

    my $icd_dir = File::Spec->catdir($res, 'vulkan', 'icd.d');
    make_path($icd_dir);
    my $dest = File::Spec->catfile($icd_dir, 'MoltenVK_icd.json');
    open(my $out, '>', $dest) or fail("cannot write $dest: $!");
    print $out JSON::PP->new->pretty->canonical->encode($manifest);
    close $out;
    log_line("Bundled MoltenVK and its ICD manifest");
}

sub verify_path {
    my ($path) = @_;
    my $real = abs_path($path);
    return (defined $real && -f $real) ? $real : undef;
}

# --------------------------------------------------------------------------
# Dylibs
# --------------------------------------------------------------------------

my %bundled_by_source;   # absolute source path => bundled path
my %bundled_by_name;     # basename => bundled path
my @to_process;          # [bundled path]

sub is_system_library {
    my ($path) = @_;
    return 1 if $path =~ m{^/System/};
    return 1 if $path =~ m{^/usr/lib/};
    return 0;
}

sub otool_dependencies {
    my ($file) = @_;
    my @deps;
    for my $line (split /\n/, capture('otool', '-L', $file)) {
        next unless $line =~ /^\s+(.+?)\s+\(compatibility/;
        push @deps, $1;
    }
    return @deps;
}

sub otool_rpaths {
    my ($file) = @_;
    my @rpaths;
    my $text = capture('otool', '-l', $file);
    while ($text =~ /cmd LC_RPATH.*?\n\s*cmdsize \d+\n\s*path (.+?) \(offset/sg) {
        push @rpaths, $1;
    }
    return @rpaths;
}

# Copies a dylib into Contents/Frameworks, giving it a unique name and an
# @rpath install name, then queues it for its own dependency scan.
sub copy_dependency {
    my ($source, $rpath_hint) = @_;
    my $real = abs_path($source);
    fail("cannot resolve dependency $source") unless defined $real && -f $real;
    return $bundled_by_source{$real} if exists $bundled_by_source{$real};

    my $base = basename($real);
    if (exists $bundled_by_name{$base}) {
        my ($stem, $ext) = $base =~ /^(.*?)(\.dylib|\.so|)$/;
        my $n = 1;
        $n++ while exists $bundled_by_name{"$stem-$n$ext"};
        $base = "$stem-$n$ext";
    }
    my $dest = File::Spec->catfile($fw, $base);
    copy($real, $dest) or fail("cannot copy $real: $!");
    chmod 0644, $dest;
    $bundled_by_source{$real} = $dest;
    $bundled_by_name{$base} = $dest;
    run_or_die('install_name_tool', '-id', "\@rpath/$base", $dest);
    add_rpath($dest, '@loader_path');
    push @to_process, $dest;
    detail("bundled $base");
    return $dest;
}

sub add_rpath {
    my ($file, $rpath) = @_;
    return if grep { $_ eq $rpath } otool_rpaths($file);
    run_or_die('install_name_tool', '-add_rpath', $rpath, $file);
}

sub resolve_dependency {
    my ($dep, $parent) = @_;
    if ($dep =~ m{^\@rpath/(.+)$}) {
        my $name = $1;
        return $bundled_by_name{$name} if exists $bundled_by_name{$name};
        my @dirs = (dirname($parent), '/opt/homebrew/lib', '/usr/local/lib');
        push @dirs, map {
            my $dir = $_;
            $dir =~ s{^\@loader_path}{dirname($parent)}e;
            $dir =~ s{^\@executable_path}{$macos}e;
            $dir;
        } otool_rpaths($parent);
        for my $dir (@dirs) {
            next unless defined $dir && $dir ne '';
            my $candidate = File::Spec->catfile($dir, $name);
            return $candidate if -f $candidate;
        }
        return undef;
    }
    return undef if $dep =~ m{^\@};
    return undef if is_system_library($dep);
    return $dep if -f $dep;
    return undef;
}

sub bundle_dependencies {
    log_line("Bundling dynamic libraries");
    my $found = 0;
    for my $dep (otool_dependencies($exe)) {
        next if is_system_library($dep);
        next if $dep =~ m{^\@executable_path};

        my $resolved = resolve_dependency($dep, $exe);
        if (!defined $resolved) {
            fail("cannot find $dep needed by the executable; install it "
                . "(see ./scripts/setup_project.pl)");
        }
        my $dest = copy_dependency($resolved);
        my $base = basename($dest);
        run_or_die('install_name_tool', '-change', $dep, "\@rpath/$base", $exe)
            unless $dep eq "\@rpath/$base";
        $found++;
    }
    add_rpath($exe, '@executable_path/../Frameworks');

    while (my $file = shift @to_process) {
        for my $dep (otool_dependencies($file)) {
            next if is_system_library($dep);
            next if $dep =~ m{^\@(?:executable_path|loader_path)};
            # An @rpath reference that is already in Frameworks (including a
            # library's own install name) needs no work.
            next if $dep =~ m{^\@rpath/(.+)$} && exists $bundled_by_name{$1};
            my $resolved = resolve_dependency($dep, $file);
            $resolved = $dep unless defined $resolved;
            if (!-f $resolved) {
                fail("cannot find $dep needed by " . basename($file));
            }
            next if $resolved eq $file || dirname($resolved) eq $fw;
            my $dest = copy_dependency($resolved);
            my $base = basename($dest);
            run_or_die('install_name_tool', '-change', $dep, "\@rpath/$base", $file)
                unless $dep eq "\@rpath/$base";
        }
    }
    log_line("  $found direct dependencies, " . scalar(keys %bundled_by_name)
        . " libraries in Contents/Frameworks");
}

# --------------------------------------------------------------------------
# Signing and verification
# --------------------------------------------------------------------------

sub sign_bundle {
    my $identity = $opt{identity};
    my $hardened = $identity ne '-';
    log_line("Signing with " . ($hardened ? $identity : 'an ad-hoc identity'));

    run_or_die('xattr', '-cr', $app_dir);

    my @machos;
    opendir(my $dh, $fw) or fail("cannot read $fw: $!");
    for my $name (sort grep { $_ ne '.' && $_ ne '..' } readdir $dh) {
        my $path = File::Spec->catfile($fw, $name);
        push @machos, $path if -f $path;
    }
    closedir $dh;
    for my $path (@machos) {
        sign_file($path, $identity, $hardened);
    }
    sign_file($exe, $identity, $hardened);
    sign_file($app_dir, $identity, $hardened);
}

sub sign_file {
    my ($path, $identity, $hardened) = @_;
    my @cmd = ('codesign', '--force', '--sign', $identity);
    push @cmd, '--timestamp=none' if $identity eq '-';
    if ($identity ne '-') {
        push @cmd, '--timestamp', '--options', 'runtime' if $hardened;
    }
    run_or_die(@cmd, $path);
}

sub verify_bundle {
    log_line("Verifying the bundle");
    run_or_die('plutil', '-lint', '-s', File::Spec->catfile($app_dir, 'Contents', 'Info.plist'));
    run_or_die('codesign', '--verify', '--deep', '--strict', $app_dir) unless $no_sign;

    # macOS shows "You can't open this version of this software on this version
    # of this Mac" when a binary's deployment target is newer than the machine,
    # so the executable must not advertise more than LSMinimumSystemVersion.
    my $exe_min = minimum_os($exe);
    if (defined $exe_min && version_cmp($exe_min, $opt{min_macos}) > 0) {
        fail("$exe was built for macOS $exe_min but the bundle advertises "
            . "$opt{min_macos}; rebuild with --macos_minimum_os=$opt{min_macos} "
            . "or raise --min-macos to $exe_min");
    }

    # Bundled third-party libraries (MoltenVK, Vulkan loader, wxWidgets) come
    # prebuilt and can set their own, higher floor. Report it rather than
    # shipping an app whose Info.plist claims a lower version than it needs.
    my @newer;
    for my $file (glob(File::Spec->catfile($fw, '*'))) {
        next unless -f $file;
        my $min = minimum_os($file);
        push @newer, basename($file) . " (macOS $min)"
            if defined $min && version_cmp($min, $opt{min_macos}) > 0;
    }
    if (@newer) {
        log_line("  warning: these bundled libraries need a newer macOS than "
            . "$opt{min_macos}:");
        log_line("    $_") for @newer;
    }

    # Nothing in the app may point at the checkout or Homebrew.
    my @leaks;
    for my $file ($exe, glob(File::Spec->catfile($fw, '*'))) {
        next unless -f $file;
        for my $dep (otool_dependencies($file)) {
            next if is_system_library($dep);
            next if $dep =~ m{^\@};
            push @leaks, basename($file) . ' -> ' . $dep;
        }
    }
    fail("the bundle still points outside itself:\n  " . join("\n  ", @leaks))
        if @leaks;

    if ($opt{target} eq 'player') {
        for my $required (qw(game.mpack game.mindex engine.mpack engine.mindex)) {
            fail("missing Contents/Resources/$required")
                unless -f File::Spec->catfile($res, $required);
        }
    }

    # The ICD manifest must point at the MoltenVK that was copied in.
    my $icd = File::Spec->catfile($res, 'vulkan', 'icd.d', 'MoltenVK_icd.json');
    if (-f $icd) {
        my $manifest = JSON::PP->new->decode(slurp($icd));
        my $library = $manifest->{ICD}{library_path};
        fail("$icd has no ICD.library_path") unless defined $library;
        my $path = $library =~ m{^/}
            ? $library
            : File::Spec->rel2abs($library, dirname($icd));
        fail("the ICD manifest points at a missing $library") unless -f $path;
    }
}

sub slurp {
    my ($path) = @_;
    open(my $fh, '<', $path) or fail("cannot read $path: $!");
    local $/;
    my $text = <$fh>;
    close $fh;
    return defined $text ? $text : '';
}

# The lowest macOS release a Mach-O file can run on, from LC_BUILD_VERSION
# (or LC_VERSION_MIN_MACOSX in very old binaries). Returns undef when the file
# cannot be inspected.
sub minimum_os {
    my ($path) = @_;
    return undef unless which('vtool');
    my $out = capture('vtool', '-show-build', $path);
    return $1 if $out =~ /^\s*minos\s+([0-9]+(?:\.[0-9]+)*)/m;
    return $1 if $out =~ /^\s*version\s+([0-9]+(?:\.[0-9]+)*)/m;
    return undef;
}

# Compares dotted version strings numerically (-1, 0 or 1).
sub version_cmp {
    my ($a, $b) = @_;
    my @left  = split /\./, $a;
    my @right = split /\./, $b;
    while (@left || @right) {
        my $x = shift @left  // 0;
        my $y = shift @right // 0;
        return $x <=> $y if $x != $y;
    }
    return 0;
}
