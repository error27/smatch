#!/usr/bin/perl

use strict;

my $store = 0;
my $unstore = 0;
my $warns_dir = "old_warnings/";

my $warns_file;

while (my $arg = shift) {
    if ($arg =~ /-d/) {
        $warns_dir = shift;
        next;
    }
    if ($arg =~ /--store/) {
        $store = 1;
        next;
    }
    if ($arg =~ /--unstore/) {
        $unstore = 1;
        next;
    }
    $warns_file = $arg;
    last;
}

my $du = `du $warns_file`;
$du =~ s/([0-9]+).*/$1/;
$du =~ s/\n//;

if (int($du) > 100000) {
    print "$warns_file is too big\n";
    exit(1);
}

open(WARNS, $warns_file);

my ($orig, $file, $line, $msg);
while (<WARNS>) {

    if (!($_ =~ /(error|warn|info)/)) {
        next;
    }

    $orig = $_;
    ($file, $line, $msg) = split(/[: ]/, $_, 3);

    $msg =~ s/^.*?:\d+(|:\d+:) .*? //;
    $msg =~ s/[us](16|32|64)(min|max)//g;
    $msg =~ s/[01234567890]//g;
    if ($msg =~ /can't/) {
        $msg =~ s/(.*can't.*').*?('.*)/$1 $2/;
    } elsif ($msg =~ /don't/) {
        $msg =~ s/(.*don't.*').*?('.*)/$1 $2/;
    } else {
        $msg =~ s/'.*?'/''/g;
    }
    $msg =~ s/,//g;
    $msg =~ s/\(\w+ returns null\)/(... returns null)/;
    $msg =~ s/dma on the stack \(.*?\)/dma on the stack (...)/;
    $msg =~ s/possible ERR_PTR '' to .*/possible ERR_PTR '' to .../;
    $msg =~ s/inconsistent returns ([^ ]+?) locked \(\)/inconsistent returns ... locked ()/;
    $msg =~ s/(.*) [^ ]* (too large for) [^ ]+ (.*)/$1 $2 $3/;
    $msg =~ s/\n//;
    $msg =~ s/ /_/g;
    $msg =~ s/[\(\)]//g;

    $file =~ s/\//./g;
    $msg =~ s/\//./g;

    if ($store) {
        unless(-e "$warns_dir/" or mkdir "$warns_dir/") {
            die "Unable to create $warns_dir";
        }
        open(TMP, '>', "$warns_dir/$file.$msg") or
            die "Error opening: $warns_dir/$file.$msg\n";
        close(TMP);
        next;
    }

    if ($unstore) {
        unlink("$warns_dir/$file.$msg") and
            print "removed: $warns_dir/$file.$msg\n";
        next;
    }

    unless (-e "$warns_dir/$file.$msg") {
        print "$orig";
    }
}

close(WARNS);
