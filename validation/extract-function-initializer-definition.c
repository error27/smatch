struct operations {
	int (*open)(void);
};

int table_open(void)
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
 * check-name: extract external function initializer definition
 * check-command: extract --func=target $file && grep -Fq 'int table_open(void)' $file.extract.target && gcc -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-function-initializer-definition.c.extract.target
 * check-output-end
 */
