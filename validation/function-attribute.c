#define __pure __attribute__((pure))

struct s {
	int x;
};

static __pure struct s *grab(struct s *ptr)
{
	return ptr;
}

static void foo(struct s *ptr)
{
	struct s *ptr = grab(ptr);
}

/*
 * check-name: function-attribute
 */
