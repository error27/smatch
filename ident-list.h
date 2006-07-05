
#define IDENT(n) __IDENT(n## _ident, #n, 0)
#define IDENT_RESERVED(n) __IDENT(n## _ident, #n, 1)

/* Basic C reserved words.. */
IDENT_RESERVED(sizeof);
IDENT_RESERVED(if);
IDENT_RESERVED(else);
IDENT_RESERVED(return);
IDENT_RESERVED(switch);
IDENT_RESERVED(case);
IDENT_RESERVED(default);
IDENT_RESERVED(break);
IDENT_RESERVED(continue);
IDENT_RESERVED(for);
IDENT_RESERVED(while);
IDENT_RESERVED(do);
IDENT_RESERVED(goto);

/* C typenames. They get marked as reserved when initialized */
IDENT(struct);
IDENT(union);
IDENT(enum);
IDENT(__attribute__);
IDENT(__attribute);
IDENT(volatile);
IDENT(__volatile__);
IDENT(__volatile);

/* Extended gcc identifiers */
IDENT(asm);
IDENT(alignof);
IDENT_RESERVED(__asm__);
IDENT_RESERVED(__asm);
IDENT_RESERVED(__alignof);
IDENT_RESERVED(__alignof__); 
IDENT_RESERVED(__sizeof_ptr__);
IDENT_RESERVED(__builtin_types_compatible_p);

/* Attribute names */
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
IDENT(__used__);
IDENT(warn_unused_result);
IDENT(noinline);
IDENT(deprecated);
IDENT(__const); IDENT(__const__); IDENT(noreturn);
IDENT(__noreturn__); IDENT(regparm); IDENT(weak); IDENT(__weak__);
IDENT(__sentinel__); IDENT(sentinel);
IDENT(alias); IDENT(pure); IDENT(always_inline);
IDENT(syscall_linkage); IDENT(visibility);
IDENT(bitwise); IDENT(__bitwise__);
IDENT(model); IDENT(__model__);
IDENT(__format_arg__);
IDENT(nothrow); IDENT(__nothrow); IDENT(__nothrow__);
IDENT(__transparent_union__);
IDENT(__pure__);
IDENT(__malloc__);
IDENT(nonnull); IDENT(__nonnull); IDENT(__nonnull__);
IDENT(__deprecated__);
IDENT(__warn_unused_result__);

/* Preprocessor idents */
__IDENT(pragma_ident, "__pragma__", 0);
__IDENT(__VA_ARGS___ident, "__VA_ARGS__", 0);
__IDENT(__LINE___ident, "__LINE__", 0);
__IDENT(__FILE___ident, "__FILE__", 0);
__IDENT(__func___ident, "__func__", 0);
__IDENT(__FUNCTION___ident, "__FUNCTION__", 0);
__IDENT(__PRETTY_FUNCTION___ident, "__PRETTY_FUNCTION__", 0);

/* Sparse commands */
IDENT_RESERVED(__context__);
IDENT_RESERVED(__range__);

/* Magic function names we recognize */
IDENT(memset); IDENT(memcpy);
IDENT(copy_to_user); IDENT(copy_from_user);
IDENT(main);

#undef __IDENT
#undef IDENT
#undef IDENT_RESERVED
