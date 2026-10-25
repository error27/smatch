/*
 * check-name: extract typedef before aggregate
 * check-command: extract $file && gcc -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
 * extract: wrote $file.extract
 * check-output-end
 */

#define __bitwise __attribute__((bitwise))

typedef unsigned long __bitwise object_ref;

struct callback_table {
	void (*release)(object_ref object);
};

struct callback_table callbacks;
