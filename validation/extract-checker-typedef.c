/*
 * check-name: extract checker typedef
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
 * extract: wrote $file.extract
 * check-output-end
 */

#define __bitwise __attribute__((bitwise))

typedef unsigned short __u16;
typedef __u16 __bitwise __le16;

static __le16 value;
