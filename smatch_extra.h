enum data_type {
	DATA_NUM,
//	DATA_RETURN,
//	DATA_POINTER,
//	DATA_BITFIELD,
};

struct num_range {
	long long min;
	long long max;
};

DECLARE_PTR_LIST(range_list, struct int_range);

struct data_info {
	data_type type;
	bool initialized;
	union {
		// DATA_NUM
		struct {
			struct range_list *ranges;
			struct range_list *filters;
		};
	};
};


/* these are implimented in smatch_extra_helper.c */
void remove_num(struct range_list **range, const int num);
void remove_range(struct range_list **range, const range_list *cutter);

void add_to_range(struct range_list **range, const int num);
void combine_range(struct range_list **range, const range_list *new);

void in_range(struct range_list *list, long long num);
int ranges_overlap(struct range_list *a, struct range_list *b);

