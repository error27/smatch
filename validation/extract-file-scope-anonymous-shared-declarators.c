/*
 * check-name: extract file scope declarators sharing an anonymous struct
 * check-command: extract $file && gcc -std=gnu11 -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-file-scope-anonymous-shared-declarators.c.extract
 * check-output-end
 */

static struct { unsigned int vesa; unsigned int info; } *RSptr, vesamap[] = {
	{ 1, 2 },
	{ 0, 0 },
};

int find(unsigned int vesa)
{
	for (RSptr = vesamap; RSptr->vesa; RSptr++)
		if (RSptr->vesa == vesa)
			break;
	return RSptr->info;
}

static struct { int a; } *ptrs[2], obj = { 1 }, arr[2];

int get(void)
{
	ptrs[0] = &obj;
	ptrs[1] = arr;
	return ptrs[0]->a + ptrs[1]->a;
}
