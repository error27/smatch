======
Smatch
======

.. Table of Contents:

.. contents:: :local:


0. Introduction
===============

The Smatch mailing list is <smatch@vger.kernel.org>.

1. Building Smatch
==================

Smatch needs some dependencies to build:

In Debian run::

	apt-get install gcc make sqlite3 libsqlite3-dev libdbd-sqlite3-perl libssl-dev libtry-tiny-perl

Or in Fedora run::

	yum install gcc make sqlite3 sqlite-devel sqlite perl-DBD-SQLite openssl-devel perl-Try-Tiny

Smatch is easy to build.  Just type ``make``.  There isn't an install process
right now so just run it from the build directory.

2. Using Smatch
===============

Smatch can be used with a cross function database. It's not mandatory to
build the database but it's a useful thing to do.  Building the database
for the kernel takes 2-3 hours on my computer.  For the kernel you build
the database with::

	cd ~/path/to/kernel_dir ~/path/to/smatch_dir/smatch_scripts/build_kernel_data.sh

For projects other than the kernel you run Smatch with the options
"--call-tree --info --param-mapper --spammy" and finish building the
database by running the script::

	~/path/to/smatch_dir/smatch_data/db/create_db.sh

Each time you rebuild the cross function database it becomes more accurate. I
normally rebuild the database every morning.

If you are running Smatch over the whole kernel you can use the following
command::

	~/path/to/smatch_dir/smatch_scripts/test_kernel.sh

The test_kernel.sh script will create a .c.smatch file for every file it tests
and a combined smatch_warns.txt file with all the warnings.

If you are running Smatch just over one kernel file::

	~/path/to/smatch_dir/smatch_scripts/kchecker drivers/whatever/file.c

You can also build a directory like this::


	~/path/to/smatch_dir/smatch_scripts/kchecker drivers/whatever/


The kchecker script prints its warnings to stdout.

Running a single check
----------------------

Use ``--show-checks`` to list the available check names::

	~/path/to/smatch_dir/smatch --show-checks

Pass ``--enable=<name>`` to kchecker to run one check while retaining the
internal Smatch infrastructure that the check depends on.  The ``check_``
prefix is optional::

	~/path/to/smatch_dir/smatch_scripts/kchecker --spammy \
		--enable=uninitialized drivers/whatever/file.c

Multiple checks can be enabled with a comma-separated list::

	~/path/to/smatch_dir/smatch_scripts/kchecker --spammy \
		--enable=uninitialized,unreachable \
		drivers/whatever/file.c

Some source files register companion checks, such as an ``_info`` function
used when generating cross-function data.  List each companion explicitly
when it is needed.  For example, the unwind checker registers both
``check_unwind`` and ``check_unwind_info``::

	~/path/to/smatch_dir/smatch_scripts/kchecker --spammy \
		--enable=unwind,unwind_info drivers/whatever/file.c

Smatch reads ``smatch_db.sqlite`` from the current directory by default.  Use
``--db-file=<path>`` to select a different cross-function database::

	~/path/to/smatch_dir/smatch_scripts/kchecker --spammy \
		--enable=uninitialized \
		--db-file=~/path/to/kernel_dir/smatch_db.sqlite \
		drivers/whatever/file.c

The database builder creates ``smatch_db.sqlite.new`` as a staging file.  Once
the build and its sanity check complete, it renames that file to
``smatch_db.sqlite``.  A leftover ``.new`` file therefore belongs to an
in-progress, interrupted, or failed database build and may be incomplete.  Do
not select it for normal checking; finish or rerun ``build_kernel_data.sh`` so
that a validated database is promoted to ``smatch_db.sqlite``.

The above scripts will ensure that any ARCH or CROSS_COMPILE environment
variables are passed to kernel build system - thus allowing for the use of
Smatch with kernels that are normally built with cross-compilers.

If you are building something else (which is not the Linux kernel) then use
something like::

	make CHECK="~/path/to/smatch_dir/smatch --full-path" \
		CC=~/path/to/smatch_dir/smatch/cgcc | tee smatch_warns.txt

The makefile has to let people set the CC with an environment variable for that
to work, of course.

3. Smatch vs Sparse
===================

Smatch uses Sparse as a C parser.  I have made a few hacks to Sparse so I
have to distribute the two together.  Sparse is released under the MIT license
and Smatch is GPLv2+.  If you make changes to Sparse please send those to the
Sparse mailing list linux-sparse@vger.kernel.org and I will pick them up from
there.  Partly I do that for licensing reasons because I don't want to pull GPL
changes into the Sparse code I re-distribute.

