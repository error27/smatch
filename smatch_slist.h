struct state_history {
	unsigned int loc;
};
DECLARE_PTR_LIST(history_list, struct state_history);

struct sm_state {
        char *name;
	unsigned int owner;
	struct symbol *sym;
  	struct smatch_state *state;
	struct history_list *line_history;
	struct history_list *path_history;
};

DECLARE_ALLOCATOR(sm_state);
DECLARE_PTR_LIST(state_list, struct sm_state);
DECLARE_PTR_LIST(state_list_stack, struct state_list);
struct named_slist {
	char *name;
	struct state_list *slist;
};
DECLARE_ALLOCATOR(named_slist);
DECLARE_PTR_LIST(slist_stack, struct named_slist);

extern struct state_list *cur_slist; /* current states */

void add_history(struct sm_state *state);
struct sm_state *alloc_state(const char *name, int owner, 
			     struct symbol *sym, 
			     struct smatch_state *state);

struct sm_state *clone_state(struct sm_state *s);
struct state_list *clone_slist(struct state_list *from_slist);

struct smatch_state *merge_states(const char *name, int owner, struct symbol *sym,
		 struct smatch_state *state1, struct smatch_state *state2);
void merge_state_slist(struct state_list **slist, const char *name, int owner,
		       struct symbol *sym, struct smatch_state *state);

struct smatch_state *get_state_slist(struct state_list *slist, const char *name, int owner,
		    struct symbol *sym);

void add_state_slist(struct state_list **slist, struct sm_state *state);

void set_state_slist(struct state_list **slist, const char *name, int owner,
		     struct symbol *sym, struct smatch_state *state);

void merge_state_slist(struct state_list **slist, const char *name, int owner,
		       struct symbol *sym, struct smatch_state *state);

void delete_state_slist(struct state_list **slist, const char *name, int owner,
			struct symbol *sym);

struct smatch_state *get_state_slist(struct state_list *slist, const char *name, int owner,
		    struct symbol *sym);

void push_slist(struct state_list_stack **list_stack, struct state_list *slist);

struct state_list *pop_slist(struct state_list_stack **list_stack);

void del_slist(struct state_list **slist);

void del_slist_stack(struct state_list_stack **slist_stack);

void set_state_stack(struct state_list_stack **stack, const char *name, 
		     int owner, struct symbol *sym, struct smatch_state *state);

struct smatch_state *get_state_stack(struct state_list_stack *stack, const char *name,
		    int owner, struct symbol *sym);

void merge_state_stack(struct state_list_stack **stack, const char *name,
		       int owner, struct symbol *sym, struct smatch_state *state);

void merge_slist(struct state_list **to, struct state_list *slist);
void and_slist_stack(struct state_list_stack **slist_stack, 
		     struct state_list *tmp_slist);

void or_slist_stack(struct state_list_stack **slist_stack);

struct state_list *get_slist_from_slist_stack(struct slist_stack *stack,
					      const char *name);

void overwrite_slist(struct state_list *from, struct state_list **to);
