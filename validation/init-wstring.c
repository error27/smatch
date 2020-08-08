static const __WCHAR_TYPE__ ok0[] = L"abc";
_Static_assert(sizeof(ok0) == 4 * sizeof(__WCHAR_TYPE__));
static const __WCHAR_TYPE__ ok1[] = (L"abc");
_Static_assert(sizeof(ok1) == 4 * sizeof(__WCHAR_TYPE__));
static const __WCHAR_TYPE__ ok2[] = { L"abc" };
_Static_assert(sizeof(ok2) == 4 * sizeof(__WCHAR_TYPE__));

static const __WCHAR_TYPE__ ok3[4] = L"abc";
_Static_assert(sizeof(ok3) == 4 * sizeof(__WCHAR_TYPE__));
static const __WCHAR_TYPE__ ok4[4] = (L"abc");
_Static_assert(sizeof(ok4) == 4 * sizeof(__WCHAR_TYPE__));
static const __WCHAR_TYPE__ ok5[4] = { (L"abc") };
_Static_assert(sizeof(ok5) == 4 * sizeof(__WCHAR_TYPE__));

static const __WCHAR_TYPE__ ok6[7] = L"abc";
_Static_assert(sizeof(ok6) == 7 * sizeof(__WCHAR_TYPE__));
static const __WCHAR_TYPE__ ok7[7] = (L"abc");
_Static_assert(sizeof(ok7) == 7 * sizeof(__WCHAR_TYPE__));
static const __WCHAR_TYPE__ ok8[7] = { (L"abc") };
_Static_assert(sizeof(ok8) == 7 * sizeof(__WCHAR_TYPE__));

static const __WCHAR_TYPE__ *ptr[] =  { L"abc" };
_Static_assert(sizeof(ptr) == sizeof(void *));

static struct s {
	const __WCHAR_TYPE__ str[4];
} str = { L"xyz" };

static const __WCHAR_TYPE__ ko3[3] = L"abc";
static const __WCHAR_TYPE__ ko2[2] = L"abc";

/*
 * check-name: init-wstring
 * check-command: sparse -Winit-cstring $file
 *
 * check-error-start
init-wstring.c:29:38: warning: too long initializer-string for array of char(no space for nul char)
init-wstring.c:30:38: warning: too long initializer-string for array of char
 * check-error-end
 */
