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
void add_history(struct sm_state *sm);
int cmp_tracker(const struct sm_state *a, const struct sm_state *b);
char *alloc_sname(const char *str);

void free_every_single_sm_state(void);
struct sm_state *clone_sm(struct sm_state *s);
int is_merged(struct sm_state *sm);
int is_implied(struct sm_state *sm);
struct state_list *clone_slist(struct state_list *from_slist);
struct state_list_stack *clone_stack(struct state_list_stack *from_stack);

int slist_has_state(struct state_list *slist, struct smatch_state *state);
struct smatch_state *merge_states(int owner, const char *name, 
				  struct symbol *sym,
				  struct smatch_state *state1,
				  struct smatch_state *state2);

void add_pool(struct state_list_stack **pools, struct state_list *new);
struct sm_state *merge_sm_states(struct sm_state *one, struct sm_state *two);
struct smatch_state *get_state_slist(struct state_list *slist, int owner, const char *name, 
		    struct symbol *sym);

struct sm_state *get_sm_state_slist(struct state_list *slist, int owner, const char *name, 
		    struct symbol *sym);

void overwrite_sm_state(struct state_list **slist, struct sm_state *sm);
void overwrite_sm_state_stack(struct state_list_stack **stack, struct sm_state *sm);
struct sm_state *set_state_slist(struct state_list **slist, int owner, const char *name, 
		     struct symbol *sym, struct smatch_state *state);

void delete_state_slist(struct state_list **slist, int owner, const char *name, 
			struct symbol *sym);

void delete_state_stack(struct state_list_stack **stack, int owner, const char *name, 
			struct symbol *sym);
void push_slist(struct state_list_stack **list_stack, struct state_list *slist);

struct state_list *pop_slist(struct state_list_stack **list_stack);

void free_slist(struct state_list **slist);
void free_stack(struct state_list_stack **stack);
void free_stack_and_slists(struct state_list_stack **slist_stack);

struct sm_state *set_state_stack(struct state_list_stack **stack, int owner, const char *name, 
				struct symbol *sym, struct smatch_state *state);

struct smatch_state *get_state_stack(struct state_list_stack *stack, int owner, 
				const char *name, struct symbol *sym);

int out_of_memory(void);
void merge_slist(struct state_list **to, struct state_list *slist);
void filter_slist(struct state_list **slist, struct state_list *filter);
void and_slist_stack(struct state_list_stack **slist_stack);

void or_slist_stack(struct state_list_stack **pre_conds,
		    struct state_list *cur_slist,
		    struct state_list_stack **slist_stack);

struct state_list **get_slist_from_named_stack(struct named_stack *stack,
					      const char *name);

void overwrite_slist(struct state_list *from, struct state_list **to);

/* add stuff smatch_returns.c here */

void all_return_states_hook(void (*callback)(struct state_list *slist));

