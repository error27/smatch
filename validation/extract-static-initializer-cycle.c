struct item {
	struct item *next;
};

static struct item item = {
	.next = &item,
};

int target(void)
{
	return item.next == &item;
}

/*
 * check-name: extract static initializer cycle
 * check-command: extract $file >/dev/null 2>$file.error && ! grep -Fq 'cannot forward declare dependency item' $file.error && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
 * check-output-end
 */
