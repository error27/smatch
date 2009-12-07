static int (struct);
static int (union);
static int (enum);
static int (volatile);
static int (__volatile);
static int (__volatile__);
static int (const);
static int (__const);
static int (__const__);
static int (restrict);
static int (__restrict);
static int (__restrict__);
static int (typedef);
static int (__typeof);
static int (__typeof__);
static int (inline);
static int (__inline);
static int (__inline__);
/*
 * check-name: const et.al. are reserved identifiers
 * check-error-start:
reserved.c:1:12: error: Trying to use reserved word 'struct' as identifier
reserved.c:2:12: error: Trying to use reserved word 'union' as identifier
reserved.c:3:12: error: Trying to use reserved word 'enum' as identifier
reserved.c:4:12: error: Trying to use reserved word 'volatile' as identifier
reserved.c:5:12: error: Trying to use reserved word '__volatile' as identifier
reserved.c:6:12: error: Trying to use reserved word '__volatile__' as identifier
reserved.c:7:12: error: Trying to use reserved word 'const' as identifier
reserved.c:8:12: error: Trying to use reserved word '__const' as identifier
reserved.c:9:12: error: Trying to use reserved word '__const__' as identifier
reserved.c:10:12: error: Trying to use reserved word 'restrict' as identifier
reserved.c:11:12: error: Trying to use reserved word '__restrict' as identifier
reserved.c:13:12: error: Trying to use reserved word 'typedef' as identifier
reserved.c:14:12: error: Trying to use reserved word '__typeof' as identifier
reserved.c:15:12: error: Trying to use reserved word '__typeof__' as identifier
reserved.c:16:12: error: Trying to use reserved word 'inline' as identifier
reserved.c:17:12: error: Trying to use reserved word '__inline' as identifier
reserved.c:18:12: error: Trying to use reserved word '__inline__' as identifier
 * check-error-end:
 */
