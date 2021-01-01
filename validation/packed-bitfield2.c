struct bf2 {
	unsigned p1:2;
	unsigned i1:32;
	unsigned p2:2;
	unsigned s9:9;
	unsigned s9:9;
	unsigned s9:9;
	unsigned b1:1;
} __attribute__((packed));

_Static_assert(sizeof(struct bf2) == 8);

/*
 * check-name: packed-bitfield2
 */
