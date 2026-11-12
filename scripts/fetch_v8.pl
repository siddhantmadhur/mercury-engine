#!/usr/bin/env perl
#
# Fetches the pinned V8 (third_party/v8/VERSION) with depot_tools and makes its
# Bazel files usable from Mercury. Does not build V8: Bazel builds it on demand.
# Idempotent: an up-to-date checkout and patch are skipped.

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
my $DEPOT = File::Spec->catdir($V8_ROOT, 'depot_tools');
my $STAMP = File::Spec->catfile($V8_ROOT, '.fetched_version');

if (-f $STAMP && read_chomp($STAMP) eq $VERSION) {
    # The patch script is idempotent; run it so changes to the compatibility
    # patch apply without refetching V8.
    run($^X, File::Spec->catfile($ROOT, 'scripts', 'v8_bazel_compat.pl'),
        File::Spec->catdir($V8_ROOT, 'v8'));
    print "V8 $VERSION already fetched\n";
    exit 0;
}

if (!-d $DEPOT) {
    run('git', 'clone', '--depth', '1',
        'https://chromium.googlesource.com/chromium/tools/depot_tools.git', $DEPOT);
}
$ENV{PATH} = $DEPOT . ':' . ($ENV{PATH} // '');

chdir($V8_ROOT) or die "cannot chdir $V8_ROOT: $!\n";
if (!-d 'v8') {
    run('fetch', '--no-history', 'v8');
}
chdir('v8') or die "cannot chdir v8: $!\n";

my $ref = "refs/tags/$VERSION:refs/tags/$VERSION";
system('sh', '-c', 'git fetch --depth 1 origin ' . shell_quote($ref) . ' 2>/dev/null');
run('git', 'checkout', $VERSION);
run('gclient', 'sync', '-D', '--no-history');

run($^X, File::Spec->catfile($ROOT, 'scripts', 'v8_bazel_compat.pl'),
    File::Spec->catdir($V8_ROOT, 'v8'));

write_file($STAMP, "$VERSION\n");
print "Fetched and patched V8 $VERSION in " . File::Spec->catdir($V8_ROOT, 'v8') . "\n";

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

sub shell_quote {
    my ($text) = @_;
    $text =~ s/'/'"'"'/g;
    return "'$text'";
}
