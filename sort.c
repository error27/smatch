/*
 * This is a horribly stupid list sort. We
 * use it to sort C initializers into ascending
 * order.
 *
 * These things tend to be sorted already, so we
 * should optimize for that case and not really
 * care about the other ones.
 */
#include <stdio.h>
#include "lib.h"

static void array_sort(void **ptr, int nr, int (*cmp)(const void *, const void *))
{
	int i;
	for (i = 1; i < nr; i++) {
		void *p = ptr[i];
		if (cmp(ptr[i-1],p) < 0) {
			int j = i;
			do {
				ptr[j] = ptr[j-1];
				if (!--j)
					break;
			} while (cmp(ptr[j-1], p) < 0);
			ptr[j] = p;
		}
	}
}

static int merge_array(void **arr1, int nr1, void **arr2, int nr2, int (*cmp)(const void *, const void *))
{
	int i, j;
	void *a, *b;

	if (!nr1 || !nr2)
		return 0;

	i = nr1-1;
	j = 0;
	a = arr1[i];	/* last entry of first array */
	b = arr2[j];	/* first entry of last array */

	/* If they are already sorted, don't do anything else */
	if (cmp(a, b) >= 0)
		return 0;

	/*
	 * Remember: we don't care. The above was
	 * the speedpath. This is a joke. Although
	 * it happens to be a joke that gets the
	 * reverse sorted case right, I think.
	 *
	 * Damn, it's been _ages_ since I did sort
	 * routines. I feel like a first-year CS
	 * student again.
	 */
	do {
		arr2[j] = a;
		arr1[i] = b;
		if (--i < 0)
			break;
		if (++j == nr2)
			break;
		a = arr1[i];
		b = arr2[j];
	} while (cmp(a, b) < 0);

	array_sort(arr1, nr1, cmp);
	array_sort(arr2, nr2, cmp);

	return 1;
}

void sort_list(struct ptr_list **list, int (*cmp)(const void *, const void *))
{
	struct ptr_list *head = *list;

	if (head) {
		int repeat;

		/* Sort all the sub-lists */
		struct ptr_list *list = head, *next;
		do {
			array_sort(list->list, list->nr, cmp);
			list = list->next;
		} while (list != head);

		/* Merge the damn things together .. */
		do {
			repeat = 0;

			list = head;
			next = list->next;
			while (next != head) {
				repeat |= merge_array(list->list, list->nr, next->list, next->nr, cmp);
				list = next;
				next = next->next;
			}
		} while (repeat);
	}
}
