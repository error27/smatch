typedef unsigned int	u32;
typedef          int	s32;
typedef void *vdp;
typedef int  *sip;
typedef double dbl;
typedef unsigned short __attribute__((bitwise)) le16;

static _Bool fs32(s32 a) { return a; }
static _Bool fu32(u32 a) { return a; }
static _Bool fvdp(vdp a) { return a; }
static _Bool fsip(sip a) { return a; }
static _Bool fdbl(dbl a) { return a; }
static _Bool ffun(void)  { return ffun; }

static _Bool fres(le16 a) { return a; }

/*
 * check-name: bool-cast-implicit
 * check-command: test-linearize $file
 * check-output-ignore
 * check-output-excludes: cast\\.
 *
 * check-error-start
bool-cast-implicit.c:15:36: warning: incorrect type in return expression (different base types)
bool-cast-implicit.c:15:36:    expected bool
bool-cast-implicit.c:15:36:    got restricted le16 [usertype] a
 * check-error-end
 */
