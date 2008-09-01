DECLARE_ALLOCATOR(smatch_state);
DECLARE_PTR_LIST(state_list, struct smatch_state);
DECLARE_PTR_LIST(state_list_stack, struct state_list);
struct named_slist {
	char *name;
	struct state_list *slist;
};
DECLARE_ALLOCATOR(named_slist);
DECLARE_PTR_LIST(slist_stack, struct named_slist);

extern struct state_list *cur_slist; /* current states */

void add_history(struct smatch_state *state);
struct smatch_state *alloc_state(const char *name, int owner, 
				 struct symbol *sym, int state);

struct smatch_state *clone_state(struct smatch_state *s);
struct state_list *clone_slist(struct state_list *from_slist);

int merge_states(const char *name, int owner, struct symbol *sym,
		 int state1, int state2);
void merge_state_slist(struct state_list **slist, const char *name, int owner,
		       struct symbol *sym, int state);

int get_state_slist(struct state_list *slist, const char *name, int owner,
		    struct symbol *sym);

void add_state_slist(struct state_list **slist, struct smatch_state *state);

void set_state_slist(struct state_list **slist, const char *name, int owner,
		     struct symbol *sym, int state);

void merge_state_slist(struct state_list **slist, const char *name, int owner,
		       struct symbol *sym, int state);

void delete_state_slist(struct state_list **slist, const char *name, int owner,
			struct symbol *sym);

int get_state_slist(struct state_list *slist, const char *name, int owner,
		    struct symbol *sym);

void push_slist(struct state_list_stack **list_stack, struct state_list *slist);

struct state_list *pop_slist(struct state_list_stack **list_stack);

void del_slist(struct state_list **slist);

void del_slist_stack(struct state_list_stack **slist_stack);

void set_state_stack(struct state_list_stack **stack, const char *name, 
		     int owner, struct symbol *sym, int state);

int get_state_stack(struct state_list_stack *stack, const char *name,
		    int owner, struct symbol *sym);

void merge_state_stack(struct state_list_stack **stack, const char *name,
		       int owner, struct symbol *sym, int state);

void merge_slist(struct state_list *slist);
void and_slist_stack(struct state_list_stack **slist_stack, 
		     struct state_list *tmp_slist);

void or_slist_stack(struct state_list_stack **slist_stack);

struct state_list *get_slist_from_slist_stack(const char *name);

