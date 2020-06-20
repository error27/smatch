struct s;

void foo(int n)
{
	_Generic(n, default: 1, default: 2);
	_Generic(n, int[n]:0, default:1);
	_Generic(n, struct s:0, default:1);
	_Generic(n, void:0, default:1);
	_Generic(n, void (void):0, default:1);
	_Generic(&n, int:5, signed int:7, default:23);
	_Generic(n, void *:5);
}

/*
 * check-name: generic-bad0
 *
 * check-error-start
generic-bad0.c:5:33: warning: multiple default in generic expression
generic-bad0.c:5:30: note: previous was here
generic-bad0.c:6:25: warning: Variable length array is used.
generic-bad0.c:6:21: error: variable length array type in generic selection
generic-bad0.c:7:21: error: incomplete type in generic selection
generic-bad0.c:8:21: error: incomplete type in generic selection
generic-bad0.c:9:21: error: function type in generic selection
generic-bad0.c:11:17: error: no generic selection for 'int [addressable] n'
 * check-error-end
 */
