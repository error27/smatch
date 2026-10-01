struct callback {
	void (*fn)(void);
};

static void target(void)
{
}

static const struct callback callbacks[] = {
	(struct callback) { .fn = target },
};

/*
 * check-name: smatch function pointer in compound initializer
 * check-command: validation/smatch_function_ptr_initializer.sh sm_function_ptr_initializer.c
 *
 * check-output-start
target -> (struct callback)->fn
 * check-output-end
 */
