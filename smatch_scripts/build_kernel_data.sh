#!/bin/bash

PROJECT=kernel

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

SCRIPT_DIR=$(dirname $0)
if [ -e $SCRIPT_DIR/../smatch ] ; then
    CMD=$SCRIPT_DIR/../smatch
    DATA_DIR=$SCRIPT_DIR/../smatch_data
else
    echo "This script should be run from the smatch_scripts/ directory."
    exit 1
fi

$SCRIPT_DIR/test_kernel.sh --call-tree --info --param-mapper --data=$DATA_DIR

for i in $SCRIPT_DIR/gen_* ; do
	$i warns.txt
done

mv ${PROJECT}.* $DATA_DIR
mv no_return_funcs $DATA_DIR/${PROJECT}.no_return_funcs
$SCRIPT_DIR/db/create_db.sh warns.txt

