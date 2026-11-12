#!/usr/bin/env perl
# Benchmarks model_viewer maps with the profiler's bench mode.
#
#   tools/benchmark.pl [build-dir] [map...]
#
# Each map runs with a fixed 1/60 s time step (the flythrough is at the same
# place on the same frame every run) and no vsync. It prints one summary per
# map and saves it, with a screenshot taken right after the measured frames, under
# $BENCH_DIR (default: bench/<date>-<commit>).
#
#   WARMUP=300 FRAMES=1000   frames skipped, frames measured

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Path qw(make_path);
use File::Spec;
use POSIX qw(strftime);

if ($^O ne 'linux' && $^O ne 'darwin') {
    print STDERR "OS not supported\n";
    exit 1;
}

my (undef, $SCRIPT_DIR, undef) = File::Spec->splitpath(File::Spec->rel2abs($0));
my $root = abs_path(File::Spec->catdir($SCRIPT_DIR, '..'));
die "cannot resolve project root\n" if !defined $root;

my $build_arg = shift @ARGV;
$build_arg = File::Spec->catdir($root, 'build-release') if !defined $build_arg;
my $build = abs_path($build_arg);
die "cannot find build directory: $build_arg\n" if !defined $build;

my @maps = @ARGV;
@maps = ('san_miguel', 'sponza', 'living_room', 'cornell_original') if !@maps;

my $warmup = defined $ENV{WARMUP} ? $ENV{WARMUP} : 300;
my $frames = defined $ENV{FRAMES} ? $ENV{FRAMES} : 1000;

my $commit = git_short_head();
my $stamp = strftime('%Y%m%d-%H%M%S', localtime);
my $out = defined $ENV{BENCH_DIR}
    ? $ENV{BENCH_DIR}
    : File::Spec->catdir($root, 'bench', "$stamp-$commit");
make_path($out);

my $exe = File::Spec->catfile($build, 'examples', 'model_viewer', 'model_viewer');

chdir(File::Spec->catdir($root, 'examples', 'model_viewer'))
    or die "cannot chdir to examples/model_viewer: $!\n";

for my $map (@maps) {
    print "== $map\n";

    my $log = File::Spec->catfile($out, "$map.log");
    {
        local %ENV = %ENV;
        $ENV{MERCURY_FIXED_DT} = 1;
        $ENV{MERCURY_VSYNC} = 0;
        $ENV{MERCURY_BENCH_WARMUP} = $warmup;
        $ENV{MERCURY_BENCH_FRAMES} = $frames;
        $ENV{MERCURY_BENCH_OUT} = File::Spec->catfile($out, "$map.txt");
        $ENV{MERCURY_BENCH_CAPTURE} = File::Spec->catfile($out, "$map.png");
        run_redirected($log, $exe, '--map', "res://maps/$map.mmap");
    }

    if (open my $log_fh, '<', $log) {
        while (my $line = <$log_fh>) {
            print $line if $line =~ /Loaded .* in|Loaded .* from the cache|settled|probes from the cache/;
        }
        close $log_fh;
    }

    my $summary = File::Spec->catfile($out, "$map.txt");
    if (-e $summary) {
        open my $summary_fh, '<', $summary
            or die "cannot read $summary: $!\n";
        print while <$summary_fh>;
        close $summary_fh;
    } else {
        print "(no summary; see $log)\n";
    }
}

print "results in $out\n";

sub run_redirected {
    my ($log, @cmd) = @_;
    my $pid = fork();
    die "fork: $!\n" if !defined $pid;
    if ($pid == 0) {
        open STDOUT, '>', $log or exit 127;
        open STDERR, '>&', \*STDOUT or exit 127;
        exec @cmd;
        exit 127;
    }
    waitpid($pid, 0);
    return $?;
}

sub git_short_head {
    open my $fh, '-|', 'git', '-C', $root, 'rev-parse', '--short', 'HEAD'
        or die "git rev-parse failed: $!\n";
    my $commit = <$fh>;
    close $fh;
    die "git rev-parse failed\n" if $? != 0;
    $commit = '' if !defined $commit;
    chomp $commit;
    return $commit;
}
