#!/usr/bin/python3

import os
import subprocess
import sys

from bsddb3 import db


def usage():
	print(f"usage: {sys.argv[0]} <database directory>", file=sys.stderr)
	sys.exit(1)


if len(sys.argv) != 2:
	usage()

db_dir = sys.argv[1]
try:
	os.mkdir(db_dir)
except FileExistsError:
	print(f"{db_dir}: already exists", file=sys.stderr)
	sys.exit(1)

env = db.DBEnv()
try:
	env.set_lk_detect(db.DB_LOCK_DEFAULT)
	env.open(db_dir, db.DB_CREATE | db.DB_INIT_LOCK | db.DB_INIT_LOG |
		 db.DB_INIT_MPOOL | db.DB_INIT_TXN | db.DB_THREAD)

	source = db.DB(env)
	source.open("source.db", None, db.DB_BTREE,
		    db.DB_AUTO_COMMIT | db.DB_CREATE | db.DB_THREAD)
	source.close()

	destination = db.DB(env)
	destination.set_flags(db.DB_DUPSORT)
	destination.open("destination.db", None, db.DB_BTREE,
			 db.DB_AUTO_COMMIT | db.DB_CREATE | db.DB_THREAD)
	destination.close()

	parsed_files = db.DB(env)
	parsed_files.open("parsed_files.db", None, db.DB_BTREE,
			  db.DB_AUTO_COMMIT | db.DB_CREATE | db.DB_THREAD)
	parsed_files.close()

	file_numbers = db.DB(env)
	file_numbers.open("file_numbers.db", None, db.DB_BTREE,
			  db.DB_AUTO_COMMIT | db.DB_CREATE | db.DB_THREAD)
	file_names = db.DB(env)
	file_names.open("file_names.db", None, db.DB_BTREE,
			db.DB_AUTO_COMMIT | db.DB_CREATE | db.DB_THREAD)

	git_root = subprocess.check_output(
		["git", "rev-parse", "--show-toplevel"], text=True).strip()
	tracked = subprocess.check_output(
		["git", "-C", git_root, "ls-files", "-z"])
	filenames = tracked.rstrip(b"\0").split(b"\0") if tracked else []
	if len(filenames) > 0xffffff:
		raise RuntimeError("too many tracked files for a 24-bit file number")

	txn = env.txn_begin()
	try:
		for number, filename in enumerate(filenames, 1):
			packed = number.to_bytes(3, "big")
			file_numbers.put(filename, packed, txn=txn)
			file_names.put(packed, filename, txn=txn)
			if number % 4096 == 0:
				txn.commit()
				txn = env.txn_begin()
		txn.commit()
	except Exception:
		txn.abort()
		raise
	finally:
		file_names.close()
		file_numbers.close()
finally:
	env.close()
