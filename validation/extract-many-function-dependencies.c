/*
 * check-name: extract more than 8192 function dependencies
 * check-command: extract --func=target $file && gcc -Werror=implicit-function-declaration -x c -fsyntax-only -fms-extensions $file.extract.target
 *
 * check-output-start
extract: wrote extract-many-function-dependencies.c.extract.target
 * check-output-end
 */

#define CONCAT1(a, b) a ## b
#define CONCAT(a, b) CONCAT1(a, b)

#define ASSERT_ID(id) \
	do { \
		extern void CONCAT(assert_, id)(void); \
		CONCAT(assert_, id)(); \
	} while (0)
#define ASSERT() ASSERT_ID(__COUNTER__)

#define REPEAT_1() do { ASSERT(); } while (0)
#define REPEAT_2() do { REPEAT_1(); REPEAT_1(); } while (0)
#define REPEAT_4() do { REPEAT_2(); REPEAT_2(); } while (0)
#define REPEAT_8() do { REPEAT_4(); REPEAT_4(); } while (0)
#define REPEAT_16() do { REPEAT_8(); REPEAT_8(); } while (0)
#define REPEAT_32() do { REPEAT_16(); REPEAT_16(); } while (0)
#define REPEAT_64() do { REPEAT_32(); REPEAT_32(); } while (0)
#define REPEAT_128() do { REPEAT_64(); REPEAT_64(); } while (0)
#define REPEAT_256() do { REPEAT_128(); REPEAT_128(); } while (0)
#define REPEAT_512() do { REPEAT_256(); REPEAT_256(); } while (0)
#define REPEAT_1024() do { REPEAT_512(); REPEAT_512(); } while (0)
#define REPEAT_2048() do { REPEAT_1024(); REPEAT_1024(); } while (0)
#define REPEAT_4096() do { REPEAT_2048(); REPEAT_2048(); } while (0)
#define REPEAT_8192() do { REPEAT_4096(); REPEAT_4096(); } while (0)

static void target(void)
{
	REPEAT_8192();
	ASSERT();
}
