#!/bin/bash

echo "delete from caller_info where type = 1003; delete from return_states where type = 1003;" | sqlite3 smatch_db.sqlite


