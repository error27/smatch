/*
 * Linearize - walk the statement tree (but _not_ the expressions)
 * to generate a linear version of it and the basic blocks. 
 *
 * NOTE! We're not interested in the actual sub-expressions yet,
 * even though they can generate conditional branches and
 * subroutine calls. That's all "local" behaviour.
 *
 * Copyright (C) 2004 Linus Torvalds
 * Copyright (C) 2004 Christopher Li
 */

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "parse.h"
#include "expression.h"
#include "linearize.h"

pseudo_t linearize_statement(struct entrypoint *ep, struct statement *stmt);
pseudo_t linearize_expression(struct entrypoint *ep, struct expression *expr);

static struct instruction *alloc_instruction(int opcode, struct symbol *type)
{
	struct instruction * insn = __alloc_instruction(0);
	insn->type = type;
	insn->opcode = opcode;
	return insn;
}

static struct entrypoint *alloc_entrypoint(void)
{
	return __alloc_entrypoint(0);
}

static struct basic_block *alloc_basic_block(void)
{
	return __alloc_basic_block(0);
}

static struct multijmp* alloc_multijmp(struct basic_block *target, int begin, int end)
{
	struct multijmp *multijmp = __alloc_multijmp(0);
	multijmp->target = target;
	multijmp->begin = begin;
	multijmp->end = end;
	return multijmp;
}

static struct phi* alloc_phi(struct basic_block *source, pseudo_t pseudo)
{
	struct phi *phi = __alloc_phi(0);
	phi->source = source;
	phi->pseudo = pseudo;
	return phi;
}

static void show_instruction(struct instruction *insn)
{
	int op = insn->opcode;

	switch (op) {
	case OP_BADOP:
		printf("\tAIEEE! (%d %d)\n", insn->target.nr, insn->src.nr);
		break;
	case OP_BR:
		if (insn->bb_true && insn->bb_false) {
			printf("\tbr\t%%r%d, .L%p, .L%p\n", insn->cond.nr, insn->bb_true, insn->bb_false);
			break;
		}
		printf("\tbr\t.L%p\n", insn->bb_true ? insn->bb_true : insn->bb_false);
		break;

	case OP_SETVAL: {
		struct expression *expr = insn->val;
		switch (expr->type) {
		case EXPR_VALUE:
			printf("\t%%r%d <- %lld\n",
				insn->target.nr, expr->value);
			break;
		case EXPR_STRING:
			printf("\t%%r%d <- %s\n",
				insn->target.nr, show_string(expr->string));
			break;
		case EXPR_SYMBOL:
			printf("\t%%r%d <- %s\n",  
				insn->target.nr, show_ident(expr->symbol->ident));
			break;
		default:
			printf("\t SETVAL ?? ");
		}
		break;
	}
	case OP_SWITCH: {
		struct multijmp *jmp;
		printf("\tswitch %%r%d", insn->target.nr);
		FOR_EACH_PTR(insn->multijmp_list, jmp) {
			if (jmp->begin == jmp->end)
				printf(", %d -> .L%p", jmp->begin, jmp->target);
			else if (jmp->begin < jmp->end)
				printf(", %d ... %d -> .L%p", jmp->begin, jmp->end, jmp->target);
			else
				printf(", default -> .L%p\n", jmp->target);
		} END_FOR_EACH_PTR;
		printf("\n");
		break;
	}
	
	case OP_PHI: {
		struct phi *phi;
		char *s = " ";
		printf("\t%%r%d <- phi", insn->target.nr);
		FOR_EACH_PTR(insn->phi_list, phi) {
			printf("%s(%%r%d, .L%p)", s, phi->pseudo.nr, phi->source);
			s = ", ";
		} END_FOR_EACH_PTR;
		printf("\n");
		break;
	}	
	case OP_LOAD:
		printf("\tload %%r%d <- [%%r%d]\n", insn->target.nr, insn->src.nr);
		break;
	case OP_STORE:
		printf("\tstore %%r%d -> [%%r%d]\n", insn->target.nr, insn->src.nr);
		break;
	case OP_ARGUMENT:
		printf("\tpush %%r%d\n", insn->src.nr);
		break;
	case OP_CALL:
		printf("\t%%r%d <- CALL %s\n", insn->target.nr, show_ident(insn->address->ident));
		break;
	case OP_INDCALL:
		printf("\t%%r%d <- CALL [%%r%d]\n", insn->target.nr, insn->src.nr);
		break;
	case OP_CAST:
		printf("\t%%r%d <- CAST(%d->%d) %%r%d\n",
			insn->target.nr,
			insn->orig_type->bit_size, insn->type->bit_size, 
			insn->src.nr);
		break;
	case OP_UNOP ... OP_LASTUNOP:
		printf("\t%%r%d <- %s %%r%d\n",
			insn->target.nr,
			show_special(op - OP_UNOP), insn->src.nr);
		break;
	case OP_BINOP ... OP_LASTBINOP:
		printf("\t%%r%d <- %%r%d %s %%r%d\n",
			insn->target.nr,
			insn->src1.nr, show_special(op - OP_UNOP), insn->src2.nr);
		break;
	default:
		printf("\top %d ???\n", op);
	}
}

static void show_bb(struct basic_block *bb)
{
	struct instruction *insn;

	printf("bb: %p\n", bb);
	if (bb->parents) {
		struct basic_block *from;
		FOR_EACH_PTR(bb->parents, from) {
			printf("  **from %p**\n", from);
		} END_FOR_EACH_PTR;
	}
	FOR_EACH_PTR(bb->insns, insn) {
		show_instruction(insn);
	} END_FOR_EACH_PTR;
	if (!bb_terminated(bb))
		printf("\tEND\n");
	printf("\n");
}

static void show_entry(struct entrypoint *ep)
{
	struct symbol *sym;
	struct basic_block *bb;

	printf("ep %p: %s\n", ep, show_ident(ep->name->ident));

	FOR_EACH_PTR(ep->syms, sym) {
		printf("   sym: %p %s\n", sym, show_ident(sym->ident));
	} END_FOR_EACH_PTR;

	printf("\n");

	FOR_EACH_PTR(ep->bbs, bb) {
		show_bb(bb);
	} END_FOR_EACH_PTR;

	printf("\n");
}

static void bind_label(struct symbol *label, struct basic_block *bb, struct position pos)
{
	if (label->bb_target)
		warn(pos, "label already bound\n");
	label->bb_target = bb;
}

static struct basic_block * get_bound_block(struct entrypoint *ep, struct symbol *label)
{
	struct basic_block *bb = label->bb_target;

	if (!bb) {
		label->bb_target = bb = alloc_basic_block();
		bb->flags |= BB_REACHABLE;
	}
	return bb;
}

static void add_goto(struct entrypoint *ep, struct basic_block *dst)
{
	struct basic_block *src = ep->active;
	if (bb_reachable(src)) {
		struct instruction *br = alloc_instruction(OP_BR, NULL);
		br->bb_true = dst;
		add_bb(&dst->parents, src);
		add_instruction(&src->insns, br);
		ep->active = NULL;
	}
}

static void add_one_insn(struct entrypoint *ep, struct position pos, struct instruction *insn)
{
	struct basic_block *bb = ep->active;    

	if (bb_reachable(bb))
		add_instruction(&bb->insns, insn);
}

static void set_activeblock(struct entrypoint *ep, struct basic_block *bb)
{
	if (!bb_terminated(ep->active))
		add_goto(ep, bb);

	ep->active = bb;
	if (bb_reachable(bb))
		add_bb(&ep->bbs, bb);
}

static void add_branch(struct entrypoint *ep, struct expression *expr, pseudo_t cond, struct basic_block *bb_true, struct basic_block *bb_false)
{
	struct basic_block *bb = ep->active;
	struct instruction *br;

	if (bb_reachable(bb)) {
       		br = alloc_instruction(OP_BR, expr->ctype);
		br->cond = cond;
		br->bb_true = bb_true;
		br->bb_false = bb_false;
		add_bb(&bb_true->parents, bb);
		add_bb(&bb_false->parents, bb);
		add_one_insn(ep, expr->pos, br);
	}
}

/* Dummy pseudo allocator */
static pseudo_t alloc_pseudo(void)
{
	static int nr = 0;
	return to_pseudo(++nr);
}

/*
 * FIXME! Not all accesses are memory loads. We should
 * check what kind of symbol is behind the dereference.
 */
static pseudo_t linearize_address_gen(struct entrypoint *ep, struct expression *expr)
{
	if (expr->type == EXPR_PREOP)
		return linearize_expression(ep, expr->unop);
	if (expr->type == EXPR_BITFIELD)
		return linearize_expression(ep, expr->address);
	warn(expr->pos, "generating address of non-lvalue");
	return VOID;
}

static void linearize_store_gen(struct entrypoint *ep, pseudo_t value, struct expression *expr, pseudo_t addr)
{
	struct instruction *store = alloc_instruction(OP_STORE, expr->ctype);
	store->target = value;
	store->src = addr;
	add_one_insn(ep, expr->pos, store);
}

static pseudo_t linearize_load_gen(struct entrypoint *ep, struct expression *expr, pseudo_t addr)
{
	pseudo_t new = alloc_pseudo();
	struct instruction *insn = alloc_instruction(OP_LOAD, expr->ctype);

	insn->target = new;
	insn->src = addr;
	add_one_insn(ep, expr->pos, insn);
	if (expr->type == EXPR_PREOP)
		return new;

	/* bitfield load */
	/* FIXME! Add shift and mask!!! */
	return new;		
}

static pseudo_t linearize_access(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t addr = linearize_address_gen(ep, expr);
	return linearize_load_gen(ep, expr, addr);
}

static pseudo_t linearize_inc_dec(struct entrypoint *ep, struct expression *expr, int postop)
{
	pseudo_t addr = linearize_address_gen(ep, expr->unop);
	pseudo_t retval, new;
	struct instruction *insn;

	retval = linearize_load_gen(ep, expr->unop, addr);
	new = retval;
	if (postop)
		new = alloc_pseudo();

	/* Generate the inc/dec */
	insn = alloc_instruction(OP_UNOP + expr->op, expr->ctype);
	insn->target = new;
	insn->src = retval;
	add_one_insn(ep, expr->pos, insn);

	/* Store back value */
	linearize_store_gen(ep, new, expr->unop, addr);
	return retval;
}

static pseudo_t linearize_regular_preop(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t target = linearize_expression(ep, expr->unop);
	pseudo_t new = alloc_pseudo();
	struct instruction *insn = alloc_instruction(OP_UNOP + expr->op, expr->ctype);
	insn->target = new;
	insn->src1 = target;
	return new;
}

static pseudo_t linearize_preop(struct entrypoint *ep, struct expression *expr)
{
	/*
	 * '*' is an lvalue access, and is fundamentally different
	 * from an arithmetic operation. Maybe it should have an
	 * expression type of its own..
	 */
	if (expr->op == '*')
		return linearize_access(ep, expr);
	if (expr->op == SPECIAL_INCREMENT || expr->op == SPECIAL_DECREMENT)
		return linearize_inc_dec(ep, expr, 0);
	return linearize_regular_preop(ep, expr);
}

static pseudo_t linearize_postop(struct entrypoint *ep, struct expression *expr)
{
	return linearize_inc_dec(ep, expr, 1);
}	

static pseudo_t linearize_assignment(struct entrypoint *ep, struct expression *expr)
{
	struct expression *target = expr->left;
	pseudo_t value, address;

	value = linearize_expression(ep, expr->right);
	address = linearize_address_gen(ep, target);
	linearize_store_gen(ep, value, target, address);
	return value;
}

static void push_argument(struct entrypoint *ep, struct expression *expr, pseudo_t pseudo)
{
	struct instruction *insn = alloc_instruction(OP_ARGUMENT, expr->ctype);
	insn->src = pseudo;
	add_one_insn(ep, expr->pos, insn);
}

static pseudo_t linearize_direct_call(struct entrypoint *ep, struct expression *expr, struct symbol *direct)
{
	struct instruction *insn = alloc_instruction(OP_CALL, expr->ctype);
	pseudo_t retval = alloc_pseudo();

	insn->target = retval;
	insn->address = direct;
	add_one_insn(ep, expr->pos, insn);
	return retval;
}

static pseudo_t linearize_indirect_call(struct entrypoint *ep, struct expression *expr, pseudo_t pseudo)
{
	struct instruction *insn = alloc_instruction(OP_INDCALL, expr->ctype);
	pseudo_t retval = alloc_pseudo();

	insn->target = retval;
	insn->src = pseudo;
	add_one_insn(ep, expr->pos, insn);
	return retval;
}

static pseudo_t linearize_call_expression(struct entrypoint *ep, struct expression *expr)
{
	struct symbol *direct;
	struct expression *arg, *fn;
	pseudo_t retval;

	if (!expr->ctype) {
		warn(expr->pos, "\tcall with no type!");
		return VOID;
	}

	FOR_EACH_PTR_REVERSE(expr->args, arg) {
		pseudo_t new = linearize_expression(ep, arg);
		push_argument(ep, arg, new);
	} END_FOR_EACH_PTR_REVERSE;

	fn = expr->fn;

	/* Remove dereference, if any */
	direct = NULL;
	if (fn->type == EXPR_PREOP) {
		if (fn->unop->type == EXPR_SYMBOL) {
			struct symbol *sym = fn->unop->symbol;
			if (sym->ctype.base_type->type == SYM_FN)
				direct = sym;
		}
	}
	if (direct) {
		retval = linearize_direct_call(ep, expr, direct);
	} else {
		pseudo_t fncall = linearize_expression(ep, fn);
		retval = linearize_indirect_call(ep, expr, fncall);
	}
	return retval;
}

static pseudo_t linearize_binop(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t src1, src2, result;
	struct instruction *insn;

	src1 = linearize_expression(ep, expr->left);
	src2 = linearize_expression(ep, expr->right);
	result = alloc_pseudo();
	insn = alloc_instruction(OP_BINOP + expr->op, expr->ctype);
	insn->target = result;
	insn->src1 = src1;
	insn->src2 = src2;
	add_one_insn(ep, expr->pos, insn);
	return result;
}

static pseudo_t linearize_logical_branch(struct entrypoint *ep, struct expression *expr, struct basic_block *bb_true, struct basic_block *bb_false);

pseudo_t linearize_cond_branch(struct entrypoint *ep, struct expression *expr, struct basic_block *bb_true, struct basic_block *bb_false);

static pseudo_t linearize_logical(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t src1, src2, target;
	struct basic_block *bb_true = alloc_basic_block();
	struct basic_block *bb_false = alloc_basic_block();
	struct basic_block *merge = alloc_basic_block();
	struct basic_block *first = bb_true;
	struct basic_block *second = bb_false;
	struct instruction *insn;

	if (expr->op == SPECIAL_LOGICAL_OR) {
		first = bb_false;
		second = bb_true;
	}

	linearize_cond_branch(ep, expr->left, bb_true, bb_false);

	set_activeblock(ep, first);
	src1 = linearize_expression(ep, expr->right);
	add_goto(ep, merge);

	set_activeblock(ep, second);
       	insn = alloc_instruction(OP_SETVAL, expr->ctype);
	insn->target = src2 = alloc_pseudo();
	insn->val = alloc_const_expression(expr->pos, expr->op == SPECIAL_LOGICAL_OR);
	add_one_insn(ep, expr->pos,insn);

	set_activeblock(ep, merge);

	if (bb_reachable(bb_true) && bb_reachable(bb_false)) {
		struct instruction *phi_node = alloc_instruction(OP_PHI, expr->ctype);
		add_phi(&phi_node->phi_list, alloc_phi(first, src1));
		add_phi(&phi_node->phi_list, alloc_phi(second, src2));
		phi_node->target = target = alloc_pseudo();
		add_one_insn(ep, expr->pos, phi_node);
		set_activeblock(ep, alloc_basic_block());
		return target;
	}

	return bb_reachable(first) ? src1 : src2;
}

pseudo_t linearize_cond_branch(struct entrypoint *ep, struct expression *expr, struct basic_block *bb_true, struct basic_block *bb_false)
{
	if (!expr || !bb_reachable(ep->active))
		return VOID;

	switch (expr->type) {

	case EXPR_STRING:
	case EXPR_VALUE:
		add_goto(ep, expr->value ? bb_true : bb_false);
		return VOID;
		
	case EXPR_LOGICAL:
		linearize_logical_branch(ep, expr, bb_true, bb_false);
		return VOID;

	case EXPR_PREOP:
		if (expr->op == '!')
			return linearize_cond_branch(ep, expr->unop, bb_false, bb_true);
		/* fall through */
	default: {
		pseudo_t cond = linearize_expression(ep, expr);
		add_branch(ep, expr, cond, bb_true, bb_false);

		return VOID;
	}
	}
	return VOID;
}

static pseudo_t linearize_logical_branch(struct entrypoint *ep, struct expression *expr, struct basic_block *bb_true, struct basic_block *bb_false)
{
	struct basic_block *next = alloc_basic_block();

	if (expr->op == SPECIAL_LOGICAL_OR)
		linearize_cond_branch(ep, expr->left, bb_true, next);
	else
		linearize_cond_branch(ep, expr->left, next, bb_false);
	set_activeblock(ep, next);
	linearize_cond_branch(ep, expr->right, bb_true, bb_false);
	return VOID;
}

pseudo_t linearize_cast(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t src, result;
	struct instruction *insn;

	src = linearize_expression(ep, expr->cast_expression);
	insn = alloc_instruction(OP_CAST, expr->ctype);
	result = alloc_pseudo();
	insn->target = result;
	insn->src = src;
	insn->orig_type = expr->cast_expression->ctype;
	add_one_insn(ep, expr->pos, insn);
	return result;
}

pseudo_t linearize_expression(struct entrypoint *ep, struct expression *expr)
{
	if (!expr)
		return VOID;

	switch (expr->type) {
	case EXPR_VALUE: case EXPR_STRING: case EXPR_SYMBOL: {
		pseudo_t pseudo;
		struct instruction *insn = alloc_instruction(OP_SETVAL, expr->ctype);
		insn->target = pseudo = alloc_pseudo();
		insn->val = expr;
		add_one_insn(ep, expr->pos,insn);
		return pseudo;
	}

	case EXPR_STATEMENT:
		return linearize_statement(ep, expr->statement);

	case EXPR_CALL:
		return linearize_call_expression(ep, expr);

	case EXPR_BINOP:
		return linearize_binop(ep, expr);

	case EXPR_LOGICAL:
		return linearize_logical(ep, expr);

	case EXPR_COMMA: {
		linearize_expression(ep, expr->left);
		return linearize_expression(ep, expr->right);
	}

	case EXPR_ASSIGNMENT:
		return linearize_assignment(ep, expr);

	case EXPR_PREOP:
		return linearize_preop(ep, expr);

	case EXPR_POSTOP:
		return linearize_postop(ep, expr);

	case EXPR_CAST:
		return linearize_cast(ep, expr);

	default: {
		struct instruction *bad = alloc_instruction(OP_BADOP, expr->ctype);
		bad->target.nr = expr->type;
		bad->src.nr = expr->op;
		add_one_insn(ep, expr->pos, bad);
		return VOID;
	}
	}
	return VOID;
}

pseudo_t linearize_statement(struct entrypoint *ep, struct statement *stmt)
{
	if (!stmt)
		return VOID;

	switch (stmt->type) {
	case STMT_NONE:
		break;

	case STMT_EXPRESSION:
		return linearize_expression(ep, stmt->expression);

	case STMT_ASM:
		/* FIXME */
		break;

	case STMT_RETURN: {
		pseudo_t pseudo = linearize_expression(ep, stmt->expression);
		return pseudo;
	}

	case STMT_CASE: {
		struct basic_block *bb = get_bound_block(ep, stmt->case_label);
		set_activeblock(ep, bb);
		linearize_statement(ep, stmt->case_statement);
		break;
	}

	case STMT_LABEL: {
		struct symbol *label = stmt->label_identifier;
		struct basic_block *bb;

		if (label->used) {
			bb = get_bound_block(ep, stmt->label_identifier);
			set_activeblock(ep, bb);
			linearize_statement(ep, stmt->label_statement);
		}
		break;
	}

	case STMT_GOTO: {
		add_goto(ep, get_bound_block(ep, stmt->goto_label));
		break;
	}

	case STMT_COMPOUND: {
		pseudo_t pseudo;
		struct statement *s;
		concat_symbol_list(stmt->syms, &ep->syms);
		FOR_EACH_PTR(stmt->stmts, s) {
			pseudo = linearize_statement(ep, s);
		} END_FOR_EACH_PTR;
		return pseudo;
	}

	/*
	 * This could take 'likely/unlikely' into account, and
	 * switch the arms around appropriately..
	 */
	case STMT_IF: {
		struct basic_block *bb_true, *bb_false, *endif;
 		struct expression *cond = stmt->if_conditional;

		bb_true = alloc_basic_block();
		bb_false = endif = alloc_basic_block();

 		linearize_cond_branch(ep, cond, bb_true, bb_false);

		set_activeblock(ep, bb_true);
 		linearize_statement(ep, stmt->if_true);
 
 		if (stmt->if_false) {
			endif = alloc_basic_block();
			add_goto(ep, endif);
			set_activeblock(ep, bb_false);
 			linearize_statement(ep, stmt->if_false);
		}
		set_activeblock(ep, endif);
		break;
	}

	case STMT_SWITCH: {
		struct symbol *sym;
		struct instruction *switch_ins;
		struct basic_block *switch_end = alloc_basic_block();
		pseudo_t pseudo;

		pseudo = linearize_expression(ep, stmt->switch_expression);
		switch_ins = alloc_instruction(OP_SWITCH, NULL);
		switch_ins->target = pseudo;
		add_one_insn(ep, stmt->pos, switch_ins);

		FOR_EACH_PTR(stmt->switch_case->symbol_list, sym) {
			struct statement *case_stmt = sym->stmt;
			struct basic_block *bb_case = get_bound_block(ep, sym);
			struct multijmp *jmp;
			int begin, end;
			if (!case_stmt->case_expression) {
			      jmp = alloc_multijmp(bb_case, 1, 0);
			} else {
				if (case_stmt->case_expression)
					begin = end = case_stmt->case_expression->value;
				if (case_stmt->case_to)
					end = case_stmt->case_to->value;
				if (begin > end)
					jmp = alloc_multijmp(bb_case, end, begin);
				else
					jmp = alloc_multijmp(bb_case, begin, end);

			}
			add_multijmp(&switch_ins->multijmp_list, jmp);
			add_bb(&bb_case->parents, ep->active);
		} END_FOR_EACH_PTR;

		bind_label(stmt->switch_break, switch_end, stmt->pos);

		/* And linearize the actual statement */
		linearize_statement(ep, stmt->switch_statement);
		set_activeblock(ep, switch_end);

		break;
	}

	case STMT_ITERATOR: {
		struct statement  *pre_statement = stmt->iterator_pre_statement;
		struct expression *pre_condition = stmt->iterator_pre_condition;
		struct statement  *statement = stmt->iterator_statement;
		struct statement  *post_statement = stmt->iterator_post_statement;
		struct expression *post_condition = stmt->iterator_post_condition;
		struct basic_block *loop_top, *loop_body, *loop_continue, *loop_end;

		concat_symbol_list(stmt->iterator_syms, &ep->syms);
		linearize_statement(ep, pre_statement);

 		loop_body = loop_top = alloc_basic_block();
 		loop_continue = alloc_basic_block();
 		loop_end = alloc_basic_block();
 
		if (!post_statement && (pre_condition == post_condition)) {
			/*
			 * If it is a while loop, optimize away the post_condition.
			 */
			post_condition = NULL;
			loop_body = loop_continue;
			loop_continue = loop_top;
			loop_top->flags |= BB_REACHABLE;
			set_activeblock(ep, loop_top);
		}

		loop_top->flags |= BB_REACHABLE;
		if (pre_condition) 
 			linearize_cond_branch(ep, pre_condition, loop_body, loop_end);

		bind_label(stmt->iterator_continue, loop_continue, stmt->pos);
		bind_label(stmt->iterator_break, loop_end, stmt->pos);

		set_activeblock(ep, loop_body);
		linearize_statement(ep, statement);
		add_goto(ep, loop_continue);

		if (post_condition) {
			set_activeblock(ep, loop_continue);
			linearize_statement(ep, post_statement);
 			linearize_cond_branch(ep, post_condition, loop_top, loop_end);
		}

		set_activeblock(ep, loop_end);
		break;
	}

	default:
		break;
	}
	return VOID;
}

void mark_bb_reachable(struct basic_block *bb)
{
	struct basic_block *child;
	struct terminator_iterator term;
	struct basic_block_list *bbstack = NULL;

	if (!bb || bb->flags & BB_REACHABLE)
		return;

	add_bb(&bbstack, bb);
	while (bbstack) {
		bb = delete_last_basic_block(&bbstack);
		if (bb->flags & BB_REACHABLE)
			continue;
		bb->flags |= BB_REACHABLE;
		init_terminator_iterator(last_instruction(bb->insns), &term);
		while ((child=next_terminator_bb(&term))) {
			if (!(child->flags & BB_REACHABLE))
				add_bb(&bbstack, child);
		}
	}
}

void remove_unreachable_bbs(struct basic_block_list **bblist)
{
	struct basic_block *bb, *child;
	struct list_iterator iterator;
	struct terminator_iterator term;

	init_iterator((struct ptr_list **) bblist, &iterator, 0);
	while((bb=next_basic_block(&iterator)))
		bb->flags &= ~BB_REACHABLE;

	init_iterator((struct ptr_list **) bblist, &iterator, 0);
	mark_bb_reachable(next_basic_block(&iterator));
	while((bb=next_basic_block(&iterator))) {
		if (bb->flags & BB_REACHABLE)
			continue;
		init_terminator_iterator(last_instruction(bb->insns), &term);
		while ((child=next_terminator_bb(&term)))
			replace_basic_block_list(&child->parents, bb, NULL);
		delete_iterator(&iterator);
	}
}

void pack_basic_blocks(struct basic_block_list **bblist)
{
	struct basic_block *child, *bb, *parent;
	struct list_iterator iterator;
	struct terminator_iterator term;
	struct instruction *jmp;

	remove_unreachable_bbs(bblist);
	init_bb_iterator(bblist, &iterator, 0);
	while((bb=next_basic_block(&iterator))) {
		if (is_branch_goto(jmp=first_instruction(bb->insns))) {
			/*
			 * This is an empty goto block. Transfer the parents' terminator
			 * to target directly.
			 */
			struct list_iterator it_parents;
			struct basic_block *target;

			target = jmp->bb_true ? jmp->bb_true : jmp->bb_false;
			replace_basic_block_list(&target->parents, bb, NULL);
			init_bb_iterator(&bb->parents, &it_parents, 0);
			while((parent=next_basic_block(&it_parents))) {
				init_terminator_iterator(last_instruction(parent->insns), &term);
				while ((child=next_terminator_bb(&term))) {
					if (child == bb) {
						replace_terminator_bb(&term, target);
						add_bb(&target->parents, parent);
					}
				}
			}
			delete_iterator(&iterator);
			continue;
		}
		
		if (bb_list_size(bb->parents)!=1)
		       continue;
		parent = first_basic_block(bb->parents);
		if (parent!=bb && is_branch_goto(last_instruction(parent->insns))) {
			/*
			 * Combine this block with the parent.
			 */
			delete_last_instruction(&parent->insns);
			concat_instruction_list(bb->insns, &parent->insns);
			init_terminator_iterator(last_instruction(bb->insns), &term);
			while ((child=next_terminator_bb(&term)))
				replace_basic_block_list(&child->parents, bb, parent);
			delete_iterator(&iterator);
		}
	}
}

void linearize_symbol(struct symbol *sym)
{
	struct symbol *base_type;

	if (!sym)
		return;
	base_type = sym->ctype.base_type;
	if (!base_type)
		return;
	if (base_type->type == SYM_FN) {
		if (base_type->stmt) {
			struct entrypoint *ep = alloc_entrypoint();
			struct basic_block *bb = alloc_basic_block();

			ep->name = sym;
			bb->flags |= BB_REACHABLE;
			set_activeblock(ep, bb);
			concat_symbol_list(base_type->arguments, &ep->syms);
			linearize_statement(ep, base_type->stmt);
			pack_basic_blocks(&ep->bbs);
			show_entry(ep);
		}
	}
}
