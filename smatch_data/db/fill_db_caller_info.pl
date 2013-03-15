#!/usr/bin/perl -w

use strict;
use DBI;
use Scalar::Util qw(looks_like_number);

sub usage()
{
    print "usage:  $0 <warns.txt>\n";
    exit(1);
}

my %too_common_funcs;
sub get_too_common_functions($)
{
    my $warns = shift;

    open(FUNCS, "cat $warns | grep 'SQL_caller_info: ' | grep '%call_marker%' | cut -d \"'\" -f 6 | sort | uniq -c | ");

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

my $db = DBI->connect("dbi:SQLite:smatch_db.sqlite", "", "", {AutoCommit => 0});
$db->do("PRAGMA synchronous = OFF");
$db->do("PRAGMA cache_size = 800000");
$db->do("PRAGMA journal_mode = OFF");

my $call_id = 0;
my ($fn, $dummy, $sql);

open(WARNS, "<$warns");
while (<WARNS>) {
    # test.c:11 frob() SQL_caller_info: insert into caller_info values ('test.c', 'frob', '__smatch_buf_size', %CALL_ID%, 1, 0, -1, '', ');

    if (!($_ =~ /^.*? \w+\(\) SQL_caller_info: /)) {
        next;
    }
    ($dummy, $dummy, $dummy, $dummy, $dummy, $fn, $dummy) = split(/'/);

    if ($fn =~ /__builtin_/) {
        next;
    }
    if ($fn =~ /(printk|memset|memcpy|kfree|printf|dev_err|writel)/) {
        next;
    }

    if (defined($too_common_funcs{$fn})) {
        next;
    }

    ($dummy, $dummy, $sql) = split(/:/, $_, 3);

    $sql =~ s/%CALL_ID%/$call_id/;
    if ($sql =~ /%call_marker%/) {
        $sql =~ s/%call_marker%//; # don't need this taking space in the db.
        $call_id++;
    }

    $db->do($sql);
}
$db->commit();
$db->disconnect();
