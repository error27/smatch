static inline void fun(void) { }

#define EXPR	({ fun(); 42; })

int bar(void)
{
	// GCC doesn't consider EXPR as a constant
	return __builtin_constant_p(EXPR);
}

/*
 * check-name: builtin_constant_inline1
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-start
bar:
.L0:
	<entry-point>
	ret.32      $0


 * check-output-end
 */
