enum foobar {
	FOO = (void*)0,
	BAR = (void*)1,
	BAZ = (int*)0,
	QUX = (int*)123,
};

/*
 * check-name: enum-type-dubious
 * check-known-to-fail
 *
 * check-error-start
validation/enum-type-dubious.c:2:8: error: enumerator value for 'FOO' is not an integer constant
validation/enum-type-dubious.c:3:8: error: enumerator value for 'BAR' is not an integer constant
validation/enum-type-dubious.c:4:8: error: enumerator value for 'BAZ' is not an integer constant
validation/enum-type-dubious.c:5:8: error: enumerator value for 'QUX' is not an integer constant
 * check-error-end
 */
