#include <crater/rbtree.h>

static inline struct rbtree_node* node_parent(struct rbtree_node* node) {
	return (struct rbtree_node*)(node->_parent_color & ~((uintptr_t)1 << 0));
}

static inline void node_set_parent(struct rbtree_node* node, struct rbtree_node* parent_node) {
	node->_parent_color = (node->_parent_color & ((uintptr_t)1 << 0)) | (uintptr_t)parent_node;
}

static inline int node_color(const struct rbtree_node* node) {
	return node->_parent_color & ((uintptr_t)1 << 0);
}

static inline void node_set_color(struct rbtree_node* node, int color) {
	node->_parent_color = (node->_parent_color & ~((uintptr_t)1 << 0)) | (uintptr_t)color;
}

/*
 * Rotate left:				Rotate right:
 *      N                  V               N                V
 *     / \                / \             / \              / \
 *    a   V     -->      N   c           V   c   -->      a   N
 *       / \            / \             / \                  / \
 *      b   c          a   b           a   b                b   c
 */
static void rotate(struct rbtree* rbtree, struct rbtree_node* node, int direction) {
	struct rbtree_node* pivot = node->child[!direction];
	struct rbtree_node* parent = node_parent(node);

	node->child[!direction] = pivot->child[direction];
	if (pivot->child[direction])
		node_set_parent(pivot->child[direction], node);

	pivot->child[direction] = node;
	node_set_parent(pivot, parent);

	if (parent)
		parent->child[node == parent->child[RBTREE_DIRECTION_RIGHT]] = pivot;
	else
		rbtree->root = pivot;

	node_set_parent(node, pivot);
}

void rbtree_insert_fixup(struct rbtree* rbtree, struct rbtree_node* node) {
	struct rbtree_node* parent;
	while ((parent = node_parent(node)) != NULL && node_color(parent) == RBTREE_COLOR_RED) {
		struct rbtree_node* gparent = node_parent(parent);
		int direction = (parent == gparent->child[RBTREE_DIRECTION_RIGHT]);
		struct rbtree_node* uncle = gparent->child[!direction];

		/* 
		 * Red uncle, flip the generations colors:
		 *        G                  g
		 *       / \                / \
		 *      p   u     -->      P   U
		 *     / \                / \
		 *    n   .              n   .
		 */
		if (uncle && node_color(uncle) == RBTREE_COLOR_RED) {
			node_set_color(uncle, RBTREE_COLOR_BLACK);
			node_set_color(parent, RBTREE_COLOR_BLACK);
			node_set_color(gparent, RBTREE_COLOR_RED);
			node = gparent;
			continue;
		}

		/*
		 *        G                  G
		 *       / \                / \
		 *      p   U     -->      n   U
		 *     / \                / \
		 *    a   n              p   c
		 *       / \            / \
		 *      b   c          a   b
		 */
		if (node == parent->child[!direction]) {
			rotate(rbtree, parent, direction);
			struct rbtree_node* tmp = parent;
			parent = node;
			node = tmp;
		}

		/*
		 *        G                  P
		 *       / \                / \
		 *      p   U     -->      n   g
		 *     / \                    / \
		 *    n   c                  c   U
		 */
		node_set_color(parent, RBTREE_COLOR_BLACK);
		node_set_color(gparent, RBTREE_COLOR_RED);
		rotate(rbtree, gparent, !direction);
	}
	node_set_color(rbtree->root, RBTREE_COLOR_BLACK);
}

static inline bool node_is_null_or_black(const struct rbtree_node* node) {
	return (!node || node_color(node) == RBTREE_COLOR_BLACK);
}

static void rbtree_remove_fixup(struct rbtree* rbtree, struct rbtree_node* node, struct rbtree_node* parent) {
	struct rbtree_node* sibling;
	int direction;
	while (node_is_null_or_black(node) && node != rbtree->root) {
		direction = (node == parent->child[RBTREE_DIRECTION_RIGHT]);
		sibling = parent->child[!direction];

		/*
		 * Red sibling, rotate it away.
		 *
		 *      P                     S
		 *     / \                   / \
		 *    N   s      -->        p   d
		 *       / \               / \
		 *      C   d             N   C
		 */
		if (node_color(sibling) == RBTREE_COLOR_RED) {
			node_set_color(sibling, RBTREE_COLOR_BLACK);
			node_set_color(parent, RBTREE_COLOR_RED);
			rotate(rbtree, parent, direction);
			sibling = parent->child[!direction];
		}

		/*
		 * Two black nephews, recolor and push up.
		 *
		 *      ?P                    ?P
		 *     /  \                  /  \
		 *    N    S      -->       N    s     n := P
		 *        / \                   / \
		 *       C   D                 C   D
		 *
		 */
		if (node_is_null_or_black(sibling->child[RBTREE_DIRECTION_LEFT]) && node_is_null_or_black(sibling->child[RBTREE_DIRECTION_RIGHT])) {
			node_set_color(sibling, RBTREE_COLOR_RED);
			node = parent;
			parent = node_parent(node);
			continue;
		}

		/* 
		 * Red inner nephew (and black outer one), make the red nephew the outer one.
		 *
		 *      ?P                    ?P
		 *     /  \                  /  \
		 *    N    S      -->       N    C
		 *        / \                   / \
		 *       c   D                 a   s
		 *      / \                       / \
		 *     a   b                     b   D
		 */
		if (node_is_null_or_black(sibling->child[!direction])) {
			node_set_color(sibling->child[direction], RBTREE_COLOR_BLACK);
			node_set_color(sibling, RBTREE_COLOR_RED);
			rotate(rbtree, sibling, !direction);
			sibling = parent->child[!direction];
		}

		/* 
		 * Red outer nephew, rotate.
		 *
		 *      ?P                    ?S
		 *     /  \                  /  \
		 *    N    S      -->       P    D
		 *        / \              / \
		 *       c   d            N   c
		 */
		node_set_color(sibling, node_color(parent));
		node_set_color(parent, RBTREE_COLOR_BLACK);
		node_set_color(sibling->child[!direction], RBTREE_COLOR_BLACK);
		rotate(rbtree, parent, direction);
		node = rbtree->root;
		break;
	}

	if (node)
		node_set_color(node, RBTREE_COLOR_BLACK);
}

void rbtree_remove(struct rbtree* rbtree, struct rbtree_node* node) {
	struct rbtree_node* const removed = node;
	struct rbtree_node* child, *parent;
	int color;
	if (node->left && node->right) {
		struct rbtree_node* victim = node;
		struct rbtree_node* left;

		node = node->right;
		while ((left = node->left) != NULL)
			node = left;

		parent = node_parent(victim);
		if (parent)
			parent->child[victim == parent->child[RBTREE_DIRECTION_RIGHT]] = node;
		else
			rbtree->root = node;

		child = node->right;
		parent = node_parent(node);
		color = node_color(node);

		if (parent == victim) {
			 /* 
			  * S is V's direct right child, so c does not move.
			  *
			  *      ?p                ?p
			  *      |                 |
			  *     ?V                ?S
			  *     / \      -->      / \
			  *    L   ?S            L   c
			  *         \
			  *          c
			  */
			parent = node;
		} else {
			 /*
			  * S sits further down R's left spine. c is spliced into S's old slot, then S adopts R.
			  *
			  *      ?V                ?S
			  *     /  \              /  \
			  *    L    R    -->     L    R
			  *        /                 /
			  *      ...               ...
			  *      /                 /
			  *     F                 F
			  *    /                 /
			  *  ?S                 c
			  *    \
			  *     c
			  */
			if (child)
				node_set_parent(child, parent);
			parent->left = child;
			node->right = victim->right;
			node_set_parent(victim->right, node);
		}

		node->_parent_color = victim->_parent_color;
		node->left = victim->left;
		node_set_parent(victim->left, node);
	} else {
		/*
		 * At most one child, N collapses into it.
		 *
		 *      ?p               ?p
		 *      |                |
		 *     ?N       -->      c        c may be NULL
		 *     /
		 *    c
		 */
		child = node->left ? node->left : node->right;
		parent = node_parent(node);
		color = node_color(node);
		if (child)
			node_set_parent(child, parent);
		if (parent)
			parent->child[node == parent->child[RBTREE_DIRECTION_RIGHT]] = child;
		else
			rbtree->root = child;
	}

	if (color == RBTREE_COLOR_BLACK)
		rbtree_remove_fixup(rbtree, child, parent);

	rbtree_node_init(removed);
}

static inline struct rbtree_node* descend(struct rbtree* rbtree, int direction) {
	struct rbtree_node* node = rbtree->root;
	if (node) {
		while (node->child[direction])
			node = node->child[direction];
	}
	return node;
}

struct rbtree_node* rbtree_first_node(struct rbtree* rbtree) {
	return descend(rbtree, RBTREE_DIRECTION_LEFT);
}

struct rbtree_node* rbtree_last_node(struct rbtree* rbtree) {
	return descend(rbtree, RBTREE_DIRECTION_RIGHT);
}

static struct rbtree_node* step(struct rbtree_node* node, int direction) { 
	if (RBTREE_IS_NODE_EMPTY(node))
		return NULL;

	if (node->child[direction]) {
		node = node->child[direction];
		while (node->child[!direction])
			node = node->child[!direction];

		return node;
	}

	struct rbtree_node* parent;
	while ((parent = node_parent(node)) != NULL && node == parent->child[direction])
		node = parent;

	return parent;
}
 
struct rbtree_node* rbtree_next_node(struct rbtree_node* node) {
	return step(node, RBTREE_DIRECTION_RIGHT);
}
 
struct rbtree_node* rbtree_prev_node(struct rbtree_node* node) {
	return step(node, RBTREE_DIRECTION_LEFT);
}

static struct rbtree_node* deepest_node(struct rbtree_node* node) {
	while (1) {
		if (node->left)
			node = node->left;
		else if (node->right)
			node = node->right;
		else
			return node;
	}
}

struct rbtree_node* rbtree_first_postorder_node(struct rbtree* rbtree) {
	if (!rbtree->root)
		return NULL;
	return deepest_node(rbtree->root);
}

struct rbtree_node* rbtree_next_postorder_node(struct rbtree_node* node) {
	if (!node)
		return NULL;

	struct rbtree_node* parent = node_parent(node);
	if (parent && node == parent->left && parent->right)
		return deepest_node(parent->right);

	return parent;
}
