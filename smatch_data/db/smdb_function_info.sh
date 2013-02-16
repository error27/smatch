#!/bin/bash

usage()
{
    echo "Usage $0 <function>"
    exit 1
}

get_function_pointers()
{
    local func=$1

    OLD_IFS=$IFS
    IFS="
"
    ptrs=$(echo "select ptr from function_ptr where function = '$func';" | sqlite3 smatch_db.sqlite)
    for ptr in $ptrs ; do
        echo "select * from caller_info where function = '$ptr';" | sqlite3 smatch_db.sqlite
    done
    IFS=$OLD_IFS
}

func=$1

if [ "$func" = "" ] ; then
    usage
fi

echo "select * from caller_info where function = '$func';" | sqlite3 smatch_db.sqlite
get_function_pointers $func
