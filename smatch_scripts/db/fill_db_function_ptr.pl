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

$db->do("delete from function_ptr;");

open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ /info: sets_fn_ptr /)) {
	next;
    }

    # kernel/ksysfs.c +87 (null)(87) info: sets_fn_ptr '(struct kobj_attribute)->show' 'profiling_show'

    s/\n//;

    my ($file_and_line, $file, $dummy, $struct_bit, $func, $ptr);
    ($file_and_line, $dummy, $dummy, $dummy, $dummy, $struct_bit, $ptr, $func) = split(/ /, $_);
    ($file, $dummy) = split(/:/, $file_and_line);

    if (!defined($func)) {
	next;
    }
    if (!($struct_bit =~ /^'\(struct$/)) {
	next;
    }
    if (!($func =~ /^'[\w_]+'$/)) {
	next;
    }

    $struct_bit =~ s/'//;
    $ptr =~ s/'//;
    $func =~ s/'//g;

    $db->do("insert into function_ptr values ('$file', '$func', '$struct_bit $ptr')\n");
}
$db->commit();
$db->disconnect();
