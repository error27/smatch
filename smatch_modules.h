#ifndef CK
#define CK(_x) void _x(int id);
#define __undo_CK_def
#endif

CK(smatch_db_call_marker) /* always has to be first  */
CK(smatch_ssa_pointer)	  /* needs to be really early */
CK(smatch_mtag_data)      /* before smatch_extra to clear cache at start of function */
CK(smatch_param_used)     /* get_state_hooks have to be registered before smatch_extra */
CK(smatch_container_of)
CK(smatch_container_of2)
CK(smatch_extra)
CK(smatch_smatch_extra_links)
CK(smatch_buf_comparison)
CK(smatch_buf_comparison_links)
CK(smatch_buf_comparison2)
CK(smatch_modification_hooks)
CK(smatch_hooks)
CK(smatch_flow)

/*
 * Implications should probably be after all the modification and smatch_extra
 * hooks have run.
 *
 */
CK(smatch_implications)
CK(smatch_function_hooks_early)
CK(smatch_definition_db_callbacks)
CK(smatch_project)        /* has to be early to set up some global stuff */
CK(smatch_untracked_param)
CK(smatch_param_compare_limit)
CK(smatch_param_compare_limit_links)
CK(smatch_returns_early)
CK(smatch_cleanup)

CK(smatch_buf_cleared)	/* param_set relies on buf_cleared */
CK(smatch_param_limit)	/* param limit has to be before param_set */
CK(smatch_param_set)

CK(smatch_param_bits_set)

/* order doesn't matter for the rest so they go in alphabetical order */

CK(smatch_about_fn_ptr_arg)
CK(smatch_allocations)
CK(smatch_allocations_locations)
CK(smatch_annotate)
CK(smatch_array_values)
CK(smatch_bits)
CK(smatch_buf_size)
CK(smatch_buf_zero)
CK(smatch_capped)
CK(smatch_common_functions)
CK(smatch_comparison)
CK(smatch_comparison_inc_dec)
CK(smatch_comparison_inc_dec_links)
CK(smatch_comparison_links)
CK(smatch_constraints)
CK(smatch_constraints_required)
CK(smatch_data_source)
CK(smatch_dereferences)
CK(smatch_estate)
CK(smatch_fn_arg_link)
CK(smatch_free_locations)
CK(smatch_free_refcounted)
CK(smatch_free_return_states)
CK(smatch_fresh_alloc)
CK(smatch_function_info)
CK(smatch_function_ptrs)
CK(smatch_goto_tracker)
CK(smatch_hard_value)
CK(smatch_imaginary_absolute)
CK(smatch_impossible)
CK(smatch_impossible_return)
CK(smatch_impossible_split_return)
CK(smatch_integer_overflow)
CK(smatch_integer_overflow_links)
CK(smatch_kernel_atomic_dec_test_path)
CK(smatch_kernel_devm)
CK(smatch_kernel_err_ptr)
CK(smatch_kernel_err_ptr_possible)
CK(smatch_kernel_has_devm_cleanup)
CK(smatch_kernel_host_data)
CK(smatch_kernel_kref_put)
CK(smatch_kernel_list_add_entry)
CK(smatch_kernel_list_del)
CK(smatch_kernel_netdev_priv)
CK(smatch_kernel_NOT_ENABLED)
CK(smatch_kernel_put_device)
CK(smatch_kernel_put_device_info)
CK(smatch_kernel_rcu_assign_pointer)
CK(smatch_kernel_task_state)
CK(smatch_kernel_task_state_info)
CK(smatch_kernel_user_data)
CK(smatch_kernel_user_data2)
CK(smatch_kernel_xa_err)
CK(smatch_leaf_fn)
CK(smatch_locking)
CK(smatch_locking_info)
CK(smatch_locking_type)
CK(smatch_math)
CK(smatch_mtag)
CK(smatch_mtag_map)
CK(smatch_nul_terminator)
CK(smatch_nul_terminator_param_set)
CK(smatch_parameter_names)
CK(smatch_param_filter)
CK(smatch_param_to_mtag_data)
CK(smatch_parse_call_math)
CK(smatch_points_to_container)
CK(smatch_points_to_host_data)
CK(smatch_points_to_user_data)
CK(smatch_power_of_two)
CK(smatch_real_absolute)
CK(smatch_refcount)
CK(smatch_refcount_info)
CK(smatch_return_to_param)
CK(smatch_return_to_param_links)
CK(smatch_simple_no_overflow)
CK(smatch_smatch_ignore)
CK(smatch_ssa)
CK(smatch_start_states)
CK(smatch_state_assigned)
CK(smatch_statement_count)
CK(smatch_strings)
CK(smatch_strlen)
CK(smatch_strlen_equiv)
CK(smatch_struct_assignment)
CK(smatch_type_info)
CK(smatch_type_links)
CK(smatch_type_val)
CK(smatch_unconstant_macros)
CK(smatch_units)

/* normally checks have one register function but sometimes they can have
 * more than one.  So I guess add them here until I can think of a nicer
 * way to handle this.
 */
CK(check_get_user_overflow2)
CK(check_index_overflow_loop_marker)
CK(check_nospec_barrier)
CK(check_ns_capable)
CK(check_rosenberg2)
CK(check_rosenberg3)
CK(check_unwind_info)

#ifdef __undo_CK_def
#undef CK
#undef __undo_CK_def
#endif
