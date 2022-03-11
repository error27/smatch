#!/bin/bash

echo "delete from caller_info where type = 8017; delete from return_states where type = 8017 or type = 9017;" | sqlite3 smatch_db.sqlite
echo "delete from caller_info where type = 9018; delete from return_states where type = 9019;" | sqlite3 smatch_db.sqlite



