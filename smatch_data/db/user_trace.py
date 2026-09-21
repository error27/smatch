#!/usr/bin/python3

import os
import sqlite3
import sys


PARAM_VALUE = 1001
PTRACKER = 2538
PTRACKER_MERGE = 2539
USER_DATA = 8017


def usage():
    print("usage: %s function parameter_name [option]" % sys.argv[0],
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
        "select distinct parameter from parameter_name "
        "where function = ? and value = ? order by parameter",
        (function, name),
    ).fetchall()
    parameters = [int(row[0]) for row in rows]
    if not parameters:
        raise ValueError("no parameter named '%s' for %s()" %
                         (name, function))
    if len(parameters) != 1:
        raise ValueError("parameter '%s' has conflicting numbers for %s(): %s" %
                         (name, function,
                          ", ".join(str(param) for param in parameters)))
    return parameters[0]


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


def caller_rows(con, file_id, function, static, parameter, data_type):
    rows = []
    seen = set()
    for pointer in get_function_pointers(con, function):
        args = [pointer, parameter, data_type]
        where = "function = ? and parameter = ? and key = '$' and type = ?"
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
            print("error: invalid ptracker ID: %s" % item[0], file=sys.stderr)
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


def user_range_for_call(con, row, parameter):
    file_id, caller, function, call_id, _line, _value = row
    rows = con.execute(
        "select distinct value from caller_info "
        "where file = ? and caller = ? and function = ? and call_id = ? "
        "and parameter = ? and key = '$' and type = ? order by value",
        (file_id, caller, function, call_id, parameter, USER_DATA),
    ).fetchall()
    if not rows:
        return "unknown"
    return ",".join(item[0] for item in rows)


def print_call(con, row, parameter, indent):
    file_id, caller, function, _call_id, line, value = row
    name = get_parameter_name(con, function, parameter)
    if value == "unknown":
        value = user_range_for_call(con, row, parameter)
    print("%s%s:%d %s() %s=%s" %
          (" " * indent, filename(con, file_id), line, caller, name, value))


def trace_tracker(con, tracker_id, indent, path):
    if tracker_id in path:
        print("%s[ptracker cycle at %d]" % (" " * indent, tracker_id))
        return

    rows = con.execute(
        "select type, value from ptracker where id = ? order by type, value",
        (tracker_id,),
    ).fetchall()
    if not rows:
        print("%s[ptracker %d not found]" % (" " * indent, tracker_id))
        return

    next_path = path | {tracker_id}
    for tracker_type, value in rows:
        if tracker_type == PARAM_VALUE:
            print("%ssource: %s" % (" " * indent, value))
        elif tracker_type == PTRACKER_MERGE:
            try:
                merged_id = int(value, 0)
            except ValueError:
                print("%s[invalid merged ptracker ID: %s]" %
                      (" " * indent, value))
                continue
            trace_tracker(con, merged_id, indent, next_path)
        elif tracker_type == PTRACKER:
            try:
                parameter, file_id, function, static = parse_ptracker(value)
            except ValueError as error:
                print("%s[%s]" % (" " * indent, error))
                continue
            callers = caller_rows(con, file_id, function, static, parameter,
                                  PTRACKER)
            if not callers:
                name = get_parameter_name(con, function, parameter)
                print("%s%s() %s [no earlier caller]" %
                      (" " * indent, function, name))
                continue
            for caller in callers:
                caller = caller[:-1] + ("unknown",)
                print_call(con, caller, parameter, indent)
                for next_id in tracker_ids_for_call(con, caller, parameter):
                    trace_tracker(con, next_id, indent + 2, next_path)
        else:
            print("%s[unknown ptracker type %d]" %
                  (" " * indent, tracker_type))


def print_options(candidates, con):
    for number, row in enumerate(candidates, 1):
        file_id, caller, _function, _call_id, _line, value = row
        print("[%d] %s, %s, %s" %
              (number, filename(con, file_id), caller, value))


def select_candidate(candidates, option):
    try:
        number = int(option, 10)
    except ValueError:
        raise ValueError("invalid option: %s" % option)
    if number < 1 or number > len(candidates):
        raise ValueError("option must be between 1 and %d" % len(candidates))
    return candidates[number - 1]


def main():
    if len(sys.argv) not in (3, 4):
        usage()
    function = sys.argv[1]
    parameter_name = sys.argv[2]
    option = sys.argv[3] if len(sys.argv) == 4 else None
    db_file = os.environ.get("SMATCH_DB_FILE", "smatch_db.sqlite")

    try:
        con = sqlite3.connect("file:%s?mode=ro" % db_file, uri=True)
    except sqlite3.Error as error:
        print("error: %s" % error, file=sys.stderr)
        return 1

    try:
        parameter = get_parameter(con, function, parameter_name)
        candidates = caller_rows(con, 0, function, None, parameter, USER_DATA)
        candidates = deduplicate_candidates(con, candidates, parameter)
        if not candidates:
            raise ValueError("no callers pass user data to %s(%s)" %
                             (function, parameter_name))
        if option is None:
            print_options(candidates, con)
            return 0
        selected = select_candidate(candidates, option)
        print("%s(%s)" % (function, parameter_name))
        print_call(con, selected, parameter, 2)
        tracker_ids = tracker_ids_for_call(con, selected, parameter)
        if not tracker_ids:
            print("    [no ptracker information]")
        for tracker_id in tracker_ids:
            trace_tracker(con, tracker_id, 4, set())
    except (sqlite3.Error, ValueError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    finally:
        con.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
