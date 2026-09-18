#!/usr/bin/env python3

import os
from pathlib import Path
import re
import sqlite3
import sys


def usage():
    print("usage: %s function [return-range] type [key [value]]" %
          sys.argv[0], file=sys.stderr)
    return 1


def read_types():
    dbtypes = Path(__file__).resolve().parents[2] / "smatch_dbtypes.h"
    types = {}

    try:
        with dbtypes.open(encoding="utf-8") as source:
            for line in source:
                match = re.match(
                    r"\s*([A-Z][A-Z0-9_]*)\s*=\s*([0-9]+)\s*,", line)
                if match:
                    types[match.group(1)] = int(match.group(2))
    except OSError as error:
        print("error: %s" % error, file=sys.stderr)
        return None

    return types


def parse_type(value, types):
    if value in types:
        return types[value]

    try:
        return int(value, 0)
    except ValueError:
        return None


def parse_args(types):
    args = sys.argv[1:]
    if len(args) < 2 or len(args) > 5:
        return None

    function = args.pop(0)
    entry_type = parse_type(args[0], types)
    return_range = None

    if len(args) >= 2:
        second_type = parse_type(args[1], types)
        if second_type is not None:
            return_range = args.pop(0)
            entry_type = second_type

    if entry_type is None:
        return None

    args.pop(0)
    key = args.pop(0) if args else None
    value = args.pop(0) if args else None
    return function, return_range, entry_type, key, value


def main():
    types = read_types()
    if types is None:
        return 1

    parsed = parse_args(types)
    if parsed is None:
        return usage()

    function, return_range, entry_type, key, value = parsed
    query = "delete from return_states where function = ? and type = ?"
    parameters = [function, entry_type]

    if return_range is not None:
        query += " and return = ?"
        parameters.append(return_range)
    if key is not None:
        query += " and key = ?"
        parameters.append(key)
    if value is not None:
        query += " and value = ?"
        parameters.append(value)

    try:
        con = sqlite3.connect(
            os.environ.get("SMATCH_DB_FILE", "smatch_db.sqlite"))
    except sqlite3.Error as error:
        print("error: %s" % error, file=sys.stderr)
        return 1

    try:
        with con:
            cursor = con.execute(query, parameters)
            if cursor.rowcount == 0:
                print("error: no matching return states", file=sys.stderr)
                return 1
    except sqlite3.Error as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    finally:
        con.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
