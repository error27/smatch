#!/usr/bin/perl -w

use strict;
use DBI;

my $warns = shift;

if (!defined($warns)) {
    print "usage:  $0 <warns.txt>\n";
    exit(1);
}

my $db = DBI->connect("dbi:SQLite:smatch_db.sqlite", "", "", {AutoCommit => 0});
$db->do("PRAGMA synchronous = OFF");
$db->do("PRAGMA cache_size = 800000");
$db->do("PRAGMA journal_mode = OFF");

$db->do("delete from no_side_effects;");

my ($file_and_line, $file, $func, $gs, $static, $dummy);

open(WARNS, "<$warns");
while (<WARNS>) {
    # info: no_side_effects
    if (!($_ =~ /info: no_side_effects /)) {
        next;
    }

    ($file_and_line, $func, $dummy, $dummy, $gs) = split(/ /, $_);
    ($file, $dummy) = split(/:/, $file_and_line);
    $func =~ s/\(\)//;

    $static = 0;
    if ($gs =~ /static/) {
        $static = 1;
    }

    $db->do("insert into no_side_effects values ('$file', '$func', $static)");
}
$db->commit();
$db->disconnect();
