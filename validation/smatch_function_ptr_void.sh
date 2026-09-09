#!/bin/bash

set -e

info_file=$(mktemp)
cleanup()
{
	rm -f "$info_file" smatch_db.sqlite smatch_db.sqlite.new
}
trap cleanup EXIT

rm -f smatch_db.sqlite smatch_db.sqlite.new

for pass in 1 2 3 ; do
	../smatch --info "$@" > "$info_file"
	../smatch_data/db/create_db.sh "$info_file" > /dev/null 2>&1
done

sqlite3 smatch_db.sqlite <<'EOF'
select function || ' -> ' || ptr
  from function_ptr
 where function = '(struct callback)->fn'
   and ptr = 'pass_to_client param 0';
EOF
