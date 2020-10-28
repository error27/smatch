typedef	  signed int	sint;
typedef	unsigned int	uint;

sint seq(sint p, sint a) { return (123 == p) == (p == 123); }
sint sne(sint p, sint a) { return (123 != p) == (p != 123); }

sint slt(sint p, sint a) { return (123 >  p) == (p <  123); }
sint sle(sint p, sint a) { return (123 >= p) == (p <= 123); }
sint sge(sint p, sint a) { return (123 <= p) == (p >= 123); }
sint sgt(sint p, sint a) { return (123 <  p) == (p >  123); }

uint ueq(uint p, uint a) { return (123 == p) == (p == 123); }
uint une(uint p, uint a) { return (123 != p) == (p != 123); }

uint ubt(uint p, uint a) { return (123 >  p) == (p <  123); }
uint ube(uint p, uint a) { return (123 >= p) == (p <= 123); }
uint uae(uint p, uint a) { return (123 <= p) == (p >= 123); }
uint uat(uint p, uint a) { return (123 <  p) == (p >  123); }

/*
 * check-name: canonical-cmp
 * check-description: check that constants move rightside
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: \\$123,
 */
