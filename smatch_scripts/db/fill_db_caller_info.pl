#!/usr/bin/perl -w

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
my $type;

$db->do("delete from caller_info");

open(WARNS, "<$warns");
while (<WARNS>) {
    if (!($_ =~ /info:/)) {
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

    if ($_ =~ /info: passes param_value /) {
        # init/main.c +165 obsolete_checksetup(7) info: passes param_value strlen 0 min-max
	$type = 1;
	($file, $line, $dummy, $dummy, $dummy, $dummy, $func, $param, $value) = split(/ /, $_);

    } elsif ($_ =~ /info: passes_buffer /) {
        # init/main.c +175 obsolete_checksetup(17) info: passes_buffer 'printk' 0 38
	$type = 2;
	($file, $line, $dummy, $dummy, $dummy, $func, $param, $value) = split(/ /, $_);
    } else {
	next;
    }

    if (!defined($value) || !($param =~ /^\d+$/)) {
	next;
    }

    $func =~ s/'//g;

    if ($prev_fn ne $func || $prev_line ne $line || $param < $prev_param) {
	$func_id++;
    }

    $db->do("insert into caller_info values ('$file', '$func', $func_id, $type, $param, '$value')");
}
$db->commit();
$db->disconnect();
