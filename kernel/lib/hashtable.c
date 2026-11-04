#include <crater/common.h>
#include <crater/hashtable.h>
#include <crater/slab.h>
#include <crater/string.h>

#define FNV1A64_PRIME 0x00000100000001b3ull
#define FNV1A64_OFFSET 0xcbf29ce484222325ull

static inline u64 fnv1a64_hash(const void* bytes, size_t size) {
	const u8* b = bytes;

	u64 hash = FNV1A64_OFFSET;
	while (size--) {
		hash ^= *b++;
		hash *= FNV1A64_PRIME;
	}
	return hash;
}

struct hashtable* hashtable_create(unsigned int bucket_count, size_t value_size) {
	if (bucket_count == 0 || value_size == 0)
		return NULL;
	struct hashtable* table = kmalloc(sizeof(*table), MM_ZONE_NORMAL);
	if (!table)
		return NULL;

	table->buckets = kmalloc(bucket_count * sizeof(*table->buckets), MM_ZONE_NORMAL);
	if (!table->buckets) {
		kfree(table);
		return NULL;
	}

	for (unsigned int i = 0; i < bucket_count; i++)
		list_head_init(&table->buckets[i]);

	table->bucket_count = bucket_count;
	table->size = 0;
	table->value_size = value_size;
	mutex_init(&table->lock);

	return table;
}

static struct hashtable_node* 
hashtable_node_create(const void* key, const void* value, size_t key_size, size_t value_size) {
	struct hashtable_node* node = kmalloc(sizeof(struct hashtable_node), MM_ZONE_NORMAL);
	if (!node)
		return NULL;

	node->key = kmalloc(key_size, MM_ZONE_NORMAL);
	if (!node->key) {
		kfree(node);
		return NULL;
	}
	node->key_size = key_size;

	node->value = kmalloc(value_size, MM_ZONE_NORMAL);
	if (!node->value) {
		kfree(node->key);
		kfree(node);
		return NULL;
	}

	memcpy(node->key, key, key_size);
	memcpy(node->value, value, value_size);
	list_node_init(&node->link);

	return node;
}

int hashtable_insert(struct hashtable* table, const void* key, size_t key_size, const void* value) {
	u64 hash = fnv1a64_hash(key, key_size);
	int ret = 0;

	mutex_acquire(&table->lock);

	size_t index = hash % table->bucket_count;
	struct list_head* bucket = &table->buckets[index];

	/* Check if the key is already in the table */
	struct list_node* pos;
	list_for_each(pos, bucket) {
		struct hashtable_node* node = list_entry(pos, struct hashtable_node, link);
		if (key_size != node->key_size)
			continue;
		if (memcmp(node->key, key, key_size) == 0) {
			memcpy(node->value, value, table->value_size);
			goto out;
		}
	}

	/* Since there is no matching key, another node needs to be created */
	struct hashtable_node* new = hashtable_node_create(key, value, key_size, table->value_size);
	if (!new) {
		ret = -ENOMEM;
		goto out;
	}

	list_add(bucket, &new->link);
	table->size++;
out:
	mutex_release(&table->lock);
	return ret;
}

int hashtable_search(struct hashtable* table, const void* key, size_t key_size, void* value) {
	u64 hash = fnv1a64_hash(key, key_size);
	int ret = 0;

	mutex_acquire(&table->lock);

	size_t index = hash % table->bucket_count;
	struct list_head* bucket = &table->buckets[index];

	struct list_node* pos;
	list_for_each(pos, bucket) {
		struct hashtable_node* node = list_entry(pos, struct hashtable_node, link);
		if (key_size != node->key_size)
			continue;
		if (memcmp(node->key, key, key_size) == 0) {
			memcpy(value, node->value, table->value_size);
			goto out;
		}
	}

	ret = -ENOENT;
out:
	mutex_release(&table->lock);
	return ret;
}

void hashtable_for_each_node_remove(struct hashtable* table, struct hashtable_node* node) {
	kfree(node->key);
	kfree(node->value);
	list_remove(&node->link);
	kfree(node);
	table->size--;
}

int hashtable_remove(struct hashtable* table, const void* key, size_t key_size) {
	u64 hash = fnv1a64_hash(key, key_size);
	int ret = 0;

	mutex_acquire(&table->lock);

	size_t index = hash % table->bucket_count;
	struct list_head* bucket = &table->buckets[index];

	struct list_node* pos, *tmp;
	list_for_each_safe(pos, tmp, bucket) {
		struct hashtable_node* node = list_entry(pos, struct hashtable_node, link);
		if (key_size != node->key_size)
			continue;
		if (memcmp(node->key, key, key_size) == 0) {
			hashtable_for_each_node_remove(table, node);
			goto out;
		}
	}

	ret = -ENOENT;
out:
	mutex_release(&table->lock);
	return ret;
}

void hashtable_destroy(struct hashtable* table) {
	for (size_t i = 0; i < table->bucket_count; i++) {
		struct list_head* bucket = &table->buckets[i];
		struct hashtable_node* pos, *tmp;
		list_for_each_entry_safe(pos, tmp, bucket, link) {
			kfree(pos->key);
			kfree(pos->value);
			list_remove(&pos->link);
			kfree(pos);
		}
	}

	kfree(table->buckets);
	kfree(table);
}

void hashtable_for_each_entry_safe(struct hashtable* table,
		foreach_iteration_desicion_t (*visit)(struct hashtable*, struct hashtable_node*, void*), void* ctx) {
	mutex_acquire(&table->lock);

	for (unsigned int b = 0; b < table->bucket_count; b++) {
		struct hashtable_node* n, *tmp;
		list_for_each_entry_safe(n, tmp, &table->buckets[b], link) {
			foreach_iteration_desicion_t desicion = visit(table, n, ctx);
			if (desicion == FOREACH_ITERATION_DESICION_BREAK) {
				mutex_release(&table->lock);
				return;
			}
		}
	}

	mutex_release(&table->lock);
}

bool hashtable_empty(struct hashtable* table) {
	mutex_acquire(&table->lock);
	bool ret = (table->size == 0);
	mutex_release(&table->lock);
	return ret;
}
