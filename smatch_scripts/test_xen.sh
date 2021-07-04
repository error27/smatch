#!/bin/bash

NR_CPU=$(cat /proc/cpuinfo | grep ^processor | wc -l)
WLOG="smatch_warns.txt"
LOG="smatch_compile.warns"

function usage
{
    echo
    echo "Usage:  $0 [smatch options]"
    echo "Compiles the xen with -j${NR_CPU}"
    echo " available options:"
    echo "	--endian          : enable endianess check"
    echo "	--log {FILE}      : Output compile log to file, default is: $LOG"
    echo "	--wlog {FILE}     : Output warnigs to file, default is: $WLOG"
    echo "	--help            : Show this usage"
    exit 1
}

while true; do
    if [[ "$1" == "--endian" ]]; then
        ENDIAN="CF=-D__CHECK_ENDIAN__"
        shift
    elif [[ "$1" == "--log" ]]; then
        shift
        LOG="$1"
        shift
    elif [[ "$1" == "--wlog" ]]; then
        shift
        WLOG="$1"
        shift
    elif [[ "$1" == "--help" ]]; then
        usage
    else
        break
    fi
done

SCRIPT_DIR=$(dirname $0)
if [ -e $SCRIPT_DIR/../smatch ]; then
    cp $SCRIPT_DIR/../smatch $SCRIPT_DIR/../bak.smatch
    CMD=$SCRIPT_DIR/../bak.smatch
elif which smatch | grep smatch >/dev/null; then
    CMD=smatch
else
    echo "Smatch binary not found."
    exit 1
fi

if ! command -v one-line-scan &>/dev/null; then
    echo "one-line-scan binary not found."
    exit 1
fi

# start fresh, remote .smatch files
make clean -C xen -j $NR_CPU
find -name \*.c.smatch -exec rm \{\} \;
rm -rf SMATCH

# tell where the data base file resides
touch smatch/smatch_db.sqlite
export SMATCH_DB=$(readlink -e smatch/smatch_db.sqlite)

# set kernel, file-output, and everything else passed to this script to smatch
export SMATCH_EXTRA_ARG="-p=kernel --file-output $*"

# compile xen with one-line-scan
one-line-scan -o SMATCH --use-existing --smatch --no-gotocc --no-analysis \
    ${SMATCH_ONE_LINE_SCAN_ARSG:-} \
    -- make xen -j $NR_CPU -B | tee $LOG

find -name \*.c.smatch -exec cat \{\} \; -exec rm \{\} \; >$WLOG
find -name \*.c.smatch.sql -exec cat \{\} \; -exec rm \{\} \; >$WLOG.sql
find -name \*.c.smatch.caller_info -exec cat \{\} \; -exec rm \{\} \; >$WLOG.caller_info

echo "Done.  The warnings are saved to $WLOG"
