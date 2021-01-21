typedef unsigned int uint;
typedef   signed int sint;


uint fact_ior_shl(uint a, uint b, uint s)
{
	return ((a << s) | (b << s)) == ((a | b) << s);
}

uint fact_ior_lsr(uint a, uint b, uint s)
{
	return ((a >> s) | (b >> s)) == ((a | b) >> s);
}

sint fact_ior_asr(sint a, sint b, sint s)
{
	return ((a >> s) | (b >> s)) == ((a | b) >> s);
}

/*
 * check-name: fact-ior-shift
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
