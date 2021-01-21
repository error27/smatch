_Static_assert(__builtin_isdigit('0'));
_Static_assert(__builtin_isdigit('9'));

_Static_assert(!__builtin_isdigit(0));
_Static_assert(!__builtin_isdigit(' '));
_Static_assert(!__builtin_isdigit('z'));

/*
 * check-name: builtin_isdigit
 */
