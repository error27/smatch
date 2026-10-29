struct inner;

struct outer {
	struct inner *value;
};

struct inner {
	int value;
};

static struct inner entry = {
	.value = 1,
};

static struct outer table = {
	.value = &entry,
};

int target(void)
{
	return table.value != 0;
}

/*
 * check-name: extract initialized aggregate dependency
 * check-command: extract --func=target $file && gcc -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-initializer-aggregate-definition.c.extract.target
 * check-output-end
 */
