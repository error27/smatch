struct page { void *mapping; };
struct folio { struct page page; };

void *test(struct folio *head, struct folio *tail)
{
	(&tail->page + 0)->mapping = (&head->page + 0)->mapping;
	return &(&head->page)[0];
}

/*
 * check-name: extract subscript of an address of a member
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-subscript-address-member.c.extract.last
 * check-output-end
 */
