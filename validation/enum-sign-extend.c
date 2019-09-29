enum num {
	a = 0x80000000,
	b = -1,
};

_Static_assert([typeof(b)] == [long], "type");
_Static_assert(b == -1L,              "value");

/*
 * check-name: enum-sign-extend
 * check-command: sparse -m64 $file
 */
