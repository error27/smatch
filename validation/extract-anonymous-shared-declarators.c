/*
 * check-name: extract anonymous struct shared by declarators
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-anonymous-shared-declarators.c.extract
 * check-output-end
 */

void copy_words(void *a, void *b)
{
	struct {
		unsigned long long words[2];
	} *ptr1, *ptr2;
	int other;

	ptr1 = a;
	ptr2 = b;
	other = 0;
	*ptr1 = *ptr2;
	ptr1->words[other] = 1;
}
