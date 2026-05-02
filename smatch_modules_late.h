#ifndef CK
#define CK(_x) void _x(int id);
#define __undo_CK_def
#endif

CK(register_scope)
CK(register_stored_conditions)
CK(register_stored_conditions_links)
CK(register_parsed_conditions)
CK(register_sval)
CK(register_buf_size_late)
CK(register_smatch_extra_late)
CK(register_assigned_expr) /* This is used by smatch_extra.c so it has to come really late */
CK(register_assigned_expr_links)
CK(register_modification_hooks_late)  /* has to come after smatch_extra */
CK(register_param_key) /* has to come after smatch_extra */
CK(register_comparison_late) /* has to come after modification_hooks_late */
CK(register_function_hooks)
CK(register_definition_db_callbacks_late) /* has to come after register_function_hooks() */
CK(register_kernel)  /* this is overwriting stuff from smatch_extra_late */
CK(register_wine)
CK(register_returns)

#ifdef __undo_CK_def
#undef CK
#undef __undo_CK_def
#endif
