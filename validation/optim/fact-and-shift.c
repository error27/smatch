typedef unsigned int uint;
typedef   signed int sint;


uint fact_and_shl(uint a, uint b, uint s)
{
	return ((a << s) & (b << s)) == ((a & b) << s);
}

uint fact_and_lsr(uint a, uint b, uint s)
{
	return ((a >> s) & (b >> s)) == ((a & b) >> s);
}

sint fact_and_asr(sint a, sint b, sint s)
{
	return ((a >> s) & (b >> s)) == ((a & b) >> s);
}

/*
 * check-name: fact-and-shift
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
