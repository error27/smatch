/*
 * check-name: extract transparent union typedef
 * check-command: extract $file && gcc -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
 * extract: wrote $file.extract
 * check-output-end
 */

struct encryption_key {
	int rounds;
};

struct full_key {
	int rounds;
	int inverse_rounds;
};

typedef union {
	const struct encryption_key *encryption_key;
	const struct full_key *full_key;
} encrypt_arg __attribute__((__transparent_union__));

extern void encrypt(encrypt_arg key);

void encrypt_with_encryption_key(struct encryption_key *key)
{
	encrypt(key);
}
