#!/bin/bash

INFO=""
VALGRIND=""
GREP="grep"
while true; do
    if [ "$1" == "--info" ] ; then
        INFO="--info"
        shift
        continue
    fi
    if [ "$1" == "--valgrind" ] ; then
        VALGRIND="valgrind"
        shift
        continue
    fi
    if [ "$1" == "--nogrep" ] ; then
        GREP=""
        shift
        continue
    fi
    break
done

FILE=$1
FILE=$(echo ${FILE/.c/})

DIRNAME=$(dirname $0)

if [[ -e $DIRNAME/kernel_build_cfg ]] ; then
    source $DIRNAME/kernel_build_cfg
fi

if [[ ! -v KERNEL_DIR ]]; then
    KERNEL_DIR=/lib/modules/$(uname -r)/build
fi
if [[ ! -e $KERNEL_DIR ]] ; then
    echo "KERNEL_DIR ($KERNEL_DIR) not found."
    exit 1
fi

if pwd | grep -q validation/kernel$ ; then
    MOD_DIR=$(pwd)
else
    MOD_DIR=$(pwd)/kernel
fi

rm -f $MOD_DIR/${FILE}.o

if [ "$INFO" != "" ] ; then
    make V=1 C=2 \
        CHECK="$VALGRIND ../../smatch -p=kernel --db-file=$KERNEL_DIR/smatch_db.sqlite $INFO " \
        -C $KERNEL_DIR M=$MOD_DIR ${FILE}.o
elif [ "$GREP" == "" ]; then
    make V=1 C=2 \
        CHECK="$VALGRIND ../../smatch -p=kernel --db-file=$KERNEL_DIR/smatch_db.sqlite" \
        -C $KERNEL_DIR M=$MOD_DIR ${FILE}.o
else
    make V=1 C=2 \
        CHECK="$VALGRIND ../../smatch -p=kernel --db-file=$KERNEL_DIR/smatch_db.sqlite" \
        -C $KERNEL_DIR M=$MOD_DIR ${FILE}.o | grep ^${FILE}.c
fi
