#!/usr/bin/env perl
#
# Builds the CI toolchain image from docker/Dockerfile and pushes it to the
# GitLab container registry. The image is what .gitlab-ci.yml runs every job in
# ($CI_REGISTRY_IMAGE/build-env:latest), so re-run this whenever docker/
# changes.
#
# Prerequisites: Docker running, and `docker login <registry>` already done
# (use a GitLab personal access token with write_registry scope as the password).
#
# Usage: perl scripts/build_ci_image.pl [options]
#   --image NAME      Full image name (default: <registry>/<project path>/build-env:latest,
#                     with the project path taken from the `origin` remote)
#   --registry HOST   Registry host (default: registry.smadhur.dev)
#   --platform P      Target platform (default: linux/amd64, what the runners use)
#   --no-push         Build only
#   --no-cache        Pass --no-cache to docker build
#
# Works on Linux, macOS and Windows (any Perl 5.22+); no shell is involved.

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Spec;
use Getopt::Long qw(GetOptions);

my $registry = 'registry.smadhur.dev';
my $platform = 'linux/amd64';
my ($image, $no_push, $no_cache);

GetOptions(
    'image=s'    => \$image,
    'registry=s' => \$registry,
    'platform=s' => \$platform,
    'no-push'    => \$no_push,
    'no-cache'   => \$no_cache,
) or die "usage: $0 [--image NAME] [--registry HOST] [--platform P] [--no-push] [--no-cache]\n";

my (undef, $script_dir, undef) = File::Spec->splitpath(File::Spec->rel2abs($0));
my $root = abs_path(File::Spec->catdir($script_dir, '..'));
my $docker_dir = File::Spec->catdir($root, 'docker');
die "missing $docker_dir/Dockerfile\n" unless -f File::Spec->catfile($docker_dir, 'Dockerfile');

$image //= default_image();
print "==> Image: $image ($platform)\n";

my @build = ('docker', 'build', '--platform', $platform, '-t', $image);
push @build, '--no-cache' if $no_cache;
push @build, $docker_dir;
run(@build);

if ($no_push) {
    print "==> Built $image (not pushed)\n";
} else {
    run('docker', 'push', $image);
    print "==> Pushed $image\n";
}

sub default_image {
    my $url = capture('git', '-C', $root, 'remote', 'get-url', 'origin');
    die "cannot read the origin remote; pass --image\n" unless defined $url;
    # https://host/group/project.git, ssh://git@host/group/project.git or git@host:group/project.git
    my ($path) = $url =~ m{^[a-z+]+://[^/]+/(.+?)(?:\.git)?/?$}i;
    ($path) = $url =~ m{^[^@/]+@[^:]+:(.+?)(?:\.git)?/?$} unless defined $path;
    die "cannot derive a project path from '$url'; pass --image\n" unless defined $path;
    return lc("$registry/$path") . '/build-env:latest';
}

sub capture {
    my (@cmd) = @_;
    open(my $fh, '-|', @cmd) or return undef;
    my $out = do { local $/; <$fh> };
    close($fh) or return undef;
    $out =~ s/\s+\z// if defined $out;
    return $out;
}

sub run {
    my (@cmd) = @_;
    print "+ @cmd\n";
    system(@cmd);
    die "command failed (@cmd): " . ($? == -1 ? $! : "exit " . ($? >> 8)) . "\n" if $? != 0;
}
