#!/bin/bash

# mark some paramaters as coming from user space
cat << EOF | sqlite3 smatch_db.sqlite

delete from return_states where function = 'strlen';
delete from return_states where function = 'strnlen';
delete from return_states where function = 'sprintf';
delete from return_states where function = 'snprintf';

EOF

