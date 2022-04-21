#!/bin/bash

dir=$1
if [ "$dir" = "" ] ; then
    echo "$0 all or dir/"
    exit 1
fi

if [ "$dir" == "all" ] ; then
    echo "delete from caller_info where (type = 8017 or type = 9018);" | sqlite3 smatch_db.sqlite
    echo "delete from return_states where (type = 8017 or type = 9017 or type = 9018);" | sqlite3 smatch_db.sqlite
else
    echo "delete from caller_info where (type = 8017 or type = 9018) and file like '$dir%';" | sqlite3 smatch_db.sqlite
    echo "delete from return_states where (type = 8017 or type = 9017 or type = 9018) and file like '$dir%';" | sqlite3 smatch_db.sqlite
fi


