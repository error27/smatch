#!/usr/bin/perl -w

use strict;
use DBI;

my $warns = shift;

if (!defined($warns)) {
    print "usage:  $0 <warns.txt>\n";
    exit(1);
}

my $db = DBI->connect("dbi:SQLite:smatch_db.sqlite", "", "", {RaiseError => 1, AutoCommit => 0});

$db->do("delete from untrusted;");

open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ / user_data /)) {
	next;
    }
    if ($_ =~ /kfree/) {
	next;
    }

    s/\n//;

    my ($file, $dummy, $func, $param);
    ($file, $dummy, $dummy, $dummy, $dummy, $func, $param) = split(/ /, $_);

    if (!defined($param) || !($param =~ /^\d+$/)) {
	next;
    }

    $db->do("insert into untrusted values ('$file', '$func', $param)");
}
$db->commit();
$db->disconnect();
