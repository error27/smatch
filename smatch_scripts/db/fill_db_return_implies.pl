#!/usr/bin/perl

use strict;
use DBI;
use bignum;

my $warns = shift;

if (!defined($warns)) {
    print "usage:  $0 <warns.txt>\n";
    exit(1);
}

my $db = DBI->connect("dbi:SQLite:smatch_db.sqlite", "", "", {RaiseError => 1, AutoCommit => 0});
$db->do("PRAGMA synchronous = OFF");
$db->do("PRAGMA cache_size = 800000");
$db->do("PRAGMA journal_mode = OFF");

$db->do("delete from return_implies");

my $type = 7;  # RANGE_CAP

sub get_error_num($)
{
    my $str = shift;

    $str =~ s/min/-4095/;
    if ($str =~ /^\((.*)\)/) {
        $str = $1;
    }
    return $str;
}

sub get_num($)
{
    my $str = shift;

    if ($str =~ /^\((.*)\)/) {
        $str = $1;
    }
    return $str;
}

sub show_num($)
{
    my $str = shift;

    if ($str =~ /^-/) {
        $str = "($str)";
    }
    return $str;
}

sub show_range($$)
{
    my $min = shift;
    my $max = shift;

    if ($min =~ /^$max$/) {
        return "$min";
    }
    return "$min-$max";
}

sub get_error_returns($)
{
    my $total_ranges = shift;
    my @error_ranges;
    my $ret;

    foreach my $range (split(",", $total_ranges)) {
        my ($range_1, $range_2);
        my ($min, $max);

        if ($range =~ /(.*[^(])-(.*)/) {
            $range_1 = $1;
            $range_2 = $2;
        } else {
            $range_1 = $range;
            $range_2 = $range;
        }

        $min = get_error_num($range_1);
        $max = get_error_num($range_2);

        if ($max + 0 < 0) {
            push @error_ranges, $range;
        } elsif ($min + 0 < 0) {
            push @error_ranges, show_num($min) . "-(-1)";
        }
    }

    foreach my $range (@error_ranges) {
        if ($ret) {
            $ret = $ret . ",";
        }
        $ret = $ret . $range;
    }
    return $ret;
}

sub get_success_returns($)
{
    my $total_ranges = shift;
    my @success_ranges;
    my $ret;

    foreach my $range (split(",", $total_ranges)) {
        my ($range_1, $range_2);
        my ($min, $max);

        if ($range =~ /(.*[^(])-(.*)/) {
            $range_1 = $1;
            $range_2 = $2;
        } else {
            $range_1 = $range;
            $range_2 = $range;
        }

        $min = get_error_num($range_1);
        $max = get_error_num($range_2);

        if ($min + 0 >= 0) {
            push @success_ranges, $range;
        } elsif ($max + 0 >= 0 || $max =~ /max/) {
            push @success_ranges, show_range(0, $range_2);
        }
    }

    foreach my $range (@success_ranges) {
        if ($ret) {
            $ret = $ret . ",";
        }
        $ret = $ret . $range;
    }
    return $ret;
}

sub invert_range($)
{
    my $orig = shift;
    my $old_max;
    my $ret;

    foreach my $range (split(",", $orig)) {
        my ($range_1, $range_2);
        my ($min, $max);

        if ($range =~ /(.*[^(])-(.*)/) {
            $range_1 = $1;
            $range_2 = $2;
        } else {
            $range_1 = $range;
            $range_2 = $range;
        }

        $min = get_num($range_1);
        $min = show_num($min - 1);

        $max = get_num($range_2);
        $max = show_num($max + 1);

        if (!defined($old_max)) {
            if (!($range_1 =~ /min/)) {
                $ret = "min-" . $min;
            }
        } else {
            if ($ret) {
                $ret = $ret . ",";
            }

            $ret = $ret . show_range($old_max, $min);
        }
        $old_max = $max;
    }

    if (!($orig =~ /max$/)) {
        if ($ret) {
            $ret = $ret . ",";
        }
        $ret = $ret . "$old_max-max";
    }

    return $ret;
}

my $old_func = "";
my $total_returns = "";

open(FILE, "<$warns");
while (<FILE>) {
    # test.c:26 func() info: function_return_values '(-20),(-12),0' global
    if (/.*?:\d+ (\w+)\(\) info: function_return_values '(.*?)'/) {
        $old_func = $1;
        $total_returns = $2;
    }

    # test.c:14 func() info: param 0 range 'min-(-1),12-max' implies error return static
    if (/(.*?):\d+ (\w+)\(\) info: param (\d+) range '(.*?)' implies error return (global|static)/) {
        my $file = $1;
        my $func = $2;
        my $param = $3;
        my $bad_range = $4;

        my $static = 0;
        if ($5 =~ /static/) {
            $static = 1;
        }

        my $error_returns;
        my $success_returns;
        my $good_range = invert_range($bad_range);

        if (!($func =~ /^$old_func$/)) {
            next;
        }

        $error_returns = get_error_returns($total_returns);
        $success_returns = get_success_returns($total_returns);

        $db->do("insert into return_implies values ('$file', '$func', $static, $type, '$success_returns', $param, '', '$good_range')");
        $db->do("insert into return_implies values ('$file', '$func', $static, $type, '$error_returns', $param, '', 'min-max')");
   }
}
close(FILE);


open(FILE, "<$warns");
while (<FILE>) {
    # crypto/cbc.c:54 is_power_of_2() info: bool_return_implication "1" 0 "min-(-1),1-max static"
    if (/(.*?):\d+ (\w+)\(\) info: bool_return_implication "(.*?)" (\d+) "(.*?)" (global|static)/) {
        my $file = $1;
        my $func = $2;
        my $return_range = $3;
        my $param = $4;
        my $implied_range = $5;
        my $static = 0;
        if ($6 =~ /static/) {
            $static = 1;
        }

        $db->do("insert into return_implies values ('$file', '$func', $static, $type, '$return_range', $param, '', '$implied_range')");
   }
}
close(FILE);


$db->commit();
$db->disconnect();
