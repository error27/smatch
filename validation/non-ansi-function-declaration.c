

extern void myfunction(), myfunc2();

/*
 * check-name: -Wno-non-ansi-function-declaration works
 * check-command: sparse -Wno-non-ansi-function-declaration $file
 * check-error-start
 * check-error-end
 */
