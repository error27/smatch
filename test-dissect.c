#include "dissect.h"

static inline const char *show_mode(unsigned mode)
{
	static char str[3];

	if (mode == -1)
		return "def";

#define	U(u_r)	"-rwm"[(mode / u_r) & 3]
	str[0] = U(U_R_AOF);
	str[1] = U(U_R_VAL);
	str[2] = U(U_R_PTR);
#undef	U

	return str;
}

static void print_usage(struct position *pos, struct symbol *sym, unsigned mode)
{
	static unsigned curr_stream = -1;
	static struct ident null;
	struct ident *ctx = &null;

	if (curr_stream != pos->stream) {
		curr_stream = pos->stream;
		printf("\nFILE: %s\n\n", stream_name(curr_stream));
	}

	if (dissect_ctx)
		ctx = dissect_ctx->ident;

	printf("%4d:%-3d %-16.*s %s ",
		pos->line, pos->pos, ctx->len, ctx->name, show_mode(mode));

}

static char symscope(struct symbol *sym)
{
	if (sym_is_local(sym)) {
		if (!dissect_ctx)
			warning(sym->pos, "no context");
		return '.';
	}
	return ' ';
}

static void r_symbol(unsigned mode, struct position *pos, struct symbol *sym)
{
	print_usage(pos, sym, mode);

	if (!sym->ident)
		sym->ident = built_in_ident("__asm__");

	printf("%c %c %-32.*s %s\n",
		symscope(sym), sym->kind, sym->ident->len, sym->ident->name,
		show_typename(sym->ctype.base_type));

	switch (sym->kind) {
	case 's':
		if (sym->type == SYM_STRUCT || sym->type == SYM_UNION)
			break;
		goto err;

	case 'f':
		if (sym->type != SYM_BAD && sym->ctype.base_type->type != SYM_FN)
			goto err;
	case 'v':
		if (sym->type == SYM_NODE || sym->type == SYM_BAD)
			break;
		goto err;
	default:
		goto err;
	}

	return;
err:
	warning(*pos, "r_symbol bad sym type=%d kind=%d", sym->type, sym->kind);
}

static void r_member(unsigned mode, struct position *pos, struct symbol *sym, struct symbol *mem)
{
	struct ident *ni, *si, *mi;

	print_usage(pos, sym, mode);

	ni = built_in_ident("?");
	si = sym->ident ?: ni;
	/* mem == NULL means entire struct accessed */
	mi = mem ? (mem->ident ?: ni) : built_in_ident("*");

	printf("%c m %.*s.%-*.*s %s\n",
		symscope(sym), si->len, si->name,
		32-1 - si->len, mi->len, mi->name,
		show_typename(mem ? mem->ctype.base_type : sym));

	if (sym->ident && sym->kind != 's')
		warning(*pos, "r_member bad sym type=%d kind=%d", sym->type, sym->kind);
	if (mem && mem->kind != 'm')
		warning(*pos, "r_member bad mem->kind = %d", mem->kind);
}

static void r_symdef(struct symbol *sym)
{
	r_symbol(-1, &sym->pos, sym);
}

static void r_memdef(struct symbol *sym, struct symbol *mem)
{
	r_member(-1, &mem->pos, sym, mem);
}

int main(int argc, char **argv)
{
	static struct reporter reporter = {
		.r_symdef = r_symdef,
		.r_memdef = r_memdef,
		.r_symbol = r_symbol,
		.r_member = r_member,
	};

	struct string_list *filelist = NULL;
	sparse_initialize(argc, argv, &filelist);
	dissect(&reporter, filelist);

	return 0;
}
