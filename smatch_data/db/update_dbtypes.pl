#!/usr/bin/perl

use strict;
use File::Temp qw/ tempfile tempdir /;
use File::Copy 'move';

sub print_types($)
{
    my $fh = shift;

    open(TYPES, "smatch_dbtypes.h") or die("Can't open smatch_dbtypes.h\n");
    while (<TYPES>) {
        if ($_ =~ /\t(\S+)\W*=\W*(\d+),.*/) {
            print $fh "    $2: \"$1\",\n";
        }
    }
    close(TYPES);
}

my ($fh, $file) = tempfile();
open(SMDB, "<smatch_data/db/smdb.py") or die ("Can't open smdb.py\n");
my $skip = 0;
while (<SMDB>) {
    if ($_ =~ /#DB TYPES START/) {
        print $fh $_;
        print_types($fh);
        $skip = 1;
        next;
    }
    if ($_ =~ /#DB TYPES END/) {
        $skip = 0;
    }
    if ($skip == 1) {
        next;
    }

    print $fh $_;
}
close(SMDB);
close($fh);

move("smatch_data/db/smdb.py", "smatch_data/db/smdb.py.bak");
move($file, "smatch_data/db/smdb.py");
chmod(0755, "smatch_data/db/smdb.py");

