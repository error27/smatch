/*
 * Copyright (C) 2010 Joseph Adams <joeyadams3.14159@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef CCAN_AVL_H
#define CCAN_AVL_H

#include <stdbool.h>
#include <stddef.h>

struct sm_state;

typedef struct AVL           AVL;
typedef struct AvlNode       AvlNode;
typedef struct AvlIter       AvlIter;

void avl_free(AVL **avl);
	/* Free an AVL tree. */

struct sm_state *avl_lookup(const AVL *avl, const struct sm_state *sm);
	/* O(log n). Lookup a sm.  Return NULL if the sm is not present. */

#define avl_member(avl, sm) (!!avl_lookup_node(avl, sm))
	/* O(log n). See if a sm is present. */

size_t avl_count(const AVL *avl);
	/* O(1). Return the number of elements in the tree. */

bool avl_insert(AVL **avl, const struct sm_state *sm);
	/*
	 * O(log n). Insert an sm or replace it if already present.
	 *
	 * Return false if the insertion replaced an existing sm.
	 */

bool avl_remove(AVL **avl, const struct sm_state *sm);
	/*
	 * O(log n). Remove an sm (if present).
	 *
	 * Return true if it was removed.
	 */

bool avl_check_invariants(AVL *avl);
	/* For testing purposes.  This function will always return true :-) */


/************************* Traversal *************************/

#define avl_foreach(iter, avl)         avl_traverse(iter, avl, FORWARD)
	/*
	 * O(n). Traverse an AVL tree in order.
	 *
	 * Example:
	 *
	 * AvlIter i;
	 *
	 * avl_foreach(i, avl)
	 *     printf("%s -> %s\n", i.sm->name, i.sm->state->name);
	 */

#define avl_foreach_reverse(iter, avl) avl_traverse(iter, avl, BACKWARD)
	/* O(n). Traverse an AVL tree in reverse order. */

typedef enum AvlDirection {FORWARD = 0, BACKWARD = 1} AvlDirection;

struct AvlIter {
	struct sm_state *sm;
	AvlNode      *node;

	/* private */
	AvlNode      *stack[100];
	int           stack_index;
	AvlDirection  direction;
};

void avl_iter_begin(AvlIter *iter, AVL *avl, AvlDirection dir);
void avl_iter_next(AvlIter *iter);
#define avl_traverse(iter, avl, direction)        \
	for (avl_iter_begin(&(iter), avl, direction); \
	     (iter).node != NULL;                     \
	     avl_iter_next(&iter))


/***************** Internal data structures ******************/

struct AVL {
	AvlNode    *root;
	size_t      count;
};

struct AvlNode {
	const struct sm_state *sm;

	AvlNode    *lr[2];
	int         balance; /* -1, 0, or 1 */
};

AvlNode *avl_lookup_node(const AVL *avl, const struct sm_state *sm);
	/* O(log n). Lookup an AVL node by sm.  Return NULL if not present. */

AVL *avl_clone(AVL *orig);

#endif
