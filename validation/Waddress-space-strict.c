#define __user __attribute__((address_space(1)))

typedef unsigned long ulong;
typedef long long llong;
typedef struct s obj_t;

static void expl(int i, ulong u, llong l, void *v, obj_t *o, obj_t __user *p)
{
	(obj_t*)(i);
	(obj_t __user*)(i);

	(obj_t*)(u);
	(obj_t __user*)(u);

	(obj_t*)(l);
	(obj_t __user*)(l);

	(obj_t*)(v);
	(obj_t __user*)(v);

	(int)(o);
	(ulong)(o);
	(llong)(o);
	(void *)(o);
	(obj_t*)(o);
	(obj_t __user*)(o);

	(int)(p);		// w
	(ulong)(p);		// w!
	(llong)(p);		// w
	(void *)(p);		// w
	(obj_t*)(p);		// w
	(obj_t __user*)(p);	// ok
}

/*
 * check-name: Waddress-space-strict
 * check-command: sparse -Wcast-from-as -Wcast-to-as $file
 *
 * check-error-start
Waddress-space-strict.c:10:10: warning: cast adds address space to expression (<asn:1>)
Waddress-space-strict.c:13:10: warning: cast adds address space to expression (<asn:1>)
Waddress-space-strict.c:16:10: warning: cast adds address space to expression (<asn:1>)
Waddress-space-strict.c:19:10: warning: cast adds address space to expression (<asn:1>)
Waddress-space-strict.c:26:10: warning: cast adds address space to expression (<asn:1>)
Waddress-space-strict.c:28:10: warning: cast removes address space of expression
Waddress-space-strict.c:29:10: warning: cast removes address space of expression
Waddress-space-strict.c:30:10: warning: cast removes address space of expression
Waddress-space-strict.c:31:10: warning: cast removes address space of expression
Waddress-space-strict.c:32:10: warning: cast removes address space of expression
Waddress-space-strict.c:9:18: warning: non size-preserving integer to pointer cast
Waddress-space-strict.c:10:25: warning: non size-preserving integer to pointer cast
Waddress-space-strict.c:21:15: warning: non size-preserving pointer to integer cast
Waddress-space-strict.c:28:15: warning: non size-preserving pointer to integer cast
 * check-error-end
 */
