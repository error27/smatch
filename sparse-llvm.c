/*
 * Example usage:
 *	./sparse-llvm hello.c | llc | as -o hello.o
 */

#include <llvm-c/Core.h>
#include <llvm-c/BitWriter.h>

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "symbol.h"
#include "expression.h"
#include "linearize.h"
#include "flow.h"

struct function {
	LLVMBuilderRef			builder;
	LLVMTypeRef			type;
	LLVMValueRef			fn;
	LLVMModuleRef			module;
};

static inline bool symbol_is_fp_type(struct symbol *sym)
{
	if (!sym)
		return false;

	return sym->ctype.base_type == &fp_type;
}

static LLVMTypeRef symbol_type(struct symbol *sym);

#define MAX_STRUCT_MEMBERS 64

static LLVMTypeRef sym_struct_type(struct symbol *sym)
{
	LLVMTypeRef elem_types[MAX_STRUCT_MEMBERS];
	struct symbol *member;
	unsigned nr = 0;

	FOR_EACH_PTR(sym->symbol_list, member) {
		assert(nr < MAX_STRUCT_MEMBERS);

		elem_types[nr++] = symbol_type(member);
	} END_FOR_EACH_PTR(member);

	return LLVMStructType(elem_types, nr, 0 /* packed? */);
}

static LLVMTypeRef sym_ptr_type(struct symbol *sym)
{
	LLVMTypeRef type = symbol_type(sym->ctype.base_type);

	return LLVMPointerType(type, 0);
}

static LLVMTypeRef sym_basetype_type(struct symbol *sym)
{
	LLVMTypeRef ret = NULL;

	if (symbol_is_fp_type(sym)) {
		switch (sym->bit_size) {
		case 32:
			ret = LLVMFloatType();
			break;
		case 64:
			ret = LLVMDoubleType();
			break;
		case 80:
			ret = LLVMX86FP80Type();
			break;
		default:
			die("invalid bit size %d for type %d", sym->bit_size, sym->type);
			break;
		}
	} else {
		switch (sym->bit_size) {
		case -1:
			ret = LLVMVoidType();
			break;
		case 8:
			ret = LLVMInt8Type();
			break;
		case 16:
			ret = LLVMInt16Type();
			break;
		case 32:
			ret = LLVMInt32Type();
			break;
		case 64:
			ret = LLVMInt64Type();
			break;
		default:
			die("invalid bit size %d for type %d", sym->bit_size, sym->type);
			break;
		}
	}

	return ret;
}

static LLVMTypeRef symbol_type(struct symbol *sym)
{
	LLVMTypeRef ret = NULL;

	switch (sym->type) {
	case SYM_NODE:
		ret = symbol_type(sym->ctype.base_type);
		break;
	case SYM_BASETYPE:
		ret = sym_basetype_type(sym);
		break;
	case SYM_PTR:
		ret = sym_ptr_type(sym);
		break;
	case SYM_STRUCT:
		ret = sym_struct_type(sym);
		break;
	default:
		assert(0);
	}
	return ret;
}

static LLVMTypeRef insn_symbol_type(struct instruction *insn)
{
	if (insn->type)
		return symbol_type(insn->type);

	switch (insn->size) {
		case 8:		return LLVMInt8Type();
		case 16:	return LLVMInt16Type();
		case 32:	return LLVMInt32Type();
		case 64:	return LLVMInt64Type();

		default:
			die("invalid bit size %d", insn->size);
			break;
	}

	return NULL;	/* not reached */
}

static LLVMLinkage data_linkage(struct symbol *sym)
{
	if (sym->ctype.modifiers & MOD_STATIC)
		return LLVMPrivateLinkage;

	return LLVMExternalLinkage;
}

static LLVMLinkage function_linkage(struct symbol *sym)
{
	if (sym->ctype.modifiers & MOD_STATIC)
		return LLVMInternalLinkage;

	return LLVMExternalLinkage;
}

#define MAX_PSEUDO_NAME 64

static void pseudo_name(pseudo_t pseudo, char *buf)
{
	switch (pseudo->type) {
	case PSEUDO_REG:
		snprintf(buf, MAX_PSEUDO_NAME, "R%d", pseudo->nr);
		break;
	case PSEUDO_SYM:
		assert(0);
		break;
	case PSEUDO_VAL:
		assert(0);
		break;
	case PSEUDO_ARG: {
		assert(0);
		break;
	}
	case PSEUDO_PHI:
		snprintf(buf, MAX_PSEUDO_NAME, "PHI%d", pseudo->nr);
		break;
	default:
		assert(0);
	}
}

static LLVMValueRef pseudo_to_value(struct function *fn, struct instruction *insn, pseudo_t pseudo)
{
	LLVMValueRef result = NULL;

	switch (pseudo->type) {
	case PSEUDO_REG:
		result = pseudo->priv;
		break;
	case PSEUDO_SYM: {
		struct symbol *sym = pseudo->sym;
		struct expression *expr;

		assert(sym->bb_target == NULL);
		assert(sym->ident == NULL);

		expr = sym->initializer;
		if (expr) {
			switch (expr->type) {
			case EXPR_STRING: {
				const char *s = expr->string->data;
				LLVMValueRef indices[] = { LLVMConstInt(LLVMInt64Type(), 0, 0), LLVMConstInt(LLVMInt64Type(), 0, 0) };
				LLVMValueRef data;

				data = LLVMAddGlobal(fn->module, LLVMArrayType(LLVMInt8Type(), strlen(s) + 1), ".str");
				LLVMSetLinkage(data, LLVMPrivateLinkage);
				LLVMSetGlobalConstant(data, 1);
				LLVMSetInitializer(data, LLVMConstString(strdup(s), strlen(s) + 1, true));

				result = LLVMConstGEP(data, indices, ARRAY_SIZE(indices));
				break;
			}
			default:
				assert(0);
			}
		}
		break;
	}
	case PSEUDO_VAL:
		result = LLVMConstInt(insn_symbol_type(insn), pseudo->value, 1);
		break;
	case PSEUDO_ARG: {
		result = LLVMGetParam(fn->fn, pseudo->nr - 1);
		break;
	}
	case PSEUDO_PHI:
		result = pseudo->priv;
		break;
	case PSEUDO_VOID:
		result = NULL;
		break;
	default:
		assert(0);
	}

	return result;
}

static LLVMTypeRef pseudo_type(struct function *fn, struct instruction *insn, pseudo_t pseudo)
{
	LLVMValueRef v;
	LLVMTypeRef result = NULL;

	if (pseudo->priv) {
		v = pseudo->priv;
		return LLVMTypeOf(v);
	}

	switch (pseudo->type) {
	case PSEUDO_REG:
		result = symbol_type(pseudo->def->type);
		break;
	case PSEUDO_SYM: {
		struct symbol *sym = pseudo->sym;
		struct expression *expr;

		assert(sym->bb_target == NULL);
		assert(sym->ident == NULL);

		expr = sym->initializer;
		if (expr) {
			switch (expr->type) {
			case EXPR_STRING:
				result = LLVMPointerType(LLVMInt8Type(), 0);
				break;
			default:
				assert(0);
			}
		}
		break;
	}
	case PSEUDO_VAL:
		result = insn_symbol_type(insn);
		break;
	case PSEUDO_ARG:
		result = LLVMTypeOf(LLVMGetParam(fn->fn, pseudo->nr - 1));
		break;
	case PSEUDO_PHI:
		assert(0);
		break;
	case PSEUDO_VOID:
		result = LLVMVoidType();
		break;
	default:
		assert(0);
	}

	return result;
}

static void output_op_binary(struct function *fn, struct instruction *insn)
{
	LLVMValueRef lhs, rhs, target;
	char target_name[64];

	lhs = pseudo_to_value(fn, insn, insn->src1);

	rhs = pseudo_to_value(fn, insn, insn->src2);

	pseudo_name(insn->target, target_name);

	switch (insn->opcode) {
	/* Binary */
	case OP_ADD:
		if (symbol_is_fp_type(insn->type))
			target = LLVMBuildFAdd(fn->builder, lhs, rhs, target_name);
		else
			target = LLVMBuildAdd(fn->builder, lhs, rhs, target_name);
		break;
	case OP_SUB:
		if (symbol_is_fp_type(insn->type))
			target = LLVMBuildFSub(fn->builder, lhs, rhs, target_name);
		else
			target = LLVMBuildSub(fn->builder, lhs, rhs, target_name);
		break;
	case OP_MULU:
		if (symbol_is_fp_type(insn->type))
			target = LLVMBuildFMul(fn->builder, lhs, rhs, target_name);
		else
			target = LLVMBuildMul(fn->builder, lhs, rhs, target_name);
		break;
	case OP_MULS:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildMul(fn->builder, lhs, rhs, target_name);
		break;
	case OP_DIVU:
		if (symbol_is_fp_type(insn->type))
			target = LLVMBuildFDiv(fn->builder, lhs, rhs, target_name);
		else
			target = LLVMBuildUDiv(fn->builder, lhs, rhs, target_name);
		break;
	case OP_DIVS:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildSDiv(fn->builder, lhs, rhs, target_name);
		break;
	case OP_MODU:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildURem(fn->builder, lhs, rhs, target_name);
		break;
	case OP_MODS:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildSRem(fn->builder, lhs, rhs, target_name);
		break;
	case OP_SHL:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildShl(fn->builder, lhs, rhs, target_name);
		break;
	case OP_LSR:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildLShr(fn->builder, lhs, rhs, target_name);
		break;
	case OP_ASR:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildAShr(fn->builder, lhs, rhs, target_name);
		break;
	
	/* Logical */
	case OP_AND:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildAnd(fn->builder, lhs, rhs, target_name);
		break;
	case OP_OR:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildOr(fn->builder, lhs, rhs, target_name);
		break;
	case OP_XOR:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildXor(fn->builder, lhs, rhs, target_name);
		break;
	case OP_AND_BOOL:
		assert(0);
		break;
	case OP_OR_BOOL:
		assert(0);
		break;

	/* Binary comparison */
	case OP_SET_EQ:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildICmp(fn->builder, LLVMIntEQ, lhs, rhs, target_name);
		break;
	case OP_SET_NE:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildICmp(fn->builder, LLVMIntNE, lhs, rhs, target_name);
		break;
	case OP_SET_LE:
		assert(0);
		break;
	case OP_SET_GE:
		assert(0);
		break;
	case OP_SET_LT:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildICmp(fn->builder, LLVMIntSLT, lhs, rhs, target_name);
		break;
	case OP_SET_GT:
		assert(!symbol_is_fp_type(insn->type));
		target = LLVMBuildICmp(fn->builder, LLVMIntSGT, lhs, rhs, target_name);
		break;
	case OP_SET_B:
		assert(0);
		break;
	case OP_SET_A:
		assert(0);
		break;
	case OP_SET_BE:
		assert(0);
		break;
	case OP_SET_AE:
		assert(0);
		break;
	default:
		assert(0);
		break;
	}

	insn->target->priv = target;
}

static void output_op_ret(struct function *fn, struct instruction *insn)
{
	pseudo_t pseudo = insn->src;

	if (pseudo && pseudo != VOID) {
		LLVMValueRef result = pseudo_to_value(fn, insn, pseudo);

		LLVMBuildRet(fn->builder, result);
	} else
		LLVMBuildRetVoid(fn->builder);
}

static void output_op_load(struct function *fn, struct instruction *insn)
{
	LLVMTypeRef int_type;
	LLVMValueRef src_p, src_i, ofs_i, addr_i, addr, target;

	/* int type large enough to hold a pointer */
	int_type = LLVMIntType(bits_in_pointer);

	/* convert to integer, add src + offset */
	src_p = pseudo_to_value(fn, insn, insn->src);
	src_i = LLVMBuildPtrToInt(fn->builder, src_p, int_type, "src_i");

	ofs_i = LLVMConstInt(int_type, insn->offset, 0);
	addr_i = LLVMBuildAdd(fn->builder, src_i, ofs_i, "addr_i");

	/* convert address back to pointer */
	addr = LLVMBuildIntToPtr(fn->builder, addr_i,
				 LLVMPointerType(int_type, 0), "addr");

	/* perform load */
	target = LLVMBuildLoad(fn->builder, addr, "load_target");

	insn->target->priv = target;
}

static void output_op_store(struct function *fn, struct instruction *insn)
{
	LLVMTypeRef int_type;
	LLVMValueRef src_p, src_i, ofs_i, addr_i, addr, target, target_in;

	/* int type large enough to hold a pointer */
	int_type = LLVMIntType(bits_in_pointer);

	/* convert to integer, add src + offset */
	src_p = pseudo_to_value(fn, insn, insn->src);
	src_i = LLVMBuildPtrToInt(fn->builder, src_p, int_type, "src_i");

	ofs_i = LLVMConstInt(int_type, insn->offset, 0);
	addr_i = LLVMBuildAdd(fn->builder, src_i, ofs_i, "addr_i");

	/* convert address back to pointer */
	addr = LLVMBuildIntToPtr(fn->builder, addr_i,
				 LLVMPointerType(int_type, 0), "addr");

	target_in = pseudo_to_value(fn, insn, insn->target);

	/* perform store */
	target = LLVMBuildStore(fn->builder, target_in, addr);

	insn->target->priv = target;
}

static void output_op_br(struct function *fn, struct instruction *br)
{
	if (br->cond) {
		LLVMValueRef cond = pseudo_to_value(fn, br, br->cond);

		LLVMBuildCondBr(fn->builder, cond,
				br->bb_true->priv,
				br->bb_false->priv);
	} else
		LLVMBuildBr(fn->builder,
			    br->bb_true ? br->bb_true->priv :
			    br->bb_false->priv);
}

static void output_op_sel(struct function *fn, struct instruction *insn)
{
	LLVMValueRef target, src1, src2, src3;

	src1 = pseudo_to_value(fn, insn, insn->src1);
	src2 = pseudo_to_value(fn, insn, insn->src2);
	src3 = pseudo_to_value(fn, insn, insn->src3);

	target = LLVMBuildSelect(fn->builder, src1, src2, src3, "select");

	insn->target->priv = target;
}

static void output_op_switch(struct function *fn, struct instruction *insn)
{
	LLVMValueRef sw_val, target;
	struct basic_block *def = NULL;
	struct multijmp *jmp;
	int n_jmp = 0;

	FOR_EACH_PTR(insn->multijmp_list, jmp) {
		if (jmp->begin == jmp->end) {		/* case N */
			n_jmp++;
		} else if (jmp->begin < jmp->end) {	/* case M..N */
			assert(0);
		} else					/* default case */
			def = jmp->target;
	} END_FOR_EACH_PTR(jmp);

	sw_val = pseudo_to_value(fn, insn, insn->target);
	target = LLVMBuildSwitch(fn->builder, sw_val,
				 def ? def->priv : NULL, n_jmp);

	FOR_EACH_PTR(insn->multijmp_list, jmp) {
		if (jmp->begin == jmp->end) {		/* case N */
			LLVMAddCase(target,
				LLVMConstInt(LLVMInt32Type(), jmp->begin, 0),
				jmp->target->priv);
		} else if (jmp->begin < jmp->end) {	/* case M..N */
			assert(0);
		}
	} END_FOR_EACH_PTR(jmp);

	insn->target->priv = target;
}

struct llfunc {
	char		name[256];	/* wasteful */
	LLVMValueRef	func;
};

DECLARE_ALLOCATOR(llfunc);
DECLARE_PTR_LIST(llfunc_list, struct llfunc);
ALLOCATOR(llfunc, "llfuncs");

static struct local_module {
	struct llfunc_list	*llfunc_list;
} mi;

static LLVMTypeRef get_func_type(struct function *fn, struct instruction *insn)
{
	struct symbol *sym = insn->func->sym;
	char buffer[256];
	LLVMTypeRef func_type, ret_type;
	struct pseudo *arg;
	int n_arg = 0;
	LLVMTypeRef *arg_type;

	sprintf(buffer, "%.*s", sym->ident->len, sym->ident->name);

	/* VERIFY: is this correct, for functions? */
	func_type = LLVMGetTypeByName(fn->module, buffer);
	if (func_type)
		return func_type;

	/* to avoid strangeness with varargs [for now], we build
	 * the function and type anew, for each call.  This
	 * is probably wrong.  We should look up the
	 * symbol declaration info.
	 */

	/* build return type */
	if (insn->target && insn->target != VOID)
		ret_type = pseudo_type(fn, insn, insn->target);
	else
		ret_type = LLVMVoidType();

	/* count args, build argument type information */
	FOR_EACH_PTR(insn->arguments, arg) {
		n_arg++;
	} END_FOR_EACH_PTR(arg);

	arg_type = calloc(n_arg, sizeof(LLVMTypeRef));

	int idx = 0;
	FOR_EACH_PTR(insn->arguments, arg) {
		arg_type[idx++] = pseudo_type(fn, insn, arg);
	} END_FOR_EACH_PTR(arg);

	func_type = LLVMFunctionType(ret_type, arg_type, n_arg,
				     /* varargs? */ 0);

	return func_type;
}

static LLVMValueRef get_function(struct function *fn, struct instruction *insn)
{
	struct symbol *sym = insn->func->sym;
	char buffer[256];
	LLVMValueRef func;
	struct llfunc *f;

	sprintf(buffer, "%.*s", sym->ident->len, sym->ident->name);

	/* search for pre-built function type definition */
	FOR_EACH_PTR(mi.llfunc_list, f) {
		if (!strcmp(f->name, buffer))
			return f->func;		/* found match; return */
	} END_FOR_EACH_PTR(f);

	/* build function type definition */
	LLVMTypeRef func_type = get_func_type(fn, insn);

	func = LLVMAddFunction(fn->module, buffer, func_type);

	/* store built function on list, for later referencing */
	f = calloc(1, sizeof(*f));
	strncpy(f->name, buffer, sizeof(f->name) - 1);
	f->func = func;

	add_ptr_list(&mi.llfunc_list, f);

	return func;
}

static void output_op_call(struct function *fn, struct instruction *insn)
{
	LLVMValueRef target, func;
	int n_arg = 0, i;
	struct pseudo *arg;
	LLVMValueRef *args;

	FOR_EACH_PTR(insn->arguments, arg) {
		n_arg++;
	} END_FOR_EACH_PTR(arg);

	args = calloc(n_arg, sizeof(LLVMValueRef));

	i = 0;
	FOR_EACH_PTR(insn->arguments, arg) {
		args[i++] = pseudo_to_value(fn, insn, arg);
	} END_FOR_EACH_PTR(arg);

	func = get_function(fn, insn);
	target = LLVMBuildCall(fn->builder, func, args, n_arg, "");

	insn->target->priv = target;
}

static void output_op_phi(struct function *fn, struct instruction *insn)
{
	pseudo_t phi;
	LLVMValueRef target;

	target = LLVMBuildPhi(fn->builder, insn_symbol_type(insn),
				"phi");
	int pll = 0;
	FOR_EACH_PTR(insn->phi_list, phi) {
		if (pseudo_to_value(fn, insn, phi))	/* skip VOID */
			pll++;
	} END_FOR_EACH_PTR(phi);

	LLVMValueRef *phi_vals = calloc(pll, sizeof(LLVMValueRef));
	LLVMBasicBlockRef *phi_blks = calloc(pll, sizeof(LLVMBasicBlockRef));

	int idx = 0;
	FOR_EACH_PTR(insn->phi_list, phi) {
		LLVMValueRef v;

		v = pseudo_to_value(fn, insn, phi);
		if (v) {			/* skip VOID */
			phi_vals[idx] = v;
			phi_blks[idx] = phi->def->bb->priv;
			idx++;
		}
	} END_FOR_EACH_PTR(phi);

	LLVMAddIncoming(target, phi_vals, phi_blks, pll);

	insn->target->priv = target;
}

static void output_op_cast(struct function *fn, struct instruction *insn, LLVMOpcode op)
{
	LLVMValueRef src, target;
	char target_name[64];

	src = insn->src->priv;
	if (!src)
		src = pseudo_to_value(fn, insn, insn->src);

	pseudo_name(insn->target, target_name);

	assert(!symbol_is_fp_type(insn->type));

	if (insn->size < LLVMGetIntTypeWidth(LLVMTypeOf(src)))
		target = LLVMBuildTrunc(fn->builder, src, insn_symbol_type(insn), target_name);
	else
		target = LLVMBuildCast(fn->builder, op, src, insn_symbol_type(insn), target_name);

	insn->target->priv = target;
}

static void output_op_copy(struct function *fn, struct instruction *insn,
			   pseudo_t pseudo)
{
	LLVMValueRef src, target;
	LLVMTypeRef const_type;
	char target_name[64];

	pseudo_name(insn->target, target_name);
	src = pseudo_to_value(fn, insn, pseudo);
	const_type = insn_symbol_type(insn);

	/*
	 * This is nothing more than 'target = src'
	 *
	 * TODO: find a better way to provide an identity function,
	 * than using "X + 0" simply to produce a new LLVM pseudo
	 */

	if (symbol_is_fp_type(insn->type))
		target = LLVMBuildFAdd(fn->builder, src,
			LLVMConstReal(const_type, 0.0), target_name);
	else
		target = LLVMBuildAdd(fn->builder, src,
			LLVMConstInt(const_type, 0, 0), target_name);

	insn->target->priv = target;
}

static void output_insn(struct function *fn, struct instruction *insn)
{
	switch (insn->opcode) {
	case OP_RET:
		output_op_ret(fn, insn);
		break;
	case OP_BR:
		output_op_br(fn, insn);
		break;
	case OP_SYMADDR:
		assert(0);
		break;
	case OP_SETVAL:
		assert(0);
		break;
	case OP_SWITCH:
		output_op_switch(fn, insn);
		break;
	case OP_COMPUTEDGOTO:
		assert(0);
		break;
	case OP_PHISOURCE:
		/* target = src */
		insn->target->priv = pseudo_to_value(fn, insn, insn->phi_src);
		break;
	case OP_PHI:
		output_op_phi(fn, insn);
		break;
	case OP_LOAD:
		output_op_load(fn, insn);
		break;
	case OP_LNOP:
		assert(0);
		break;
	case OP_STORE:
		output_op_store(fn, insn);
		break;
	case OP_SNOP:
		assert(0);
		break;
	case OP_INLINED_CALL:
		assert(0);
		break;
	case OP_CALL:
		output_op_call(fn, insn);
		break;
	case OP_CAST:
		output_op_cast(fn, insn, LLVMZExt);
		break;
	case OP_SCAST:
		output_op_cast(fn, insn, LLVMSExt);
		break;
	case OP_FPCAST:
		assert(0);
		break;
	case OP_PTRCAST:
		assert(0);
		break;
	case OP_BINARY ... OP_BINARY_END:
	case OP_BINCMP ... OP_BINCMP_END:
		output_op_binary(fn, insn);
		break;
	case OP_SEL:
		output_op_sel(fn, insn);
		break;
	case OP_SLICE:
		assert(0);
		break;
	case OP_NOT: {
		LLVMValueRef src, target;
		char target_name[64];

		src = pseudo_to_value(fn, insn, insn->src);

		pseudo_name(insn->target, target_name);

		target = LLVMBuildNot(fn->builder, src, target_name);

		insn->target->priv = target;
		break;
	}
	case OP_NEG:
		assert(0);
		break;
	case OP_CONTEXT:
		assert(0);
		break;
	case OP_RANGE:
		assert(0);
		break;
	case OP_NOP:
		assert(0);
		break;
	case OP_DEATHNOTE:
		assert(0);
		break;
	case OP_ASM:
		assert(0);
		break;
	case OP_COPY:
		output_op_copy(fn, insn, insn->src);
		break;
	default:
		break;
	}
}

static void output_bb(struct function *fn, struct basic_block *bb, unsigned long generation)
{
	struct instruction *insn;

	bb->generation = generation;

	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;

		output_insn(fn, insn);
	}
	END_FOR_EACH_PTR(insn);
}

#define MAX_ARGS	64

static void output_fn(LLVMModuleRef module, struct entrypoint *ep)
{
	unsigned long generation = ++bb_generation;
	struct symbol *sym = ep->name;
	struct symbol *base_type = sym->ctype.base_type;
	struct symbol *ret_type = sym->ctype.base_type->ctype.base_type;
	LLVMTypeRef arg_types[MAX_ARGS];
	LLVMTypeRef return_type;
	struct function function;
	struct basic_block *bb;
	struct symbol *arg;
	const char *name;
	int nr_args = 0;
	struct llfunc *f;

	FOR_EACH_PTR(base_type->arguments, arg) {
		struct symbol *arg_base_type = arg->ctype.base_type;

		arg_types[nr_args++] = symbol_type(arg_base_type);
	} END_FOR_EACH_PTR(arg);

	name = show_ident(sym->ident);

	return_type = symbol_type(ret_type);

	function.module = module;

	function.type = LLVMFunctionType(return_type, arg_types, nr_args, 0);

	function.fn = LLVMAddFunction(module, name, function.type);
	LLVMSetFunctionCallConv(function.fn, LLVMCCallConv);

	LLVMSetLinkage(function.fn, function_linkage(sym));

	/* store built function on list, for later referencing */
	f = calloc(1, sizeof(*f));
	strncpy(f->name, name, sizeof(f->name) - 1);
	f->func = function.fn;

	add_ptr_list(&mi.llfunc_list, f);

	function.builder = LLVMCreateBuilder();

	static int nr_bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb->generation == generation)
			continue;

		LLVMBasicBlockRef bbr;
		char bbname[32];

		sprintf(bbname, "L%d", nr_bb++);
		bbr = LLVMAppendBasicBlock(function.fn, bbname);

		bb->priv = bbr;
	}
	END_FOR_EACH_PTR(bb);

	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb->generation == generation)
			continue;

		LLVMPositionBuilderAtEnd(function.builder, bb->priv);

		output_bb(&function, bb, generation);
	}
	END_FOR_EACH_PTR(bb);
}

static int output_data(LLVMModuleRef module, struct symbol *sym)
{
	struct expression *initializer = sym->initializer;
	LLVMValueRef initial_value;
	LLVMValueRef data;
	const char *name;

	if (initializer) {
		switch (initializer->type) {
		case EXPR_VALUE:
			initial_value = LLVMConstInt(symbol_type(sym), initializer->value, 1);
			break;
		case EXPR_SYMBOL: {
			struct symbol *sym = initializer->symbol;

			initial_value = LLVMGetNamedGlobal(module, show_ident(sym->ident));
			break;
		}
		default:
			assert(0);
		}
	} else {
		LLVMTypeRef type = symbol_type(sym);

		initial_value = LLVMConstNull(type);
	}

	name = show_ident(sym->ident);

	data = LLVMAddGlobal(module, symbol_type(sym->ctype.base_type), name);

	LLVMSetLinkage(data, data_linkage(sym));

	LLVMSetInitializer(data, initial_value);

	return 0;
}

static int compile(LLVMModuleRef module, struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct entrypoint *ep;
		expand_symbol(sym);
		ep = linearize_symbol(sym);
		if (ep)
			output_fn(module, ep);
		else
			output_data(module, sym);
	}
	END_FOR_EACH_PTR(sym);

	return 0;
}

int main(int argc, char **argv)
{
	struct string_list * filelist = NULL;
	char *file;

	LLVMModuleRef module = LLVMModuleCreateWithName("sparse");

	compile(module, sparse_initialize(argc, argv, &filelist));

	FOR_EACH_PTR_NOTAG(filelist, file) {
		compile(module, sparse(file));
	} END_FOR_EACH_PTR_NOTAG(file);

	LLVMWriteBitcodeToFD(module, STDOUT_FILENO, 0, 0);

	LLVMDisposeModule(module);

	return 0;
}
