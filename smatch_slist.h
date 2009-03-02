struct state_history {
	unsigned int loc;
};
DECLARE_PTR_LIST(history_list, struct state_history);

DECLARE_ALLOCATOR(sm_state);
DECLARE_PTR_LIST(state_list, struct sm_state);
DECLARE_PTR_LIST(state_list_stack, struct state_list);
struct named_slist {
	char *name;
	struct state_list *slist;
};
DECLARE_ALLOCATOR(named_slist);
DECLARE_PTR_LIST(named_stack, struct named_slist);

extern struct state_list_stack *implied_pools;

void __print_slist(struct state_list *slist);
void add_history(struct sm_state *state);
int cmp_tracker(const struct sm_state *a, const struct sm_state *b);
struct sm_state *alloc_state(const char *name, int owner, 
			     struct symbol *sym, 
			     struct smatch_state *state);

void free_every_single_sm_state();
struct sm_state *clone_state(struct sm_state *s);
struct state_list *clone_slist(struct state_list *from_slist);
struct state_list_stack *clone_stack(struct state_list_stack *from_stack);

int slist_has_state(struct state_list *slist, struct smatch_state *state);
struct smatch_state *merge_states(const char *name, int owner,
				  struct symbol *sym,
				  struct smatch_state *state1,
				  struct smatch_state *state2);

struct smatch_state *get_state_slist(struct state_list *slist, const char *name, int owner,
		    struct symbol *sym);

struct sm_state *get_sm_state_slist(struct state_list *slist, const char *name, int owner,
		    struct symbol *sym);

void overwrite_sm_state(struct state_list **slist, struct sm_state *state);
void overwrite_sm_state_stack(struct state_list_stack **stack,
			struct sm_state *state);
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

void free_slist(struct state_list **slist);

void del_slist_stack(struct state_list_stack **slist_stack);

void set_state_stack(struct state_list_stack **stack, const char *name, 
		     int owner, struct symbol *sym, struct smatch_state *state);

struct smatch_state *get_state_stack(struct state_list_stack *stack, const char *name,
		    int owner, struct symbol *sym);

void merge_slist(struct state_list **to, struct state_list *slist);
struct state_list *clone_states_in_pool(struct state_list *pool,
				struct state_list *cur_slist);
void filter(struct state_list **slist, struct state_list *filter,
	struct state_list *cur_slist);
void and_slist_stack(struct state_list_stack **slist_stack);

void or_slist_stack(struct state_list_stack **slist_stack);

struct state_list **get_slist_from_named_stack(struct named_stack *stack,
					      const char *name);

void overwrite_slist(struct state_list *from, struct state_list **to);
