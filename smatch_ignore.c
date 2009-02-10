#include "smatch.h"

static struct tracker_list *ignored;

void add_ignore(const char *name, int owner, struct symbol *sym)
{
	struct tracker *tmp;

	tmp = malloc(sizeof(*tmp));
	tmp->name = name;
	tmp->owner = owner;
	tmp->sym = sym;
	add_ptr_list(&ignored, tmp);
}

int is_ignored(const char *name, int owner, struct symbol *sym)
{
	struct tracker *tmp;

	FOR_EACH_PTR(ignored, tmp) {
		if (tmp->owner == owner && tmp->sym == sym 
		    && !strcmp(tmp->name, name))
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

static void clear_ignores()
{
	__free_ptr_list((struct ptr_list **)&ignored);
}

void register_smatch_ignore(int id)
{
	add_hook(&clear_ignores, END_FUNC_HOOK);
}
