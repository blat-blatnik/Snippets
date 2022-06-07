#include <stdlib.h> // malloc, free
#include <string.h> // memcmp, memcpy, memset

struct slab {
	struct slab *prev;
	int capacity;
	int cursor;
	// Memory comes right after here.
};

struct header { // [header][keyvals][metadata]
	int(*compare)(void *context, const void *key_a, const void *key_b, int key_size);
	void(*copy)(void *context, void *destination, const void *keyval, int keyval_size, struct slab **slab);
	unsigned long long(*hash)(void *context, const void *key, int size);
	void *compare_context;
	void *copy_context;
	void *hash_context;
	struct slab *slab;
	unsigned char *metadata;
	int count;
	int capacity;
};

#define TOMBSTONE 0xFF

#define table(KV) KV*

#define reserve(ptable, min_capacity)\
	private__reserve((ptable),(min_capacity),sizeof*(*(ptable)))

#define get_header(ptable)\
	((!*(ptable)?(reserve((ptable),64),0):0),(struct header*)(*(ptable))-1)

#define add(ptable, new_key, new_value)do{\
	reserve((ptable), 1 + count(*(ptable)));\
	(*(ptable))[capacity(*(ptable))]->key = (new_key);\
	(*(ptable))[capacity(*(ptable))]->val = (new_value);\
	private__add(*(ptable), *(ptable) + capacity(*(ptable)), sizeof *(*(ptable)));\
}while(0)

#define get(table, key)\
	private__get((table),)

#define get_value(table, key)\
	((table)[get((table),(key))]->val)

#define contains(table, key)\
	(get((table),(key))>=0)

#define remove(ptable, key)

int default_compare(void *context, const void *key_a, const void *key_b, int key_size) {
	(void)context;
	return memcmp(key_a, key_b, (size_t)key_size) == 0;
}

void default_copy(void *context, void *destination, const void *keyval, int keyval_size, struct slab **slab) {
	(void)context; (void)slab;
	memcpy(destination, keyval, (size_t)keyval_size);
}

unsigned long long default_hash(void *context, const void *key, int size) {
	(void)context;
	const unsigned char *bytes = key;
	unsigned long long hash = 14695981039346656037u;
	for (int i = 0; i < size; ++i)
		hash = (hash ^ bytes[i]) * 1099511628211u;
	return hash;
}

int count(const table(void) table) {
	return table ? ((struct header *)table)[-1].count : 0;
}

int capacity(const table(void) table) {
	return table ? ((struct header *)table)[-1].capacity : 0;
}

void destroy(table(void) *ptable);

void private__reserve(table(void) *ptable, int min_capacity, int keyval_size) {
	int cap = capacity(*ptable);
	if (3 * min_capacity > 2 * cap) {
		int capacity_for_load_factor = (3 * min_capacity) / 2;
		int pow2;
		for (pow2 = 0; (1 << pow2) < capacity_for_load_factor; ++pow2);
		int new_capacity = 1 << pow2;
		int num_keyvals = new_capacity + 1; // We need 1 keyval for scratch space.

		void *new_memory = malloc(sizeof(struct header) + num_keyvals * (keyval_size + sizeof(unsigned char)));
		struct header *new_header = new_memory;
		char *new_keyvals = (char *)(new_header + 1);
		unsigned char *new_metadata = (unsigned char *)(new_keyvals + num_keyvals * keyval_size);
		memset(new_metadata, 0, (size_t)num_keyvals);

		new_header->capacity = new_capacity;
		new_header->count = count(*ptable);
		if (*ptable)
			*new_header = ((struct header *)(*ptable))[-1];
		else {
			new_header->hash = default_hash;
			new_header->copy = default_copy;
			new_header->compare = default_compare;
			new_header->hash_context = NULL;
			new_header->copy_context = NULL;
			new_header->compare_context = NULL;
		}
		new_header->slab = NULL;
		new_header->metadata = new_metadata;

		struct header old_header;
		unsigned char *old_metadata = NULL;
		char *old_keyvals = *ptable;
		if (*ptable) {
			old_header = ((struct header *)(*ptable))[-1];
			old_metadata = old_header.metadata;
		}

		unsigned mask = (unsigned)new_capacity - 1;
		for (int i = 0; i < cap; ++i) {
			if (old_metadata[i] && old_metadata[i] != TOMBSTONE) {
				unsigned long long hash = old_header.hash(old_header.hash_context, old_keyvals + i * keyval_size, keyval_size);
				unsigned metadata = hash & 0xFF;
				metadata += (metadata == 0);
				metadata -= (metadata == TOMBSTONE);
				for (unsigned j = (unsigned)hash & mask;; j = (j + 1) & mask) {
					if (!new_metadata[j]) {
						new_metadata[j] = metadata;
						old_header.copy(old_header.copy_context, new_keyvals + j * keyval_size, old_keyvals + i * keyv)
						break;
					}
				}
			}
		}
	}
}

void private__add(table(void) *ptable, const void *keyval, int keyval_size) {
	struct header *header = (struct header *)(*ptable) - 1;
}

int private__get(const table(void) table, const void *key, int key_size) {

}

#include <assert.h>
int main(void) {

}