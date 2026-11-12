#!/usr/bin/env perl
#
# Builds the pinned V8 (third_party/v8/VERSION) as a static monolith library
# with GN. This is the fallback for hosts where V8's Bazel build cannot be
# used; the normal Bazel build does not run this script.
#
# Takes ~30-60 min and ~15 GB of disk. Idempotent: finished steps are skipped.

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Spec;

if ($^O ne 'linux' && $^O ne 'darwin') {
    print STDERR "OS not supported\n";
    exit 1;
}

my (undef, $SCRIPT_DIR, undef) = File::Spec->splitpath(File::Spec->rel2abs($0));
my $ROOT = abs_path(File::Spec->catdir($SCRIPT_DIR, '..'));
die "cannot resolve project root\n" if !defined $ROOT;
my $V8_ROOT = File::Spec->catdir($ROOT, 'third_party', 'v8');
my $VERSION = read_version(File::Spec->catfile($V8_ROOT, 'VERSION'));
my $OUT = File::Spec->catdir($V8_ROOT, 'v8', 'out', 'release');
my $LIB = File::Spec->catfile($OUT, 'obj', 'libv8_monolith.a');
my $STAMP = File::Spec->catfile($V8_ROOT, '.built_version');

if (-f $LIB && -f $STAMP && read_chomp($STAMP) eq $VERSION) {
    print "V8 $VERSION already built: $LIB\n";
    exit 0;
}

# Fetch sources (and the Bazel compatibility patch) first.
run($^X, File::Spec->catfile($ROOT, 'scripts', 'fetch_v8.pl'));

$ENV{PATH} = File::Spec->catdir($V8_ROOT, 'depot_tools') . ':' . ($ENV{PATH} // '');
chdir(File::Spec->catdir($V8_ROOT, 'v8')) or die "cannot chdir V8 checkout: $!\n";

my $GN_ARGS;
if ($^O eq 'linux') {
    # Chromium's build emits ELF CREL relocations when it links with lld, and
    # GNU ld (which links the engine) cannot read them. Switch them off in the
    # checkout (idempotent).
    my $compiler = File::Spec->catfile('build', 'config', 'compiler', 'BUILD.gn');
    my $text = read_file($compiler);
    $text =~ s/if \(is_linux && use_lld && current_cpu != "arm" && current_cpu != "s390x"\) \{/if (false && is_linux && use_lld && current_cpu != "arm" && current_cpu != "s390x") {/g;
    write_file($compiler, $text);
    $GN_ARGS = read_chomp(File::Spec->catfile($V8_ROOT, 'args.gn'));
} else {
    # use_sysroot/use_glib are Linux-only GN args.
    $GN_ARGS = filter_gn_args(read_file(File::Spec->catfile($V8_ROOT, 'args.gn')));
}

my $machine = `uname -m`;
chomp $machine;
my $CPU = ($machine eq 'arm64' || $machine eq 'aarch64') ? 'arm64' : 'x64';

my $args = $GN_ARGS . "\ntarget_cpu=\"$CPU\"";
run('gn', 'gen', $OUT, '--args=' . $args);
run('ninja', '-C', $OUT, 'v8_monolith');
write_file($STAMP, "$VERSION\n");
print "Built $LIB\n";

sub run {
    my (@cmd) = @_;
    system(@cmd);
    die "command failed (@cmd): $!\n" if $? != 0;
}

sub read_file {
    my ($path) = @_;
    open my $fh, '<', $path or die "cannot read $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    return defined $text ? $text : '';
}

sub write_file {
    my ($path, $text) = @_;
    open my $fh, '>', $path or die "cannot write $path: $!\n";
    print {$fh} $text;
    close $fh or die "cannot write $path: $!\n";
}

sub read_version {
    my ($path) = @_;
    my $text = read_file($path);
    $text =~ s/\s+//g;
    return $text;
}

sub read_chomp {
    my ($path) = @_;
    my $text = read_file($path);
    $text =~ s/\r?\n+\z//;
    return $text;
}

sub filter_gn_args {
    my ($text) = @_;
    $text =~ s/\n+\z//;
    my @lines = grep { !/^[ \t]*(use_sysroot|use_glib)[ \t]*=/ } split /\n/, $text;
    return join("\n", @lines);
}
