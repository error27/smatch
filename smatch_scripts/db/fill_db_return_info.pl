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

my $type = 5;  # RETURN_VALUE
open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ /info: return_value /)) {
        next;
    }

    s/\n//;

    my ($file_and_line, $file, $func, $dummy, $value);

    # sound/pci/hda/patch_sigmatel.c:3125 create_controls_idx() info: return_value min-(-1)

    ($file_and_line, $func, $dummy, $dummy, $value) = split(/ /, $_);
    ($file, $dummy) = split(/:/, $file_and_line);
    $func =~ s/\(\)//;
    $value =~ s/unknown/min-max/;

    if (!defined($value)) {
        next;
    }

    $value =~ s/'//g;

    # print "insert into return_info values ('$file', '$func', '$value')\n";
    $db->do("insert into return_info values ('$file', '$func', $type, '$value')");
}
close(WARNS);

$type = 3;  # USER_DATA
open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ /info: returns_user_data/)) {
        next;
    }

    s/\n//;

    my ($file_and_line, $file, $func, $dummy);

    #include/linux/netfilter/ipset/ip_set.h:402 ip_set_get_h32() info: returns_user_data

    ($file_and_line, $func, $dummy) = split(/ /, $_);
    ($file, $dummy) = split(/:/, $file_and_line);
    $func =~ s/\(\)//;

    $db->do("insert into return_info values ('$file', '$func', $type, '1')");
}
close(WARNS);

$db->do("delete from return_info where function='strnlen';");

$db->commit();
$db->disconnect();
