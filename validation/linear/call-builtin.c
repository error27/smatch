typedef unsigned int u32;

u32 ff(u32 a) { return __builtin_popcount(a); }

u32 f0(u32 a) { return (__builtin_popcount)(a); }

/*
 * check-name: builtin calls
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: load
 * check-output-pattern(2): call\..*__builtin_.*, %arg1
 */
