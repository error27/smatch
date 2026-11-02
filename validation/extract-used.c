struct entry { int line; };

static void __attribute__((__used__)) helper(void)
{
}

int test(void)
{
	static const struct entry entry __attribute__((__used__)) = { .line = 1 };
	static const struct entry *entry_ptr __attribute__((__used__)) = &entry;

	return 0;
}

/*
 * check-name: extract keeps the used attribute
 * check-command: extract $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-used.c.extract
 * check-output-end
 */
