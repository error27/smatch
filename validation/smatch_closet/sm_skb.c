struct sk_buff {
	int poop;
};
struct foo {
	int x;
};
struct ture {
	struct sk_buff *skb;
};

struct wrap1 {
	struct ture *a;
};
struct wrap2 {
	struct foo *c; 
	struct wrap1 *b;
};
struct wrap3 {
	struct foo *c; 
};

struct sk_buff *skb;
struct sk_buff **ptr;
struct ture *x;
struct wrap1 *u;
struct wrap2 *y;
struct wrap3 *z;

void kfree(void *data);

void func (void)
{
	kfree(skb);
	kfree(x->skb);
	kfree(y->c);
	kfree(u->a->skb);
	kfree(u->a);
	kfree(y->b->a->skb);
	kfree(z->c);
	kfree(ptr);
}

/*
 * You're not allowed to pass sk_buff pointers to kfree().  There is a 
 * check for this.  It's a good check.  But there are currently no bugs
 * of this type in the kernel so the check is disabled by default.
 */
