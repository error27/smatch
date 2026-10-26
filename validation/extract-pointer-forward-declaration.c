struct container {
	void (*callback)(struct incomplete *member);
};

/*
 * check-name: extract function pointer forward declaration
 * check-command: extract $file && grep -x 'struct incomplete;' $file.extract
 *
 * check-output-start
extract: wrote extract-pointer-forward-declaration.c.extract
struct incomplete;
 * check-output-end
 */
