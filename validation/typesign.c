static unsigned int * s_to_u_return(signed int *sp)
{
	return sp;
}

static signed int * u_to_s_return(unsigned int *up)
{
	return up;
}

static unsigned int * s_to_u_init(signed int *sp)
{
	unsigned int *up = sp;
	return up;
}

static signed int * u_to_s_init(unsigned int *up)
{
	signed int *sp = up;
	return sp;
}

static unsigned int * s_to_u_assign(signed int *sp)
{
	unsigned int *up;
	up = sp;
	return up;
}

static signed int * u_to_s_assign(unsigned int *up)
{
	signed int *sp;
	sp = up;
	return sp;
}

/*
 * check-name: -Wtypesign
 * check-command: sparse -Wtypesign $file
 *
 * check-error-start
typesign.c:3:9: warning: incorrect type in return expression (different signedness)
typesign.c:3:9:    expected unsigned int *
typesign.c:3:9:    got signed int *sp
typesign.c:8:9: warning: incorrect type in return expression (different signedness)
typesign.c:8:9:    expected signed int *
typesign.c:8:9:    got unsigned int *up
typesign.c:13:21: warning: incorrect type in initializer (different signedness)
typesign.c:13:21:    expected unsigned int *up
typesign.c:13:21:    got signed int *sp
typesign.c:19:19: warning: incorrect type in initializer (different signedness)
typesign.c:19:19:    expected signed int *sp
typesign.c:19:19:    got unsigned int *up
typesign.c:26:5: warning: incorrect type in assignment (different signedness)
typesign.c:26:5:    expected unsigned int *up
typesign.c:26:5:    got signed int *sp
typesign.c:33:5: warning: incorrect type in assignment (different signedness)
typesign.c:33:5:    expected signed int *sp
typesign.c:33:5:    got unsigned int *up
 * check-error-end
 */
