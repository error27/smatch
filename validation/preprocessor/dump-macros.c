#define ABC abc
#undef ABC

#define	DEF def
#undef DEF
#define DEF xyz

#define NYDEF ydef

#define STRING(x) #x
#define CONCAT(x,y) x ## y
/*
 * check-name: dump-macros
 * check-command: sparse -E -dD -DIJK=ijk -UNDEF -UNYDEF $file
 *
 * check-output-ignore
check-output-pattern(1): #define __CHECKER__ 1
check-output-contains: #define IJK ijk
check-output-contains: #define DEF xyz
check-output-contains: #define NYDEF ydef
check-output-contains: #define STRING(x) #x
check-output-contains: #define CONCAT(x,y) x ## y
 */
