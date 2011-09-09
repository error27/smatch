struct ctype {
	int			type;
};

struct symbol {
	void			*p;
	const char		*name;
	struct ctype		ctype;
};

static struct symbol sym;
static struct symbol *sym_p;
static struct symbol *sym_q = &sym;

/*
 * check-name: Struct code generation
 * check-command: ./sparsec -c $file -o tmp.o
 */
