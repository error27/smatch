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

    my ($file_and_line, $file, $func, $dummy, $value, $gs);

    # sound/pci/hda/patch_sigmatel.c:3125 create_controls_idx() info: return_value min-(-1) global

    ($file_and_line, $func, $dummy, $dummy, $value, $gs) = split(/ /, $_);
    ($file, $dummy) = split(/:/, $file_and_line);
    $func =~ s/\(\)//;
    $value =~ s/unknown/min-max/;

    if (!defined($gs)) {
        next;
    }
    my $static = 0;
    if ($gs =~ /static/) {
        $static = 1;
    }

    $value =~ s/'//g;

    $db->do("insert into return_info values ('$file', '$func', $static, $type, '$value')");
}
close(WARNS);

$type = 3;  # USER_DATA
open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ /info: returns_user_data/)) {
        next;
    }

    s/\n//;

    my ($file_and_line, $file, $func, $dummy, $gs);

    #include/linux/netfilter/ipset/ip_set.h:402 ip_set_get_h32() info: returns_user_data static

    ($file_and_line, $func, $dummy, $gs) = split(/ /, $_);
    ($file, $dummy) = split(/:/, $file_and_line);
    $func =~ s/\(\)//;

    my $static = 0;
    if ($gs =~ /static/) {
        $static = 1;
    }

    $db->do("insert into return_info values ('$file', '$func', $static, $type, '1')");
}
close(WARNS);

$db->do("delete from return_info where function='strnlen';");

$db->commit();
$db->disconnect();
