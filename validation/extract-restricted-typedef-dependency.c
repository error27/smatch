/*
 * check-name: extract restricted typedef dependency
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-restricted-typedef-dependency.c.extract
 * check-output-end
 */

#define __bitwise __attribute__((bitwise))

typedef unsigned int __bitwise vm_fault_t;

struct vm_fault;
struct vm_operations_struct {
	vm_fault_t (*fault)(struct vm_fault *vmf);
};

struct vm_operations_struct vm_operations;
