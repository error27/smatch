struct s {
	char c[1];
};

struct s fun(void);


static void foo(void)
{
	char c[1];
	c = fun().c;
}

/*
 * check-name: eval-bad-assign2
 *
 * check-error-start
eval-bad-assign2.c:11:11: warning: incorrect type in assignment (invalid types)
eval-bad-assign2.c:11:11:    expected char c[1]
eval-bad-assign2.c:11:11:    got char *
 * check-error-end
 */
