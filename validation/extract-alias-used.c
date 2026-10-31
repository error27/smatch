/*
 * check-name: extract puts used on the alias target
 * check-command: extract $file && grep -Fq 'sbox[] __attribute__((used))' $file.extract && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-alias-used.c.extract
 * check-output-end
 */

static const unsigned char sbox[] = { 1, 2, 3, 4 };
extern const unsigned char crypto_sbox[4] __attribute__((__alias__("sbox")));
static void *addressable = (void *)&crypto_sbox;
