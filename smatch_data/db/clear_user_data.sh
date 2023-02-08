#!/bin/bash

dir=$1
if [ "$dir" = "" ] ; then
    echo "$0 all or dir/"
    exit 1
fi

USER_TYPE="(type = 8017 or (type >= 9017 and type <= 9019))"
HOST_TYPE="(type >= 7016 and type <= 7019)"

if [ "$dir" == "all" ] ; then
    echo "delete from caller_info where ${USER_TYPE} or ${HOST_TYPE};" | sqlite3 smatch_db.sqlite
    echo "delete from return_states where ${USER_TYPE} or ${HOST_TYPE};" | sqlite3 smatch_db.sqlite
else
    echo "delete from caller_info where ${USER_TYPE} or ${HOST_TYPE} and file like '$dir%';" | sqlite3 smatch_db.sqlite
    echo "delete from return_states where ${USER_TYPE} or ${HOST_TYPE} or type = 9018) and file like '$dir%';" | sqlite3 smatch_db.sqlite
fi


