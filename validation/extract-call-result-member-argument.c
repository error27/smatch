/*
 * check-name: extract call result member access with opaque argument
 * check-command: extract $file >$file.output 2>&1 && ! grep -Fq 'missing definition for struct opaque_ref' $file.output && cat $file.output && gcc -std=gnu11 -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-call-result-member-argument.c.extract
 * check-output-end
 */

typedef struct opaque_ref *key_ref_t;

struct key {
	int serial;
};

extern struct key *key_ref_to_ptr(key_ref_t key_ref);

int key_ref_serial(key_ref_t key)
{
	return key_ref_to_ptr(key)->serial;
}
