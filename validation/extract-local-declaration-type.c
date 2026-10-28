struct local_declaration_type {
	int member;
};

int target(void)
{
	return ({
		static struct local_declaration_type value;

		0;
	});
}

/*
 * check-name: extract aggregate used by a local declaration
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-local-declaration-type.c.extract.last
 * check-output-end
 */
