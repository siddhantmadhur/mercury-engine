#!/usr/bin/env perl
use strict;
use warnings;
use File::Find;

my $search_dir = $ARGV[0] // '.';

my $target_header = <<'END_HEADER';
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

END_HEADER
#------

$target_header =~ s/\r?\n/\n/g;
$target_header =~ s/\n*\z/\n\n/;

find(\&process_file, $search_dir);

sub process_file {

  # skip directories and files that aren't source code
  return unless -f $_;
  return unless /\.(?:c|cc|h|rs|as|py|js|ts)\z/i;

  my $file = $_;

  # reading file into memory
  open(my $in, "<", $file) or do {
    warn "Could not open '$File::Find::name' for reading: $!\n";
    return; 
  };

  my $content = do { local $/; <$in> } // '';
  close($in);

  (my $normalized = $content) =~ s/\r\n/\n/g;
  if (index($normalized, $target_header) == 0) {
    return;
  }

  open(my $out, '>', $file) or do {
    warn "Could not open '$File::Find::name' for writing: $!\n";
    return;
  };

  print $out $target_header, $content;
  close($out);

  print "Updated: $File::Find::name...\n";
}

