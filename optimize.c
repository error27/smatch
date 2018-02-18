// SPDX-License-Identifier: MIT
//
// optimize.c - main optimization loop
//
// Copyright (C) 2004 Linus Torvalds
// Copyright (C) 2004 Christopher Li

#include "optimize.h"
#include "linearize.h"
#include "flow.h"


static void clear_symbol_pseudos(struct entrypoint *ep)
{
	pseudo_t pseudo;

	FOR_EACH_PTR(ep->accesses, pseudo) {
		pseudo->sym->pseudo = NULL;
	} END_FOR_EACH_PTR(pseudo);
}

void optimize(struct entrypoint *ep)
{
	if (fdump_ir & PASS_LINEARIZE)
		show_entry(ep);

	/*
	 * Do trivial flow simplification - branches to
	 * branches, kill dead basicblocks etc
	 */
	kill_unreachable_bbs(ep);

	/*
	 * Turn symbols into pseudos
	 */
	if (fpasses & PASS_MEM2REG)
		simplify_symbol_usage(ep);
	if (fdump_ir & PASS_MEM2REG)
		show_entry(ep);

	if (!(fpasses & PASS_OPTIM))
		return;
repeat:
	/*
	 * Remove trivial instructions, and try to CSE
	 * the rest.
	 */
	do {
		cleanup_and_cse(ep);
		pack_basic_blocks(ep);
	} while (repeat_phase & REPEAT_CSE);

	kill_unreachable_bbs(ep);
	vrfy_flow(ep);

	/* Cleanup */
	clear_symbol_pseudos(ep);

	/* And track pseudo register usage */
	track_pseudo_liveness(ep);

	/*
	 * Some flow optimizations can only effectively
	 * be done when we've done liveness analysis. But
	 * if they trigger, we need to start all over
	 * again
	 */
	if (simplify_flow(ep)) {
		clear_liveness(ep);
		goto repeat;
	}

	/* Finally, add deathnotes to pseudos now that we have them */
	if (dbg_dead)
		track_pseudo_death(ep);
}
