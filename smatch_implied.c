/*
 * sparse/smatch_implied.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

/*
 * Imagine we have this code:
 * foo = 0;
 * if (bar)
 *         foo = 1;
 *                   //  <-- point #1
 * else
 *         frob();
 *                   //  <-- point #2
 * if (foo == 1)     //  <-- point #3
 *         bar->baz; //  <-- point #4
 *
 * Currently (Oct 2008) in smatch when we merge bar states
 * null and nonnull, at point #2, the state becomes undefined.
 * As a result we get an error at point #3.
 *
 * The idea behind "implied state pools" is to fix that.
 *
 * The implied pools get created in merge_slist().  Whatever 
 * is unique to one slist being merged gets put into a pool.
 *
 * If we set a state that removes it from all pools.
 * 
 * When we come to an if statement where "foo" has some pools
 * associated we take all the pools where "foo == 1" and keep
 * all the states that are consistent across those pools.
 *
 * The point of doing this is to turn an undefined state into
 * a defined state.  This hopefully gets rid of some false positives.
 * What it doesn't do is find new errors that were
 * missed before.
 * 
 * There are quite a few implementation details I haven't figured
 * out.  How do you create implied state pools inside a 
 * complex condition?  How do you determine what is implied 
 * from a complex condition?  The initial patch is extremely rudimentary...
 */

#include "smatch.h"
#include "smatch_slist.h"

/*
 * This condition hook is very connected to smatch_extra.c.
 * It's registered there.
 */

void __implied_states_hook(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	struct sm_state *state;

	//printf("type = %d\n", expr->type);

	if (expr->type != EXPR_COMPARE || expr->op != SPECIAL_EQUAL)
		return;

	name = get_variable_from_expr_simple(expr->left, &sym);
	state = __get_sm_state(name, SMATCH_EXTRA, sym);
}
