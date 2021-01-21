#include "flex-array-union-array.h"

/*
 * check-name: flex-array-union-yes
 * check-command: sparse -Wflexible-array-array -Wflexible-array-union $file
 *
 * check-error-start
flex-array-union-array-yes.c: note: in included file:
flex-array-union-array.h:11:17: warning: array of flexible structures
 * check-error-end
 */
