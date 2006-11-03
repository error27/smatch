/* Tests for the "Initializer entry defined twice" warning. */

/* Initializing a struct field twice should trigger the warning. */
struct normal {
	int field1;
	int field2;
};

struct normal struct_error = {
	.field1 = 0,
	.field1 = 0
};

/* Initializing two different fields of a union should trigger the warning. */
struct has_union {
	int x;
	union {
		int a;
		int b;
	} y;
	int z;
};

struct has_union union_error = {
	.y = {
		.a = 0,
		.b = 0
	}
};

/* Empty structures can make two fields have the same offset in a struct.
 * Initializing both should not trigger the warning. */
struct empty { };

struct same_offset {
	struct empty field1;
	int field2;
};

struct same_offset not_an_error = {
	.field1 = { },
	.field2 = 0
};
