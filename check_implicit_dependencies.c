#include "smatch.h"
#include "smatch_slist.h"


static int my_id;
static struct symbol *cur_syscall;
static char *syscall_name;

static struct tracker_list *read_list; // what fields does syscall branch on?
static struct tracker_list *write_list; // what fields does syscall modify?

static inline void prefix() {
	printf("%s:%d %s() ", get_filename(), get_lineno(), get_function());
}

static void match_syscall_definition(struct symbol *sym)
{
	// struct symbol *arg;
	char *macro;
	char *name;
	int is_syscall = 0;

	macro = get_macro_name(sym->pos);
	if (macro &&
	    (strncmp("SYSCALL_DEFINE", macro, strlen("SYSCALL_DEFINE")) == 0 ||
	     strncmp("COMPAT_SYSCALL_DEFINE", macro, strlen("COMPAT_SYSCALL_DEFINE")) == 0))
		is_syscall = 1;

	name = get_function();
	
	if (name && strncmp(name, "sys_", 4) == 0)
		is_syscall = 1;

	if (name && strncmp(name, "compat_sys_", 11) == 0)
		is_syscall = 1;

	if (!is_syscall)
		return;

	syscall_name = name;
	// printf("-------------------------\n");	
	// printf("\nsyscall found: %s at: ", name);
	// prefix(); printf("\n");
	cur_syscall = sym;

	/*
	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		set_state(my_id, arg->ident->name, arg, &user_data_set);
	} END_FOR_EACH_PTR(arg);
	*/
}

static void print_read_list()
{
    struct tracker *tracker;
    sm_printf("%s read_list: [", syscall_name);
    FOR_EACH_PTR(read_list, tracker) {
	sm_printf("%s, ", tracker->name);
    } END_FOR_EACH_PTR(tracker);
    sm_printf("]\n");
    return;
}

static void print_write_list()
{
    return;
}

static void match_after_syscall(struct symbol *sym) {
    if (!cur_syscall || sym != cur_syscall)
	return;
    // printf("\n"); prefix();
    // printf("exiting scope of syscall %s\n", get_function());
    // printf("-------------------------\n");
    print_read_list();
    print_write_list();
    free_trackers_and_list(&read_list);
    free_trackers_and_list(&write_list);
    cur_syscall = NULL;
    syscall_name = NULL;
}

static void print_read_member_type(struct expression *expr)
{
	char *member;
	struct symbol *sym;

	member = get_member_name(expr);
	if (!member)
		return;

	sym = get_type(expr->deref);
	add_tracker(&read_list, my_id, member, sym);
	// sm_msg("info: uses %s", member);
	// prefix();
	// printf("info: uses %s\n", member);
	free_string(member);
}

static void match_condition(struct expression *expr) {
    struct expression *arg;

    if (!cur_syscall)
	return;
    
    // prefix(); printf("-- condition found\n");

    if (expr->type == EXPR_COMPARE || expr->type == EXPR_BINOP
	    || expr->type == EXPR_LOGICAL
	    || expr->type == EXPR_ASSIGNMENT
	    || expr->type == EXPR_COMMA) {
	    match_condition(expr->left);
	    match_condition(expr->right);
	    return;
    } else if (expr->type == EXPR_CALL) {
	FOR_EACH_PTR(expr->args, arg) {
	    // if we find deref in conditional call,
	    // mark it as a read dependency
	    print_read_member_type(arg);
	} END_FOR_EACH_PTR(arg);
	return;
    }
	
    print_read_member_type(expr);
}


/* when we are parsing an inline function and can no longer nest,
 * assume that all struct fields passed to nested inline functions
 * are implicit dependencies
 */
static void match_call_info(struct expression *expr)
{
    struct expression *arg;
    int i;

    if (!__inline_fn || !cur_syscall)
	return;

    // prefix(); printf("fn: %s\n", expr->fn->symbol->ident->name);

    i = 0;
    FOR_EACH_PTR(expr->args, arg) {
	/*
	if (arg->type == EXPR_DEREF)
	    printf("arg %d is deref\n", i);
	*/
	print_read_member_type(arg);
	i++;
    } END_FOR_EACH_PTR(arg);
}

void check_implicit_dependencies(int id)
{
    my_id = id;

    if (option_project != PROJ_KERNEL)
	return;

    add_hook(&match_syscall_definition, AFTER_DEF_HOOK);
    add_hook(&match_after_syscall, AFTER_FUNC_HOOK);
    add_hook(&match_condition, CONDITION_HOOK);
    add_hook(&match_call_info, FUNCTION_CALL_HOOK);
}

