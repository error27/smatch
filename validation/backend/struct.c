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

/*
 * check-name: Struct code generation
 * check-command: ./sparsec -c $file -o tmp.o
 */
