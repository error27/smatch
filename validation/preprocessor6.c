/*
 * We get '##' wrong for the kernel.
 *
 * It could possibly be argued that the kernel usage
 * is undefined (since the different sides of the '##'
 * are not proper tokens), but that's probably a load
 * of bull. We should just try to do it right.
 *
 * This _should_ result in
 *
 *	static char __vendorstr_003d[] __devinitdata = "Lockheed Martin-Marietta Corp";
 *
 * but we break up the "003d" into two tokens ('003' and 'd')
 * and then we also put the 'o' marker to mark the token 003
 * as an octal number, so we end up with
 *
 *	static char __vendorstr_o03 d [ ] __devinitdata = "Lockheed Martin-Marietta Corp";
 *
 * which doesn't work, of course.
 */

#define VENDOR( vendor, name ) \
	static char __vendorstr_##vendor[] __devinitdata = name;
VENDOR(003d,"Lockheed Martin-Marietta Corp")

