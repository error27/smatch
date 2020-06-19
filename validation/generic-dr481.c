static char const* a = _Generic("bla", char*: "blu");
static char const* b = _Generic("bla", char[4]: "blu");
static char const* c = _Generic((int const){ 0 }, int: "blu");
static char const* d = _Generic((int const){ 0 }, int const: "blu");
static char const* e = _Generic(+(int const){ 0 }, int: "blu");
static char const* f = _Generic(+(int const){ 0 }, int const: "blu");

/*
 * check-name: generic-dr481
 *
 * check-error-start
generic-dr481.c:2:32: error: no generic selection for 'char *'
generic-dr481.c:4:32: error: no generic selection for 'int const [toplevel]'
generic-dr481.c:6:32: error: no generic selection for 'int'
 * check-error-end
 */
