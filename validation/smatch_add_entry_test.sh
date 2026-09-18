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

"$smatch_dir/smatch_data/db/add_entry.py" choose 1 103 0 '$' 1-10
"$smatch_dir/smatch_data/db/add_entry.py" choose 1 103 0 '$' 1-10

sqlite3 smatch_db.sqlite <<'EOF'
select return, type, parameter, key, value, count(*)
from return_states
where function = 'choose' and return = '1' and type = 103
and value = '1-10'
group by return, type, parameter, key, value;
EOF
