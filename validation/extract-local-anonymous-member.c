struct item {
	int value;
};

extern void use_item(struct item *item);

int target(void)
{
	struct {
		struct item item;
	} wrapper;

	use_item(&wrapper.item);
	return wrapper.item.value;
}

/*
 * check-name: extract local anonymous member dependency
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-local-anonymous-member.c.extract.last
 * check-output-end
 */
