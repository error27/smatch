/*
 * check-name: extract printed dependencies
 * check-command: extract $file && { grep -c '^struct item' $file.extract || :; }
 *
 * check-output-start
 * extract: wrote $file.extract
1
 * check-output-end
 */

typedef unsigned int item_id_t;

struct item;

struct item {
	item_id_t id;
};

static inline int item_id(struct item *item)
{
	return item->id;
}

static inline int item_id(struct item *item);

int use_item(struct item *item)
{
	return item_id(item);
}
