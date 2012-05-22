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

$db->do("delete from call_implies");

my $type = 6;  # DEREFERENCE
open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ /info: dereferences_param /)) {
        next;
    }

    s/\n//;

    my ($file_and_line, $file, $func, $dummy, $param, $value, $gs);

    # fs/buffer.c:62 list_add() info: dereferences_param 1 static

    $value = 1;
    ($file_and_line, $func, $dummy, $dummy, $param, $gs) = split(/ /, $_);
    ($file, $dummy) = split(/:/, $file_and_line);
    $func =~ s/\(\)//;

    if (!defined($gs)) {
        next;
    }

    my $static = 0;
    if ($gs =~ /static/) {
        $static = 1;
    }

    # print "insert into call_implies values ('$file', '$func', $static, $type, $param, $value)\n";
    $db->do("insert into call_implies values ('$file', '$func', $static, $type, $param, $value)");
}
close(WARNS);

$db->commit();
$db->disconnect();
