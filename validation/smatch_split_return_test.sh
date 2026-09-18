#!/bin/bash

set -e

test_file=$1
validation_dir=$(cd "$(dirname "$0")" && pwd)
smatch_dir=$(cd "$validation_dir/.." && pwd)
db_source="$validation_dir/${test_file%.c}.db.c"
test_source="$validation_dir/$test_file"
tmp_dir=$(mktemp -d)

cleanup()
{
	rm -rf "$tmp_dir"
}
trap cleanup EXIT

cp "$db_source" "$tmp_dir/$test_file"
cd "$tmp_dir"
"$smatch_dir/smatch" --info -p=kernel --arch=arm64 -m64 \
	"$test_file" > warns.txt
"$smatch_dir/smatch_data/db/create_db.sh" -p=kernel warns.txt \
	>/dev/null 2>&1

sqlite3 smatch_db.sqlite <<'EOF'
select 'return: ' || distinct_return
from (
	select distinct return as distinct_return
	from return_states
	where function = 'array_index_mask_nospec'
)
order by distinct_return;

select 'entry: ' || return || '|' || type || '|' || parameter || '|' ||
       key || '|' || value
from return_states
where function = 'array_index_mask_nospec' and type = 1028
order by return;
EOF

cp "$test_source" "$tmp_dir/$test_file"
"$smatch_dir/smatch" --db-file="$tmp_dir/smatch_db.sqlite" \
	-p=kernel --arch=arm64 -m64 -I"$smatch_dir" "$test_file"
