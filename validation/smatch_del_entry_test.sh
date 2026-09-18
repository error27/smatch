#!/bin/bash

set -e

test_file=$1
validation_dir=$(cd "$(dirname "$0")" && pwd)
smatch_dir=$(cd "$validation_dir/.." && pwd)
test_source="$validation_dir/$test_file"
tmp_dir=$(mktemp -d)

cleanup()
{
	rm -rf "$tmp_dir"
}
trap cleanup EXIT

cp "$test_source" "$tmp_dir/$test_file"
cd "$tmp_dir"
"$smatch_dir/smatch" --info "$test_file" > warns.txt
"$smatch_dir/smatch_data/db/create_db.sh" warns.txt >/dev/null 2>&1

add_entry="$smatch_dir/smatch_data/db/add_entry.py"
del_entry="$smatch_dir/smatch_data/db/del_entry.py"

"$add_entry" choose 0 1013 0 '$' one
"$add_entry" choose 1 1013 0 '$' one
"$add_entry" choose 0 1013 0 '$->member' member
"$add_entry" choose 1 1013 0 '$->member' member
"$add_entry" choose 1 103 0 '$' keep
"$add_entry" choose 1 1014 0 '$' source

"$del_entry" choose 1 FREED '$' one
"$del_entry" choose 1013 '$->member'
"$del_entry" choose DATA_SOURCE

sqlite3 smatch_db.sqlite <<'EOF'
select return, type, parameter, key, value
from return_states
where function = 'choose' and type in (103, 1013, 1014)
and value in ('one', 'member', 'keep', 'source')
order by return, type, key, value;
EOF
