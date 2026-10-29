struct hidden;

extern struct hidden global;

int target(void)
{
	return &global != 0;
}

/*
 * check-name: extract extern incomplete aggregate
 * check-command: extract --func=target $file >$file.output 2>&1 && ! grep -Fq 'missing definition for struct hidden' $file.output && cat $file.output && gcc -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-extern-incomplete-aggregate.c.extract.target
 * check-output-end
 */
