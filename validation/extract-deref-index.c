/*
 * check-name: extract dereference index
 * check-command: extract $file && grep -Eq '^[[:space:]]+switch [(]fmt[[]1[]][)] \{$' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-deref-index.c.extract
 * check-output-end
 */

int index_switch(const int *fmt)
{
	switch (*(fmt + 1)) {
	case 1000:
		return 1;
	default:
		return 0;
	}
}
