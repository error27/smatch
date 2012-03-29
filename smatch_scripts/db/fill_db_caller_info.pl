#!/usr/bin/perl -w

use strict;
use DBI;

sub usage()
{
    print "usage:  $0 <warns.txt>\n";
    exit(1);
}

my %too_common_funcs;
sub get_too_common_functions($)
{
    my $warns = shift;

    open(FUNCS, "cat $warns | grep 'info: passes param_value' | grep \" -1 '\\\$\\\$' min-max\" | cut -d \"'\" -f 2 | sort | uniq -c | ");

    while (<FUNCS>) {
        if ($_ =~ /(\d+) (.*)/) {
            if (int($1) > 200) {
                $too_common_funcs{$2} = 1;
            }
        }
    }

    close(FUNCS);
}

my $warns = shift;

if (!defined($warns)) {
    usage();
}

get_too_common_functions($warns);

my $db = DBI->connect("dbi:SQLite:smatch_db.sqlite", "", "", {RaiseError => 1, AutoCommit => 0});
$db->do("PRAGMA synchronous = OFF");
$db->do("PRAGMA cache_size = 800000");
$db->do("PRAGMA journal_mode = OFF");

my $prev_fn = "";
my $prev_line = "+0";
my $prev_param = 0;
my $func_id = 1;
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

    my ($file_and_line, $file, $line, $dummy, $func, $param, $key, $value);

    if ($_ =~ /info: passes param_value /) {
        # init/main.c +165 obsolete_checksetup(7) info: passes param_value strlen 0 min-max
	$type = 1;
	($file_and_line, $dummy, $dummy, $dummy, $dummy, $func, $param, $key, $value) = split(/ /, $_);
	($file, $line) = split(/:/, $file_and_line);

	if ($func eq "'(struct") {
	    ($file_and_line, $dummy, $dummy, $dummy, $dummy, $dummy, $func, $param, $key, $value) = split(/ /, $_);
	    ($file, $line) = split(/:/, $file_and_line);
	    $func = "$dummy $func";
	}

    } elsif ($_ =~ /info: passes_buffer /) {
        # init/main.c +175 obsolete_checksetup(17) info: passes_buffer 'printk' 0 '$$' 38
	$type = 2;
	($file_and_line, $dummy, $dummy, $dummy, $func, $param, $key, $value) = split(/ /, $_);
	($file, $line) = split(/:/, $file_and_line);

	if ($func eq "'(struct") {
	    ($file_and_line, $dummy, $dummy, $dummy, $dummy, $func, $param, $key, $value) = split(/ /, $_);
	    ($file, $line) = split(/:/, $file_and_line);
	    $func = "$dummy $func";
	}
    } elsif ($_ =~ /info: passes user_data /) {
	# test.c +24 func(11) info: passes user_data 'frob' 2 '$$->data'
	$type = 3;
	$value = 1;
	($file_and_line, $dummy, $dummy, $dummy, $dummy, $func, $param, $key) = split(/ /, $_);
	($file, $line) = split(/:/, $file_and_line);

	if ($func eq "'(struct") {
	    ($file_and_line, $dummy, $dummy, $dummy, $dummy, $dummy, $func, $param, $key) = split(/ /, $_);
            ($file, $line) = split(/:/, $file_and_line);
            $func = "$dummy $func";
	}

    } elsif ($_ =~ /info: passes capped_data /) {
	# test.c +24 func(11) info: passes capped_data 'frob' 2 '$$->data'
	$type = 4;
	$value = 1;
	($file_and_line, $dummy, $dummy, $dummy, $dummy, $func, $param, $key) = split(/ /, $_);
	($file, $line) = split(/:/, $file_and_line);

	if ($func eq "'(struct") {
	    ($file_and_line, $dummy, $dummy, $dummy, $dummy, $dummy, $func, $param, $key) = split(/ /, $_);
	    ($file, $line) = split(/:/, $file_and_line);
            $func = "$dummy $func";
	}

    } else {
	next;
    }

    if (!defined($value) || !($param =~ /^-*\d+$/)) {
	next;
    }

    $func =~ s/'//g;
    $key =~ s/'//g;
    $value =~ s/'//g;

    if (defined($too_common_funcs{$func})) {
        next;
    }

    if ($prev_fn ne $func || $prev_line ne $line) {
	$prev_fn = $func;
	$prev_line = $line;
	$prev_param = $param;
	$func_id++;
    }

#    print "insert into caller_info values ('$file', '$func', $func_id, $type, $param, '$key', '$value')\n";
    $db->do("insert into caller_info values ('$file', '$func', $func_id, $type, $param, '$key', '$value')");
}
$db->commit();
$db->disconnect();
