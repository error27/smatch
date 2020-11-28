typedef unsigned int uint;
typedef   signed int sint;


uint fact_xor_shl(uint a, uint b, uint s)
{
	return ((a << s) ^ (b << s)) == ((a ^ b) << s);
}

uint fact_xor_lsr(uint a, uint b, uint s)
{
	return ((a >> s) ^ (b >> s)) == ((a ^ b) >> s);
}

sint fact_xor_asr(sint a, sint b, sint s)
{
	return ((a >> s) ^ (b >> s)) == ((a ^ b) >> s);
}

/*
 * check-name: fact-xor-shift
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
