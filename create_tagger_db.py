#!/usr/bin/python3

import os
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
	source.open("source.db", None, db.DB_BTREE, db.DB_CREATE | db.DB_THREAD)
	source.close()

	destination = db.DB(env)
	destination.set_flags(db.DB_DUPSORT)
	destination.open("destination.db", None, db.DB_BTREE,
			 db.DB_CREATE | db.DB_THREAD)
	destination.close()
finally:
	env.close()
