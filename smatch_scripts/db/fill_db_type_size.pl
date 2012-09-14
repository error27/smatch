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

$db->do("delete from type_size;");

my ($file_and_line, $file, $dummy, $struct_bit, $member, $size);

open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ / allocated_buf_size /)) {
        next;
    }

    # lib/prio_heap.c:12 heap_init() info: '(struct ptr_heap)->ptrs' allocated_buf_size 4096

    s/\n//;
    s/'//g;

    if ($_ =~ /\(struct /) {
        ($file_and_line, $dummy, $dummy, $struct_bit, $member, $dummy, $size) = split(/ /, $_);
        $member = "(struct $member";
    } else {
        ($file_and_line, $dummy, $dummy, $member, $dummy, $size) = split(/ /, $_);
    }
    ($file, $dummy) = split(/:/, $file_and_line);

    if (!defined($size)) {
        next;
    }

    $db->do("insert into type_size values ('$file', '$member', '$size')\n");
}

$db->commit();
$db->disconnect();
