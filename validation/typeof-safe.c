#define	__safe		__attribute__((safe))

static void test_safe(void)
{
	int obj;
	int __safe *ptr;

	int __safe *ptr2 = ptr;
	typeof(ptr) ptr3 = ptr;
	typeof(*ptr) var2 = obj;
	int __safe  var3 = obj;
	int *ptr4 = &obj;
	int *ptr5 = ptr;		// KO

	typeof(*ptr) sobj;
	typeof(&sobj) ptr6 = &obj;
	typeof(&sobj) ptr7 = ptr;	// KO

	obj = obj;
	ptr = ptr;
	obj = *ptr;
	ptr = (int __safe *) &obj;
}

/*
 * check-name: typeof-safe
 *
 * check-error-start
typeof-safe.c:13:21: warning: incorrect type in initializer (different modifiers)
typeof-safe.c:13:21:    expected int *ptr5
typeof-safe.c:13:21:    got int [safe] *ptr
typeof-safe.c:17:30: warning: incorrect type in initializer (different modifiers)
typeof-safe.c:17:30:    expected int *ptr7
typeof-safe.c:17:30:    got int [safe] *ptr
 * check-error-end
 */
