#!/usr/bin/python3

import os
import shlex
import sqlite3
import sys


PARAM_VALUE = 1001
PTRACKER = 2538
PTRACKER_MERGE = 2539
USER_DATA = 8017

# Retain the path to recursion in --full mode without printing a fake source.
RECURSION_MARKER = object()
SOURCE_MARKER = object()
SOURCE_LINE = object()


def usage():
    print("usage: %s [--full] function parameter_name [option]" % sys.argv[0],
          file=sys.stderr)
    sys.exit(1)


def get_function_pointers(con, function):
    functions = [function]
    searched = {function}

    def add_function_pointers(current):
        rows = con.execute(
            "select distinct ptr from function_ptr where function = ?",
            (current,),
        )
        for row in rows:
            pointer = row[0]
            if pointer not in functions:
                functions.append(pointer)
            if pointer not in searched:
                searched.add(pointer)
                add_function_pointers(pointer)

    add_function_pointers(function)
    return functions


def get_parameter(con, function, name):
    rows = con.execute(
        "select distinct parameter, value from parameter_name "
        "where function = ? order by parameter",
        (function,),
    ).fetchall()
    matches = []
    for parameter, parameter_name in rows:
        if (name == parameter_name or
                name.startswith(parameter_name + "->") or
                name.startswith(parameter_name + ".")):
            matches.append((int(parameter),
                            "$" + name[len(parameter_name):]))
    matches = sorted(set(matches))
    if not matches:
        raise ValueError("no parameter named '%s' for %s()" %
                         (name, function))
    if len(matches) != 1:
        raise ValueError("parameter '%s' has conflicting numbers for %s(): %s" %
                         (name, function,
                          ", ".join(str(param) for param, _key in matches)))
    return matches[0]


def get_parameter_name(con, function, parameter):
    row = con.execute(
        "select value from parameter_name "
        "where function = ? and parameter = ? limit 1",
        (function, parameter),
    ).fetchone()
    if row:
        return row[0]
    return "param%d" % parameter


def filename(con, file_id):
    row = con.execute(
        "select value from hash_string where hash = ? limit 1",
        (file_id,),
    ).fetchone()
    if row:
        return row[0]
    return "0x%x" % file_id


def parse_ptracker(value):
    fields = value.split(",", 3)
    if len(fields) != 4:
        raise ValueError("invalid PTRACKER value: %s" % value)
    return int(fields[0], 0), int(fields[1], 0), fields[2], int(fields[3], 0)


def caller_rows(con, file_id, function, static, parameter, data_type, key="$"):
    rows = []
    seen = set()
    for pointer in get_function_pointers(con, function):
        args = [pointer, parameter, key, data_type]
        where = "function = ? and parameter = ? and key = ? and type = ?"
        if pointer == function and file_id and static:
            where += " and file = ? and static = ?"
            args.extend((file_id, static))
        elif pointer == function and static is not None:
            where += " and static = ?"
            args.append(static)
        query = (
            "select file, caller, function, call_id, line, value "
            "from caller_info where %s "
            "order by file, line, caller, call_id, value" % where
        )
        for row in con.execute(query, args):
            item = tuple(row)
            if item not in seen:
                seen.add(item)
                rows.append(item)
    return rows


def tracker_ids_for_call(con, row, parameter):
    file_id, caller, function, call_id, _line, _value = row
    rows = con.execute(
        "select distinct value from caller_info "
        "where file = ? and caller = ? and function = ? and call_id = ? "
        "and parameter = ? and key = '$' and type = ? order by value",
        (file_id, caller, function, call_id, parameter, PTRACKER),
    )
    tracker_ids = []
    for item in rows:
        try:
            tracker_ids.append(int(item[0], 0))
        except ValueError:
            continue
    return tracker_ids


def deduplicate_candidates(con, candidates, parameter):
    unique = []
    seen = set()

    for row in candidates:
        tracker_ids = tuple(tracker_ids_for_call(con, row, parameter))
        if tracker_ids:
            identity = ("ptracker", tracker_ids)
        else:
            identity = ("call",) + row[:4]
        if identity in seen:
            continue
        seen.add(identity)
        unique.append(row)
    return unique


def user_range_for_call(con, row, parameter, key):
    file_id, caller, function, call_id, _line, _value = row
    rows = con.execute(
        "select distinct value from caller_info "
        "where file = ? and caller = ? and function = ? and call_id = ? "
        "and parameter = ? and key = ? and type = ? order by value",
        (file_id, caller, function, call_id, parameter, key, USER_DATA),
    ).fetchall()
    if not rows:
        return None
    return ",".join(item[0] for item in rows)


def format_call(con, row, parameter, key, indent, full=False):
    file_id, caller, function, _call_id, line, value = row
    name = key.replace("$", get_parameter_name(con, function, parameter), 1)
    if value is None:
        value = user_range_for_call(con, row, parameter, key)
    if value is None:
        if not full:
            return None
        value = "unknown"
    return "%s%s:%d %s() %s=%s" % (
        " " * indent, filename(con, file_id), line, caller, name, value)


def source_expression(value):
    fields = value.split(") ", 1)
    if len(fields) == 2:
        return fields[1]
    return value


def attach_sources(call, branch):
    output = []
    remaining = []

    for item in branch:
        if isinstance(item, tuple) and item[0] is SOURCE_MARKER:
            output.append((SOURCE_LINE, call, source_expression(item[1])))
        else:
            remaining.append(item)
    if remaining:
        output.append(call)
        output.extend(remaining)
    return output


def trace_tracker(con, tracker_id, key, indent, path, full=False):
    if tracker_id in path:
        if full:
            return [RECURSION_MARKER]
        return []

    rows = con.execute(
        "select type, value from ptracker where id = ? order by type, value",
        (tracker_id,),
    ).fetchall()
    if not rows:
        if full:
            return ["%s[ptracker %d not found]" %
                    (" " * indent, tracker_id)]
        return []

    output = []
    next_path = path | {tracker_id}
    for tracker_type, value in rows:
        if tracker_type == PARAM_VALUE:
            output.append((SOURCE_MARKER, value))
        elif tracker_type == PTRACKER_MERGE:
            try:
                merged_id = int(value, 0)
            except ValueError:
                if full:
                    output.append("%s[invalid merged ptracker ID: %s]" %
                                  (" " * indent, value))
                continue
            output.extend(trace_tracker(con, merged_id, key, indent,
                                        next_path, full))
        elif tracker_type == PTRACKER:
            try:
                parameter, file_id, function, static = parse_ptracker(value)
            except ValueError:
                if full:
                    output.append("%s[invalid PTRACKER value: %s]" %
                                  (" " * indent, value))
                continue
            callers = caller_rows(con, file_id, function, static, parameter,
                                  PTRACKER)
            if not callers and full:
                name = key.replace(
                    "$", get_parameter_name(con, function, parameter), 1)
                output.append("%s%s() %s [no earlier caller]" %
                              (" " * indent, function, name))
            for caller in callers:
                caller = caller[:-1] + (None,)
                call = format_call(con, caller, parameter, key, indent, full)
                if call is None:
                    continue
                branch = []
                for next_id in tracker_ids_for_call(con, caller, parameter):
                    branch.extend(trace_tracker(con, next_id, key, indent + 2,
                                                next_path, full))
                if not branch and full:
                    branch.append("%s[no ptracker information]" %
                                  (" " * (indent + 2)))
                if branch:
                    output.extend(attach_sources(call, branch))
    return output


def print_options(candidates, con, function, parameter_name, full):
    print("Callers passing USER_DATA to %s(%s):\n" %
          (function, parameter_name))
    for number, row in enumerate(candidates, 1):
        file_id, caller, _function, _call_id, _line, value = row
        print("[%d] file: %s, caller: %s(), range: %s" %
              (number, filename(con, file_id), caller, value))
    command_args = [sys.argv[0]]
    if full:
        command_args.append("--full")
    command_args.extend((function, parameter_name))
    command = shlex.join(command_args)
    print("\nTrace one option by running:")
    print("  %s <option>" % command)


def select_candidate(candidates, option):
    try:
        number = int(option, 10)
    except ValueError:
        raise ValueError("invalid option: %s" % option)
    if number < 1 or number > len(candidates):
        raise ValueError("option must be between 1 and %d" % len(candidates))
    return candidates[number - 1]


def print_trace(con, function, parameter_name, parameter, key, selected, full):
    output = []
    for tracker_id in tracker_ids_for_call(con, selected, parameter):
        output.extend(trace_tracker(con, tracker_id, key, 4, set(), full))
    if not output and full:
        output.append("    [no ptracker information]")
    if not output:
        return

    call = format_call(con, selected, parameter, key, 2)
    output = attach_sources(call, output)
    print("%s(%s)" % (function, parameter_name))
    source_count = 0
    for item in output:
        if item is RECURSION_MARKER:
            continue
        if isinstance(item, tuple) and item[0] is SOURCE_LINE:
            source_count += 1
            print("[ %d ] %s (source %s)" %
                  (source_count, item[1].lstrip(), item[2]))
        else:
            print(item)


def main():
    args = sys.argv[1:]
    full = False
    if "--full" in args:
        args.remove("--full")
        full = True
    if len(args) not in (2, 3):
        usage()
    function = args[0]
    parameter_name = args[1]
    option = args[2] if len(args) == 3 else None
    db_file = os.environ.get("SMATCH_DB_FILE", "smatch_db.sqlite")

    try:
        con = sqlite3.connect("file:%s?mode=ro" % db_file, uri=True)
    except sqlite3.Error as error:
        print("error: %s" % error, file=sys.stderr)
        return 1

    try:
        parameter, key = get_parameter(con, function, parameter_name)
        candidates = caller_rows(con, 0, function, None, parameter, USER_DATA,
                                 key)
        candidates = deduplicate_candidates(con, candidates, parameter)
        if not candidates:
            raise ValueError("no callers pass user data to %s(%s)" %
                             (function, parameter_name))
        if option is None:
            if len(candidates) > 1:
                print_options(candidates, con, function, parameter_name, full)
                return 0
            selected = candidates[0]
        else:
            selected = select_candidate(candidates, option)
        print_trace(con, function, parameter_name, parameter, key, selected,
                    full)
    except (sqlite3.Error, ValueError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    finally:
        con.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
