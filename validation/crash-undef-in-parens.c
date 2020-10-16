void foo(void) { return (UNDEF_STUFF_IN_PARENS); }

/*
 * check-name: crash-undef-in-parens
 *
 * check-error-start
crash-undef-in-parens.c:1:26: error: undefined identifier 'UNDEF_STUFF_IN_PARENS'
 * check-error-end
 */
