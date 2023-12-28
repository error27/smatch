extern char __tdata_size[];
extern char __tdata_align[];

unsigned long z_tls_data_size(void);
unsigned long z_tls_data_size(void)
{
	return ((unsigned long)__tdata_size) + ((unsigned long)__tdata_align - 1);
}

/*
 * check-name: bug-zephyr-63417
 * check-timeout:
 */
