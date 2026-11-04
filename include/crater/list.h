#pragma once

#include <crater/types.h>
#include <crater/common.h>

struct list_node {
	struct list_node* prev, *next;
};
#define LIST_NODE_INITIALIZER { .prev = NULL, .next = NULL }

struct list_head {
	struct list_node node;
};

#define LIST_HEAD_DEFINE(n) struct list_head n = { .node.prev = &n.node, .node.next = &n.node }
#define LIST_HEAD_INITIALIZER(n) { .node.prev = &n.node, .node.next = &n.node }

static inline void list_head_init(struct list_head* head) {
	head->node.prev = &head->node;
	head->node.next = &head->node;
}

static inline void list_node_init(struct list_node* node) {
	node->prev = NULL;
	node->next = NULL;
}

static inline bool list_node_linked(struct list_node* node) {
	return !(node->prev == NULL && node->next == NULL);
}

static inline void __list_add(struct list_node* node, struct list_node* prev, struct list_node* next) {
	next->prev = node;
	node->next = next;
	node->prev = prev;
	prev->next = node;
}

static inline void list_add(struct list_head* head, struct list_node* node) {
	__list_add(node, &head->node, head->node.next);
}

static inline void list_add_tail(struct list_head* head, struct list_node* node) {
	__list_add(node, head->node.prev, &head->node);
}

static inline void list_add_before(struct list_node* pos, struct list_node* node) {
	__list_add(node, pos->prev, pos);
}

static inline void list_add_after(struct list_node* pos, struct list_node* node) {
	__list_add(node, pos, pos->next);
}

static inline void __list_remove(struct list_node* prev, struct list_node* next) {
	next->prev = prev;
	prev->next = next;
}

static inline void list_remove(struct list_node* node) {
	__list_remove(node->prev, node->next);
	node->prev = NULL;
	node->next = NULL;
}

static inline bool list_is_first(const struct list_head* head, const struct list_node* node) {
	return node->prev == &head->node;
}

static inline bool list_is_last(const struct list_head* head, const struct list_node* node) {
	return node->next == &head->node;
}

static inline bool list_empty(const struct list_head* head) {
	return head->node.next == &head->node;
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_for_each(pos, head) for ((pos) = (head)->node.next; (pos) != &(head)->node; (pos) = (pos)->next)
#define list_for_each_safe(pos, n, head) for ((pos) = (head)->node.next, (n) = (pos)->next; (pos) != &(head)->node; (pos) = (n), (n) = (pos)->next)
#define list_for_each_entry(pos, head, member) \
	for ((pos) = list_entry((head)->node.next, typeof(*(pos)), member); \
			&(pos)->member != &(head)->node; \
			(pos) = list_entry((pos)->member.next, typeof(*(pos)), member))
#define list_for_each_entry_safe(pos, n, head, member) \
	for ((pos) = list_entry((head)->node.next, typeof(*(pos)), member), \
			(n) = list_entry((pos)->member.next, typeof(*(pos)), member); \
			&(pos)->member != &(head)->node; \
			(pos) = (n), \
			(n) = list_entry((n)->member.next, typeof(*(pos)), member))
#define list_for_each_cont(pos, head) for (; (pos) != &(head)->node; (pos) = (pos)->next)
#define list_for_each_entry_cont(pos, head, member) \
	for (; &(pos)->member != &(head)->node; (pos) = list_entry((pos)->member.next, typeof(*(pos)), member))
#define list_first_entry(head, type, member) list_entry((head)->node.next, type, member)
#define list_next_entry(pos, member) list_entry((pos)->member.next, typeof(*(pos)), member)
#define list_prev_entry(pos, member) list_entry((pos)->member.prev, typeof(*(pos)), member)
