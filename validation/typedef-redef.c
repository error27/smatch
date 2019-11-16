typedef int  ok_t;
typedef int  ok_t;

typedef int  ko_t;
typedef long ko_t;

/*
 * check-name: typedef-redef
 *
 * check-error-start
typedef-redef.c:5:14: error: symbol 'ko_t' redeclared with different type (different type sizes):
typedef-redef.c:5:14:    long [usertype] ko_t
typedef-redef.c:4:14: note: previously declared as:
typedef-redef.c:4:14:    int [usertype] ko_t
 * check-error-end
 */
