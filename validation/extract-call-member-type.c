struct hidden {
	int member;
};

static inline struct hidden *get_hidden(void)
{
	return 0;
}

int target(void)
{
	return get_hidden()->member;
}

/*
 * check-name: extract aggregate used by function-call member access
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-call-member-type.c.extract.last
 * check-output-end
 */
