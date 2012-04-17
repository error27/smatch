#!/bin/bash

usage()
{
    echo "Usage $0 <function>"
    exit 1
}

PARAM=$2
TYPE=2

get_function_pointers()
{
    local func=$1

    OLD_IFS=$IFS
    IFS="
"
    ptrs=$(echo "select ptr from function_ptr where function = '$func';" | sqlite3 smatch_db.sqlite)
    for ptr in $ptrs ; do
        if [ "$PARAM" = "" ] ; then
            echo "select * from caller_info where function = '$ptr' and type='$TYPE';" | sqlite3 smatch_db.sqlite
        else
            echo "select * from caller_info where function = '$ptr' and type='$TYPE' and parameter='$PARAM';" | sqlite3 smatch_db.sqlite
        fi
    done
    IFS=$OLD_IFS
}

func=$1

if [ "$func" = "" ] ; then
    usage
fi

if [ "$PARAM" = "" ] ; then
    echo "select * from caller_info where function = '$func' and type='$TYPE';" | sqlite3 smatch_db.sqlite
else
    echo "select * from caller_info where function = '$func' and type='$TYPE' and parameter='$PARAM';" | sqlite3 smatch_db.sqlite
fi
get_function_pointers $func
