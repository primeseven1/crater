#pragma once

#include <crater/list.h>
#include <crater/mutex.h>
#include <crater/foreach.h>

struct hashtable_node {
	void* key;
	size_t key_size;
	void* value;
	struct list_node link;
};

struct hashtable {
	struct list_head* buckets;
	unsigned int bucket_count;
	size_t size;
	size_t value_size;
	mutex_t lock;
};

/**
 * @brief Create a general-purpose hashtable
 *
 * The structure of the hashtable does not care about types, and in order to avoid
 * collisions, it uses separate chaining. Since this implementation does implement rehashing,
 * it's important that you balance the head count and memory usage.
 *
 * @param bucket_count The number of buckets, the higher the number, the less collisions
 * @param value_size The size of the values
 *
 * @return A pointer to the new hashtable, NULL if memory could not be allocated
 */
struct hashtable* hashtable_create(unsigned int bucket_count, size_t value_size);

/**
 * @brief Insert a value into a hash table
 *
 * If the key already exists in the hashtable, this function will replace
 * the value there.
 *
 * @param table The hashtable to use
 * @param key The key to use. This is copied over
 * @param value The value to use, this is also copied over
 *
 * @retval 0 Succesful
 * @retval -ENOMEM Cannot allocate memory for new value
 */
int hashtable_insert(struct hashtable* table, const void* key, size_t key_size, const void* value);

/**
 * @brief Search for a value in a hash table
 *
 * @param[in] table The table to search in
 * @param[in] key The key to use
 * @param[in] key_size The size of the key
 * @param[out] value The pointer to where the value will be copied
 *
 * @retval 0 Successful
 * @retval -ENOENT Key not found
 */
int hashtable_search(struct hashtable* table, const void* key, size_t key_size, void* value);

/**
 * @brief Remove a node from a hashtable
 *
 * This function is meant to be used in for-each loops, as the hashtable is locked
 * during a for each loop. After calling this function, the node is invalid.
 *
 * @param table The table to remove from
 * @param node The node to remove
 */
void hashtable_for_each_node_remove(struct hashtable* table, struct hashtable_node* node);

/**
 * @brief Remove a value from a hashtable
 *
 * @param table The table to remove the value from
 * @param key The key to use
 *
 * @retval 0 Successful
 * @retval -ENOENT Key not found
 */
int hashtable_remove(struct hashtable* table, const void* key, size_t key_size);

/**
 * @brief Destroy a hashtable
 *
 * This function will free everything that's related 
 * to the hash table
 *
 * @param table The table to destroy
 */
void hashtable_destroy(struct hashtable* table);

/**
 * @brief Loop through the entries in a hashtable
 *
 * You can safely use hashtable_for_each_node_remove() on the node
 *
 * @param table The table to iterate through
 * @param visit The callback for visiting a node
 * @param ctx User provided
 */
void hashtable_for_each_entry_safe(struct hashtable* table,
		foreach_iteration_desicion_t (*visit)(struct hashtable*, struct hashtable_node*, void*),
		void* ctx);

/**
 * @brief Loop through the entries in a hashtable
 *
 * If hashtable_for_each_node_remove() is used on the node, it will be considered a bug.
 *
 * @param table The table to iterate through
 * @param visit The callback for visiting a node
 * @param ctx User provided
 */
static inline void hashtable_for_each_entry(struct hashtable* table,
		foreach_iteration_desicion_t (*visit)(struct hashtable*, struct hashtable_node*, void*),
		void* ctx) {
	hashtable_for_each_entry_safe(table, visit, ctx);
}

/**
 * @brief Check if a hashtable is empty
 * @param table The table to check
 * @return true if the hashtable is empty, otherwise false
 */
bool hashtable_empty(struct hashtable* table);
