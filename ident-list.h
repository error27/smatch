
#ifndef IDENT
#define IDENT(n) __IDENT(n## _ident, #n)
#endif

IDENT(struct); IDENT(union); IDENT(enum);
IDENT(sizeof); IDENT(alignof); IDENT(__alignof);
IDENT(__alignof__); IDENT(if); IDENT(else);
IDENT(return); IDENT(switch); IDENT(case);
IDENT(default); IDENT(break); IDENT(continue);
IDENT(for); IDENT(while); IDENT(do);
IDENT(goto); IDENT(__asm__); IDENT(__asm);
IDENT(asm); IDENT(__volatile__); IDENT(__volatile);
IDENT(volatile); IDENT(__attribute__); IDENT(__attribute);
IDENT(defined); IDENT(packed); IDENT(__packed__);
IDENT(aligned); IDENT(__aligned__); IDENT(nocast);
IDENT(noderef); IDENT(safe); IDENT(force);
IDENT(address_space); IDENT(context); IDENT(mode);
IDENT(__mode__); IDENT(__QI__); IDENT(QI);
IDENT(__HI__); IDENT(HI); IDENT(__SI__);
IDENT(SI); IDENT(__DI__); IDENT(DI);
IDENT(__word__); IDENT(word); IDENT(format);
IDENT(__format__); IDENT(section); IDENT(__section__);
IDENT(unused); IDENT(__unused__); IDENT(const);
IDENT(__const); IDENT(__const__); IDENT(noreturn);
IDENT(__noreturn__); IDENT(regparm); IDENT(weak);
IDENT(alias); IDENT(pure); IDENT(always_inline);
IDENT(syscall_linkage); IDENT(visibility);
IDENT(bitwise);
IDENT(model); IDENT(__model__);

__IDENT(pragma_ident, "__pragma__");
__IDENT(__VA_ARGS___ident, "__VA_ARGS__");
__IDENT(__LINE___ident, "__LINE__");
__IDENT(__FILE___ident, "__FILE__");
__IDENT(__func___ident, "__func__");

#undef __IDENT

