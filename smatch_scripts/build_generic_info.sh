#!/bin/bash

# This is a generic script to parse --info output.  For the kernel, don't use
# this script, use build_kernel_data.sh instead.

SCRIPT_DIR=$(dirname $0)
DATA_DIR=smatch_data
PROJECT=smatch_generic

function usage {
    echo
    echo "Usage:  $0"
    echo "Updates the smatch_data/ directory and builds the smatch database"
    echo
    exit 1
}

if [ "$1" = "-h" ] || [ "$1" = "--help" ] ; then
	usage;
fi

if [ -e $SCRIPT_DIR/../smatch ] ; then
    BIN_DIR=$SCRIPT_DIR/../
else
    echo "This script should be located in the smatch_scripts/ subdirectory of the smatch source."
    exit 1
fi

# If someone is building the database for the first time then make sure all the
# required packages are installed
if [ ! -e smatch_db.sqlite ] ; then
    [ -e smatch_warns.txt ] || touch smatch_warns.txt
    if ! $SCRIPT_DIR/../smatch_data/db/create_db.sh -p=kernel smatch_warns.txt ; then
        echo "Hm... Not working.  Make sure you have all the sqlite3 packages"
        echo "And the sqlite3 libraries for Perl and Python"
        exit 1
    fi
fi

make CHECK="$BIN_DIR/smatch --call-tree --info --param-mapper --spammy --file-output" $*

find -name \*.c.smatch -exec cat \{\} \; -exec rm \{\} \; > smatch_warns.txt

for i in $SCRIPT_DIR/gen_* ; do
        $i smatch_warns.txt -p=${PROJECT}
done

mkdir -p $DATA_DIR
mv $PROJECT.* $DATA_DIR

$SCRIPT_DIR/../smatch_data/db/create_db.sh smatch_warns.txt

