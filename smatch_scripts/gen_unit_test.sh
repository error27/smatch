#!/bin/bash
#
# Extract a kernel function and the functions which it calls into a
# directory, with a main() which calls it with zeroed data, and build
# them into one program.  Functions which are not extracted are stubbed.
#
# Run it from the top of a kernel tree which has been built, because the
# saved compile commands (.*.o.cmd) are used to run extract, and which
# has a Smatch cross function database.

set -e

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
extract=${EXTRACT:-$script_dir/../extract}
db=smatch_db.sqlite
out_dir=test
depth=2
stop_list=$script_dir/gen_unit_test/kernel.stop_list

usage() {
	echo "usage: $0 [-d db] [-o out_dir] [-D depth] [-s stop_list] function [file.c]" >&2
	echo "  -d  the cross function database (default: $db)" >&2
	echo "  -o  the output directory, which must not exist (default: $out_dir)" >&2
	echo "  -D  how many calls into other files to follow (default: $depth)" >&2
	echo "  -s  functions and files not to extract (default: $stop_list)" >&2
	echo "  file.c is needed if several files define the function" >&2
	exit 1
}

die() {
	echo "$0: $*" >&2
	exit 1
}

while getopts "d:o:D:s:" opt; do
	case $opt in
	d) db=$OPTARG ;;
	o) out_dir=$OPTARG ;;
	D) depth=$OPTARG ;;
	s) stop_list=$OPTARG ;;
	*) usage ;;
	esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || [ $# -eq 2 ] || usage
func=$1
root_file=${2#./}

[ -e "$db" ] || die "cannot find the database $db"
[ -x "$extract" ] || die "cannot find extract at $extract"
[ -e "$stop_list" ] || die "cannot find the stop list $stop_list"
[[ "$depth" =~ ^[0-9]+$ ]] || die "the depth must be a number"
[ ! -e "$out_dir" ] || die "$out_dir already exists"

# Scanning the database is slow, so keep the calls and the definitions
# next to it and only rebuild them when the database changes.
calls=$db.calls
defs=$db.defs
if [ ! -s "$calls" ] || [ "$db" -nt "$calls" ]; then
	echo "gen_unit_test: reading the calls from $db"
	sqlite3 -separator ' ' "$db" "
		SELECT DISTINCT h.value, c.caller, c.function, c.static
		FROM caller_info c JOIN hash_string h ON c.file = h.hash
		WHERE c.type = 0 AND c.parameter = -1;" > "$calls.tmp"
	mv "$calls.tmp" "$calls"
fi
if [ ! -s "$defs" ] || [ "$db" -nt "$defs" ]; then
	echo "gen_unit_test: reading the function definitions from $db"
	sqlite3 -separator ' ' "$db" "
		SELECT DISTINCT h.value, r.function, r.static
		FROM return_states r JOIN hash_string h ON r.file = h.hash;" > "$defs.tmp"
	mv "$defs.tmp" "$defs"
fi

if [ -z "$root_file" ]; then
	root_file=$(awk -v f="$func" '$2 == f { print $1 }' "$defs" | sort -u)
	[ -n "$root_file" ] || die "$func is not in the database"
	count=$(echo "$root_file" | wc -l)
	[ "$count" -eq 1 ] ||
		die "$func is defined in $count files, so give one of them:" \
		    $(echo "$root_file" | head -n 10)
fi

mkdir -p "$out_dir"

# Walk the calls from the root function.  extract prints the functions
# which a file needs anyway, so only calls into other files count towards
# the depth.  The output is "file function"
# with the root function first.
awk -v root_file="$root_file" -v root_func="$func" -v max_depth="$depth" \
    -v stop_list="$stop_list" -v defs="$defs" '
function stopped(file, fn,    i) {
	for (i = 1; i <= nstop_file; i++)
		if (file ~ stop_file[i])
			return 1;
	for (i = 1; i <= nstop_func; i++)
		if (fn ~ stop_func[i])
			return 1;
	return 0;
}
FILENAME == stop_list {
	if ($1 == "file")
		stop_file[++nstop_file] = $2;
	else if ($1 == "func")
		stop_func[++nstop_func] = $2;
	next;
}
FILENAME == defs {
	if ($3 == 0) {
		if (($2 in global) && global[$2] != $1)
			dup[$2] = 1;
		global[$2] = $1;
	}
	next;
}
$3 ~ /^[A-Za-z_][A-Za-z0-9_]*$/ {
	callees[$1 SUBSEP $2] = callees[$1 SUBSEP $2] " " $3 ":" $4;
}
END {
	head = tail = 0;
	key = root_file SUBSEP root_func;
	seen[key] = 1;
	qfile[tail] = root_file; qfunc[tail] = root_func; qdepth[tail++] = 0;
	while (head < tail) {
		file = qfile[head]; fn = qfunc[head]; d = qdepth[head++];
		print file, fn;
		n = split(callees[file SUBSEP fn], list, " ");
		for (i = 1; i <= n; i++) {
			split(list[i], parts, ":");
			callee = parts[1];
			if (parts[2] == 1) {
				callee_file = file;
				callee_depth = d;
			} else {
				if (!(callee in global) || callee in dup)
					continue;
				callee_file = global[callee];
				callee_depth = d + (callee_file != file);
			}
			if (callee_depth > max_depth || stopped(callee_file, callee))
				continue;
			key = callee_file SUBSEP callee;
			if (key in seen)
				continue;
			seen[key] = 1;
			qfile[tail] = callee_file; qfunc[tail] = callee;
			qdepth[tail++] = callee_depth;
		}
	}
}' "$stop_list" "$defs" "$calls" > "$out_dir/functions"

# The compiler flags for a file, without the compiler, its output files and
# anything after the first command.
compile_flags() {
	local cmd_file line skip=0

	cmd_file=$(dirname "$1")/.$(basename "${1%.c}").o.cmd
	[ -e "$cmd_file" ] || return 1
	line=$(grep -m1 "^savedcmd_" "$cmd_file" | sed 's/^savedcmd_[^ ]* := //; s/;.*//')
	printf '%s\n' "$line" | xargs printf '%s\n' | tail -n +2 |
	while IFS= read -r arg; do
		if [ $skip -eq 1 ]; then
			skip=0
			continue
		fi
		case "$arg" in
		-o) skip=1 ;;
		-o*|-Wp,-M*|-c) ;;
		*) printf '%s\n' "$arg" ;;
		esac
	done
}

log=$out_dir/extract.log
: > "$log"
for file in $(awk '!seen[$1]++ { print $1 }' "$out_dir/functions"); do
	if ! mapfile -t flags < <(compile_flags "$file") || [ ${#flags[@]} -eq 0 ]; then
		echo "gen_unit_test: $file was not built, so it is stubbed"
		continue
	fi
	names=$(awk -v f="$file" '$1 == f { print $2 }' "$out_dir/functions" | paste -sd,)
	main=
	[ "$file" = "$root_file" ] && main=--main
	# extract stops at a function which it cannot find, so drop it and retry.
	while :; do
		if "$extract" $main --func="$names" --out-dir="$out_dir" \
		    "${flags[@]}" >> "$log" 2>&1; then
			break
		fi
		missing=$(tail -n 1 "$log" | sed -n 's/^function \(.*\) was not found$/\1/p')
		rm -f "$out_dir/$file"
		if [ -z "$missing" ] || [ "$missing" = "$func" ] ||
		   ! [[ ",$names," == *",$missing,"* ]]; then
			[ "$file" != "$root_file" ] || die "extracting $file failed, see $log"
			echo "gen_unit_test: extracting $file failed, so it is stubbed"
			break
		fi
		names=$(echo ",$names," | sed "s/,$missing,/,/; s/^,//; s/,$//")
		[ -n "$names" ] || break
	done
done

cp "$script_dir/gen_unit_test/Makefile" "$script_dir/gen_unit_test/gen_stubs.sh" "$out_dir/"
echo "gen_unit_test: extracted $(wc -l < "$out_dir/functions") functions from" \
     "$(awk '!seen[$1]++' "$out_dir/functions" | wc -l) files into $out_dir"
make -C "$out_dir"
