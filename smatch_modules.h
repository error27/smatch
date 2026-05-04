#ifndef CK
#define CK(_x) void _x(int id);
#define __undo_CK_def
#endif

CK(register_db_call_marker) /* always has to be first  */
CK(register_mtag_data)      /* before smatch_extra to clear cache at start of function */
CK(register_param_used)     /* get_state_hooks have to be registered before smatch_extra */
CK(register_container_of)
CK(register_container_of2)
CK(register_smatch_extra)
CK(register_smatch_extra_links)
CK(register_buf_comparison)
CK(register_buf_comparison_links)
CK(register_buf_comparison2)
CK(register_modification_hooks)
CK(register_hooks)

/*
 * Implications should probably be after all the modification and smatch_extra
 * hooks have run.
 *
 */
CK(register_implications)
CK(register_function_hooks_early)
CK(register_definition_db_callbacks)
CK(register_project)        /* has to be early to set up some global stuff */
CK(register_untracked_param)
CK(register_param_compare_limit)
CK(register_param_compare_limit_links)
CK(register_returns_early)

CK(register_param_cleared)  /* param_set relies on param_cleared */
CK(register_param_limit)    /* param limit has to be before param_set */
CK(register_param_set)

/* order doesn't matter for the rest so they go in alphabetical order */

CK(register_about_fn_ptr_arg)
CK(register_allocations)
CK(register_allocations_locations)
CK(register_annotate)
CK(register_array_values)
CK(register_bits)
CK(register_buf_size)
CK(register_capped)
CK(register_common_functions)
CK(register_comparison)
CK(register_comparison_inc_dec)
CK(register_comparison_inc_dec_links)
CK(register_comparison_links)
CK(register_constraints)
CK(register_constraints_required)
CK(register_data_source)
CK(register_dereferences)
CK(register_fn_arg_link)
CK(register_free)
CK(register_free_locations)
CK(register_free_return_states)
CK(register_fresh_alloc)
CK(register_function_info)
CK(register_function_ptrs)
CK(register_goto_tracker)
CK(register_imaginary_absolute)
CK(register_impossible)
CK(register_impossible_return)
CK(register_integer_overflow)
CK(register_integer_overflow_links)
CK(register_kernel_atomic_dec_test_path)
CK(register_kernel_devm)
CK(register_kernel_err_ptr)
CK(register_kernel_err_ptr_possible)
CK(register_kernel_has_devm_cleanup)
CK(register_kernel_host_data)
CK(register_kernel_kref_put)
CK(register_kernel_list_add_entry)
CK(register_kernel_list_del)
CK(register_kernel_netdev_priv)
CK(register_kernel_NOT_ENABLED)
CK(register_kernel_put_device)
CK(register_kernel_put_device_info)
CK(register_kernel_rcu_assign_pointer)
CK(register_kernel_task_state)
CK(register_kernel_task_state_info)
CK(register_kernel_user_data)
CK(register_kernel_user_data2)
CK(register_kernel_xa_err)
CK(register_leaf_fn)
CK(register_locking)
CK(register_locking_info)
CK(register_locking_type)
CK(register_math)
CK(register_mtag)
CK(register_mtag_map)
CK(register_nul_terminator)
CK(register_nul_terminator_param_set)
CK(register_parameter_names)
CK(register_param_filter)
CK(register_param_to_mtag_data)
CK(register_parse_call_math)
CK(register_points_to_container)
CK(register_points_to_host_data)
CK(register_points_to_user_data)
CK(register_power_of_two)
CK(register_real_absolute)
CK(register_refcount)
CK(register_refcount_info)
CK(register_return_to_param)
CK(register_return_to_param_links)
CK(register_simple_no_overflow)
CK(register_smatch_ignore)
CK(register_ssa)
CK(register_start_states)
CK(register_state_assigned)
CK(register_statement_count)
CK(register_strings)
CK(register_strlen)
CK(register_strlen_equiv)
CK(register_struct_assignment)
CK(register_type_links)
CK(register_type_val)
CK(register_unconstant_macros)
CK(register_units)

#ifdef __undo_CK_def
#undef CK
#undef __undo_CK_def
#endif
