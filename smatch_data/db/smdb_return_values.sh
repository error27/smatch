#!/bin/bash

usage()
{
    echo "Usage $0 <function>"
    exit 1
}

func=$1

if [ "$func" = "" ] ; then
    usage
fi

echo "select * from return_values where function = '$func';" | sqlite3 smatch_db.sqlite
