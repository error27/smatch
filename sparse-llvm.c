/*
 * Example usage:
 *	./sparse-llvm hello.c | llc | as -o hello.o
 */

#include <llvm-c/Core.h>
#include <llvm-c/BitWriter.h>

#include <stdio.h>
#include <unistd.h>
#include <assert.h>

#include "symbol.h"
#include "expression.h"
#include "linearize.h"
#include "flow.h"

struct function {
	LLVMBuilderRef			builder;
	LLVMTypeRef			type;
	LLVMValueRef			fn;
};

static LLVMTypeRef symbol_type(struct symbol *sym)
{
	LLVMTypeRef ret = NULL;

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
	};

	return ret;
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

static void output_op_binary(struct function *fn, struct instruction *insn)
{
	switch (insn->opcode) {
	/* Binary */
	case OP_ADD:
		assert(0);
		break;
	case OP_SUB:
		assert(0);
		break;
	case OP_MULU:
		assert(0);
		break;
	case OP_MULS:
		assert(0);
		break;
	case OP_DIVU:
		assert(0);
		break;
	case OP_DIVS:
		assert(0);
		break;
	case OP_MODU:
		assert(0);
		break;
	case OP_MODS:
		assert(0);
		break;
	case OP_SHL:
		assert(0);
		break;
	case OP_LSR:
		assert(0);
		break;
	case OP_ASR:
		assert(0);
		break;
	
	/* Logical */
	case OP_AND:
		assert(0);
		break;
	case OP_OR:
		assert(0);
		break;
	case OP_XOR:
		assert(0);
		break;
	case OP_AND_BOOL:
		assert(0);
		break;
	case OP_OR_BOOL:

	/* Binary comparison */
	case OP_SET_EQ:
		assert(0);
		break;
	case OP_SET_NE:
		assert(0);
		break;
	case OP_SET_LE:
		assert(0);
		break;
	case OP_SET_GE:
		assert(0);
		break;
	case OP_SET_LT:
		assert(0);
		break;
	case OP_SET_GT:
		assert(0);
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
}

static void output_op_ret(struct function *fn, struct instruction *insn)
{
	pseudo_t pseudo = insn->src;

	if (pseudo && pseudo != VOID) {
		switch (pseudo->type) {
		case PSEUDO_REG:
			assert(0);
			break;
		case PSEUDO_SYM:
			assert(0);
			break;
		case PSEUDO_VAL:
			LLVMBuildRet(fn->builder, LLVMConstInt(LLVMGetReturnType(fn->type), pseudo->value, 1));
			break;
		case PSEUDO_ARG: {
			LLVMValueRef param = LLVMGetParam(fn->fn, pseudo->nr - 1);
			LLVMBuildRet(fn->builder, param);
			break;
		}
		case PSEUDO_PHI:
			assert(0);
			break;
		default:
			assert(0);
		}
	} else
		LLVMBuildRetVoid(fn->builder);
}

static void output_insn(struct function *fn, struct instruction *insn)
{
	switch (insn->opcode) {
	case OP_RET:
		output_op_ret(fn, insn);
		break;
	case OP_BR:
		break;
	case OP_SYMADDR:
		break;
	case OP_SETVAL:
		break;
	case OP_SWITCH:
		break;
	case OP_COMPUTEDGOTO:
		break;
	case OP_PHISOURCE:
		break;
	case OP_PHI:
		break;
	case OP_LOAD: case OP_LNOP:
		break;
	case OP_STORE: case OP_SNOP:
		break;
	case OP_INLINED_CALL:
	case OP_CALL:
		break;
	case OP_CAST:
	case OP_SCAST:
	case OP_FPCAST:
	case OP_PTRCAST:
		break;
	case OP_BINARY ... OP_BINARY_END:
	case OP_BINCMP ... OP_BINCMP_END:
		output_op_binary(fn, insn);
		break;
	case OP_SEL:
		break;
	case OP_SLICE:
		break;
	case OP_NOT: case OP_NEG:
		break;
	case OP_CONTEXT:
		break;
	case OP_RANGE:
		break;
	case OP_NOP:
		break;
	case OP_DEATHNOTE:
		break;
	case OP_ASM:
		break;
	case OP_COPY:
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

	FOR_EACH_PTR(base_type->arguments, arg) {
		struct symbol *arg_base_type = arg->ctype.base_type;

		arg_types[nr_args++] = symbol_type(arg_base_type);
	} END_FOR_EACH_PTR(arg);

	name = show_ident(sym->ident);

	return_type = symbol_type(ret_type);

	function.type = LLVMFunctionType(return_type, arg_types, nr_args, 0);

	function.fn = LLVMAddFunction(module, name, function.type);

	LLVMSetLinkage(function.fn, function_linkage(sym));

	unssa(ep);

	function.builder = LLVMCreateBuilder();

	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(function.fn, "entry");

	LLVMPositionBuilderAtEnd(function.builder, entry);

	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb->generation == generation)
			continue;

		output_bb(&function, bb, generation);
	}
	END_FOR_EACH_PTR(bb);
}

static int output_data(LLVMModuleRef module, struct symbol *sym)
{
	struct expression *initializer = sym->initializer;
	unsigned long long initial_value = 0;
	LLVMValueRef data;
	const char *name;

	if (initializer) {
		if (initializer->type == EXPR_VALUE)
			initial_value = initializer->value;
		else
			assert(0);
	}

	name = show_ident(sym->ident);

	data = LLVMAddGlobal(module, symbol_type(sym->ctype.base_type), name);

	LLVMSetLinkage(data, data_linkage(sym));

	LLVMSetInitializer(data, LLVMConstInt(symbol_type(sym), initial_value, 1));

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

#if 1
	LLVMWriteBitcodeToFD(module, STDOUT_FILENO, 0, 0);
#else
	LLVMDumpModule(module);
#endif

	LLVMDisposeModule(module);

	return 0;
}
