/*
 * check-name: extract aggregate member typedef
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
 * extract: wrote $file.extract
 * check-output-end
 */

#define __bitwise __attribute__((bitwise))

typedef unsigned int __bitwise result_t;

struct result {
	result_t value;
};

struct result result;
