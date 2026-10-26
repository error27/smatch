/*
 * check-name: extract redeclared inline
 * check-command: extract $file && { grep -cx 'static inline int increment.*' $file.extract || :; }
 *
 * check-output-start
 * extract: wrote $file.extract
1
 * check-output-end
 */

static inline int increment(int value)
{
	return value + 1;
}

static inline int increment(int value);

int call_increment(int value)
{
	return increment(value);
}
