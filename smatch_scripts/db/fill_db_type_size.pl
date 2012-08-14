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

my $types = {};

open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ / allocated_buf_size /)) {
        next;
    }

    # lib/prio_heap.c:12 heap_init() info: '(struct ptr_heap)->ptrs' allocated_buf_size 4096

    s/\n//;
    s/'//g;

    my ($dummy, $struct_bit, $member, $size);
    ($dummy, $dummy, $dummy, $struct_bit, $member, $dummy, $size) = split(/ /, $_);

    if (!defined($size)) {
        next;
    }
    if (!($struct_bit =~ /^\(struct$/)) {
        next;
    }

    if (defined($types->{$member}) && $types->{$member} != $size) {
        $size = -1;
    }

    $types->{$member} = $size;
}

foreach my $key (keys($types)) {
    if ($types->{$key} != -1) {
        $db->do("insert into type_size values ('(struct $key', '$types->{$key}')\n");
    }
}

$db->commit();
$db->disconnect();
