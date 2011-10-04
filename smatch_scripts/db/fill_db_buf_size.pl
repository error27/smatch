#!/usr/bin/perl -w

use strict;
use DBI;

my $warns = shift;

if (!defined($warns)) {
    print "usage:  $0 <warns.txt>\n";
    exit(1);
}

my $db = DBI->connect("dbi:SQLite:smatch_db.sqlite", "", "", {RaiseError => 1, AutoCommit => 0});

$db->do("delete from buf_size;");

open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ /passes_buffer/)) {
	next;
    }
    if ($_ =~ /(printk|memset|memcpy|kfree|printf|dev_err)/) {
	next;
    }

    s/\n//;

    my ($file, $dummy, $func, $param, $size);
    ($file, $dummy, $dummy, $dummy, $dummy, $func, $param, $size) = split(/ /, $_);

    if (!defined($size) || !($param =~ /^\d+$/)) {
	next;
    }

    $db->do("insert into buf_size values ('$file', $func, $param, $size)");
}
$db->commit();
$db->disconnect();
