#!/bin/bash

set -e

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
real_cc=${REAL_CC:-cc}
source_file=
extract_args=()
skip_output_file=0

for arg in "$@"; do
	if [ "$skip_output_file" -eq 1 ]; then
		skip_output_file=0
		continue
	fi

	case "$arg" in
	-o)
		skip_output_file=1
		continue
		;;
	-o*)
		continue
		;;
	*.c)
		source_file=$arg
		;;
	esac
	extract_args+=("$arg")
done

"$real_cc" "$@"

if [ -z "$source_file" ] || [ "$source_file" = "/dev/null" ]; then
	exit 0
fi

"$script_dir/extract" "${extract_args[@]}"

output_file="${source_file}.extract"
gcc -x c -fsyntax-only -fms-extensions "$output_file"
