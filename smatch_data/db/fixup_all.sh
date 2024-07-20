#!/bin/bash

set -e

db_file=$1
cat << EOF | sqlite3 $db_file

EOF

