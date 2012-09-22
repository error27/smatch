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

$db->do("delete from return_states;");

my $type;
my $param;
my ($file_and_line, $file, $dummy, $func, $return_id, $return_value, $key, $value, $gs);
my $static;

open(WARNS, "<$warns");
while (<WARNS>) {

    if ($_ =~ /info: return_marker/) {
        # net/ipv4/inet_diag.c:363 bitstring_match() info: return_marker 1 '1' static
        $type = 0; # INTERNAL
        $value = '';
        ($file_and_line, $func, $dummy, $dummy, $return_id, $return_value, $gs) = split(/ /, $_);
        $key = "";
        $param = -1;
    } elsif ($_ =~ /info: returns_locked/) {
        # net/ipv4/inet_diag.c:363 bitstring_match() info: returns_locked 1 '0' 'spin_lock:lock' static
        $type = 9; # LOCK_HELD
        $value = '';
        ($file_and_line, $func, $dummy, $dummy, $return_id, $return_value, $key, $gs) = split(/ /, $_);
        $param = -1;
    } elsif ($_ =~ /info: returns_released/) {
        # net/ipv4/inet_diag.c:363 bitstring_match() info: returns_locked 1 '0' 'spin_lock:lock' static
        $type = 10; # LOCK_RELEASED
        $value = '';
        ($file_and_line, $func, $dummy, $dummy, $return_id, $return_value, $key, $gs) = split(/ /, $_);
        $param = -1;
    } elsif ($_ =~ /info: returns_user_data/) {
        # net/netfilter/ipset/ip_set_hash_netiface.c:402 ip_set_get_h32() info: returns_user_data 1 '' static
        $type = 3;  # USER_DATA
        $key = '';
        $value = '';
        ($file_and_line, $func, $dummy, $dummy, $return_id, $return_value, $gs) = split(/ /, $_);
        $param = -1;
    } else {
        next;
    }

    ($file, $dummy) = split(/:/, $file_and_line);

    $func =~ s/\(\)//;
    $return_value =~ s/'//g;
    $key =~ s/'//g;
    $value =~ s/'//g;

    $static = 0;
    if ($gs =~ /static/) {
        $static = 1;
    }

    # print("insert into return_states values ('$file', '$func', $return_id, $return_value, $static, $type, $param, '$key', '$value')\n");
    $db->do("insert into return_states values ('$file', '$func', $return_id, '$return_value', $static, $type, $param, '$key', '$value')\n");
}

$db->commit();
$db->disconnect();
