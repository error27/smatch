#!/usr/bin/python3

import hashlib
import sqlite3
import sys

MTAG_ALIAS_BIT = 1 << 63

PARAM_VALUE = 1001
PTRACKER = 2538
PTRACKER_MERGE = 2539


def get_function_pointers(con, function):
    function_ptrs = [function]
    searched_ptrs = {function}

    def add_function_pointers(current):
        rows = con.execute(
            "select distinct ptr from function_ptr where function = ?;",
            (current,),
        ).fetchall()
        for row in rows:
            pointer = row[0]
            if pointer not in function_ptrs:
                function_ptrs.append(pointer)
            if pointer not in searched_ptrs:
                searched_ptrs.add(pointer)
                add_function_pointers(pointer)

    add_function_pointers(function)
    return function_ptrs


def parse_ptracker(value):
    fields = value.split(",", 3)
    if len(fields) != 4:
        raise ValueError("invalid PTRACKER value: %s" % value)

    parameter = int(fields[0], 0)
    file_id = int(fields[1], 0)
    function = fields[2]
    static = int(fields[3], 0)
    return parameter, file_id, function, static


def select_caller_ptrackers(con, file_id, function, static, parameter):
    seen = set()

    for pointer in get_function_pointers(con, function):
        if pointer != function:
            rows = con.execute(
                "select distinct value from caller_info "
                "where function = ? and parameter = ? "
                "and key = '$' and type = ?;",
                (pointer, parameter, PTRACKER),
            )
        elif file_id:
            rows = con.execute(
                "select distinct value from caller_info "
                "where file = ? and function = ? and static = ? "
                "and parameter = ? and key = '$' and type = ?;",
                (file_id, function, static, parameter, PTRACKER),
            )
        else:
            rows = con.execute(
                "select distinct value from caller_info "
                "where function = ? and static = ? and parameter = ? "
                "and key = '$' and type = ?;",
                (function, static, parameter, PTRACKER),
            )

        for row in rows:
            try:
                tracker_id = int(row[0], 0)
            except ValueError:
                print("error: invalid ptracker ID: %s" % row[0],
                      file=sys.stderr)
                continue
            if tracker_id not in seen:
                seen.add(tracker_id)
                yield tracker_id


def string_to_hash(value):
    digest = hashlib.sha1(value.encode()).digest()
    file_id = int.from_bytes(digest[:8], byteorder=sys.byteorder)
    return file_id & ~MTAG_ALIAS_BIT


def trace_ptracker(con, tracker_id, visited):
    if tracker_id in visited:
        return
    visited.add(tracker_id)

    rows = con.execute(
        "select id, type, value from ptracker where id = ?;",
        (tracker_id,),
    ).fetchall()
    if not rows:
        print("error: ptracker %d not found" % tracker_id,
              file=sys.stderr)
        return

    for _, tracker_type, value in rows:
        if tracker_type == PARAM_VALUE:
            print(value)
        elif tracker_type == PTRACKER_MERGE:
            try:
                next_id = int(value, 0)
            except ValueError:
                print("error: invalid merged ptracker ID: %s" % value,
                      file=sys.stderr)
                continue
            trace_ptracker(con, next_id, visited)
        elif tracker_type == PTRACKER:
            try:
                parameter, file_id, function, static = parse_ptracker(value)
            except ValueError as error:
                print("error: %s" % error, file=sys.stderr)
                continue
            for next_id in select_caller_ptrackers(
                    con, file_id, function, static, parameter):
                trace_ptracker(con, next_id, visited)
        else:
            print("error: unknown ptracker type %d" % tracker_type,
                  file=sys.stderr)


def usage():
    print("usage: %s [filename] function parameter" % sys.argv[0],
          file=sys.stderr)
    sys.exit(1)


def main():
    if len(sys.argv) == 3:
        file_id = 0
        function = sys.argv[1]
        parameter = sys.argv[2]
        static = 0
    elif len(sys.argv) == 4:
        file_id = string_to_hash(sys.argv[1])
        function = sys.argv[2]
        parameter = sys.argv[3]
        static = 1
    else:
        usage()

    try:
        parameter = int(parameter, 0)
    except ValueError:
        usage()
    if parameter < 0:
        usage()

    try:
        con = sqlite3.connect(
            "file:smatch_db.sqlite?mode=ro",
            uri=True,
        )
    except sqlite3.Error as error:
        print("error: %s" % error, file=sys.stderr)
        return 1

    try:
        tracker_ids = list(select_caller_ptrackers(
            con, file_id, function, static, parameter))
        if not tracker_ids:
            print("error: no ptracker information for %s parameter %d" %
                  (function, parameter), file=sys.stderr)
            return 1

        visited = set()
        for tracker_id in tracker_ids:
            trace_ptracker(con, tracker_id, visited)
    except sqlite3.Error as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    finally:
        con.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
