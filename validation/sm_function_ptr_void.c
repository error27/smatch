struct callback {
	void *fn;
};

extern void pass_to_client(void *fn, void *data);

static void set_callback(struct callback *callback, void *fn)
{
	callback->fn = fn;
	pass_to_client(callback->fn, 0);
}

static void target(void)
{
}

void test(struct callback *callback)
{
	set_callback(callback, target);
}

/*
 * check-name: smatch function pointer stored as void pointer
 * check-command: validation/smatch_function_ptr_void.sh sm_function_ptr_void.c
 *
 * check-output-start
(struct callback)->fn -> pass_to_client param 0
 * check-output-end
 */
