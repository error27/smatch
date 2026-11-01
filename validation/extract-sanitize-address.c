#ifndef __SANITIZE_ADDRESS__
#error "__SANITIZE_ADDRESS__ is not defined"
#endif
#ifndef __SANITIZE_HWADDRESS__
#error "__SANITIZE_HWADDRESS__ is not defined"
#endif

int test(void)
{
	return 0;
}

/*
 * check-name: extract defines the address sanitizer macros
 * check-command: extract --last-func -fsanitize=kernel-address,bounds -fsanitize=kernel-hwaddress $file
 *
 * check-output-start
extract: wrote extract-sanitize-address.c.extract.last
 * check-output-end
 */
