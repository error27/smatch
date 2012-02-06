#!/usr/bin/perl -w

use strict;
use DBI;

my $warns = shift;

if (!defined($warns)) {
    print "usage:  $0 <warns.txt>\n";
    exit(1);
}

my $db = DBI->connect("dbi:SQLite:smatch_db.sqlite", "", "", {RaiseError => 1, AutoCommit => 0});
$db->do("PRAGMA synchronous = OFF");
$db->do("PRAGMA cache_size = 800000");
$db->do("PRAGMA journal_mode = OFF");

$db->do("delete from return_info");

open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ /info: return_value /)) {
	next;
    }

    s/\n//;

    my ($file_and_line, $file, $func, $dummy, $value);

    # sound/pci/hda/patch_sigmatel.c:3125 create_controls_idx(29) info: return_value min-(-1)

    ($file_and_line, $func, $dummy, $dummy, $value) = split(/ /, $_);
    ($file, $dummy) = split(/:/, $file_and_line);
    $func =~ s/\(\d+\)//;
    $value =~ s/unknown/min-max/;

    if (!defined($value)) {
	next;
    }

    $value =~ s/'//g;

    # print "insert into return_info values ('$file', '$func', '$value')\n";
    $db->do("insert into return_info values ('$file', '$func', '$value')");
}
$db->commit();
$db->disconnect();
