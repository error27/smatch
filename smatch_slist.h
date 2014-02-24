struct AVL;

DECLARE_PTR_LIST(state_list, struct sm_state);
DECLARE_PTR_LIST(state_list_stack, struct state_list);
DECLARE_PTR_LIST(stree_stack, struct AVL);

struct named_slist {
	char *name;
	struct state_list *slist;
};
DECLARE_ALLOCATOR(named_slist);
DECLARE_PTR_LIST(named_stack, struct named_slist);

struct named_stree {
	char *name;
	struct AVL *stree;
};
DECLARE_ALLOCATOR(named_stree);
DECLARE_PTR_LIST(named_stree_stack, struct named_stree);


extern struct state_list_stack *implied_pools;
extern int __slist_id;

char *show_sm(struct sm_state *sm);
void __print_slist(struct state_list *slist);
void __print_stree(struct AVL *stree);
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

int too_many_possible(struct sm_state *sm);
struct sm_state *merge_sm_states(struct sm_state *one, struct sm_state *two);
struct smatch_state *get_state_slist(struct state_list *slist, int owner, const char *name,
		    struct symbol *sym);
struct smatch_state *get_state_stree(struct AVL *stree, int owner, const char *name,
		    struct symbol *sym);

struct sm_state *get_sm_state_slist(struct state_list *slist, int owner, const char *name,
		    struct symbol *sym);
struct sm_state *get_sm_state_stree(struct AVL *stree, int owner, const char *name,
		    struct symbol *sym);

void overwrite_sm_state(struct state_list **slist, struct sm_state *sm);
void overwrite_sm_state_stree(struct AVL **stree, struct sm_state *sm);
void overwrite_sm_state_stack(struct state_list_stack **stack, struct sm_state *sm);
struct sm_state *set_state_slist(struct state_list **slist, int owner, const char *name,
		     struct symbol *sym, struct smatch_state *state);
struct sm_state *set_state_stree(struct AVL **stree, int owner, const char *name,
		     struct symbol *sym, struct smatch_state *state);

void delete_state_slist(struct state_list **slist, int owner, const char *name,
			struct symbol *sym);
void delete_state_stree(struct AVL **stree, int owner, const char *name,
			struct symbol *sym);

void delete_state_stack(struct state_list_stack **stack, int owner, const char *name,
			struct symbol *sym);
void delete_state_stree_stack(struct stree_stack **stack, int owner, const char *name,
			struct symbol *sym);

void push_slist(struct state_list_stack **list_stack, struct state_list *slist);
void push_stree(struct stree_stack **list_stack, struct AVL *stree);

struct state_list *pop_slist(struct state_list_stack **list_stack);
struct AVL *pop_stree(struct stree_stack **list_stack);

void free_slist(struct state_list **slist);
void free_stree(struct AVL **stree);
void free_stack(struct state_list_stack **stack);
void free_stree_stack(struct stree_stack **stack);
void free_stack_and_slists(struct state_list_stack **slist_stack);
void free_stack_and_strees(struct stree_stack **stree_stack);

struct sm_state *set_state_stack(struct state_list_stack **stack, int owner, const char *name,
				struct symbol *sym, struct smatch_state *state);
struct sm_state *set_state_stree_stack(struct stree_stack **stack, int owner, const char *name,
				struct symbol *sym, struct smatch_state *state);

struct smatch_state *get_state_stack(struct state_list_stack *stack, int owner,
				const char *name, struct symbol *sym);
struct smatch_state *get_state_stree_stack(struct stree_stack *stack, int owner,
				const char *name, struct symbol *sym);

int out_of_memory(void);
int low_on_memory(void);
void merge_slist(struct state_list **to, struct state_list *slist);
void merge_stree(struct AVL **to, struct AVL *stree);
void filter_slist(struct state_list **slist, struct state_list *filter);
void filter_stree(struct AVL **stree, struct AVL *filter);
void and_slist_stack(struct state_list_stack **slist_stack);
void and_stree_stack(struct stree_stack **stree_stack);

void or_slist_stack(struct state_list_stack **pre_conds,
		    struct state_list *cur_slist,
		    struct state_list_stack **slist_stack);
void or_stree_stack(struct stree_stack **pre_conds,
		    struct AVL *cur_stree,
		    struct stree_stack **stack);

struct state_list **get_slist_from_named_stack(struct named_stack *stack,
					      const char *name);

struct state_list **get_stree_from_named_stack(struct named_stree_stack *stack,
					      const char *name);

void overwrite_slist(struct state_list *from, struct state_list **to);
void overwrite_stree(struct AVL *from, struct AVL **to);

/* add stuff smatch_returns.c here */

void all_return_states_hook(void (*callback)(struct state_list *slist));
void all_return_states_hook_stree(void (*callback)(struct AVL *slist));

int get_slist_id(struct state_list *slist);
int get_stree_id(struct AVL *slist);

