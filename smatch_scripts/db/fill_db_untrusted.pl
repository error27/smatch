#!/usr/bin/perl -w

use strict;
use DBI;

my $warns = shift;

if (!defined($warns)) {
    print "usage:  $0 <warns.txt>\n";
    exit(1);
}

my $db = DBI->connect("dbi:SQLite:smatch_db.sqlite", "", "", {RaiseError => 1, AutoCommit => 0});

$db->do("delete from untrusted;");

open(WARNS, "~/progs/smatch/devel/smatch_scripts/follow_params.pl $warns \$(grep user_data $warns | cut -d ' ' -f 6- | sort -u) |");
while (<WARNS>) {
    if ($_ =~ /kfree/) {
	next;
    }

    s/\n//;

    my ($file, $dummy, $func, $param);
    ($func, $param) = split(/ /, $_);

    if (!($func =~ /^[\w_]+$/)) {
	next;
    }
    if (!defined($param) || !($param =~ /^\d+$/)) {
	next;
    }

    $db->do("insert into untrusted values ('', '$func', $param)");
}
$db->commit();
$db->disconnect();
