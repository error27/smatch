#!/bin/bash

NR_CPU=$(cat /proc/cpuinfo | grep ^processor | wc -l)

function usage {
    echo
    echo "Usage:  $0 [smatch options]"
    echo "Compiles the kernel with -j${NR_CPU}"
    echo
    exit 1
}

if [ "$1" = "-h" ] || [ "$1" = "--help" ] ; then
	usage;
fi

SCRIPT_DIR=$(dirname $0)
if [ -e $SCRIPT_DIR/../smatch ] ; then
    CMD=$SCRIPT_DIR/../smatch
elif which smatch | grep smatch > /dev/null ; then
    CMD=smatch
else
    echo "Smatch binary not found."
    exit 1
fi

make clean
find -name \*.c.smatch -exec rm \{\} \;
make -j${NR_CPU} -k CHECK="$CMD -p=kernel --file-output $*" \
	C=1 bzImage modules 2>&1 | tee compile.warns
find -name \*.c.smatch -exec cat \{\} \; > warns.txt

