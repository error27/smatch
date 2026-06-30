#include <string.h>
#include "check_debug.h"

struct foo {
	void *p;
	int a, b, c;
};

void func(void *p, struct foo *foo)
{
	struct foo bar;

	__smatch_arg_key(p, "$->foo->bar");
	__smatch_arg_key(p, "*$->foo");
	__smatch_arg_key(&p, "$");
	__smatch_arg_key(&p, "$.foo");
	__smatch_arg_key(p, "(*$)->foo");
	__smatch_arg_key(&p, "(*$)->foo");
	__smatch_arg_key(p, "&$");
	__smatch_arg_key(&p, "&$");
	__smatch_arg_key(p, "*$");
	__smatch_arg_key(p, "**$");
	__smatch_arg_key(&p, "**$");
	__smatch_arg_key(foo, "(*$)->p->foo");
	__smatch_arg_key(&bar, "$->a");
	__smatch_arg_key(&bar, "$->p->xxx");
	__smatch_arg_key(bar, "$.p->xxx");
}

/*
 * check-name: smatch: arg key
 * check-command: smatch -I.. sm_arg_key1.c
 *
 * check-output-start
sm_arg_key1.c:13 func() arg='p' key='$->foo->bar' result='p->foo->bar'
sm_arg_key1.c:14 func() arg='p' key='*$->foo' result='*p->foo'
sm_arg_key1.c:15 func() arg='&p' key='$' result='&p'
sm_arg_key1.c:16 func() arg='&p' key='$.foo' result='&p.foo'
sm_arg_key1.c:17 func() arg='p' key='(*$)->foo' result='(*p)->foo'
sm_arg_key1.c:18 func() arg='&p' key='(*$)->foo' result='p->foo'
sm_arg_key1.c:19 func() arg='p' key='&$' result='&p'
sm_arg_key1.c:20 func() arg='&p' key='&$' result='&&p'
sm_arg_key1.c:21 func() arg='p' key='*$' result='*p'
sm_arg_key1.c:22 func() arg='p' key='**$' result='**p'
sm_arg_key1.c:23 func() arg='&p' key='**$' result='*p'
sm_arg_key1.c:24 func() arg='foo' key='(*$)->p->foo' result='(*foo)->p->foo'
sm_arg_key1.c:25 func() arg='&bar' key='$->a' result='bar.a'
sm_arg_key1.c:26 func() arg='&bar' key='$->p->xxx' result='bar.p->xxx'
sm_arg_key1.c:27 func() arg='bar' key='$.p->xxx' result='bar.p->xxx'
 * check-output-end
 */
