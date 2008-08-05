void a(void)
{
	switch(x) {
	case 1:
		break;
	}
}
/*
 * check-name: switch(bad_type) {...} segfault
 *
 * check-error-start
badtype4.c:3:9: error: undefined identifier 'x'
badtype4.c:4:7: error: incompatible types for 'case' statement
 * check-error-end
 */
