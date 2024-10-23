/*
 * Copyright (C) 2009 Dan Carpenter.
 * Copyright (C) 2019 Oracle.
 * Copyright 2024 Linaro Ltd.
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

STATE(lock);
STATE(unlock);
STATE(restore);
STATE(fail);
STATE(destroy);

#define RETURN_VAL -1
#define NO_ARG -2
#define FAIL -3
#define IGNORE 9999

static void match_class_device_destructor(const char *fn, struct expression *expr, void *data);
static void match_class_destructor(const char *fn, struct expression *expr, void *data);

#define irq lock_irq
#define sem lock_sem
#define rcu lock_rcu
#define rcu_read lock_rcu_read

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

	{"spin_trylock",               LOCK,   spin_lock, 0, "$", &int_one, &int_one},
	{"_spin_trylock",              LOCK,   spin_lock, 0, "$", &int_one, &int_one},
	{"__spin_trylock",             LOCK,   spin_lock, 0, "$", &int_one, &int_one},
	{"raw_spin_trylock",           LOCK,   spin_lock, 0, "$", &int_one, &int_one},
	{"_raw_spin_trylock",          LOCK,   spin_lock, 0, "$", &int_one, &int_one},
	{"spin_trylock_irq",           LOCK,   spin_lock, 0, "$", &int_one, &int_one},
	{"spin_trylock_irqsave",       LOCK,   spin_lock, 0, "$", &int_one, &int_one},
	{"spin_trylock_bh",            LOCK,   spin_lock, 0, "$", &int_one, &int_one},
	{"_spin_trylock_bh",           LOCK,   spin_lock, 0, "$", &int_one, &int_one},
	{"__spin_trylock_bh",          LOCK,   spin_lock, 0, "$", &int_one, &int_one},
	{"__raw_spin_trylock",         LOCK,   spin_lock, 0, "$", &int_one, &int_one},

	{"spin_trylock",               FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"_spin_trylock",              FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"__spin_trylock",             FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"raw_spin_trylock",           FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"_raw_spin_trylock",          FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"spin_trylock_irq",           FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"spin_trylock_irqsave",       FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"spin_trylock_bh",            FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"_spin_trylock_bh",           FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"__spin_trylock_bh",          FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"__raw_spin_trylock",         FAIL,   spin_lock, 0, "$", &int_zero, &int_zero},
	{"spin_trylock_irq",           FAIL,   spin_lock, NO_ARG, "irq", &int_zero, &int_zero},
	{"spin_trylock_irqsave",       FAIL,   spin_lock, 1, "$", &int_zero, &int_zero},


	{"_atomic_dec_and_lock",       LOCK,   spin_lock, 1, "$", &int_one, &int_one},

	{"read_lock",                 LOCK,   read_lock, 0, "$"},
	{"down_read",                 LOCK,   read_lock, 0, "$"},
	{"down_read_nested",          LOCK,   read_lock, 0, "$"},
	{"down_read_trylock",         LOCK,   read_lock, 0, "$", &int_one, &int_one},
	{"down_read_killable",        LOCK,   read_lock, 0, "$", &int_zero, &int_zero},
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
	{"__raw_write_unlock_irq",	  UNLOCK,  write_lock,  0,          "$"},
	{"__raw_write_unlock_irq",	  UNLOCK,  irq,  	0,          "irq"},
	{"__raw_write_unlock_irqrestore", UNLOCK,  write_lock,  0,          "$"},
	{"__raw_write_unlock_irqrestore", RESTORE, irq,         1,          "$"},

	{"generic__raw_read_trylock", LOCK,   read_lock, 0, "$", &int_one, &int_one},
	{"read_trylock",              LOCK,   read_lock, 0, "$", &int_one, &int_one},
	{"_read_trylock",             LOCK,   read_lock, 0, "$", &int_one, &int_one},
	{"raw_read_trylock",          LOCK,   read_lock, 0, "$", &int_one, &int_one},
	{"_raw_read_trylock",         LOCK,   read_lock, 0, "$", &int_one, &int_one},
	{"__raw_read_trylock",        LOCK,   read_lock, 0, "$", &int_one, &int_one},
	{"__read_trylock",            LOCK,   read_lock, 0, "$", &int_one, &int_one},

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

	{"write_trylock",             LOCK,   write_lock, 0, "$", &int_one, &int_one},
	{"_write_trylock",            LOCK,   write_lock, 0, "$", &int_one, &int_one},
	{"raw_write_trylock",         LOCK,   write_lock, 0, "$", &int_one, &int_one},
	{"_raw_write_trylock",        LOCK,   write_lock, 0, "$", &int_one, &int_one},
	{"__write_trylock",           LOCK,   write_lock, 0, "$", &int_one, &int_one},
	{"__raw_write_trylock",       LOCK,   write_lock, 0, "$", &int_one, &int_one},
	{"down_write_trylock",        LOCK,   write_lock, 0, "$", &int_one, &int_one},
	{"down_write_killable",       LOCK,   write_lock, 0, "$", &int_zero, &int_zero},

	{"down",               LOCK,   sem, 0, "$"},
	{"up",                 UNLOCK, sem, 0, "$"},
	{"down_trylock",       LOCK,   sem, 0, "$", &int_zero, &int_zero},
	{"down_timeout",       LOCK,   sem, 0, "$", &int_zero, &int_zero},
	{"down_interruptible", LOCK,   sem, 0, "$", &int_zero, &int_zero},
	{"down_killable",      LOCK,   sem, 0, "$", &int_zero, &int_zero},


	{"mutex_lock",                      LOCK,   mutex, 0, "$"},
	{"mutex_unlock",                    UNLOCK, mutex, 0, "$"},
	{"mutex_destroy",                   DESTROY_LOCK, mutex, 0, "$"},
	{"mutex_lock_nested",               LOCK,   mutex, 0, "$"},
	{"mutex_lock_io",                   LOCK,   mutex, 0, "$"},
	{"mutex_lock_io_nested",            LOCK,   mutex, 0, "$"},

	{"mutex_lock_interruptible",        LOCK,   mutex, 0, "$", &int_zero, &int_zero},
	{"mutex_lock_interruptible_nested", LOCK,   mutex, 0, "$", &int_zero, &int_zero},
	{"mutex_lock_killable",             LOCK,   mutex, 0, "$", &int_zero, &int_zero},
	{"mutex_lock_killable_nested",      LOCK,   mutex, 0, "$", &int_zero, &int_zero},

	{"mutex_trylock",                   LOCK,   mutex, 0, "$", &int_one, &int_one},
	{"mutex_trylock",                   FAIL,   mutex, 0, "$", &int_zero, &int_zero},

	{"ww_mutex_lock",		LOCK,   mutex, 0, "$"},
	{"__ww_mutex_lock",		LOCK,   mutex, 0, "$"},
	{"ww_mutex_lock_interruptible",	LOCK,   mutex, 0, "$", &int_zero, &int_zero},
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
	{"spin_trylock_irq",      LOCK,   irq, NO_ARG, "irq", &int_one, &int_one},
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
	{"spin_trylock_irqsave",       LOCK,      irq, 1,	   "$", &int_one, &int_one},
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
	{"spin_trylock_bh",     LOCK,   bottom_half, NO_ARG, "bh", &int_one, &int_one},
	{"_spin_trylock_bh",    LOCK,   bottom_half, NO_ARG, "bh", &int_one, &int_one},
	{"__spin_trylock_bh",   LOCK,   bottom_half, NO_ARG, "bh", &int_one, &int_one},

	{"ffs_mutex_lock",      LOCK,   mutex, 0, "$", &int_zero, &int_zero},

	{"clk_prepare_lock",    LOCK,   prepare_lock, NO_ARG, "clk"},
	{"clk_prepare_unlock",  UNLOCK, prepare_lock, NO_ARG, "clk"},
	{"clk_enable_lock",     LOCK,   enable_lock, -1, "$"},
	{"clk_enable_unlock",   UNLOCK, enable_lock,  0, "$"},

	{"dma_resv_lock",	        LOCK,   mutex, 0, "$", &int_zero, &int_zero},
	{"dma_resv_trylock",	        LOCK,	mutex, 0, "$", &int_one, &int_one},
	{"dma_resv_lock_interruptible", LOCK,	mutex, 0, "$", &int_zero, &int_zero},
	{"dma_resv_unlock",		UNLOCK, mutex, 0, "$"},

	{"modeset_lock",			  LOCK,   mutex, 0, "$", &int_zero, &int_zero},
	{"drm_ modeset_lock",			  LOCK,   mutex, 0, "$", &int_zero, &int_zero},
	{"drm_modeset_lock_single_interruptible", LOCK,   mutex, 0, "$", &int_zero, &int_zero},
	{"drm_exec_unlock_obj",			  UNLOCK, mutex, 1, "$->resv" },
	{"modeset_unlock",			  UNLOCK, mutex, 0, "$"},
//	{"nvkm_i2c_aux_acquire",		  LOCK,   mutex, 
	{"i915_gem_object_lock_interruptible",	  LOCK,	  mutex, 0, "$->base.resv", &int_zero, &int_zero},
	{"i915_gem_object_lock",		LOCK, mutex, 0, "$->base.resv", &int_zero, &int_zero},
	{"msm_gem_lock",		LOCK, mutex, 0, "$->resv"},

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

	{"rtnl_lock",			LOCK,   mutex, NO_ARG, "rtnl_lock"},
	{"rtnl_unlock",			UNLOCK, mutex, NO_ARG, "rtnl_lock"},

	{"gfs2_trans_begin", LOCK, sem, 0, "&$->sd_log_flush_lock", &int_zero, &int_zero},

	{"lock_sock",        LOCK,   spin_lock, 0, "$"},
	{"lock_sock_nested", LOCK,   spin_lock, 0, "$"},
	{"lock_sock_fast",   LOCK,   spin_lock, 0, "$"},
	{"__lock_sock",      LOCK,   spin_lock, 0, "$"},
	{"release_sock",     UNLOCK, spin_lock, 0, "$"},
	{"__release_sock",   UNLOCK, spin_lock, 0, "$"},
	{"chtls_pt_recvmsg", UNLOCK, spin_lock, 0, "$"},

	{"lock_task_sighand", LOCK,  spin_lock, 0, "&$->sighand->siglock", &valid_ptr_min_sval, &valid_ptr_max_sval},

	{"rcu_nocb_unlock_irqrestore", RESTORE, spin_lock, 0, "&$->nocb_lock"},
	{"rcu_nocb_unlock_irqrestore", RESTORE, irq, 1, "$" },

	{"bch_write_bdev_super",	IGNORE_LOCK, sem, 0, "&$->sb_write_mutex"},
	{"bcache_write_super",		IGNORE_LOCK, sem, 0, "&$->set->sb_write_mutex"},
	{"uuid_io",			IGNORE_LOCK, sem, 0, "&$->uuid_write_mutex" },
	{"dlfb_set_video_mode",		IGNORE_LOCK, sem, 0, "&$->urbs.limit_sem"},

	{"efx_rwsem_assert_write_locked", IGNORE_LOCK, sem, 0, "&"},

	// The i915_gem_ww_ctx_unlock_all() is too complicated
	{"i915_gem_object_pin_pages_unlocked", IGNORE_LOCK, mutex, 0, "$->base.resv"},
	{"i915_gem_object_pin_map_unlocked", IGNORE_LOCK, mutex, 0, "$->base.resv"},
	{"i915_gem_object_fill_blt", IGNORE_LOCK, mutex, 0, "$->base.resv"},
	{"i915_vma_pin", IGNORE_LOCK, mutex, 0, "$->base.resv"},

	{ "perf_event_period", IGNORE_LOCK, mutex, 0, "&$->ctx->mutex"},
	{ "perf_event_enable", IGNORE_LOCK, mutex, 0, "&$->ctx->mutex"},

	{ "qede_load", IGNORE_LOCK, mutex, 0, "&$->qede_lock" },
	{ "qede_unload", IGNORE_LOCK, mutex, 0, "&$->qede_lock" },

	{ "deactivate_locked_super", UNLOCK, spin_lock, 0, "&$->s_umount"},
	{ "ext4_lock_group", LOCK,	spin_lock, 0, "$"},
	{ "ext4_unlock_group", UNLOCK,	spin_lock, 0, "$"},

	{"__pte_offset_map_lock", LOCK, spin_lock, 3, "*$", &valid_ptr_min_sval, &valid_ptr_max_sval},
	{"pte_offset_map_lock", LOCK, spin_lock, 3, "*$", &valid_ptr_min_sval, &valid_ptr_max_sval},

	{"uart_unlock_and_check_sysrq_irqrestore", UNLOCK, spin_lock, 0, "&$->lock"},

	{"mt7530_mutex_lock",	LOCK,	mutex, 0, "&$->bus->mdio_lock"},
	{"mt7530_mutex_unlock",	UNLOCK,	mutex, 0, "&$->bus->mdio_lock"},

	{"class_device_destructor", UNLOCK, mutex, 0, "*$", NULL, NULL, &match_class_device_destructor},
	{"class_mutex_destructor", UNLOCK, mutex, 0, "*$", NULL, NULL, &match_class_destructor},
	{"class_rwsem_write_destructor", UNLOCK, sem, 0, "*$", NULL, NULL, &match_class_destructor},
	{"class_rwsem_read_destructor", UNLOCK, sem, 0, "*$", NULL, NULL, &match_class_destructor},
	{"class_spinlock_constructor", LOCK, spin_lock, 0, "$"},
	{"class_spinlock_destructor", UNLOCK, spin_lock, 0, "*$", NULL, NULL, &match_class_destructor},
	{"class_spinlock_irq_constructor", LOCK, spin_lock, 0, "$"},
	{"class_spinlock_irq_constructor", LOCK, irq, -2, "irq"},
	{"class_spinlock_irq_destructor", UNLOCK, spin_lock, 0, "*$", NULL, NULL, &match_class_destructor},
	{"class_spinlock_irq_destructor", UNLOCK, irq, -2, "irq"},

	{"class_mvm_destructor", UNLOCK, mutex, 0, "&$->mutex"},

	{"lock_cluster_or_swap_info",   LOCK,   spin_lock, 0, "&$->lock"},
	{"unlock_cluster_or_swap_info", UNLOCK, spin_lock, 0, "&$->lock"},

	{"bch2_trans_relock_fail",	LOCK,   mutex, 0, "$" },
	{"bch2_trans_relock",	LOCK,   mutex, 0, "$" },
	{"__bch2_trans_relock",	LOCK,   mutex, 0, "$" },
	{"__bch2_trans_mutex_lock",	LOCK,   mutex, 0, "$" },
	{"bch2_trans_relock_notrace",	LOCK,   mutex, 0, "$" },
	{"bch2_trans_unlock",	UNLOCK, mutex, 0, "$" },
	{"__bch2_trans_unlock",	UNLOCK, mutex, 0, "$" },
	{"bch2_trans_unlock_noassert",	UNLOCK,   mutex, 0, "$" },

	{"xen_mc_batch", LOCK, irq, -2, "irq"},
	{"xen_mc_issue", UNLOCK, irq, -2, "irq"},

	{"blkg_conf_exit", CLEAR_LOCK,   irq, -2, "irq" },
	{"__console_flush_and_unlock", UNLOCK, sem, -2, "&console_sem"},

	{"ipmi_ssif_lock_cond", LOCK, irq, -1, "*$"},
	{"ipmi_ssif_lock_cond", LOCK, spin_lock, 0, "&$->lock"},

	{"follow_pfnmap_start", LOCK, spin_lock, 0, "&$->lock", &int_zero, &int_zero},
	{"follow_pfnmap_end", UNLOCK, spin_lock, 0, "&$->lock"},

	{"hid_device_io_start", UNLOCK, sem, 0, "&$->driver_input_lock"},
	{"hid_device_io_stop",  LOCK,   sem, 0, "&$->driver_input_lock"},
	{"srcu_read_lock",	LOCK,	rcu, 0, "$"},
	{"srcu_read_unlock",	UNLOCK,	rcu, 0, "$"},

	{"snd_gf1_mem_lock", IGNORE, mutex, 0, "&$->memory_mutex"},
	{"emit_rpcs_query",  IGNORE, mutex, -2, ""},

	{"switch_tl_lock", UNLOCK, mutex, 0, "$->context->timeline->mutex"},
	{"switch_tl_lock", LOCK, mutex, 1, "$->context->timeline->mutex"},

	{"console_unlock", UNLOCK, sem, -2, "global &console_sem"},
	{"bus_mutex_lock", LOCK, mutex, 0, "$", &int_one, &int_one},

	{},
};

static struct locking_hook_list *lock_hooks, *unlock_hooks, *restore_hooks, *clear_hooks, *destroy_hooks;

void add_lock_hook(locking_hook *hook)
{
	add_ptr_list(&lock_hooks, hook);
}

void add_unlock_hook(locking_hook *hook)
{
	add_ptr_list(&unlock_hooks, hook);
}

void add_restore_hook(locking_hook *hook)
{
	add_ptr_list(&restore_hooks, hook);
}

void add_clear_hook(locking_hook *hook)
{
	add_ptr_list(&clear_hooks, hook);
}

void add_destroy_hook(locking_hook *hook)
{
	add_ptr_list(&destroy_hooks, hook);
}

static struct expression *locking_call;
struct expression *get_locking_call(void)
{
	return locking_call;
}

bool is_locking_primitive(const char *name)
{
	int i;

	for (i = 0; lock_table[i].function != NULL; i++) {
		if (strcmp(lock_table[i].function, name) == 0)
			return true;
	}
	return false;
}

bool is_locking_primitive_sym(struct symbol *sym)
{
	if (!sym || !sym->ident)
		return false;

	return is_locking_primitive(sym->ident->name);
}

bool is_locking_primitive_expr(struct expression *expr)
{
	if (!expr ||
	    expr->type != EXPR_SYMBOL)
		return false;

	return is_locking_primitive_sym(expr->symbol);
}

static void call_locking_hooks(struct locking_hook_list *list, struct expression *call, struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	locking_hook *hook;

	locking_call = call;
	FOR_EACH_PTR(list, hook) {
		(hook)(info, expr, name, sym);
	} END_FOR_EACH_PTR(hook);
	locking_call = NULL;
}

static struct smatch_state *get_opposite(struct smatch_state *state)
{
	if (state == &lock)
		return &unlock;
	if (state == &unlock || state == &restore)
		return &lock;
	if (state == &fail)
		return &unlock;
	return NULL;
}

static struct stree *start_states;

static void set_start_state(const char *name, struct symbol *sym, struct smatch_state *start)
{
	struct smatch_state *orig;

	/*
	 * The smatch_locking_info.c module records if a lock is held or not.
	 * This module only records transitions so we can insert transitions
	 * into the return_states table.
	 *
	 * I didn't want to record startstates, I wanted this to work like
	 * preempt where we only look at simple functions and say that they
	 * enable preempt or not.  Everything complicated would be hand edited.
	 * With preempt I have deliberately chosen to miss some bugs rather than
	 * generating a lot of false positives.  So far as I can see the preempt
	 * code works really well and I'm happy with it.
	 *
	 * However that approach doesn't work for locking.
	 *
	 * The problem is that for locking we have functions that take a lock,
	 * drop it, then take it again and actually we need to track that.  For
	 * preempt if we ignore it, fine, we will miss some bugs.  For preempt,
	 * Smatch only prints a warning about sleeping with preempt disabled.
	 * But for locking we warn about double locking, double unlocking,
	 * returning with inconsistently held locks, calling a function without
	 * holding a lock or calling a function while holding a lock.  There is
	 * a higher level of precision required.
	 *
	 * This module only records changes.  The common case is that we take a
	 * lock and then drop it.  There is no transition.  We started unlocked
	 * and ended unlocked.
	 *
	 * A tricker case might be code that does:
	 *
	 * void frob(bool lock)
	 * {
	 *	if (lock)
	 *		unlock();
	 *	else
	 *		my_unlock();
	 * }
	 *
	 * In that situation we don't want to record anything in the database.
	 * It's too complicated.
	 *
	 * This function works completely differently from how check_locks.c
	 * did it, but I think check_locks.c looks buggy.  I think the old code
	 * was basically working but it was just layer upon layer of hacks and
	 * special cases.  It also had the &start_state thing which was weird
	 * and seems unnecessary?
	 *
	 * Anyway, we'll see how well this cleaner approach works in practice.
	 * So far my first theory of doing this like preempt does was wrong so
	 * it wouldn't be the first time I was wrong.
	 *
	 */

	if (!name || !start)
		return;

	if (get_state(my_id, name, sym))
		return;

	orig = get_state_stree(start_states, my_id, name, sym);
	if (!orig)
		set_state_stree(&start_states, my_id, name, sym, start);
	else if (orig != start)
		set_state_stree(&start_states, my_id, name, sym, &undefined);
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

static void match_after_func(struct symbol *sym)
{
	free_stree(&start_states);
}

static void update_state(struct expression *expr, const char *name, struct symbol *sym, struct smatch_state *state)
{
	struct smatch_state *opposite = get_opposite(state);
	struct smatch_state *orig;
	char *param_name;
	struct symbol *param_sym;

	/* should this be done in db_param_locked_unlocked()? */
	param_name = get_param_var_sym_var_sym(name, sym, NULL, &param_sym);
	if (param_name && param_sym && get_param_num_from_sym(param_sym) >= 0) {
		name = param_name;
		sym = param_sym;
	}

	set_start_state(name, sym, opposite);

	if (state == &undefined || state == &destroy) {
		set_state(my_id, name, sym, state);
		return;
	}

	orig = get_state(my_id, name, sym);
	if (orig == &fail && state == &lock) {
		set_state(my_id, name, sym, &lock);
		return;
	}
	if (orig && orig != opposite) {
		set_state(my_id, name, sym, &undefined);
		return;
	}

	set_state(my_id, name, sym, state);
}

static void do_lock(struct expression *call, struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	call_locking_hooks(lock_hooks, call, info, expr, name, sym);
	update_state(expr, name, sym, &lock);
}

static void do_unlock(struct expression *call, struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	call_locking_hooks(unlock_hooks, call, info, expr, name, sym);
	update_state(expr, name, sym, &unlock);
}

static void do_restore(struct expression *call, struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	call_locking_hooks(restore_hooks, call, info, expr, name, sym);
	update_state(expr, name, sym, &restore);
}

static void do_clear(struct expression *call, struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	call_locking_hooks(clear_hooks, call, info, expr, name, sym);
	update_state(expr, name, sym, &undefined);
}

static void do_destroy(struct expression *call, struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	call_locking_hooks(destroy_hooks, call, info, expr, name, sym);
	update_state(expr, name, sym, &destroy);
}

static void do_fail(struct expression *call, struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	update_state(expr, name, sym, &fail);
}

static void swap_global_names(const char **p_name, struct symbol **p_sym)
{
	struct symbol *sym = *p_sym;
	char buf[64];

	if (!sym)
		return;
	if (!(sym->ctype.modifiers & MOD_TOPLEVEL))
		return;

	snprintf(buf, sizeof(buf), "global %s", *p_name);
	*p_name = alloc_sname(buf);
	*p_sym = NULL;
}

static struct expression *get_constructor_arg(struct expression *expr)
{
	struct expression *arg, *lock;

	/*
	 * What happens here is that the code looks like:
	 * class_mutex_t scope __attribute__((__cleanup__(class_mutex_destructor))) =
	 *				class_mutex_constructor(&register_mutex);
	 * Then here in this hook functions expr is set to
	 * "class_mutex_destructor(&scope)".  So we need to take "&scope" and
	 * trace it back to "&register_mutex".  I think there is a complication
	 * as well because sometimes it's like the assignment is:
	 *
	 *     scope = class_mutex_constructor(&register_mutex);
	 * but other times because we do a fake parameter assignment or
	 * something the assignment is more direct:
	 *     scope = &register_mutex;
	 *
	 */

	if (!expr || expr->type != EXPR_CALL)
		return NULL;
	arg = get_argument_from_call_expr(expr->args, 0);
	arg = strip_expr(arg);
	if (!arg || arg->type != EXPR_PREOP || arg->op != '&')
		return NULL;
	arg = strip_expr(arg->unop);
	lock = get_assigned_expr(arg);
	if (!lock)
		return NULL;
	if (lock->type == EXPR_CALL)
		lock = get_argument_from_call_expr(lock->args, 0);
	lock = strip_expr(lock);
	if (!lock || lock->type != EXPR_PREOP || lock->op != '&')
		return NULL;

	return lock;
}

static void match_class_device_destructor(const char *fn, struct expression *expr, void *data)
{
	struct expression *lock;
	struct symbol *sym;
	const char *name;
	char buf[64];

	lock = get_constructor_arg(expr);
	if (!lock)
		return;

	name = expr_to_str_sym(lock, &sym);
	snprintf(buf, sizeof(buf), "%s.mutex", name);
	lock = gen_expression_from_name_sym(name, sym);
	name = alloc_sname(buf);

	swap_global_names(&name, &sym);
	do_unlock(expr, NULL, lock, name, sym);
}

static void match_class_destructor(const char *fn, struct expression *expr, void *data)
{
	struct expression *lock;
	struct symbol *sym;
	const char *name;

	lock = get_constructor_arg(expr);
	if (!lock)
		return;

	name = expr_to_str_sym(lock, &sym);

	swap_global_names(&name, &sym);
	do_unlock(expr, NULL, lock, name, sym);
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

static struct expression *remove_XAS_INVALID(struct expression *expr)
{
	struct expression *orig = expr;
	struct expression *deref_one, *deref_two, *ret;

	if (expr->type != EXPR_PREOP || expr->op != '&')
		return orig;
	expr = strip_expr(expr->unop);
	if (expr->type != EXPR_DEREF)
		return orig;
	deref_one = expr;
	expr = strip_expr(expr->unop);
	if (expr->type != EXPR_PREOP || expr->op != '*')
		return orig;
	expr = strip_expr(expr->unop);
	if (expr->type != EXPR_DEREF)
		return orig;
	deref_two = expr;
	expr = strip_expr(expr->unop);
	if (expr->type != EXPR_PREOP || expr->op != '*')
		return orig;
	expr = strip_expr(expr->unop);
	if (expr->type != EXPR_CALL)
		return orig;
	if (!sym_name_is("XAS_INVALID", expr->fn))
		return orig;

	ret = get_argument_from_call_expr(expr->args, 0);
	ret = preop_expression(ret, '(');
	ret = member_expression(ret, '*', deref_two->member);
	ret = preop_expression(ret, '(');
	ret = member_expression(ret, '*', deref_one->member);
	ret = preop_expression(ret, '&');

	return ret;
}

static struct expression *ignored_modify;
static void clear_state(struct sm_state *sm, struct expression *mod_expr)
{
	struct expression *faked;

	if (mod_expr && mod_expr->type == EXPR_ASSIGNMENT &&
	    mod_expr->left == ignored_modify)
		return;
	faked = get_faked_expression();
	if (faked && faked->type == EXPR_ASSIGNMENT &&
	    faked->left == ignored_modify)
		return;

	do_clear(NULL, NULL, NULL, sm->name, sm->sym);
}

static void db_param_locked_unlocked(struct expression *expr, int param, const char *key, int lock_unlock, struct lock_info *info)
{
	struct expression *call, *arg, *lock = NULL;
	const char *name = NULL;
	struct symbol *sym = NULL;

	if (info && info->action == IGNORE_LOCK)
		return;

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (call->type != EXPR_CALL)
		return;

	if (!info && is_locking_primitive_expr(call->fn))
		return;

	if (param == -2) {
		if (info)
			name = info->key;
		else
			name = key;
	} else if (param == -1) {
		ignored_modify = expr->left;
		// Oct 3, 2024: Ugh... I wanted to do:
		// lock = gen_expression_from_key(expr->left, key);
		// But apparently that doesn't work when the key is "&$->lock"
		name = get_variable_from_key(expr->left, key, &sym);
		lock = gen_expression_from_name_sym(name, sym);
	} else {
		arg = get_argument_from_call_expr(call->args, param);
		if (!arg)
			return;

		// FIXME: handle idpf_vport_ctrl_lock()

		arg = remove_spinlock_check(arg);
		arg = remove_XAS_INVALID(arg);
		name = get_variable_from_key(arg, key, &sym);
		if (!name || !sym)
			name = key;
		lock = gen_expression_from_name_sym(name, sym);
	}

//	if (!name)
//		sm_msg("%s: no_name expr='%s' param=%d key=%s lock='%s'", __func__, expr_to_str(expr), param, key, expr_to_str(lock));

	swap_global_names(&name, &sym);

	if (local_debug)
		sm_msg("%s: expr='%s' lock='%s' key='%s' name='%s' lock_unlock=%d", __func__, expr_to_str(expr), expr_to_str(lock), key, name, lock_unlock);

	if (lock_unlock == LOCK)
		do_lock(call, info, lock, name, sym);
	else if (lock_unlock == UNLOCK)
		do_unlock(call, info, lock, name, sym);
	else if (lock_unlock == RESTORE)
		do_restore(call, info, lock, name, sym);
	else if (lock_unlock == CLEAR_LOCK)
		do_clear(call, info, lock, name, sym);
	else if (lock_unlock == DESTROY_LOCK)
		do_destroy(call, info, lock, name, sym);
	else if (lock_unlock == FAIL)
		do_fail(call, info, lock, name, sym);
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

static void db_param_destroy(struct expression *expr, int param, char *key, char *value)
{
	db_param_locked_unlocked(expr, param, key, DESTROY_LOCK, NULL);
}

static void match_lock_unlock(const char *fn, struct expression *expr, void *data)
{
	struct lock_info *info = data;
	struct expression *parent;

	if (info->type == IGNORE)
		return;

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

static bool is_mismatched_lock(void)
{
	struct smatch_state *one = NULL;
	struct smatch_state *two = NULL;
	struct sm_state *sm;
	int cnt;

	cnt = 0;
	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		++cnt;
		if (cnt == 1)
			one = sm->state;
		else if (cnt == 2)
			two = sm->state;
		else
			return false;
	} END_FOR_EACH_SM(sm);

	if (cnt != 2)
		return false;

	/* If we have two states and the are opposites and the
	 * smatch_locking_type.c module has one states which matches the
	 * start state, then we are mismatched.
	 */
	if (one != get_opposite(two))
		return false;
	return locking_type_is_start_state();
}

static int get_db_type(struct sm_state *sm)
{
	if (sm->state == &lock)
		return LOCK2;
	if (sm->state == &unlock)
		return UNLOCK2;
	if (sm->state == &restore)
		return RESTORE2;
	if (sm->state == &destroy)
		return DESTROY_LOCK;

	return -1;
}

static bool is_clean_transition(struct sm_state *sm)
{
	struct smatch_state *start;

	if (sm->state == &destroy)
		return true;

	start = get_start_state(sm);
	if (!start)
		return false;
	if (start == get_opposite(sm->state))
		return true;
	return false;
}

static bool is_irq_save(struct sm_state *sm)
{
	// FIXME: The *flags parameter is set and it's locked...
	// Basing this off the name is such a terrible thing and I feel
	// bad, but I'm in a rush.  Sorry!
	if (sm->state != &lock)
		return false;
	if (!strstr(sm->name, "flags"))
		return false;
	return true;
}

static void match_return_info(int return_id, char *return_ranges, struct expression *expr)
{
	const char *param_name;
	struct sm_state *sm;
	int param, type;

	if (is_mismatched_lock())
		return;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		type = get_db_type(sm);
		if (local_debug)
			sm_msg("%s: type=%d sm='%s'", __func__, type, show_sm(sm));
		if (type == -1)
			continue;

		if (!is_clean_transition(sm))
			continue;

		param = get_param_key_from_sm(sm, expr, &param_name);
		if (!is_irq_save(sm) &&
		    param >= 0 &&
		    param_was_set_var_sym(sm->name, sm->sym))
			continue;

		sql_insert_return_states(return_id, return_ranges, type,
					 param, param_name, "");
	} END_FOR_EACH_SM(sm);
}

static bool cull_null_ctx_failures(struct expression *expr, struct range_list *rl, void *unused)
{
	struct expression *ctx;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return false;

	ctx = get_argument_from_call_expr(expr->args, 1);
	if (!expr_is_zero(ctx))
		return false;

	if (!possibly_true_rl(rl, SPECIAL_EQUAL, alloc_rl(int_zero, int_zero)))
		return true;
	return false;
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

void register_locking(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;
	if (!is_smp_config())
		return;

	add_function_data((unsigned long *)&start_states);
	add_function_data((unsigned long *)&ignored_modify);
	add_hook(&match_after_func, AFTER_FUNC_HOOK);

	load_table(lock_table);
	set_dynamic_states(my_id);
	add_modification_hook(my_id, &clear_state);

	select_return_states_hook(LOCK2, &db_param_locked);
	select_return_states_hook(UNLOCK2, &db_param_unlocked);
	select_return_states_hook(RESTORE2, &db_param_restore);
	select_return_states_hook(DESTROY_LOCK, &db_param_destroy);
	add_split_return_callback(match_return_info);

	add_cull_hook("dma_resv_lock", &cull_null_ctx_failures, NULL);
	add_cull_hook("i915_gem_object_lock", &cull_null_ctx_failures, NULL);
}
