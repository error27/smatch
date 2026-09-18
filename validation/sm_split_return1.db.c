static inline unsigned long
array_index_mask_nospec(unsigned long index, unsigned long size)
{
	unsigned long mask;

	asm volatile(
	"\tcmp\t%1, %2\n"
	"\tsbc\t%0, xzr, xzr\n"
	: "=r" (mask)
	: "r" (index), "Ir" (size)
	: "cc");

	return mask;
}

unsigned long use_mask(unsigned long index)
{
	return array_index_mask_nospec(index, 4);
}
