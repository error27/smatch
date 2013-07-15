#!/bin/bash

# mark some paramaters as coming from user space
cat << EOF | sqlite3 smatch_db.sqlite

update return_states set return = '0-s32max[<=p1]' where function = 'strnlen';

EOF

