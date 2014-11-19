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
db_file=smatch_db.sqlite.new

rm -f $db_file

for i in ${bin_dir}/*.schema ; do
    cat $i | sqlite3 $db_file
done

for i in ${bin_dir}/fill_* ; do
    $i "$PROJ" $info_file $db_file
done

${bin_dir}/fixup_all.sh $db_file
if [ "$PROJ" != "" ] ; then
    ${bin_dir}/fixup_${PROJ}.sh $db_file
fi

for i in ${bin_dir}/*.idx ; do
    cat $i | sqlite3 $db_file
done

${bin_dir}/remove_mixed_up_pointer_params.pl $db_file
${bin_dir}/mark_function_ptrs_searchable.pl $db_file

mv $db_file smatch_db.sqlite
