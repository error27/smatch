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

$db->do("delete from return_values;");

my $type;
my $param;
my ($file_and_line, $file, $dummy, $func, $return_value, $gs);
my $static;

open(WARNS, "<$warns");
while (<WARNS>) {

    if ($_ =~ / info: function_return_values /) {
        # arch/x86/mm/mmap.c:354 mmap_is_ia32() info: function_return_values '0-1' static
        ($file_and_line, $func, $dummy, $dummy, $return_value, $gs) = split(/ /, $_);
    } else {
        next;
    }

    ($file, $dummy) = split(/:/, $file_and_line);

    $func =~ s/\(\)//;
    $return_value =~ s/'//g;

    $static = 0;
    if ($gs =~ /static/) {
        $static = 1;
    }

    $db->do("insert into return_values values ('$file', '$func', $static, '$return_value')\n");
}

$db->commit();
$db->disconnect();
