#!/usr/bin/perl

# This script is supposed to help use the param_mapper output.
# Give it a function and parameter and it lists the functions
# and parameters which are basically equivelent.
#
# The problem is this script is DESPERATELY SLOW.  It needs 
# a complete rewrite.

use strict;

sub usage()
{
    print ("trace_params.pl <smatch output file> <function> <parameter>\n");
    exit(1);
}

my %found;
my %looked_for;

sub find_func($$)
{
    my $file = shift;
    my $fp = shift;
    my $func;
    my $param;

    $looked_for{$fp} = 1;

    $func = (split(/%/, $fp))[0];
    $param = (split(/%/, $fp))[1];

    open(FILE, "<$file");
    while (<FILE>) {
	if (/.*? \+\d+ (.*?)\(\d+\) info: param_mapper (\d+) => $func $param/) {
	    $found{"$1%$2"} = 1;
	}
    }
}

sub find_all($$$)
{
    my $file = shift;
    my $func = shift;
    my $param = shift;

    $found{"$func%$param"} = 1;
    while (1) {
	my $new = "";
	foreach $func (keys %found){
	    if (defined($looked_for{$func})) {
		next;
	    } else {
		$new = $func;
		last;
	    }
	}
	if ($new =~ /^$/) {
	    last;
	}
	find_func($file, $new);
    }
}

sub print_found()
{
    foreach my $fp (keys %found){
	my $func = (split(/%/, $fp))[0];
	my $param = (split(/%/, $fp))[1];

	print("\t{\"$func\", $param},\n"); 
    }
}

my $file = shift();
my $func = shift();
my $param = shift();

if (!$file or !$func or !defined($param)) {
    usage();
}

if (! -e $file) {
    printf("Error:  $file does not exist.\n");
    exit(1);
}

find_all($file, $func, $param);
print_found();
