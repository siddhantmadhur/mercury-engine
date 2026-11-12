#!/usr/bin/env perl
#
# Downloads the pinned esbuild binary (the TypeScript -> JavaScript bundler used
# for game scripts) into bin/esbuild/. Node is not needed. The npm tarball is
# checked against the SHA-256 written below.

use strict;
use warnings;
use Digest::SHA qw(sha256_hex);
use Cwd qw(abs_path);
use File::Copy qw(copy);
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
my $ESBUILD_VERSION = '0.28.2';

my %platforms = (
    'Linux-x86_64'  => ['linux-x64',    '9573bb2233aab0f9ea7647d5cca9726113cc1768de61d66b17267f4db84488f6'],
    'Linux-aarch64' => ['linux-arm64',  'a96dbfa41d3ef5dbd1ef22b1c10d5187be9267e86093a870f06402a7ec931596'],
    'Darwin-x86_64' => ['darwin-x64',   'abb6a7a895aaf2cc0df36fecc3bf0042479ffec51a643a056d54e9109f27db55'],
    'Darwin-arm64'  => ['darwin-arm64', '1980cde09749094452b20d36ff267585ccb3f72749c7bc97291cd9996ccf5a2a'],
);

my $uname = `uname -s`;
chomp $uname;
my $machine = `uname -m`;
chomp $machine;
my $key = "$uname-$machine";

if (!exists $platforms{$key}) {
    print STDERR "No prebuilt esbuild for $key\n";
    exit 1;
}
my ($PLATFORM, $SHA256) = @{$platforms{$key}};

my $DEST = File::Spec->catdir($PROJECT_ROOT, 'bin', 'esbuild');
my $ESBUILD = File::Spec->catfile($DEST, 'esbuild');
if (-x $ESBUILD) {
    my $installed = capture($ESBUILD, '--version');
    if (defined $installed && $installed eq $ESBUILD_VERSION) {
        print "esbuild $ESBUILD_VERSION already installed: $ESBUILD\n";
        exit 0;
    }
}

my $TMP = tempdir(CLEANUP => 1);
my $TGZ = File::Spec->catfile($TMP, 'esbuild.tgz');

print "Downloading esbuild $ESBUILD_VERSION ($PLATFORM) into bin/esbuild ...\n";
my $url = 'https://registry.npmjs.org/@esbuild/' . $PLATFORM . '/-/'
    . $PLATFORM . '-' . $ESBUILD_VERSION . '.tgz';
run('curl', '-fL', $url, '-o', $TGZ);

my $actual = sha256_hex(read_binary($TGZ));
if ($actual ne $SHA256) {
    print STDERR "esbuild checksum mismatch: expected $SHA256, got $actual\n";
    exit 1;
}

run('tar', '-xzf', $TGZ, '-C', $TMP);
make_path($DEST);

my $source = File::Spec->catfile($TMP, 'package', 'bin', 'esbuild');
copy($source, $ESBUILD) or die "cannot copy $source to $ESBUILD: $!\n";
chmod 0755, $ESBUILD or die "cannot make $ESBUILD executable: $!\n";
run($ESBUILD, '--version');

sub run {
    my (@cmd) = @_;
    system(@cmd);
    die "command failed (@cmd): $!\n" if $? != 0;
}

sub capture {
    my (@cmd) = @_;
    open my $fh, '-|', @cmd or return undef;
    local $/;
    my $output = <$fh>;
    close $fh;
    return undef if $? != 0;
    $output //= '';
    $output =~ s/\s+\z//;
    return $output;
}

sub read_binary {
    my ($path) = @_;
    open my $fh, '<:raw', $path or die "cannot read $path: $!\n";
    local $/;
    my $data = <$fh>;
    close $fh;
    return defined $data ? $data : '';
}
