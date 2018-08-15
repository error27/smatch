/* Resembles include/linux/compiler_types.h */
#define __kernel __attribute__((address_space(0)))
#define __user __attribute__((address_space(1)))
#define __iomem __attribute__((address_space(2)))
#define __percpu __attribute__((address_space(3)))
#define __rcu __attribute__((address_space(4)))


typedef unsigned long ulong;
typedef struct s obj_t;

static void expl(obj_t __kernel *k, obj_t __iomem *o,
		 obj_t __user *p, obj_t __percpu *pc,
		 obj_t __rcu *r)
{
	(ulong)(k);
	(void *)(k);
	(obj_t*)(k);
	(obj_t __kernel*)(k);

	(ulong)(o);
	(void *)(o);
	(obj_t*)(o);
	(obj_t __iomem*)(o);

	(ulong)(p);
	(void *)(p);
	(obj_t*)(p);
	(obj_t __user*)(p);

	(ulong)(pc);
	(void *)(pc);
	(obj_t*)(pc);
	(obj_t __percpu*)(pc);

	(ulong)(r);
	(void *)(r);
	(obj_t*)(r);
	(obj_t __rcu*)(r);
}

/*
 * check-name: Waddress-space-all-attr
 * check-command: sparse -Wcast-from-as -Wcast-to-as $file
 *
 * check-error-start
Waddress-space-all-attr.c:21:10: warning: cast removes address space of expression (<asn:2>)
Waddress-space-all-attr.c:22:10: warning: cast removes address space of expression (<asn:2>)
Waddress-space-all-attr.c:23:10: warning: cast removes address space of expression (<asn:2>)
Waddress-space-all-attr.c:26:10: warning: cast removes address space of expression (<asn:1>)
Waddress-space-all-attr.c:27:10: warning: cast removes address space of expression (<asn:1>)
Waddress-space-all-attr.c:28:10: warning: cast removes address space of expression (<asn:1>)
Waddress-space-all-attr.c:31:10: warning: cast removes address space of expression (<asn:3>)
Waddress-space-all-attr.c:32:10: warning: cast removes address space of expression (<asn:3>)
Waddress-space-all-attr.c:33:10: warning: cast removes address space of expression (<asn:3>)
Waddress-space-all-attr.c:36:10: warning: cast removes address space of expression (<asn:4>)
Waddress-space-all-attr.c:37:10: warning: cast removes address space of expression (<asn:4>)
Waddress-space-all-attr.c:38:10: warning: cast removes address space of expression (<asn:4>)
 * check-error-end
 */
