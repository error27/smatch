#!/usr/bin/perl -w

# init/main.c +165 obsolete_checksetup(7) info: passes param_value strlen 0 min-max

use strict;
use DBI;

my $warns = shift;

if (!defined($warns)) {
    print "usage:  $0 <warns.txt>\n";
    exit(1);
}

my $db = DBI->connect("dbi:SQLite:smatch_db.sqlite", "", "", {RaiseError => 1, AutoCommit => 0});

my $prev_fn = "";
my $prev_line = "+0";
my $prev_param = 0;
my $func_id = 0;
my $type = 1;  # PARAM_VALUE from smatch.h

$db->do("delete from caller_info where type = $type;");

open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ /info: passes param_value /)) {
	next;
    }
    if ($_ =~ /__builtin_/) {
	next;
    }
    if ($_ =~ /(printk|memset|memcpy|kfree|printf|dev_err|writel)/) {
	next;
    }

    s/\n//;

    my ($file, $line, $dummy, $func, $param, $value);
    ($file, $line, $dummy, $dummy, $dummy, $dummy, $func, $param, $value) = split(/ /, $_);


    if (!defined($value) || !($param =~ /^\d+$/)) {
	next;
    }

    if ($prev_fn ne $func || $prev_line ne $line || $param < $prev_param) {
	$func_id++;
    }

    $db->do("insert into caller_info values ('$file', '$func', $func_id, $type, $param, '$value')");
}
$db->commit();
$db->disconnect();
