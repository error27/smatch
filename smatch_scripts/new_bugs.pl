#!/usr/bin/perl

use strict;

my $store = 0;
my $unstore = 0;
my $warns_file = shift;
if ($warns_file =~ /--store/) {
    $store = 1;
    $warns_file = shift;
}
if ($warns_file =~ /--unstore/) {
    $unstore = 1;
    $warns_file = shift;
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
        unless(-e "old_warnings/" or mkdir "old_warnings/") {
            die "Unable to create old_warnings/";
        }
        open(TMP, '>', "old_warnings/$file.$msg") or
            die "Error opening: old_warnings/$file.$msg\n";
        close(TMP);
        next;
    }

    if ($unstore) {
        unlink("old_warnings/$file.$msg") and
            print "removed: old_warnings/$file.$msg\n";
        next;
    }

    unless (-e "old_warnings/$file.$msg") {
        print "$orig";
    }
}

close(WARNS);
