static void ko(int x)
{
	__assert((~x) == (~0 + x));
}

/*
 * check-name: scheck-ko
 * check-command: scheck $file
 * check-known-to-fail
 */
