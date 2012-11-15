#ifndef __SMATCH_CHECK_DEBUG
#define __SMATCH_CHECK_DEBUG

static inline void __smatch_cur_slist(void){}
static inline void __smatch_all_values(void){}
static inline void __smatch_value(const char *unused){}
static inline void __smatch_implied(long long val){}
static inline void __smatch_implied_min(long long val){}
static inline void __smatch_implied_max(long long val){}

static inline void __smatch_hard_max(long long val){}
static inline void __smatch_fuzzy_max(long long val){}

static inline void __smatch_absolute_min(long long val){}
static inline void __smatch_absolute_max(long long val){}

static inline void __smatch_sval_info(long long val){}

static inline void __smatch_possible(const char *unused){}
static inline void __smatch_print_value(const char *unused){}

static inline void __smatch_buf_size(const void *buf){}

static inline void __smatch_note(const char *note){}

static inline void __smatch_dump_related(void){}

static inline void __smatch_debug_on(void){}
static inline void __smatch_debug_off(void){}

#endif
