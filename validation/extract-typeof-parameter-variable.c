struct req { unsigned long long tid; };
extern struct req __lookup_req_key;

static int lookup(typeof(__lookup_req_key.tid) key)
{
	return key == 1;
}

int test(void)
{
	return lookup(1);
}

/*
 * check-name: extract typeof() of a variable in a parameter type
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-typeof-parameter-variable.c.extract.last
 * check-output-end
 */
