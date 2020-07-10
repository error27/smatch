extern void def(void *);

struct s1 {
	int a;
};

int use1(void)
{
	struct s1 s = { 3 };

	def(&s.a);

	return s.a;
}

/*
 * check-name: eval/addressable-complex
 * check-command: test-linearize -Wno-decl -fdump-ir $file
 *
 * check-output-ignore
 * check-output-contains: load\\.
 * check-output-excludes: return\\..*\\$3
 */
