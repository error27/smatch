/*
 * Linearize - walk the statement tree (but _not_ the expressions)
 * to generate a linear version of it and the basic blocks. 
 *
 * NOTE! We're not interested in the actual sub-expressions yet,
 * even though they can generate conditional branches and
 * subroutine calls. That's all "local" behaviour.
 *
 * Copyright (C) 2004 Linus Torvalds
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

static void show_instruction(struct instruction *insn)
{
	int op = insn->opcode;

	switch (op) {
	case OP_BADOP:
		printf("\tAIEEE! (%d %d)\n", insn->target.nr, insn->src.nr);
		break;
	case OP_CONDTRUE: case OP_CONDFALSE:
		printf("\t%s %%r%d,%p\n",
			op == OP_CONDTRUE ? "jne" : "jz",
			insn->target.nr, insn->address->bb_target);
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
	case OP_MULTIVALUE:
		printf("\tswitch %%r%d\n", insn->target.nr);
		break;
	case OP_MULTIJUMP:
		printf("\tcase %d ... %d -> %p\n", insn->begin, insn->end, insn->type);
		break;
	case OP_LOAD:
		printf("\tload %%r%d <- [%%r%d]\n", insn->target.nr, insn->src.nr);
		break;
	case OP_STORE:
		printf("\tstore %%r%d -> [%%r%d]\n", insn->target.nr, insn->src.nr);
		break;
	case OP_MOVE:
		printf("\t%%r%d <- %%r%d\n", insn->target.nr, insn->src.nr);
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
		printf("\t%%r%d <- %c %%r%d\n",
			insn->target.nr,
			op - OP_UNOP, insn->src.nr);
		break;
	case OP_BINOP ... OP_LASTBINOP:
		printf("\t%%r%d <- %%r%d %c %%r%d\n",
			insn->target.nr,
			insn->src1.nr, op - OP_UNOP, insn->src2.nr);
		break;
	default:
		printf("\top %d ???\n", op);
	}
}

static void show_bb(struct basic_block *bb)
{
	struct instruction *insn;
	struct symbol *owner = bb->this;

	printf("bb: %p%s\n", bb, owner ? "" : " UNREACHABLE!!");
	if (owner) {
		struct basic_block *from;
		FOR_EACH_PTR(owner->bb_parents, from) {
			printf("  **from %p**\n", from);
		} END_FOR_EACH_PTR;
	}
	FOR_EACH_PTR(bb->insns, insn) {
		show_instruction(insn);
	} END_FOR_EACH_PTR;

	if (bb->next) {
		printf("\tgoto\t\t.L%p\n", bb->next->bb_target);
	} else {
		printf("\tEND\n");
	}
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

#define bb_reachable(bb) ((bb)->this != NULL)
#define ep_haslabel(ep) ((ep)->flags & EP_HASLABEL)

static struct basic_block * new_basic_block(struct entrypoint *ep, struct symbol *owner)
{
	struct basic_block *bb;

	if (!owner) {
		static struct basic_block unreachable;
		return &unreachable;
	}
		
	bb = alloc_basic_block();
	add_bb(&ep->bbs, bb);
	bb->this = owner;
	if (owner->bb_target)
		warn(owner->pos, "Symbol already has a basic block %p", owner->bb_target);
	owner->bb_target = bb;
	return bb;
}

static void add_goto(struct basic_block *bb, struct symbol *sym)
{
	if (bb_reachable(bb)) {
		bb->next = sym;
		add_bb(&sym->bb_parents, bb);
	}
}

static void add_label(struct entrypoint *ep, struct symbol *sym)
{
	struct basic_block *new_bb = new_basic_block(ep, sym);
	struct basic_block *bb = ep->active;

	add_goto(bb, sym);
	ep->active = new_bb;
}

/*
 * Add a anonymous label, return the symbol for it..
 *
 * If we already have a label for the top of the active
 * context, we can just re-use it.
 */
static struct symbol *create_label(struct entrypoint *ep, struct position pos)
{
	struct basic_block *bb = ep->active;
	struct symbol *label = bb->this;

	if (!bb_reachable(bb) || !ptr_list_empty(bb->insns)) {
		label = alloc_symbol(pos, SYM_LABEL);
		add_label(ep, label);
	}
	return label;
}

static void add_one_insn(struct entrypoint *ep, struct position pos, struct instruction *insn)
{
	struct basic_block *bb = ep->active;    

	if (bb_reachable(bb)) {
		if (bb->flags & BB_HASBRANCH) {
			add_label(ep, alloc_symbol(pos, SYM_LABEL));
			bb = ep->active;
		}
		add_instruction(&bb->insns, insn);
	}
}

static void set_unreachable(struct entrypoint *ep)
{
	ep->active = new_basic_block(ep, NULL);
}

static void add_branch(struct entrypoint *ep, int opcode, struct expression *cond, struct symbol *target)
{
	struct basic_block *bb = ep->active;

	if (bb_reachable(bb)) {
		struct instruction *jump = alloc_instruction(opcode, target);
		jump->address = target;
		bb->flags |= BB_HASBRANCH;
		add_instruction(&bb->insns, jump);
		add_bb(&target->bb_parents, bb);
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
	return linearize_expression(ep, expr->address);
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
	insn = alloc_instruction(OP_UNOP + expr->op, expr->ctype);
	insn->target = new;
	insn->src = retval;
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

static void cond_branch(struct entrypoint *ep, int op, struct symbol *ctype, pseudo_t pseudo, struct symbol *target)
{
	struct instruction *insn;
	struct basic_block *bb = ep->active;

	insn = alloc_instruction(op, ctype);
	insn->address = target;
	bb->flags |= BB_HASBRANCH;
	add_instruction(&bb->insns, insn);
	add_bb(&target->bb_parents, bb);
}

static void copy_pseudo(struct entrypoint *ep, struct expression *expr, pseudo_t old, pseudo_t new)
{
	struct instruction *insn = alloc_instruction(OP_MOVE, expr->ctype);
	insn->target = new;
	insn->src = old;
	add_one_insn(ep, expr->pos, insn);
}

static pseudo_t linearize_logical(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t src1, src2, result;
	struct symbol *label;
	int op = (expr->op == SPECIAL_LOGICAL_OR) ? OP_CONDTRUE : OP_CONDFALSE;

	src1 = linearize_expression(ep, expr->left);
	result = alloc_pseudo();
	copy_pseudo(ep, expr, src1, result);

	/* Conditional jump */
	label = alloc_symbol(expr->pos, SYM_LABEL);
	cond_branch(ep, op, expr->ctype, src1, label);

	src2 = linearize_expression(ep, expr->right);
	copy_pseudo(ep, expr, src2, result);
	add_label(ep, label);
	return result;
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
		set_unreachable(ep);
		return pseudo;
	}

	case STMT_CASE: {
		add_label(ep, stmt->case_label);
		linearize_statement(ep, stmt->case_statement);
		break;
	}

	case STMT_LABEL: {
		add_label(ep, stmt->label_identifier);
		ep->flags |= EP_HASLABEL;
		linearize_statement(ep, stmt->label_statement);
		break;
	}

	case STMT_GOTO: {
		add_goto(ep->active, stmt->goto_label);
		set_unreachable(ep);
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
		struct symbol *target;
		struct basic_block *if_block;
		struct expression *cond = stmt->if_conditional;

		if (cond->type == EXPR_VALUE) {
			struct statement *always = stmt->if_true;
			struct statement *never = stmt->if_false;

			if (!cond->value) {
				never = always;
				always = stmt->if_false;
			}
			if (always)
				linearize_statement(ep, always);
			if (never) {
				struct basic_block *bb = ep->active;
				set_unreachable(ep);
				linearize_statement(ep, never);

				/*
				 * If the "never case" is reachable some other
				 * way, we need to merge the old always case
				 * with the fallthrough of the never case.
				 */
				if (bb_reachable(ep->active)) {
					add_goto(bb, create_label(ep, never->pos));
					break;
				}

				/* Otherwise we just continue with the old always case.. */
				ep->active = bb;
			}
			break;
		}
			

		target = alloc_symbol(stmt->pos, SYM_LABEL);
		add_branch(ep, OP_CONDFALSE, cond, target);

		linearize_statement(ep, stmt->if_true);

		if_block = ep->active;
		add_label(ep, target);
		
		if (stmt->if_false) {
			struct symbol *else_target = alloc_symbol(stmt->pos, SYM_LABEL);
			add_goto(if_block, else_target);
			linearize_statement(ep, stmt->if_false);
			add_label(ep, else_target);
		}
		break;
	}

	case STMT_SWITCH: {
		int default_seen;
		struct symbol *sym;
		struct instruction *switch_value;
		pseudo_t pseudo;

		/* Create the "head node" */
		if (!bb_reachable(ep->active))
			break;

		pseudo = linearize_expression(ep, stmt->switch_expression);
		switch_value = alloc_instruction(OP_MULTIVALUE, NULL);
		switch_value->target = pseudo;
		add_one_insn(ep, stmt->pos, switch_value);

		/* Create all the sub-jumps */
		default_seen = 0;
		FOR_EACH_PTR(stmt->switch_case->symbol_list, sym) {
			struct statement *case_stmt = sym->stmt;
			struct instruction *sw_bb = alloc_instruction(OP_MULTIJUMP, sym);
			if (!case_stmt->case_expression)
				default_seen = 1;
			if (case_stmt->case_expression)
				sw_bb->begin = case_stmt->case_expression->value;
			if (case_stmt->case_to)
				sw_bb->end = case_stmt->case_to->value;
			add_one_insn(ep, stmt->pos, sw_bb);
			add_bb(&sym->bb_parents, ep->active);
		} END_FOR_EACH_PTR;

		/* Default fall-through case */
		if (!default_seen)
			add_goto(ep->active, stmt->switch_break);
		set_unreachable(ep);

		/* And linearize the actual statement */
		linearize_statement(ep, stmt->switch_statement);

		/* ..then tie it all together at the end.. */
		add_label(ep, stmt->switch_break);
		break;
	}

	case STMT_ITERATOR: {
		struct statement  *pre_statement = stmt->iterator_pre_statement;
		struct expression *pre_condition = stmt->iterator_pre_condition;
		struct statement  *statement = stmt->iterator_statement;
		struct statement  *post_statement = stmt->iterator_post_statement;
		struct expression *post_condition = stmt->iterator_post_condition;
		struct symbol *loop_top = NULL, *loop_bottom = NULL;
		struct entrypoint *oldep = NULL;

		if (!bb_reachable(ep->active)) {
			oldep = ep;
			ep = alloc_entrypoint();
			ep->active = oldep->active;
		}

		concat_symbol_list(stmt->iterator_syms, &ep->syms);
		linearize_statement(ep, pre_statement);
		if (pre_condition && bb_reachable(ep->active)) {
			if (pre_condition->type == EXPR_VALUE) {
				if (!pre_condition->value) {
					loop_bottom = alloc_symbol(stmt->pos, SYM_LABEL);
					add_goto(ep->active, loop_bottom);
					set_unreachable(ep);
				}
			} else {
				loop_bottom = alloc_symbol(stmt->pos, SYM_LABEL);
				add_branch(ep, OP_CONDFALSE, pre_condition, loop_bottom);
			}
		}

		if (!post_condition || post_condition->type != EXPR_VALUE || post_condition->value)
			loop_top = create_label(ep, stmt->pos);

		linearize_statement(ep, statement);

		if (stmt->iterator_continue->used)
			add_label(ep, stmt->iterator_continue);

		linearize_statement(ep, post_statement);

		if (!post_condition) {
			add_goto(ep->active, loop_top);
			set_unreachable(ep);
		} else {
			if (post_condition->type != EXPR_VALUE || post_condition->value)
				add_branch(ep, OP_CONDTRUE, post_condition, loop_top);
		}

		if (stmt->iterator_break->used)
			add_label(ep, stmt->iterator_break);
		if (loop_bottom)
			add_label(ep, loop_bottom);

		/*
		 * If we started out unreachable, maybe the inside
		 * of the loop is still reachable?
		 */
		if (oldep) {
			if (ep_haslabel(ep)) {
				concat_basic_block_list(ep->bbs, &oldep->bbs);
				concat_symbol_list(ep->syms, &oldep->syms);
				oldep->active = ep->active;
			}
			ep = oldep;
		}
		break;
	}

	default:
		break;
	}
	return VOID;
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

			ep->name = sym;
			ep->active = new_basic_block(ep, sym);
			concat_symbol_list(base_type->arguments, &ep->syms);
			linearize_statement(ep, base_type->stmt);
			show_entry(ep);
		}
	}
}
