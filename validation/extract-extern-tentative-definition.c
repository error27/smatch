/*
 * check-name: extract extern tentative definition
 * check-command: extract $file && grep -Fqx 'struct chain chains[10];' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-extern-tentative-definition.c.extract
 * check-output-end
 */

struct chain {
	int key;
};

extern struct chain chains[];

int first_key(void)
{
	return chains[0].key;
}

struct chain chains[10];

int chain_count(void)
{
	return sizeof(chains) / sizeof(chains[0]);
}
