#!/usr/bin/env python3

import sqlite3
import sys


def usage():
    print("usage: %s function return type parameter key value" % sys.argv[0],
          file=sys.stderr)
    return 1


def main():
    if len(sys.argv) != 7:
        return usage()

    function, return_value, entry_type, parameter, key, value = sys.argv[1:]
    try:
        entry_type = int(entry_type, 0)
        parameter = int(parameter, 0)
    except ValueError:
        return usage()

    try:
        con = sqlite3.connect("smatch_db.sqlite")
    except sqlite3.Error as error:
        print("error: %s" % error, file=sys.stderr)
        return 1

    try:
        groups = con.execute(
            "select distinct file, call_id, line, return_id, static "
            "from return_states where function = ? and return = ? "
            "and type = 0",
            (function, return_value),
        ).fetchall()
        if not groups:
            print("error: no matching return states", file=sys.stderr)
            return 1

        with con:
            for file_id, call_id, line, return_id, static in groups:
                exists = con.execute(
                    "select 1 from return_states where file = ? "
                    "and function = ? and return_id = ? and return = ? "
                    "and static = ? and type = ? and parameter = ? "
                    "and key = ? and value = ? limit 1",
                    (file_id, function, return_id, return_value, static,
                     entry_type, parameter, key, value),
                ).fetchone()
                if exists:
                    continue

                con.execute(
                    "insert into return_states values "
                    "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (file_id, function, call_id, line, return_id,
                     return_value, static, entry_type, parameter, key,
                     value),
                )
    except sqlite3.Error as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    finally:
        con.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
