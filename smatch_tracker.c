#include "smatch.h"

ALLOCATOR(tracker, "trackers");

void add_tracker(struct tracker_list **list, const char *name, int owner, 
		struct symbol *sym)
{
	struct tracker *tmp;

	if (in_tracker_list(*list, name, owner, sym))
		return;

	tmp = __alloc_tracker(0);
	tmp->name = alloc_string(name);
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

void free_tracker_list(struct tracker_list **list)
{
	__free_ptr_list((struct ptr_list **)list);
}

void free_trackers_and_list(struct tracker_list **list)
{
	struct tracker *tmp;

	FOR_EACH_PTR(*list, tmp) {
		free_string(tmp->name);
		__free_tracker(tmp);
	} END_FOR_EACH_PTR(tmp);
	free_tracker_list(list);
}

