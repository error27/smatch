#!/usr/bin/env python3

import os
import sqlite3
import sys


def usage():
	print("usage: %s function old-return first-return second-return" %
	      sys.argv[0], file=sys.stderr)
	return 1


def get_next_return_id(con, file_id, function):
	return con.execute(
		"select coalesce(max(return_id), 0) + 1 from return_states "
		"where file = ? and function = ?",
		(file_id, function),
	).fetchone()[0]


def main():
	if len(sys.argv) != 5:
		return usage()

	function, old_return, first_return, second_return = sys.argv[1:]

	try:
		con = sqlite3.connect(
			os.environ.get("SMATCH_DB_FILE", "smatch_db.sqlite"))
	except sqlite3.Error as error:
		print("error: %s" % error, file=sys.stderr)
		return 1

	try:
		rows = con.execute(
			"select rowid, file, function, call_id, line, return_id, "
			"return, static, type, parameter, key, value "
			"from return_states where function = ? and return = ?",
			(function, old_return),
		).fetchall()
		if not rows:
			print("error: no matching return states", file=sys.stderr)
			return 1

		groups = {}
		for row in rows:
			key = (row[1], row[3], row[4], row[5], row[7])
			groups.setdefault(key, []).append(row)

		next_ids = {}
		with con:
			for group in groups.values():
				file_id = group[0][1]
				id_key = (file_id, function)
				if id_key not in next_ids:
					next_ids[id_key] = get_next_return_id(
						con, file_id, function)

				for row in group:
					con.execute(
						"update return_states set return = ? "
						"where rowid = ?",
						(first_return, row[0]),
					)

				for row in group:
					values = list(row[1:])
					values[4] = next_ids[id_key]
					values[5] = second_return
					con.execute(
						"insert into return_states values "
						"(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
						values,
					)
				next_ids[id_key] += 1
	except sqlite3.Error as error:
		print("error: %s" % error, file=sys.stderr)
		return 1
	finally:
		con.close()

	return 0


if __name__ == "__main__":
	sys.exit(main())
