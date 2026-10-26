struct array_entry {
	int value;
};

static const struct array_entry values[] = {
	{ 1 },
	{ 2 },
};

static inline unsigned int value_count(void)
{
	return sizeof(values) / sizeof(values[0]);
}
