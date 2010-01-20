#ifndef CK
#define CK(_x) void _x(int id);
#define __undo_CK_def
#endif

CK(register_smatch_extra) /* smatch_extra always has to be first */
CK(register_modification_hooks)

CK(register_smatch_ignore)
CK(check_debug)
CK(check_assigned_expr)

CK(check_null_deref)
CK(check_overflow)
CK(register_check_overflow_again)
CK(check_memory)
CK(check_type)
CK(check_allocation_funcs)
CK(check_leaks)
CK(check_frees_argument)
CK(check_balanced)
CK(check_deref_check)
CK(check_redundant_null_check)
CK(check_signed)
CK(check_precedence)
CK(check_format_string)
CK(check_unused_ret)
CK(check_dma_on_stack)
CK(check_param_mapper)
CK(check_call_tree)
CK(check_dev_queue_xmit)

/* <- your test goes here */
/* CK(register_template) */

/* kernel specific */
CK(check_locking)
CK(check_puts_argument)
CK(check_err_ptr)
CK(check_err_ptr_deref)
CK(check_hold_dev)

/* wine specific stuff */
CK(check_wine)
CK(check_wine_filehandles)
CK(check_wine_WtoA)

CK(register_containers)
CK(register_implications) /* implications always has to be last */

#ifdef __undo_CK_def
#undef CK
#undef __undo_CK_def
#endif
