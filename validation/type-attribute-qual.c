static const struct s {
	int x;
} map[2];

static void foo(struct s *p, int v)
{
	p->x += v;
}

/*
 * check-name: type-attribute-qual
 * check-description: When declaring a type and a variable in the same
 *	declaration, ensure that type qualifiers apply to the variable
 *	and not to the type.
 */
