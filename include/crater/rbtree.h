#pragma once

#include <crater/types.h>
#include <crater/list.h>

#define RBTREE_COLOR_RED 0
#define RBTREE_COLOR_BLACK 1
#define RBTREE_DIRECTION_LEFT 0
#define RBTREE_DIRECTION_RIGHT 1

struct rbtree_node {
	uintptr_t _parent_color; /* First bit holds the color of this node, all other bits are for the parent pointer */
	union {
		struct rbtree_node* child[2];
		struct {
			struct rbtree_node* left, *right;
		};
	};
} __attribute__((aligned(sizeof(uintptr_t)))); /* Must be aligned by at least 2, since _parent_color uses one bit for the color */

struct rbtree {
	struct rbtree_node* root;
};

#define RBTREE_INITIALIZER { .root = NULL }
#define RBTREE_DEFINE(n) struct rbtree n = RBTREE_INITIALIZER
#define RBTREE_IS_EMPTY(r) ((r)->root == NULL)
#define RBTREE_IS_NODE_EMPTY(n) ((n)->_parent_color == (uintptr_t)(n))

static inline void rbtree_init(struct rbtree* rbtree) {
	rbtree->root = NULL;
}

static inline void rbtree_node_init(struct rbtree_node* node) {
	node->_parent_color = (uintptr_t)node;
}

/**
 * @brief Insert a node into an rbtree as a red leaf
 *
 * After calling this function, rbtree_insert_fixup() must be called.
 *
 * @param node The node to insert, must not be in the tree
 * @param parent The node that owns the slot, or NULL if the tree is empty
 * @param link Address of the NULL child slot the descendent stopped on
 */
static inline void rbtree_insert(struct rbtree_node* node, struct rbtree_node* parent, struct rbtree_node** link) {
	node->_parent_color = (uintptr_t)parent | (RBTREE_COLOR_RED << 0);
	node->left = NULL;
	node->right = NULL;
	*link = node;
}

/**
 * @brief Restore the invariants after rbtree_insert()
 *
 * @param rbtree The tree the node was linked into
 * @param node The node passed into rbtree_insert()
 */
void rbtree_insert_fixup(struct rbtree* rbtree, struct rbtree_node* node);

/**
 * @brief Remove a node from the tree and rebalance
 *
 * @param rbtree The tree the node belongs to
 * @param node The node to remove
 */
void rbtree_remove(struct rbtree* rbtree, struct rbtree_node* node);

/**
 * @brief Get the left-most node in an rbtree
 * @param rbtree The tree
 * @return The first node in the tree
 */
struct rbtree_node* rbtree_first_node(struct rbtree* rbtree);

/**
 * @brief Get the right-most node in an rbtree
 * @param rbtree The tree
 * @return The last node in the tree
 */
struct rbtree_node* rbtree_last_node(struct rbtree* rbtree);

/**
 * @brief Get the next node for an rbtree node
 * @param node The node
 * @return The next node
 */
struct rbtree_node* rbtree_next_node(struct rbtree_node* node);

/**
 * @brief Get the previous node for an rbtree node
 * @param node The node
 * @return The previous node
 */
struct rbtree_node* rbtree_prev_node(struct rbtree_node* node);

/**
 * @brief Get the first node of a post-order traversal
 * @param rbtree The tree
 * @return The first node
 */
struct rbtree_node* rbtree_first_postorder_node(struct rbtree* rbtree);

/**
 * @brief Get the next node of a post order traversal
 * @param node The current node
 * @return The next node
 */
struct rbtree_node* rbtree_next_postorder_node(struct rbtree_node* node);

#define rbtree_entry(ptr, type, member) container_of(ptr, type, member)
#define rbtree_entry_safe(ptr, type, member) \
	({ \
		typeof(ptr) ____ptr = (ptr); \
		____ptr ? rbtree_entry(____ptr, type, member) : NULL; \
	})
#define rbtree_for_each(pos, root) for ((pos) = rbtree_first_node(root); (pos); (pos) = rbtree_next_node(pos))
#define rbtree_for_each_entry(pos, root, member) \
	for ((pos) = rbtree_entry_safe(rbtree_first_node(root), typeof(*(pos)), member); \
			(pos); \
			(pos) = rbtree_entry_safe(rbtree_next_node(&(pos)->member), typeof(*(pos)), member))
#define rbtree_postorder_for_each_entry_safe(pos, n, root, member) \
	for ((pos) = rbtree_entry_safe(rbtree_first_postorder_node(root), typeof(*(pos)), member); \
			(pos) && ((n) = rbtree_entry_safe(rbtree_next_postorder_node(&(pos)->member), typeof(*(pos)), member), 1); \
			(pos) = (n))
