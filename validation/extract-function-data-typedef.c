typedef unsigned int flags_t;
extern flags_t flags;

int target(void)
{
	return flags;
}

/*
 * check-name: extract function data typedef
 * check-command: extract --func=target $file && gcc -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-function-data-typedef.c.extract.target
 * check-output-end
 */
