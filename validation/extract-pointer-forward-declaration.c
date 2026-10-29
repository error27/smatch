struct container {
	void (*callback)(struct incomplete *member);
	void (*release)(struct incomplete *member);
};

/*
 * check-name: extract function pointer forward declaration
 * check-command: extract $file && test "$(grep -cx 'struct incomplete;' $file.extract)" = 1
 *
 * check-output-start
extract: wrote extract-pointer-forward-declaration.c.extract
 * check-output-end
 */
