struct inline_data_type {
	int member;
};

static union {
	struct inline_data_type value;
	char padding[64];
} values[16];

static inline unsigned long value_address(void)
{
	return (unsigned long)&values;
}

unsigned long target(void)
{
	return value_address();
}

/*
 * check-name: extract type used by inline static data
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-inline-data-type.c.extract.last
 * check-output-end
 */
