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

static void output_insn(LLVMBuilderRef builder, struct instruction *insn)
{
	switch (insn->opcode) {
	case OP_RET:
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

static void output_bb(LLVMBuilderRef builder, struct basic_block *bb, unsigned long generation)
{
	struct instruction *insn;

	bb->generation = generation;

	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;

		output_insn(builder, insn);
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
	struct basic_block *bb;
	LLVMValueRef function;
	struct symbol *arg;
	const char *name;
	int nr_args = 0;

	FOR_EACH_PTR(base_type->arguments, arg) {
		struct symbol *arg_base_type = arg->ctype.base_type;

		arg_types[nr_args++] = symbol_type(arg_base_type);
	} END_FOR_EACH_PTR(arg);

	name = show_ident(sym->ident);

	function = LLVMAddFunction(module, name, LLVMFunctionType(symbol_type(ret_type), arg_types, nr_args, 0));

	LLVMSetLinkage(function, function_linkage(sym));

	unssa(ep);

	LLVMBuilderRef builder = LLVMCreateBuilder();

	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(function, "entry");
	LLVMBasicBlockRef exit = LLVMAppendBasicBlock(function, "exit");

	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb->generation == generation)
			continue;

		LLVMBuilderRef builder = LLVMCreateBuilder();

		LLVMPositionBuilderAtEnd(builder, entry);

		output_bb(builder, bb, generation);
	}
	END_FOR_EACH_PTR(bb);

	LLVMPositionBuilderAtEnd(builder, entry);
	LLVMBuildBr(builder, exit);

	LLVMPositionBuilderAtEnd(builder, exit);

	if (ret_type == &void_ctype)
		LLVMBuildRetVoid(builder);
	else
		LLVMBuildRet(builder, LLVMConstInt(symbol_type(ret_type), 0, 1));
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
