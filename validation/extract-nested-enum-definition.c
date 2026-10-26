struct container {
	struct {
		enum nested_state {
			NESTED_STATE,
		} state;
	} nested;
};

/*
 * check-name: extract nested enum definition
 * check-command: extract $file && gcc -xc -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-nested-enum-definition.c.extract
 * check-output-end
 */
