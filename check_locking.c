/*
 * Copyright (C) 2009 Dan Carpenter.
 * Copyright (C) 2019 Oracle.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include <ctype.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(locked);
STATE(half_locked);
STATE(start_state);
STATE(unlocked);
STATE(impossible);
STATE(restore);
STATE(ignore);

enum lock_type {
	spin_lock,
	read_lock,
	write_lock,
	mutex,
	bottom_half,
	irq,
	sem,
	prepare_lock,
	enable_lock,
	rcu,
	rcu_read,
};

const char *get_lock_name(enum lock_type type)
{
	static const char *names[] = {
		[spin_lock] = "spin_lock",
		[read_lock] = "read_lock",
		[write_lock] = "write_lock",
		[mutex] = "mutex",
		[bottom_half] = "bottom_half",
		[irq] = "irq",
		[sem] = "sem",
		[prepare_lock] = "prepare_lock",
		[enable_lock] = "enable_lock",
		[rcu] = "rcu",
		[rcu_read] = "rcu_read",
	};

	return names[type];
}

#define RETURN_VAL -1
#define NO_ARG -2

struct lock_info {
	const char *function;
	int action;
	enum lock_type type;
	int arg;
	const char *key;
	const sval_t *implies_start, *implies_end;
	func_hook *call_back;
};

static sval_t zero_sval = { .type = &int_ctype };
static sval_t one_sval = { .type = &int_ctype, .value = 1 };

static struct lock_info lock_table[] = {
	{"spin_lock",                  LOCK,   spin_lock, 0, "$"},
	{"spin_unlock",                UNLOCK, spin_lock, 0, "$"},
	{"spin_lock_nested",           LOCK,   spin_lock, 0, "$"},
	{"_spin_lock",                 LOCK,   spin_lock, 0, "$"},
	{"_spin_unlock",               UNLOCK, spin_lock, 0, "$"},
	{"_spin_lock_nested",          LOCK,   spin_lock, 0, "$"},
	{"__spin_lock",                LOCK,   spin_lock, 0, "$"},
	{"__spin_unlock",              UNLOCK, spin_lock, 0, "$"},
	{"__spin_lock_nested",         LOCK,   spin_lock, 0, "$"},
	{"raw_spin_lock",              LOCK,   spin_lock, 0, "$"},
	{"raw_spin_unlock",            UNLOCK, spin_lock, 0, "$"},
	{"_raw_spin_lock",             LOCK,   spin_lock, 0, "$"},
	{"_raw_spin_lock_nested",      LOCK,   spin_lock, 0, "$"},
	{"_raw_spin_unlock",           UNLOCK, spin_lock, 0, "$"},
	{"__raw_spin_lock",            LOCK,   spin_lock, 0, "$"},
	{"__raw_spin_unlock",          UNLOCK, spin_lock, 0, "$"},

	{"spin_lock_irq",                 LOCK,   spin_lock, 0, "$"},
	{"spin_unlock_irq",               UNLOCK, spin_lock, 0, "$"},
	{"_spin_lock_irq",                LOCK,   spin_lock, 0, "$"},
	{"_spin_unlock_irq",              UNLOCK, spin_lock, 0, "$"},
	{"__spin_lock_irq",               LOCK,   spin_lock, 0, "$"},
	{"__spin_unlock_irq",             UNLOCK, spin_lock, 0, "$"},
	{"_raw_spin_lock_irq",            LOCK,   spin_lock, 0, "$"},
	{"_raw_spin_unlock_irq",          UNLOCK, spin_lock, 0, "$"},
	{"__raw_spin_unlock_irq",         UNLOCK, spin_lock, 0, "$"},
	{"spin_lock_irqsave",             LOCK,   spin_lock, 0, "$"},
	{"spin_unlock_irqrestore",        UNLOCK, spin_lock, 0, "$"},
	{"_spin_lock_irqsave",            LOCK,   spin_lock, 0, "$"},
	{"_spin_unlock_irqrestore",       UNLOCK, spin_lock, 0, "$"},
	{"__spin_lock_irqsave",           LOCK,   spin_lock, 0, "$"},
	{"__spin_unlock_irqrestore",      UNLOCK, spin_lock, 0, "$"},
	{"_raw_spin_lock_irqsave",        LOCK,   spin_lock, 0, "$"},
	{"_raw_spin_unlock_irqrestore",   UNLOCK, spin_lock, 0, "$"},
	{"__raw_spin_lock_irqsave",       LOCK,   spin_lock, 0, "$"},
	{"__raw_spin_unlock_irqrestore",  UNLOCK, spin_lock, 0, "$"},
	{"spin_lock_irqsave_nested",      LOCK,   spin_lock, 0, "$"},
	{"_spin_lock_irqsave_nested",     LOCK,   spin_lock, 0, "$"},
	{"__spin_lock_irqsave_nested",    LOCK,   spin_lock, 0, "$"},
	{"_raw_spin_lock_irqsave_nested", LOCK,   spin_lock, 0, "$"},
	{"spin_lock_bh",                  LOCK,   spin_lock, 0, "$"},
	{"spin_unlock_bh",                UNLOCK, spin_lock, 0, "$"},
	{"_spin_lock_bh",                 LOCK,   spin_lock, 0, "$"},
	{"_spin_unlock_bh",               UNLOCK, spin_lock, 0, "$"},
	{"__spin_lock_bh",                LOCK,   spin_lock, 0, "$"},
	{"__spin_unlock_bh",              UNLOCK, spin_lock, 0, "$"},

	{"spin_trylock",               LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"_spin_trylock",              LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"__spin_trylock",             LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"raw_spin_trylock",           LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"_raw_spin_trylock",          LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"spin_trylock_irq",           LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"spin_trylock_irqsave",       LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"spin_trylock_bh",            LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"_spin_trylock_bh",           LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"__spin_trylock_bh",          LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"__raw_spin_trylock",         LOCK,   spin_lock, 0, "$", &one_sval, &one_sval},
	{"_atomic_dec_and_lock",       LOCK,   spin_lock, 1, "$", &one_sval, &one_sval},

	{"read_lock",                 LOCK,   read_lock, 0, "$"},
	{"down_read",                 LOCK,   read_lock, 0, "$"},
	{"down_read_nested",          LOCK,   read_lock, 0, "$"},
	{"down_read_trylock",         LOCK,   read_lock, 0, "$", &one_sval, &one_sval},
	{"down_read_killable",        LOCK,   read_lock, 0, "$", &zero_sval, &zero_sval},
	{"up_read",                   UNLOCK, read_lock, 0, "$"},
	{"read_unlock",               UNLOCK, read_lock, 0, "$"},
	{"_read_lock",                LOCK,   read_lock, 0, "$"},
	{"_read_unlock",              UNLOCK, read_lock, 0, "$"},
	{"__read_lock",               LOCK,   read_lock, 0, "$"},
	{"__read_unlock",             UNLOCK, read_lock, 0, "$"},
	{"_raw_read_lock",            LOCK,   read_lock, 0, "$"},
	{"_raw_read_unlock",          UNLOCK, read_lock, 0, "$"},
	{"__raw_read_lock",           LOCK,   read_lock, 0, "$"},
	{"__raw_read_unlock",         UNLOCK, read_lock, 0, "$"},
	{"read_lock_irq",             LOCK,   read_lock, 0, "$"},
	{"read_unlock_irq" ,          UNLOCK, read_lock, 0, "$"},
	{"_read_lock_irq",            LOCK,   read_lock, 0, "$"},
	{"_read_unlock_irq",          UNLOCK, read_lock, 0, "$"},
	{"__read_lock_irq",           LOCK,   read_lock, 0, "$"},
	{"__read_unlock_irq",         UNLOCK, read_lock, 0, "$"},
	{"_raw_read_unlock_irq",      UNLOCK, read_lock, 0, "$"},
	{"_raw_read_lock_irq",        LOCK,   read_lock, 0, "$"},
	{"_raw_read_lock_bh",         LOCK,   read_lock, 0, "$"},
	{"_raw_read_unlock_bh",       UNLOCK, read_lock, 0, "$"},
	{"read_lock_irqsave",         LOCK,   read_lock, 0, "$"},
	{"read_unlock_irqrestore",    UNLOCK, read_lock, 0, "$"},
	{"_read_lock_irqsave",        LOCK,   read_lock, 0, "$"},
	{"_read_unlock_irqrestore",   UNLOCK, read_lock, 0, "$"},
	{"__read_lock_irqsave",       LOCK,   read_lock, 0, "$"},
	{"__read_unlock_irqrestore",  UNLOCK, read_lock, 0, "$"},
	{"read_lock_bh",              LOCK,   read_lock, 0, "$"},
	{"read_unlock_bh",            UNLOCK, read_lock, 0, "$"},
	{"_read_lock_bh",             LOCK,   read_lock, 0, "$"},
	{"_read_unlock_bh",           UNLOCK, read_lock, 0, "$"},
	{"__read_lock_bh",            LOCK,   read_lock, 0, "$"},
	{"__read_unlock_bh",          UNLOCK, read_lock, 0, "$"},
	{"__raw_read_lock_bh",        LOCK,   read_lock, 0, "$"},
	{"__raw_read_unlock_bh",      UNLOCK, read_lock, 0, "$"},

	{"_raw_read_lock_irqsave",        LOCK,    read_lock,   0,          "$"},
	{"_raw_read_lock_irqsave",        LOCK,    irq,	        RETURN_VAL, "$"},
	{"_raw_read_unlock_irqrestore",   UNLOCK,  read_lock,   0,          "$"},
	{"_raw_read_unlock_irqrestore",   RESTORE, irq,         1,          "$"},
	{"_raw_spin_lock_bh",             LOCK,    read_lock,   0,          "$"},
	{"_raw_spin_lock_bh",             LOCK,    bottom_half, NO_ARG,     "bh"},
	{"_raw_spin_lock_nest_lock",      LOCK,    read_lock,   0,          "$"},
	{"_raw_spin_unlock_bh",           UNLOCK,  read_lock,   0,          "$"},
	{"_raw_spin_unlock_bh",           UNLOCK,  bottom_half, NO_ARG,     "bh"},
	{"_raw_write_lock_irqsave",       LOCK,    write_lock,  0,          "$"},
	{"_raw_write_lock_irqsave",       LOCK,    irq,         RETURN_VAL, "$"},
	{"_raw_write_unlock_irqrestore",  UNLOCK,  write_lock,  0,          "$"},
	{"_raw_write_unlock_irqrestore",  RESTORE, irq,         1,          "$"},
	{"__raw_write_unlock_irqrestore", UNLOCK,  write_lock,  0,          "$"},
	{"__raw_write_unlock_irqrestore", RESTORE, irq,         1,          "$"},

	{"generic__raw_read_trylock", LOCK,   read_lock, 0, "$", &one_sval, &one_sval},
	{"read_trylock",              LOCK,   read_lock, 0, "$", &one_sval, &one_sval},
	{"_read_trylock",             LOCK,   read_lock, 0, "$", &one_sval, &one_sval},
	{"raw_read_trylock",          LOCK,   read_lock, 0, "$", &one_sval, &one_sval},
	{"_raw_read_trylock",         LOCK,   read_lock, 0, "$", &one_sval, &one_sval},
	{"__raw_read_trylock",        LOCK,   read_lock, 0, "$", &one_sval, &one_sval},
	{"__read_trylock",            LOCK,   read_lock, 0, "$", &one_sval, &one_sval},

	{"write_lock",                LOCK,   write_lock, 0, "$"},
	{"down_write",                LOCK,   write_lock, 0, "$"},
	{"down_write_nested",         LOCK,   write_lock, 0, "$"},
	{"up_write",                  UNLOCK, write_lock, 0, "$"},
	{"write_unlock",              UNLOCK, write_lock, 0, "$"},
	{"_write_lock",               LOCK,   write_lock, 0, "$"},
	{"_write_unlock",             UNLOCK, write_lock, 0, "$"},
	{"__write_lock",              LOCK,   write_lock, 0, "$"},
	{"__write_unlock",            UNLOCK, write_lock, 0, "$"},
	{"write_lock_irq",            LOCK,   write_lock, 0, "$"},
	{"write_unlock_irq",          UNLOCK, write_lock, 0, "$"},
	{"_write_lock_irq",           LOCK,   write_lock, 0, "$"},
	{"_write_unlock_irq",         UNLOCK, write_lock, 0, "$"},
	{"__write_lock_irq",          LOCK,   write_lock, 0, "$"},
	{"__write_unlock_irq",        UNLOCK, write_lock, 0, "$"},
	{"_raw_write_unlock_irq",     UNLOCK, write_lock, 0, "$"},
	{"write_lock_irqsave",        LOCK,   write_lock, 0, "$"},
	{"write_unlock_irqrestore",   UNLOCK, write_lock, 0, "$"},
	{"_write_lock_irqsave",       LOCK,   write_lock, 0, "$"},
	{"_write_unlock_irqrestore",  UNLOCK, write_lock, 0, "$"},
	{"__write_lock_irqsave",      LOCK,   write_lock, 0, "$"},
	{"__write_unlock_irqrestore", UNLOCK, write_lock, 0, "$"},
	{"write_lock_bh",             LOCK,   write_lock, 0, "$"},
	{"write_unlock_bh",           UNLOCK, write_lock, 0, "$"},
	{"_write_lock_bh",            LOCK,   write_lock, 0, "$"},
	{"_write_unlock_bh",          UNLOCK, write_lock, 0, "$"},
	{"__write_lock_bh",           LOCK,   write_lock, 0, "$"},
	{"__write_unlock_bh",         UNLOCK, write_lock, 0, "$"},
	{"_raw_write_lock",           LOCK,   write_lock, 0, "$"},
	{"__raw_write_lock",          LOCK,   write_lock, 0, "$"},
	{"_raw_write_unlock",         UNLOCK, write_lock, 0, "$"},
	{"__raw_write_unlock",        UNLOCK, write_lock, 0, "$"},
	{"_raw_write_lock_bh",        LOCK,   write_lock, 0, "$"},
	{"_raw_write_unlock_bh",      UNLOCK, write_lock, 0, "$"},
	{"_raw_write_lock_irq",       LOCK,   write_lock, 0, "$"},

	{"write_trylock",             LOCK,   write_lock, 0, "$", &one_sval, &one_sval},
	{"_write_trylock",            LOCK,   write_lock, 0, "$", &one_sval, &one_sval},
	{"raw_write_trylock",         LOCK,   write_lock, 0, "$", &one_sval, &one_sval},
	{"_raw_write_trylock",        LOCK,   write_lock, 0, "$", &one_sval, &one_sval},
	{"__write_trylock",           LOCK,   write_lock, 0, "$", &one_sval, &one_sval},
	{"__raw_write_trylock",       LOCK,   write_lock, 0, "$", &one_sval, &one_sval},
	{"down_write_trylock",        LOCK,   write_lock, 0, "$", &one_sval, &one_sval},
	{"down_write_killable",       LOCK,   write_lock, 0, "$", &zero_sval, &zero_sval},

	{"down",               LOCK,   sem, 0, "$"},
	{"up",                 UNLOCK, sem, 0, "$"},
	{"down_trylock",       LOCK,   sem, 0, "$", &zero_sval, &zero_sval},
	{"down_timeout",       LOCK,   sem, 0, "$", &zero_sval, &zero_sval},
	{"down_interruptible", LOCK,   sem, 0, "$", &zero_sval, &zero_sval},
	{"down_killable",      LOCK,   sem, 0, "$", &zero_sval, &zero_sval},


	{"mutex_lock",                      LOCK,   mutex, 0, "$"},
	{"mutex_unlock",                    UNLOCK, mutex, 0, "$"},
	{"mutex_destroy",                   RESTORE, mutex, 0, "$"},
	{"mutex_lock_nested",               LOCK,   mutex, 0, "$"},
	{"mutex_lock_io",                   LOCK,   mutex, 0, "$"},
	{"mutex_lock_io_nested",            LOCK,   mutex, 0, "$"},

	{"mutex_lock_interruptible",        LOCK,   mutex, 0, "$", &zero_sval, &zero_sval},
	{"mutex_lock_interruptible_nested", LOCK,   mutex, 0, "$", &zero_sval, &zero_sval},
	{"mutex_lock_killable",             LOCK,   mutex, 0, "$", &zero_sval, &zero_sval},
	{"mutex_lock_killable_nested",      LOCK,   mutex, 0, "$", &zero_sval, &zero_sval},

	{"mutex_trylock",                   LOCK,   mutex, 0, "$", &one_sval, &one_sval},

	{"ww_mutex_lock",		LOCK,   mutex, 0, "$"},
	{"__ww_mutex_lock",		LOCK,   mutex, 0, "$"},
	{"ww_mutex_lock_interruptible",	LOCK,   mutex, 0, "$", &zero_sval, &zero_sval},
	{"ww_mutex_unlock",		UNLOCK, mutex, 0, "$"},

	{"raw_local_irq_disable", LOCK,   irq, NO_ARG, "irq"},
	{"raw_local_irq_enable",  UNLOCK, irq, NO_ARG, "irq"},
	{"spin_lock_irq",         LOCK,   irq, NO_ARG, "irq"},
	{"spin_unlock_irq",       UNLOCK, irq, NO_ARG, "irq"},
	{"_spin_lock_irq",        LOCK,   irq, NO_ARG, "irq"},
	{"_spin_unlock_irq",      UNLOCK, irq, NO_ARG, "irq"},
	{"__spin_lock_irq",       LOCK,   irq, NO_ARG, "irq"},
	{"__spin_unlock_irq",     UNLOCK, irq, NO_ARG, "irq"},
	{"_raw_spin_lock_irq",    LOCK,   irq, NO_ARG, "irq"},
	{"_raw_spin_unlock_irq",  UNLOCK, irq, NO_ARG, "irq"},
	{"__raw_spin_unlock_irq", UNLOCK, irq, NO_ARG, "irq"},
	{"spin_trylock_irq",      LOCK,   irq, NO_ARG, "irq", &one_sval, &one_sval},
	{"read_lock_irq",         LOCK,   irq, NO_ARG, "irq"},
	{"read_unlock_irq",       UNLOCK, irq, NO_ARG, "irq"},
	{"_read_lock_irq",        LOCK,   irq, NO_ARG, "irq"},
	{"_read_unlock_irq",      UNLOCK, irq, NO_ARG, "irq"},
	{"__read_lock_irq",       LOCK,   irq, NO_ARG, "irq"},
	{"_raw_read_lock_irq",    LOCK,   irq, NO_ARG, "irq"},
	{"__read_unlock_irq",     UNLOCK, irq, NO_ARG, "irq"},
	{"_raw_read_unlock_irq",  UNLOCK, irq, NO_ARG, "irq"},
	{"write_lock_irq",        LOCK,   irq, NO_ARG, "irq"},
	{"write_unlock_irq",      UNLOCK, irq, NO_ARG, "irq"},
	{"_write_lock_irq",       LOCK,   irq, NO_ARG, "irq"},
	{"_write_unlock_irq",     UNLOCK, irq, NO_ARG, "irq"},
	{"__write_lock_irq",      LOCK,   irq, NO_ARG, "irq"},
	{"__write_unlock_irq",    UNLOCK, irq, NO_ARG, "irq"},
	{"_raw_write_lock_irq",   LOCK,   irq, NO_ARG, "irq"},
	{"_raw_write_unlock_irq", UNLOCK, irq, NO_ARG, "irq"},

	{"arch_local_irq_save",        LOCK,      irq, RETURN_VAL, "$"},
	{"arch_local_irq_restore",     RESTORE,   irq, 0,	   "$"},
	{"__raw_local_irq_save",       LOCK,      irq, RETURN_VAL, "$"},
	{"raw_local_irq_restore",      RESTORE,   irq, 0,	   "$"},
	{"spin_lock_irqsave_nested",   LOCK,      irq, RETURN_VAL, "$"},
	{"spin_lock_irqsave",          LOCK,      irq, 1,	   "$"},
	{"spin_unlock_irqrestore",     RESTORE,   irq, 1,	   "$"},
	{"_spin_lock_irqsave_nested",  LOCK,      irq, RETURN_VAL, "$"},
	{"_spin_lock_irqsave",         LOCK,      irq, RETURN_VAL, "$"},
	{"_spin_lock_irqsave",         LOCK,      irq, 1,	   "$"},
	{"_spin_unlock_irqrestore",    RESTORE,   irq, 1,	   "$"},
	{"__spin_lock_irqsave_nested", LOCK,      irq, 1,	   "$"},
	{"__spin_lock_irqsave",        LOCK,      irq, 1,	   "$"},
	{"__spin_unlock_irqrestore",   RESTORE,   irq, 1,	   "$"},
	{"_raw_spin_lock_irqsave",     LOCK,      irq, RETURN_VAL, "$"},
	{"_raw_spin_lock_irqsave",     LOCK,      irq, 1,	   "$"},
	{"_raw_spin_unlock_irqrestore", RESTORE,  irq, 1,	   "$"},
	{"__raw_spin_lock_irqsave",    LOCK,      irq, RETURN_VAL, "$"},
	{"__raw_spin_unlock_irqrestore", RESTORE, irq, 1,	   "$"},
	{"_raw_spin_lock_irqsave_nested", LOCK,   irq, RETURN_VAL, "$"},
	{"spin_trylock_irqsave",       LOCK,      irq, 1,	   "$", &one_sval, &one_sval},
	{"read_lock_irqsave",          LOCK,      irq, RETURN_VAL, "$"},
	{"read_lock_irqsave",          LOCK,      irq, 1,	   "$"},
	{"read_unlock_irqrestore",     RESTORE,   irq, 1,	   "$"},
	{"_read_lock_irqsave",         LOCK,      irq, RETURN_VAL, "$"},
	{"_read_lock_irqsave",         LOCK,      irq, 1,	   "$"},
	{"_read_unlock_irqrestore",    RESTORE,   irq, 1,	   "$"},
	{"__read_lock_irqsave",        LOCK,      irq, RETURN_VAL, "$"},
	{"__read_unlock_irqrestore",   RESTORE,   irq, 1,	   "$"},
	{"write_lock_irqsave",         LOCK,      irq, RETURN_VAL, "$"},
	{"write_lock_irqsave",         LOCK,      irq, 1,	   "$"},
	{"write_unlock_irqrestore",    RESTORE,   irq, 1,	   "$"},
	{"_write_lock_irqsave",        LOCK,      irq, RETURN_VAL, "$"},
	{"_write_lock_irqsave",        LOCK,      irq, 1,	   "$"},
	{"_write_unlock_irqrestore",   RESTORE,   irq, 1,	   "$"},
	{"__write_lock_irqsave",       LOCK,      irq, RETURN_VAL, "$"},
	{"__write_unlock_irqrestore",  RESTORE,   irq, 1,	   "$"},

	{"local_bh_disable",	LOCK,	bottom_half, NO_ARG, "bh"},
	{"_local_bh_disable",	LOCK,	bottom_half, NO_ARG, "bh"},
	{"__local_bh_disable",	LOCK,	bottom_half, NO_ARG, "bh"},
	{"local_bh_enable",	UNLOCK,	bottom_half, NO_ARG, "bh"},
	{"_local_bh_enable",	UNLOCK,	bottom_half, NO_ARG, "bh"},
	{"__local_bh_enable",	UNLOCK,	bottom_half, NO_ARG, "bh"},
	{"spin_lock_bh",        LOCK,   bottom_half, NO_ARG, "bh"},
	{"spin_unlock_bh",      UNLOCK, bottom_half, NO_ARG, "bh"},
	{"_spin_lock_bh",       LOCK,   bottom_half, NO_ARG, "bh"},
	{"_spin_unlock_bh",     UNLOCK, bottom_half, NO_ARG, "bh"},
	{"__spin_lock_bh",      LOCK,   bottom_half, NO_ARG, "bh"},
	{"__spin_unlock_bh",    UNLOCK, bottom_half, NO_ARG, "bh"},
	{"read_lock_bh",        LOCK,   bottom_half, NO_ARG, "bh"},
	{"read_unlock_bh",      UNLOCK, bottom_half, NO_ARG, "bh"},
	{"_read_lock_bh",       LOCK,   bottom_half, NO_ARG, "bh"},
	{"_read_unlock_bh",     UNLOCK, bottom_half, NO_ARG, "bh"},
	{"__read_lock_bh",      LOCK,   bottom_half, NO_ARG, "bh"},
	{"__read_unlock_bh",    UNLOCK, bottom_half, NO_ARG, "bh"},
	{"_raw_read_lock_bh",   LOCK,   bottom_half, NO_ARG, "bh"},
	{"_raw_read_unlock_bh", UNLOCK, bottom_half, NO_ARG, "bh"},
	{"write_lock_bh",       LOCK,   bottom_half, NO_ARG, "bh"},
	{"write_unlock_bh",     UNLOCK, bottom_half, NO_ARG, "bh"},
	{"_write_lock_bh",      LOCK,   bottom_half, NO_ARG, "bh"},
	{"_write_unlock_bh",    UNLOCK, bottom_half, NO_ARG, "bh"},
	{"__write_lock_bh",     LOCK,   bottom_half, NO_ARG, "bh"},
	{"__write_unlock_bh",   UNLOCK, bottom_half, NO_ARG, "bh"},
	{"_raw_write_lock_bh",  LOCK,   bottom_half, NO_ARG, "bh"},
	{"_raw_write_unlock_bh",UNLOCK, bottom_half, NO_ARG, "bh"},
	{"spin_trylock_bh",     LOCK,   bottom_half, NO_ARG, "bh", &one_sval, &one_sval},
	{"_spin_trylock_bh",    LOCK,   bottom_half, NO_ARG, "bh", &one_sval, &one_sval},
	{"__spin_trylock_bh",   LOCK,   bottom_half, NO_ARG, "bh", &one_sval, &one_sval},

	{"ffs_mutex_lock",      LOCK,   mutex, 0, "$", &zero_sval, &zero_sval},

	{"clk_prepare_lock",    LOCK,   prepare_lock, NO_ARG, "clk"},
	{"clk_prepare_unlock",  UNLOCK, prepare_lock, NO_ARG, "clk"},
	{"clk_enable_lock",     LOCK,   enable_lock, -1, "$"},
	{"clk_enable_unlock",   UNLOCK, enable_lock,  0, "$"},

	{"dma_resv_lock",	        LOCK,   mutex, 0, "$", &zero_sval, &zero_sval},
	{"dma_resv_trylock",	        LOCK,	mutex, 0, "$", &one_sval, &one_sval},
	{"dma_resv_lock_interruptible", LOCK,	mutex, 0, "$", &zero_sval, &zero_sval},
	{"dma_resv_unlock",		UNLOCK, mutex, 0, "$"},

	{"modeset_lock",			  LOCK,   mutex, 0, "$", &zero_sval, &zero_sval},
	{"drm_ modeset_lock",			  LOCK,   mutex, 0, "$", &zero_sval, &zero_sval},
	{"drm_modeset_lock_single_interruptible", LOCK,   mutex, 0, "$", &zero_sval, &zero_sval},
	{"modeset_unlock",			  UNLOCK, mutex, 0, "$"},
//	{"nvkm_i2c_aux_acquire",		  LOCK,   mutex, 
//	{"i915_gem_object_lock_interruptible",	  LOCK,	  mutex, 

	{"reiserfs_write_lock_nested",	 LOCK,   mutex, 0, "$"},
	{"reiserfs_write_unlock_nested", UNLOCK, mutex, 0, "$"},

	{"rw_lock",                LOCK,   write_lock, 1, "$"},
	{"rw_unlock",              UNLOCK, write_lock, 1, "$"},

	{"sem_lock",               LOCK,   mutex, 0, "$"},
	{"sem_unlock",             UNLOCK, mutex, 0, "$"},

	{"rcu_lock_acquire",            LOCK,   rcu, NO_ARG, "rcu"},
	{"rcu_lock_release",            UNLOCK, rcu, NO_ARG, "rcu"},

	{"rcu_read_lock",               LOCK,   rcu_read, NO_ARG, "rcu_read"},
	{"rcu_read_unlock",             UNLOCK, rcu_read, NO_ARG, "rcu_read"},
	{"rcu_read_lock_bh",            LOCK,   rcu_read, NO_ARG, "rcu_read"},
	{"rcu_read_unlock_bh",          UNLOCK, rcu_read, NO_ARG, "rcu_read"},

	{"rcu_read_lock_sched",           LOCK,   rcu_read, NO_ARG, "rcu_read"},
	{"rcu_read_lock_sched_notrace",   LOCK,   rcu_read, NO_ARG, "rcu_read"},
	{"rcu_read_unlock_sched",         UNLOCK, rcu_read, NO_ARG, "rcu_read"},
	{"rcu_read_unlock_sched_notrace", UNLOCK, rcu_read, NO_ARG, "rcu_read"},

	{"bch_write_bdev_super",	IGNORE_LOCK, sem, 0, "&$->sb_write_mutex"},
	{"dlfb_set_video_mode",		IGNORE_LOCK, sem, 0, "&$->urbs.limit_sem"},

	{},
};

struct macro_info {
	const char *macro;
	int action;
	int param;
};

static struct macro_info macro_table[] = {
	{"genpd_lock",               LOCK,   0},
	{"genpd_lock_nested",        LOCK,   0},
	{"genpd_lock_interruptible", LOCK,   0},
	{"genpd_unlock",             UNLOCK, 0},
};

static const char *false_positives[][2] = {
	{"fs/jffs2/", "->alloc_sem"},
	{"fs/xfs/", "->b_sema"},
	{"mm/", "pvmw->ptl"},
};

static struct stree *start_states;

static struct tracker_list *locks;

static struct expression *ignored_reset;
static void reset(struct sm_state *sm, struct expression *mod_expr)
{
	struct expression *faked;

	if (mod_expr && mod_expr->type == EXPR_ASSIGNMENT &&
	    mod_expr->left == ignored_reset)
		return;
	faked = get_faked_expression();
	if (faked && faked->type == EXPR_ASSIGNMENT &&
	    faked->left == ignored_reset)
		return;

	set_state(my_id, sm->name, sm->sym, &start_state);
}

static struct smatch_state *get_start_state(struct sm_state *sm)
{
	struct smatch_state *orig;

	if (!sm)
		return NULL;

	orig = get_state_stree(start_states, my_id, sm->name, sm->sym);
	if (orig)
		return orig;
	return NULL;
}

static struct expression *remove_spinlock_check(struct expression *expr)
{
	if (expr->type != EXPR_CALL)
		return expr;
	if (expr->fn->type != EXPR_SYMBOL)
		return expr;
	if (strcmp(expr->fn->symbol_name->name, "spinlock_check"))
		return expr;
	expr = get_argument_from_call_expr(expr->args, 0);
	return expr;
}

static struct expression *filter_kernel_args(struct expression *arg)
{
	if (arg->type == EXPR_PREOP && arg->op == '&')
		return strip_expr(arg->unop);
	if (!is_pointer(arg))
		return arg;
	return deref_expression(strip_expr(arg));
}

static char *lock_to_name_sym(struct expression *expr, struct symbol **sym)
{
	expr = remove_spinlock_check(expr);
	expr = filter_kernel_args(expr);
	return expr_to_str_sym(expr, sym);
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return &start_state;
}

static void pre_merge_hook(struct sm_state *cur, struct sm_state *other)
{
	if (is_impossible_path())
		set_state(my_id, cur->name, cur->sym, &impossible);
}

static struct smatch_state *merge_func(struct smatch_state *s1, struct smatch_state *s2)
{
	if (s1 == &impossible)
		return s2;
	if (s2 == &impossible)
		return s1;
	return &merged;
}

static struct sm_state *get_best_match(const char *key, int lock_unlock)
{
	struct sm_state *sm;
	struct sm_state *match;
	int cnt = 0;
	int start_pos, state_len, key_len, chunks, i;

	if (strncmp(key, "$->", 3) == 0)
		key += 3;

	key_len = strlen(key);
	chunks = 0;
	for (i = key_len - 1; i > 0; i--) {
		if (key[i] == '>' || key[i] == '.')
			chunks++;
		if (chunks == 2) {
			key += (i + 1);
			key_len = strlen(key);
			break;
		}
	}

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (((lock_unlock == UNLOCK || lock_unlock == RESTORE) &&
		     sm->state != &locked) ||
		    (lock_unlock == LOCK && sm->state != &unlocked))
			continue;
		state_len = strlen(sm->name);
		if (state_len < key_len)
			continue;
		start_pos = state_len - key_len;
		if ((start_pos == 0 || !isalnum(sm->name[start_pos - 1])) &&
		    strcmp(sm->name + start_pos, key) == 0) {
			cnt++;
			match = sm;
		}
	} END_FOR_EACH_SM(sm);

	if (cnt == 1)
		return match;
	return NULL;
}

static char *use_best_match(const char *key, int lock_unlock, struct symbol **sym)
{
	struct sm_state *match;

	match = get_best_match(key, lock_unlock);
	if (!match) {
		*sym = NULL;
		return alloc_string(key);
	}
	*sym = match->sym;
	return alloc_string(match->name);
}

static void set_start_state(const char *name, struct symbol *sym, struct smatch_state *start)
{
	struct smatch_state *orig;

	orig = get_state_stree(start_states, my_id, name, sym);
	if (!orig)
		set_state_stree(&start_states, my_id, name, sym, start);
	else if (orig != start)
		set_state_stree(&start_states, my_id, name, sym, &undefined);
}

static bool common_false_positive(const char *name)
{
	const char *path, *lname;
	int i, len_total, len_path, len_name, skip;

	if (!get_filename())
		return false;

	len_total = strlen(name);
	for (i = 0; i < ARRAY_SIZE(false_positives); i++) {
		path = false_positives[i][0];
		lname = false_positives[i][1];

		len_path = strlen(path);
		len_name = strlen(lname);

		if (len_name > len_total)
			continue;
		skip = len_total - len_name;

		if (strncmp(get_filename(), path, len_path) == 0 &&
		    strcmp(name + skip, lname) == 0)
			return true;
	}

	return false;
}

static bool sm_in_start_states(struct sm_state *sm)
{
	if (!sm || !cur_func_sym)
		return false;
	if (sm->line == cur_func_sym->pos.line)
		return true;
	return false;
}

static void warn_on_double(struct sm_state *sm, struct smatch_state *state)
{
	struct sm_state *tmp;

	if (!sm)
		return;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state == state)
			goto found;
	} END_FOR_EACH_PTR(tmp);

	return;
found:
	// FIXME: called with read_lock held
	// drivers/scsi/aic7xxx/aic7xxx_osm.c:1591 ahc_linux_isr() error: double locked 'flags' (orig line 1584)
	if (strcmp(sm->name, "bottom_half") == 0)
		return;
	if (strstr(sm->name, "rcu"))
		return;
	if (common_false_positive(sm->name))
		return;

	if (state == &locked && sm_in_start_states(tmp)) {
//		sm_warning("called with lock held.  '%s'", sm->name);
	} else {
//		sm_msg("error: double %s '%s' (orig line %u)",
//		       state->name, sm->name, tmp->line);
	}
}

static bool handle_macro_lock_unlock(void)
{
	struct expression *expr, *arg;
	struct macro_info *info;
	struct sm_state *sm;
	struct symbol *sym;
	const char *macro;
	char *name;
	bool ret = false;
	int i;

	expr = last_ptr_list((struct ptr_list *)big_expression_stack);
	while (expr && expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (!expr || expr->type != EXPR_CALL)
		return false;

	macro = get_macro_name(expr->pos);
	if (!macro)
		return false;

	for (i = 0; i < ARRAY_SIZE(macro_table); i++) {
		info = &macro_table[i];

		if (strcmp(macro, info->macro) != 0)
			continue;
		arg = get_argument_from_call_expr(expr->args, info->param);
		name = expr_to_str_sym(arg, &sym);
		if (!name || !sym)
			goto free;
		sm = get_sm_state(my_id, name, sym);

		if (info->action == LOCK) {
			if (!get_start_state(sm))
				set_start_state(name, sym, &unlocked);
			if (sm && sm->line != expr->pos.line)
				warn_on_double(sm, &locked);
			set_state(my_id, name, sym, &locked);
		} else {
			if (!get_start_state(sm))
				set_start_state(name, sym, &locked);
			if (sm && sm->line != expr->pos.line)
				warn_on_double(sm, &unlocked);
			set_state(my_id, name, sym, &unlocked);
		}
		ret = true;
free:
		free_string(name);
		return ret;
	}
	return false;
}

static bool is_local_IRQ_save(const char *name, struct symbol *sym, struct lock_info *info)
{
	if (name && strcmp(name, "flags") == 0)
		return true;
	if (!sym)
		return false;
	if (!sym->ident || strcmp(sym->ident->name, name) != 0)
		return false;
	if (!info)
		return false;
	return strstr(info->function, "irq") && strstr(info->function, "save");
}

static void do_lock(const char *name, struct symbol *sym, struct lock_info *info)
{
	struct sm_state *sm;
	bool delete_null = false;

	if (!info && handle_macro_lock_unlock())
		return;

	add_tracker(&locks, my_id, name, sym);

	sm = get_sm_state(my_id, name, sym);
	if (!get_start_state(sm))
		set_start_state(name, sym, &unlocked);
	if (!sm && !is_local_IRQ_save(name, sym, info) && sym) {
		sm = get_best_match(name, LOCK);
		if (sm) {
			name = sm->name;
			if (sm->sym)
				sym = sm->sym;
			else
				delete_null = true;
		}
	}
	warn_on_double(sm, &locked);
	if (delete_null)
		set_state(my_id, name, NULL, &ignore);

	set_state(my_id, name, sym, &locked);
}

static void do_unlock(const char *name, struct symbol *sym, struct lock_info *info)
{
	struct sm_state *sm;
	bool delete_null = false;

	if (__path_is_null())
		return;

	if (!info && handle_macro_lock_unlock())
		return;

	add_tracker(&locks, my_id, name, sym);
	sm = get_sm_state(my_id, name, sym);
	if (!sm && !info && !is_local_IRQ_save(name, sym, info)) {
		sm = get_best_match(name, UNLOCK);
		if (sm) {
			name = sm->name;
			if (sm->sym)
				sym = sm->sym;
			else
				delete_null = true;
		}
	}
	if (!get_start_state(sm))
		set_start_state(name, sym, &locked);
	warn_on_double(sm, &unlocked);
	if (delete_null)
		set_state(my_id, name, NULL, &ignore);
	set_state(my_id, name, sym, &unlocked);
}

static void do_restore(const char *name, struct symbol *sym, struct lock_info *info)
{
	struct sm_state *sm;

	if (__path_is_null())
		return;

	sm = get_sm_state(my_id, name, sym);
	if (!get_start_state(sm))
		set_start_state(name, sym, &locked);

	add_tracker(&locks, my_id, name, sym);
	set_state(my_id, name, sym, &restore);
}

static int get_db_type(struct sm_state *sm)
{
	/*
	 * Bottom half is complicated because it's nestable.
	 * Say it's merged at the start and we lock and unlock then
	 * it should go back to merged.
	 */
	if (sm->state == get_start_state(sm)) {
		if (sm->state == &locked)
			return KNOWN_LOCKED;
		if (sm->state == &unlocked)
			return KNOWN_UNLOCKED;
	}

	if (sm->state == &locked)
		return LOCK;
	if (sm->state == &unlocked)
		return UNLOCK;
	if (sm->state == &restore)
		return RESTORE;
	return LOCK;
}

static void match_return_info(int return_id, char *return_ranges, struct expression *expr)
{
	struct smatch_state *start;
	struct sm_state *sm;
	const char *param_name;
	int param;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (sm->state != &locked &&
		    sm->state != &unlocked &&
		    sm->state != &restore)
			continue;
		if (sm->name[0] == '$')
			continue;

		/*
		 * If the state is locked at the end, that doesn't mean
		 * anything.  It could be been locked at the start.  Or
		 * it could be &merged at the start but its locked now
		 * because of implications and not because we set the
		 * state.
		 *
		 * This is slightly a hack, but when we change the state, we
		 * call set_start_state() so if get_start_state() returns NULL
		 * that means we haven't manually the locked state.
		 */
		start = get_start_state(sm);
		if (sm->state == &restore) {
			if (start != &locked)
				continue;
		} else if (!start || sm->state == start)
			continue; /* !start means it was passed in */

		param = get_param_key_from_sm(sm, expr, &param_name);
		sql_insert_return_states(return_id, return_ranges,
					 get_db_type(sm),
					 param, param_name, "");
	} END_FOR_EACH_SM(sm);
}

static int sm_both_locked_and_unlocked(struct sm_state *sm)
{
	int is_locked = 0;
	int is_unlocked = 0;
	struct sm_state *tmp;

	if (sm->state != &merged)
		return 0;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state == &locked)
			is_locked = 1;
		if (tmp->state == &unlocked)
			is_unlocked = 1;
	} END_FOR_EACH_PTR(tmp);

	return is_locked && is_unlocked;
}

enum {
	ERR_PTR, VALID_PTR, NEGATIVE, ZERO, POSITIVE, NUM_BUCKETS,
};

static bool is_EINTR(struct range_list *rl)
{
	sval_t sval;

	if (!rl_to_sval(rl, &sval))
		return false;
	return sval.value == -4;
}

static int success_fail_positive(struct range_list *rl)
{
	/* void returns are the same as success (zero in the kernel) */
	if (!rl)
		return ZERO;

	if (rl_type(rl)->type != SYM_PTR &&
	    !is_whole_rl(rl) &&
	    sval_is_negative(rl_min(rl)))
		return NEGATIVE;

	if (rl_min(rl).value == 0 && rl_max(rl).value == 0)
		return ZERO;

	if (is_err_ptr(rl_min(rl)) &&
	    is_err_ptr(rl_max(rl)))
		return ERR_PTR;

	/*
	 * Trying to match ERR_PTR(ret) but without the expression struct.
	 * Ugly...
	 */
	if (type_bits(&long_ctype) == 64 &&
	    rl_type(rl)->type == SYM_PTR &&
	    rl_min(rl).value == INT_MIN)
		return ERR_PTR;

	return POSITIVE;
}

static bool sym_in_lock_table(struct symbol *sym)
{
	int i;

	if (!sym || !sym->ident)
		return false;

	for (i = 0; lock_table[i].function != NULL; i++) {
		if (strcmp(lock_table[i].function, sym->ident->name) == 0)
			return true;
	}
	return false;
}

static bool func_in_lock_table(struct expression *expr)
{
	if (expr->type != EXPR_SYMBOL)
		return false;
	return sym_in_lock_table(expr->symbol);
}

static void check_lock(char *name, struct symbol *sym)
{
	struct range_list *locked_lines = NULL;
	struct range_list *unlocked_lines = NULL;
	int locked_buckets[NUM_BUCKETS] = {};
	int unlocked_buckets[NUM_BUCKETS] = {};
	struct stree *stree, *orig;
	struct sm_state *return_sm;
	struct sm_state *sm;
	sval_t line = sval_type_val(&int_ctype, 0);
	int bucket;
	int i;

	if (sym_in_lock_table(cur_func_sym))
		return;

	FOR_EACH_PTR(get_all_return_strees(), stree) {
		orig = __swap_cur_stree(stree);

		if (is_impossible_path())
			goto swap_stree;

		return_sm = get_sm_state(RETURN_ID, "return_ranges", NULL);
		if (!return_sm)
			goto swap_stree;
		line.value = return_sm->line;

		sm = get_sm_state(my_id, name, sym);
		if (!sm)
			goto swap_stree;

		if (parent_is_gone_var_sym(sm->name, sm->sym))
			goto swap_stree;

		if (sm->state != &locked &&
		    sm->state != &unlocked &&
		    sm->state != &restore)
			goto swap_stree;

		if ((sm->state == &unlocked || sm->state == &restore) &&
		    is_EINTR(estate_rl(return_sm->state)))
			goto swap_stree;

		bucket = success_fail_positive(estate_rl(return_sm->state));
		if (sm->state == &locked) {
			add_range(&locked_lines, line, line);
			locked_buckets[bucket] = true;
		}
		if (sm->state == &unlocked || sm->state == &restore) {
			add_range(&unlocked_lines, line, line);
			unlocked_buckets[bucket] = true;
		}
swap_stree:
		__swap_cur_stree(orig);
	} END_FOR_EACH_PTR(stree);


	if (!locked_lines || !unlocked_lines)
		return;

	for (i = 0; i < NUM_BUCKETS; i++) {
		if (locked_buckets[i] && unlocked_buckets[i])
			goto complain;
	}
	if (locked_buckets[NEGATIVE] &&
	    (unlocked_buckets[ZERO] || unlocked_buckets[POSITIVE]))
		goto complain;

	if (locked_buckets[ERR_PTR])
		goto complain;

	return;

complain:
	sm_msg("warn: inconsistent returns '%s'.", name);
	sm_printf("  Locked on  : %s\n", show_rl(locked_lines));
	sm_printf("  Unlocked on: %s\n", show_rl(unlocked_lines));
}

static void match_func_end(struct symbol *sym)
{
	struct tracker *tracker;

	FOR_EACH_PTR(locks, tracker) {
		check_lock(tracker->name, tracker->sym);
	} END_FOR_EACH_PTR(tracker);
}

static void db_param_locked_unlocked(struct expression *expr, int param, const char *key, int lock_unlock, struct lock_info *info)
{
	struct expression *call, *arg;
	char *name;
	struct symbol *sym;

	if (info && info->action == IGNORE_LOCK)
		return;

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (call->type != EXPR_CALL)
		return;

	if (!info && func_in_lock_table(call->fn))
		return;

	if (param == -2) {
		if (!info)
			name = use_best_match(key, lock_unlock, &sym);
		else {
			name = alloc_string(info->key);
			sym = NULL;
		}
	} else if (param == -1) {
		if (expr->type != EXPR_ASSIGNMENT)
			return;
		ignored_reset = expr->left;

		name = get_variable_from_key(expr->left, key, &sym);
		if (!name || !sym)
			goto free;
	} else {
		arg = get_argument_from_call_expr(call->args, param);
		if (!arg)
			return;

		arg = remove_spinlock_check(arg);
		name = get_variable_from_key(arg, key, &sym);
		if (!name || !sym)
			goto free;
	}

	if (!name || !sym)
		goto free;

	if (lock_unlock == LOCK)
		do_lock(name, sym, info);
	else if (lock_unlock == UNLOCK)
		do_unlock(name, sym, info);
	else if (lock_unlock == RESTORE)
		do_restore(name, sym, info);

free:
	free_string(name);
}

static void db_param_locked(struct expression *expr, int param, char *key, char *value)
{
	db_param_locked_unlocked(expr, param, key, LOCK, NULL);
}

static void db_param_unlocked(struct expression *expr, int param, char *key, char *value)
{
	db_param_locked_unlocked(expr, param, key, UNLOCK, NULL);
}

static void db_param_restore(struct expression *expr, int param, char *key, char *value)
{
	db_param_locked_unlocked(expr, param, key, RESTORE, NULL);
}

static void match_lock_unlock(const char *fn, struct expression *expr, void *data)
{
	struct lock_info *info = data;
	struct expression *parent;

	if (info->arg == -1) {
		parent = expr_get_parent_expr(expr);
		while (parent && parent->type != EXPR_ASSIGNMENT)
			parent = expr_get_parent_expr(parent);
		if (!parent || parent->type != EXPR_ASSIGNMENT)
			return;
		expr = parent;
	}

	db_param_locked_unlocked(expr, info->arg, info->key, info->action, info);
}

static void match_lock_held(const char *fn, struct expression *call_expr,
			    struct expression *assign_expr, void *data)
{
	struct lock_info *info = data;

	db_param_locked_unlocked(assign_expr ?: call_expr, info->arg, info->key, info->action, info);
}

static void match_assign(struct expression *expr)
{
	struct smatch_state *state;

	/* This is only for the DB */
	if (!__in_fake_var_assign)
		return;
	state = get_state_expr(my_id, expr->right);
	if (!state)
		return;
	set_state_expr(my_id, expr->left, state);
}

static struct stree *printed;
static void call_info_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	int locked_type = 0;

	if (sm->state == &locked)
		locked_type = LOCK;
	else if (sm->state == &unlocked)
		locked_type = UNLOCK;
	else if (slist_has_state(sm->possible, &locked) ||
		 slist_has_state(sm->possible, &half_locked))
		locked_type = HALF_LOCKED;
	else
		return;

	avl_insert(&printed, sm);
	sql_insert_caller_info(call, locked_type, param, printed_name, "");
}

static void match_call_info(struct expression *expr)
{
	struct sm_state *sm;
	const char *name;
	int locked_type;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (sm->state == &locked)
			locked_type = LOCK;
		else if (sm->state == &half_locked ||
			 slist_has_state(sm->possible, &locked))
			locked_type = HALF_LOCKED;
		else
			continue;

		if (avl_lookup(printed, sm))
			continue;

		if (strcmp(sm->name, "bottom_half") == 0)
			name = "bh";
		else if (strcmp(sm->name, "rcu_read") == 0)
			name = "rcu_read_lock";
		else
			name = sm->name;

		sql_insert_caller_info(expr, locked_type, -2, name, "");
	} END_FOR_EACH_SM(sm);
	free_stree(&printed);
}

static void set_locked_called_state(const char *name, struct symbol *sym,
				    char *key, char *value,
				    struct smatch_state *state)
{
	char fullname[256];

	if (name && key[0] == '$')
		snprintf(fullname, sizeof(fullname), "%s%s", name, key + 1);
	else
		snprintf(fullname, sizeof(fullname), "%s", key);

	if (strstr(fullname, ">>") || strstr(fullname, "..")) {
//		sm_msg("warn: invalid lock name.  name = '%s' key = '%s'", name, key);
		return;
	}

	set_state(my_id, fullname, sym, state);
}

static void set_locked(const char *name, struct symbol *sym, char *key, char *value)
{
	set_locked_called_state(name, sym, key, value, &locked);
}

static void set_half_locked(const char *name, struct symbol *sym, char *key, char *value)
{
	set_locked_called_state(name, sym, key, value, &half_locked);
}

static void set_unlocked(const char *name, struct symbol *sym, char *key, char *value)
{
	set_locked_called_state(name, sym, key, value, &unlocked);
}

static void match_after_func(struct symbol *sym)
{
	free_stree(&start_states);
}

static void match_dma_resv_lock_NULL(const char *fn, struct expression *call_expr,
				     struct expression *assign_expr, void *_index)
{
	struct expression *lock, *ctx;
	char *lock_name;
	struct symbol *sym;

	lock = get_argument_from_call_expr(call_expr->args, 0);
	ctx = get_argument_from_call_expr(call_expr->args, 1);
	if (!expr_is_zero(ctx))
		return;

	lock_name = lock_to_name_sym(lock, &sym);
	if (!lock_name || !sym)
		goto free;
	do_lock(lock_name, sym, NULL);
free:
	free_string(lock_name);
}

/* print_held_locks() is used in check_call_tree.c */
void print_held_locks(void)
{
	struct stree *stree;
	struct sm_state *sm;
	int i = 0;

	stree = __get_cur_stree();
	FOR_EACH_MY_SM(my_id, stree, sm) {
		if (sm->state != &locked)
			continue;
		if (i++)
			sm_printf(" ");
		sm_printf("'%s'", sm->name);
	} END_FOR_EACH_SM(sm);
}

static void load_table(struct lock_info *lock_table)
{
	int i;

	for (i = 0; lock_table[i].function != NULL; i++) {
		struct lock_info *lock = &lock_table[i];

		if (lock->call_back) {
			add_function_hook(lock->function, lock->call_back, lock);
		} else if (lock->implies_start) {
			return_implies_state_sval(lock->function,
					*lock->implies_start,
					*lock->implies_end,
					&match_lock_held, lock);
		} else {
			add_function_hook(lock->function, &match_lock_unlock, lock);
		}
	}
}

static bool is_smp_config(void)
{
	struct ident *id;

	id = built_in_ident("CONFIG_SMP");
	return !!lookup_symbol(id, NS_MACRO);
}

void check_locking(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	if (!is_smp_config())
		return;

	load_table(lock_table);

	set_dynamic_states(my_id);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_pre_merge_hook(my_id, &pre_merge_hook);
	add_merge_hook(my_id, &merge_func);
	add_modification_hook(my_id, &reset);

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_func_end, END_FUNC_HOOK);

	add_hook(&match_after_func, AFTER_FUNC_HOOK);
	add_function_data((unsigned long *)&start_states);

	add_caller_info_callback(my_id, call_info_callback);
	add_hook(&match_call_info, FUNCTION_CALL_HOOK);

	add_split_return_callback(match_return_info);
	select_return_states_hook(LOCK, &db_param_locked);
	select_return_states_hook(UNLOCK, &db_param_unlocked);
	select_return_states_hook(RESTORE, &db_param_restore);

	select_caller_info_hook(set_locked, LOCK);
	select_caller_info_hook(set_half_locked, HALF_LOCKED);
	select_caller_info_hook(set_unlocked, UNLOCK);

	return_implies_state("dma_resv_lock", -4095, -1, &match_dma_resv_lock_NULL, 0);
}
