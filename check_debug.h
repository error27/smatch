#ifndef __SMATCH_CHECK_DEBUG
#define __SMATCH_CHECK_DEBUG

#define cast_ptr(x) _Generic(x, \
	signed char: x, unsigned char: x, \
	short: x, unsigned short: x, \
	int: x, unsigned int: x, \
	long: x, unsigned long: x, \
	long long: x, unsigned long long: x, \
	float: x, double: x, long double: x, \
	default: (unsigned long)(x))

static inline void __smatch_about(long var){}
#define __smatch_about(x) __smatch_about(cast_ptr(x))

static inline void __smatch_cur_stree(void){}
static inline void __smatch_all_values(void){}
static inline void __smatch_state(const char *check_name, const char *state_name){}
static inline void __smatch_states(const char *check_name){}
static inline void __smatch_value(const char *unused){}
static inline void __smatch_known(long long val){}
static inline void __smatch_implied(long long val){}
#define __smatch_implied(x) __smatch_implied(cast_ptr(x))
static inline void __smatch_implied_min(long long val){}
static inline void __smatch_implied_max(long long val){}
static inline void __smatch_user_rl(long long val){}
static inline void __smatch_host_rl(long long val){}
static inline void __smatch_capped(long long val){}

static inline void __smatch_hard_max(long long val){}
static inline void __smatch_fuzzy_max(long long val){}

static inline void __smatch_absolute(long long val){}
static inline void __smatch_absolute_min(long long val){}
static inline void __smatch_absolute_max(long long val){}
static inline void __smatch_real_absolute(long long val){}

static inline void __smatch_sval_info(long long val){}

static inline void __smatch_member_name(long long val){}
#define __smatch_member_name(x) __smatch_member_name(cast_ptr(x))

static inline void __smatch_possible(const char *unused){}
static inline void __smatch_print_value(const char *unused){}

static inline void __smatch_strlen(const void *buf){}
static inline void __smatch_buf_size(const void *buf){}

static inline void __smatch_note(const char *note){}

static inline void __smatch_dump_related(void){}

static inline void __smatch_compare(long long one, long long two){}
#define __smatch_compare(x, y) __smatch_compare(cast_ptr(x), cast_ptr(y))

static inline void __smatch_debug_on(void){}
static inline void __smatch_debug_check(const char *check_name){}
static inline void __smatch_debug_var(const char *var_name){}
static inline void __smatch_debug_state_cnt(void){}
static inline void __smatch_debug_off(void){}

static inline void __smatch_local_debug_on(void){}
static inline void __smatch_local_debug_off(void){}

static inline void __smatch_debug_db_on(void){}
static inline void __smatch_debug_db_off(void){}

static inline void __smatch_debug_implied_on(void){}
static inline void __smatch_debug_implied_off(void){}

static inline void __smatch_intersection(long long one, long long two){}
static inline void __smatch_type(long long one){}
#define __smatch_type(x) __smatch_type(cast_ptr(x))

static long long __smatch_val;
static inline long long __smatch_type_rl_helper(long long type, const char *str, ...)
{
	return __smatch_val;
}
#define __smatch_type_rl(type, fmt...) __smatch_type_rl_helper((type)0, fmt)
#define __smatch_rl(fmt...) __smatch_type_rl(long long, fmt)

static inline void __smatch_bits(long long expr){}

static inline void __smatch_oops(unsigned long null_val){}

static inline void __smatch_merge_tree(long long var){}

static inline void __smatch_stree_id(void){}

static inline void __smatch_mtag(void *p){}
static inline void __smatch_mtag_data(long long arg){}
#define __smatch_mtag_data(x) __smatch_mtag_data(cast_ptr(x))
static inline void __smatch_exit(void){}

static inline void __smatch_constraints(long long arg){}
#define __smatch_constraints(x) __smatch_constraints(cast_ptr(x))

static inline void __smatch_expr(const char *str, void *p){}

static inline void __smatch_state_count(void){}
static inline void __smatch_mem(void){}

static inline void __smatch_units(long long var){}
#define __smatch_units(x) __smatch_units(cast_ptr(x))

static inline void __smatch_timer_start(void){}
static inline void __smatch_timer_stop(void){}

static inline void __smatch_container(long long container, long long x){}
#define __smatch_container(container, x) \
	__smatch_container(cast_ptr(container), cast_ptr(x))
static inline void __smatch_param_key(long long val){}
#define __smatch_param_key(x) __smatch_param_key(cast_ptr(x))

static inline void __smatch_force_on(void){}
static inline void __smatch_force_off(void){}

static inline void __smatch_debug_passes(void){}

#endif
