#include "smatch.h"

void add_tracker(struct tracker_list **list, const char *name, int owner, 
		struct symbol *sym)
{
	struct tracker *tmp;

	if (in_tracker_list(*list, name, owner, sym))
		return;

	tmp = malloc(sizeof(*tmp));
	tmp->name = name;
	tmp->owner = owner;
	tmp->sym = sym;
	add_ptr_list(list, tmp);
}

int in_tracker_list(struct tracker_list *list, const char *name, int owner,
		struct symbol *sym)
{
	struct tracker *tmp;

	FOR_EACH_PTR(list, tmp) {
		if (tmp->owner == owner && tmp->sym == sym 
		    && !strcmp(tmp->name, name))
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}
