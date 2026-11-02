int test(int x)
{
	struct req_data {
		short addr;
		char val;
	};
	const int size = 4 * sizeof(struct req_data);
	struct req_data *data = 0;

	if (x) {
		struct nested { int a, b; };

		x = sizeof(struct nested);
	}
	return size + x + (data != 0);
}

/*
 * check-name: extract a local struct which is used before any variable of that type
 * check-command: extract --func=test $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-local-tag-sizeof.c.extract.test
 * check-output-end
 */
