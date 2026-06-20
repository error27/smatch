#ifndef CK
#define CK(_x) void _x(int id);
#define __undo_CK_def
#endif

CK(smatch_free)
CK(smatch_stored_conditions)
CK(smatch_stored_conditions_links)
CK(smatch_parsed_conditions)
CK(smatch_sval)
CK(smatch_buf_size_late)
CK(smatch_smatch_extra_late)
CK(smatch_assigned_expr) /* This is used by smatch_extra.c so it has to come really late */
CK(smatch_assigned_expr_links)
CK(smatch_modification_hooks_late)  /* has to come after smatch_extra */
CK(smatch_param_key) /* has to come after smatch_extra */
CK(smatch_comparison_late) /* has to come after modification_hooks_late */
CK(smatch_function_hooks)
CK(smatch_definition_db_callbacks_late) /* has to come after smatch_function_hooks() */
CK(smatch_kernel)  /* this is overwriting stuff from smatch_extra_late */
CK(smatch_wine)
CK(smatch_returns)

#ifdef __undo_CK_def
#undef CK
#undef __undo_CK_def
#endif
