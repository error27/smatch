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

my ($dummy, $sql);

open(WARNS, "<$warns");
while (<WARNS>) {

    if (!($_ =~ /^.*? \w+\(\) SQL: /)) {
        next;
    }
    ($dummy, $dummy, $sql) = split(/:/);

    $db->do($sql);
}

$db->commit();
$db->disconnect();
