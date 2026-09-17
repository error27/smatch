struct item {
	void *data;
	unsigned long len;
};

struct buffer {
	struct item array[1];
	struct item *pointer;
};

static unsigned long read_len(struct item *item)
{
	return item->len;
}

static unsigned long read_array(struct buffer *buf)
{
	return read_len(buf->array);
}

static unsigned long read_pointer(struct buffer *buf)
{
	return read_len(buf->pointer);
}

unsigned long test_array(struct buffer *buf)
{
	__builtin_memset(buf, 0, sizeof(*buf));
	return read_array(buf);
}

unsigned long test_pointer(struct buffer *buf)
{
	__builtin_memset(buf, 0, sizeof(*buf));
	return read_pointer(buf);
}

/*
 * check-name: smatch: dereference before set ignores arrays
 * check-command: validation/smatch_db_test.sh -p=kernel sm_deref_before_set_array.c
 *
 * check-output-start
sm_deref_before_set_array.c:35 test_pointer() warn: pointer dereferenced without being set 'buf->pointer'
 * check-output-end
 */
