unsigned int reset_devices;
extern typeof(reset_devices) reset_devices;

static inline unsigned int read_reset_devices(void)
{
	return reset_devices;
}

int main(void)
{
	return read_reset_devices();
}

/*
 * check-name: extract inline typeof data
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-inline-typeof-data.c.extract
 * check-output-end
 */
