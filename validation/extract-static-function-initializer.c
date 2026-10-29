struct operations {
	int (*open)(void);
};

static int table_open(void)
{
	return 0;
}

static const struct operations table_ops = {
	.open = table_open,
};

int target(void)
{
	return table_ops.open();
}

/*
 * check-name: extract static function initializer dependency
 * check-command: extract --func=target $file && gcc -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-static-function-initializer.c.extract.target
 * check-output-end
 */
