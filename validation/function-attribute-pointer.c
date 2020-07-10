#define __noreturn __attribute__((__noreturn__))

void set_die(void (*)(void));
void set_die_nr(__noreturn void (*)(void));

void die(void);
void __noreturn die_nr(void);

static void foo(void)
{
	set_die(die);
	set_die(die_nr);
	set_die_nr(die_nr);
	set_die_nr(die);

	           void (*fptr0)(void) = die;
	           void (*fptr1)(void) = die_nr;
	__noreturn void (*fptr3)(void) = die_nr;
	__noreturn void (*fptr2)(void) = die;
}

/*
 * check-name: function-attribute-pointer
 *
 * check-error-start
function-attribute-pointer.c:14:20: warning: incorrect type in argument 1 (different modifiers)
function-attribute-pointer.c:14:20:    expected void ( [noreturn] * )( ... )
function-attribute-pointer.c:14:20:    got void ( * )( ... )
function-attribute-pointer.c:19:42: warning: incorrect type in initializer (different modifiers)
function-attribute-pointer.c:19:42:    expected void ( [noreturn] *fptr2 )( ... )
function-attribute-pointer.c:19:42:    got void ( * )( ... )
 * check-error-end
 */
