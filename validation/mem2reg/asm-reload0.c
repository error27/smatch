static int asm_reload(void)
{
	int mem = 0;
	asm volatile ("[%1] <= 1" : "=m" (mem));
	return mem;
}

/*
 * check-name: asm-reload0
 * check-command: test-linearize $file
 *
 * check-output-ignore
 * check-output-contains: load\\.
 */
