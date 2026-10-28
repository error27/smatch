struct lock {
	int value;
};

struct outer {
	struct lock *lock;
};

static inline int read_lock(struct outer *outer)
{
	return &((outer->lock))->value != 0;
}

int target(struct outer *outer)
{
	return read_lock(outer);
}

/*
 * check-name: extract inline member type
 * check-command: extract --func=target $file && gcc -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-inline-member-type.c.extract.target
 * check-output-end
 */
