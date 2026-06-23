#!/usr/bin/perl -w

use strict;
use warnings;
use bigint;
use DBI;
use Data::Dumper;

use FindBin qw($Bin);
use lib $Bin;
use RangeList qw(str_to_rl add_range show_rl);

my $db_file = shift;
my $db = DBI->connect("dbi:SQLite:$db_file", "", "", {AutoCommit => 0});

$db->do("PRAGMA cache_size = 800000");
$db->do("PRAGMA journal_mode = OFF");
$db->do("PRAGMA count_changes = OFF");
$db->do("PRAGMA temp_store = MEMORY");
$db->do("PRAGMA locking = EXCLUSIVE");

my ($update, $sth, $ret_type, $fn_ptr, $rets, $save, $count);

$sth = $db->prepare('select distinct(ptr) from function_ptr;');
$ret_type = $db->prepare('select value from return_states join function_ptr where return_states.function == function_ptr.function and ptr = ? and type = 0 limit 1;');
$rets = $db->prepare('select distinct(return) from return_states join function_ptr where return_states.function == function_ptr.function and ptr = ? and type = 0;');
$save = $db->prepare('insert into function_ptrs_return values (-1, ?, -1, 0, ?, 0, 0, 0, "", ?);');

$sth->execute();
while ($fn_ptr = $sth->fetchrow_array()) {

    $ret_type->execute($fn_ptr);
    my $type = $ret_type->fetchrow_array();
    if (!$type) {
        # Not all function pointers have an implementation.  We could
        # still find the type, but who cares?  Skip.
        next;
    }
    # skip void functions as well
    if ($type =~ /^void\(\*\)\(/) {
        next;
    }

    my $rl = ();

    $rets->execute($fn_ptr);
    while (my ($ret) = $rets->fetchrow_array()) {
        my $new_rl = str_to_rl($ret);
        foreach my $range (@$new_rl) {
            $rl = add_range($rl, $range->{min}, $range->{max});
        }
    }
    $save->execute($fn_ptr, show_rl($rl), $type);
}

$db->commit();
$db->disconnect();
