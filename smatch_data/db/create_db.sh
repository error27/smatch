#!/bin/bash

if echo $1 | grep -q '^-p' ; then
    PROJ=$(echo $1 | cut -d = -f 2)
    shift
fi

info_file=$1

if [[ "$info_file" = "" ]] ; then
    echo "Usage:  $0 -p=<project> <file with smatch messages>"
    exit 1
fi

bin_dir=$(dirname $0)
db_file=smatch_db.sqlite

rm -f $db_file

for i in ${bin_dir}/*.schema ; do
    cat $i | sqlite3 $db_file
done

for i in ${bin_dir}/fill_* ; do
    $i "$PROJ" $info_file
done

if [ "$PROJ" != "" ] ; then
    ${bin_dir}/fixup_${PROJ}.sh
fi
