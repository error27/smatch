typedef const int ci_t;
typedef       int  ia_t[2];
typedef const int cia_t[2];

static const int	ci__a[2];
static ci_t		cit_a[2];
static const ia_t	c_iat;
static cia_t		ciat_;
static cia_t		ciata[2];

static const void *const ok_ci__a = &ci__a;
static       void *const ko_ci__a = &ci__a;
static const void *const ok_cit_a = &cit_a;
static       void *const ko_cit_a = &cit_a;
static const void *const ok_c_iat = &c_iat;
static       void *const ko_c_iat = &c_iat;
static const void *const ok_ciat_ = &ciat_;
static       void *const ko_ciat_ = &ciat_;
static const void *const ok_ciata = &ciata;
static       void *const ko_ciata = &ciata;

static volatile int	vi__a[2];
static volatile void *const ok_vi__a = &vi__a;
static          void *const ko_vi__a = &vi__a;

/*
 * check-name: array-quals1
 *
 * check-error-start
eval/array-quals1.c:12:38: warning: incorrect type in initializer (different modifiers)
eval/array-quals1.c:12:38:    expected void *static const [toplevel] ko_ci__a
eval/array-quals1.c:12:38:    got int const ( * )[2]
eval/array-quals1.c:14:38: warning: incorrect type in initializer (different modifiers)
eval/array-quals1.c:14:38:    expected void *static const [toplevel] ko_cit_a
eval/array-quals1.c:14:38:    got int const [usertype] ( * )[2]
eval/array-quals1.c:16:38: warning: incorrect type in initializer (different modifiers)
eval/array-quals1.c:16:38:    expected void *static const [toplevel] ko_c_iat
eval/array-quals1.c:16:38:    got int const ( * )[2]
eval/array-quals1.c:18:38: warning: incorrect type in initializer (different modifiers)
eval/array-quals1.c:18:38:    expected void *static const [toplevel] ko_ciat_
eval/array-quals1.c:18:38:    got int const ( * )[2]
eval/array-quals1.c:20:38: warning: incorrect type in initializer (different modifiers)
eval/array-quals1.c:20:38:    expected void *static const [toplevel] ko_ciata
eval/array-quals1.c:20:38:    got int const [usertype] ( * )[2][2]
eval/array-quals1.c:24:41: warning: incorrect type in initializer (different modifiers)
eval/array-quals1.c:24:41:    expected void *static const [toplevel] ko_vi__a
eval/array-quals1.c:24:41:    got int volatile ( * )[2]
 * check-error-end
 */
