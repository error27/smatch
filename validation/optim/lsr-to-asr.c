int lsr_to_asr24(int x)
{
	return ((signed char)(((unsigned)x) >> 24)) == (x >> 24);
}


struct s {
	int :30;
	signed int f:2;
};

int lsr_to_asr30(int a)
{
	union {
		int i;
		struct s s;
	} u = { .i = a };
	return u.s.f == (a >> 30);
}

/*
 * check-name: lsr-to-asr
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
