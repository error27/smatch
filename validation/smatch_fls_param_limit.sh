#!/bin/bash

set -e

test_file=$1
validation_dir=$(cd "$(dirname "$0")" && pwd)
smatch_dir=$(cd "$validation_dir/.." && pwd)
db_source="$validation_dir/${test_file%.c}.db.c"
test_source="$validation_dir/$test_file"
tmp_dir=$(mktemp -d)
work_file=slub.i

cleanup()
{
	rm -rf "$tmp_dir"
}
trap cleanup EXIT

cp "$db_source" "$tmp_dir/$work_file"
cd "$tmp_dir"
"$smatch_dir/smatch" --info -p=kernel -I"$smatch_dir" \
	"$work_file" > warns.txt
"$smatch_dir/smatch_data/db/create_db.sh" -p=kernel warns.txt \
	>/dev/null 2>&1

cp "$test_source" "$tmp_dir/$work_file"
"$smatch_dir/smatch" --db-file="$tmp_dir/smatch_db.sqlite" \
	-p=kernel -I"$smatch_dir" "$work_file"
