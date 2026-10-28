#define __bitwise __attribute__((bitwise))

typedef unsigned int __bitwise word_t;

static unsigned int load_word(const word_t *word)
{
	return *word;
}

static const struct {
	word_t value;
} data;

int target(void)
{
	return load_word(&data.value);
}

/*
 * check-name: extract const typedef parameter
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-const-typedef-parameter.c.extract
 * check-output-end
 */
