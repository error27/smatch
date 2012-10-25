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

    open(FUNCS, "cat $warns | grep 'info: call_marker ' | cut -d \"'\" -f 2 | sort | uniq -c | ");

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

    my ($file_and_line, $file, $line, $caller, $dummy, $func, $param, $key, $value, $gs);

    if ($_ =~ /info: call_marker /) {
        # crypto/zlib.c:50 zlib_comp_exit() info: call_marker 'zlib_deflateEnd' global
        $type = 0;  # INTERNAL
        ($file_and_line, $caller, $dummy, $dummy, $func, $gs) = split(/ /, $_);
        ($file, $line) = split(/:/, $file_and_line);
        $param = -1;
        $key = "";
        $value = "";

        if ($func eq "'(struct") {
            ($file_and_line, $dummy, $dummy, $dummy, $dummy, $func, $gs) = split(/ /, $_);
            ($file, $line) = split(/:/, $file_and_line);
            $func = "$dummy $func";
        }

    } elsif ($_ =~ /info: passes param_value /) {
        # init/main.c +165 obsolete_checksetup(7) info: passes param_value strlen 0 min-max static
        $type = 1;  # PARAM_VALUE
        ($file_and_line, $caller, $dummy, $dummy, $dummy, $func, $param, $key, $value, $gs) = split(/ /, $_);
        ($file, $line) = split(/:/, $file_and_line);

        if ($func eq "'(struct") {
            ($file_and_line, $dummy, $dummy, $dummy, $dummy, $dummy, $func, $param, $key, $value, $gs) = split(/ /, $_);
            ($file, $line) = split(/:/, $file_and_line);
            $func = "$dummy $func";
        }

    } elsif ($_ =~ /info: passes absolute_limits /) {
        # init/main.c +165 obsolete_checksetup(7) info: passes param_value strlen 0 min-max static
        $type = 10;  # ABSOLUTE_LIMITS
        ($file_and_line, $caller, $dummy, $dummy, $dummy, $func, $param, $key, $value, $gs) = split(/ /, $_);
        ($file, $line) = split(/:/, $file_and_line);

        if ($func eq "'(struct") {
            ($file_and_line, $dummy, $dummy, $dummy, $dummy, $dummy, $func, $param, $key, $value, $gs) = split(/ /, $_);
            ($file, $line) = split(/:/, $file_and_line);
            $func = "$dummy $func";
        }

    } elsif ($_ =~ /info: passes_buffer /) {
        # init/main.c +175 obsolete_checksetup(17) info: passes_buffer 'printk' 0 '$$' 38 global
        $type = 2;  # BUF_SIZE
        ($file_and_line, $caller, $dummy, $dummy, $func, $param, $key, $value, $gs) = split(/ /, $_);
        ($file, $line) = split(/:/, $file_and_line);

        if ($func eq "'(struct") {
            ($file_and_line, $caller, $dummy, $dummy, $dummy, $func, $param, $key, $value, $gs) = split(/ /, $_);
            ($file, $line) = split(/:/, $file_and_line);
            $func = "$dummy $func";
        }
    } elsif ($_ =~ /info: passes user_data /) {
        # test.c +24 func(11) info: passes user_data 'frob' 2 '$$->data' global
        $type = 3;  # USER_DATA
        $value = 1;
        ($file_and_line, $caller, $dummy, $dummy, $dummy, $func, $param, $key, $gs) = split(/ /, $_);
        ($file, $line) = split(/:/, $file_and_line);

        if ($func eq "'(struct") {
            ($file_and_line, $caller, $dummy, $dummy, $dummy, $dummy, $func, $param, $key, $gs) = split(/ /, $_);
            ($file, $line) = split(/:/, $file_and_line);
            $func = "$dummy $func";
        }

    } elsif ($_ =~ /info: passes capped_data /) {
        # test.c +24 func(11) info: passes capped_data 'frob' 2 '$$->data' static
        $type = 4;  # CAPPED_DATA
        $value = 1;
        ($file_and_line, $caller, $dummy, $dummy, $dummy, $func, $param, $key, $gs) = split(/ /, $_);
        ($file, $line) = split(/:/, $file_and_line);

        if ($func eq "'(struct") {
            ($file_and_line, $caller, $dummy, $dummy, $dummy, $dummy, $func, $param, $key, $gs) = split(/ /, $_);
            ($file, $line) = split(/:/, $file_and_line);
            $func = "$dummy $func";
        }

    } else {
        next;
    }

    if (!looks_like_number($param)) {
        next;
    }

    $caller =~ s/\(\)//;
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

    my $static = 0;
    if ($gs =~ /static/) {
        $static = 1;
    }

#    print "insert into caller_info values ('$file', '$caller', '$func', $func_id, $type, $param, '$key', '$value')\n";
    $db->do("insert into caller_info values ('$file', '$caller', '$func', $func_id, $static, $type, $param, '$key', '$value')");
}
$db->commit();
$db->disconnect();
