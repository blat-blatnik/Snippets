#include <stdlib.h> // malloc, free
#include <string.h> // memcmp, memcpy, memset
#include <stdint.h> // uintptr_t

struct slab {
	struct slab *prev;
	int capacity;
	int cursor;
	// Memory comes right after here.
};

struct header { // [header][keyvals][metadata]
	int(*equal)(void *context, const void *key_a, const void *key_b, int key_size);
	void(*copy)(void *context, void *destination, const void *keyval, int keyval_size, struct slab **slab);
	unsigned long long(*hash)(void *context, const void *key, int key_size);
	void *equal_context;
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
	private__reserve((ptable),(min_capacity),sizeof*(*(ptable)),sizeof(*(ptable))->key)

#define get_header(ptable)\
	((!*(ptable)?(reserve((ptable),64),0):0),(struct header*)(*(ptable))-1)

#define add(ptable, new_key, new_value)do{\
	reserve((ptable), 1 + count(*(ptable)));\
	(*(ptable))[capacity(*(ptable))].key = (new_key);\
	(*(ptable))[capacity(*(ptable))].val = (new_value);\
	private__add((void **)(ptable), *(ptable) + capacity(*(ptable)), sizeof*(*(ptable)), sizeof(*(ptable))->key);\
}while(0)

#define get(table, target_key)(\
	count(table)?\
		((table)[capacity(table)].key=(target_key), private__get((table),&(table)[capacity(table)].key, sizeof*(table), sizeof(table)->key))\
		:-1)

#define get_value(table, key)\
	((table)[get((table),(key))].val)

#define contains(table, key)\
	(get((table),(key))>=0)

#define remove(ptable, existing_key)do{\
	(*(ptable))[capacity(*(ptable))].key = (existing_key);\
	private__remove((void **)(ptable), *(ptable) + capacity(*(ptable)), sizeof*(*(ptable)), sizeof(*(ptable))->key);\
}while(0)

void *allocate(struct slab **slab, int size, int alignment) {
	uintptr_t mask = (uintptr_t)alignment - 1; // Alignment must be a power of 2.
	for (;;) {
		int capacity = 128;
		if (*slab) {
			capacity = (*slab)->capacity;
			uintptr_t unaligned = (uintptr_t)(*slab + 1) + (*slab)->cursor;
			uintptr_t aligned = (unaligned + mask) & ~mask;
			int needed_size = size + (int)(aligned - unaligned);
			if (needed_size <= capacity - (*slab)->cursor)
			{
				(*slab)->cursor += needed_size;
				return (void *)aligned;
			}
		}

		int new_capacity = 2 * capacity;
		while (new_capacity < size + alignment - 1)
			new_capacity *= 2;

		struct slab *new_slab = malloc(sizeof new_slab[0] + new_capacity);
		new_slab->capacity = new_capacity;
		new_slab->cursor = 0;
		new_slab->prev = *slab;
		*slab = new_slab;
	}
}

void freeall(struct slab **slab) {
	while (*slab) {
		struct slab *prev = (*slab)->prev;
		free(*slab);
		*slab = prev;
	}
}

int count(const table(void) table) {
	return table ? ((struct header *)table)[-1].count : 0;
}

int capacity(const table(void) table) {
	return table ? ((struct header *)table)[-1].capacity : 0;
}

void destroy(table(void) *ptable) {
	if (*ptable) {
		struct header *header = ((struct header *)*ptable) - 1;
		freeall(&header->slab);
		free(header);
		*ptable = NULL;
	}
}

int first_index(const table(void) table) {
	int cap = capacity(table);
	struct header *header = (struct header *)table - 1;
	for (int i = 0; i < cap; ++i)
		if (header->metadata[i] && header->metadata[i] != TOMBSTONE)
			return i;
	return -1;
}

int next_index(const table(void) table, int index) {
	int cap = capacity(table);
	struct header *header = (struct header *)table - 1;
	for (int i = index + 1; i < cap; ++i)
		if (header->metadata[i] && header->metadata[i] != TOMBSTONE)
			return i;
	return -1;
}

int default_compare(void *context, const void *key_a, const void *key_b, int key_size) {
	(void)context;
	return memcmp(key_a, key_b, (size_t)key_size) == 0;
}

void default_copy(void *context, void *destination, const void *keyval, int keyval_size, struct slab **slab) {
	(void)context; (void)slab;
	memcpy(destination, keyval, (size_t)keyval_size);
}

unsigned long long default_hash(void *context, const void *key, int key_size) {
	(void)context;
	// FNV-1a https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1a_hash
	const unsigned char *bytes = key;
	unsigned long long hash = 14695981039346656037u;
	for (int i = 0; i < key_size; ++i)
		hash = (hash ^ bytes[i]) * 1099511628211u;
	return hash;
}

void private__reserve(table(void) *ptable, int min_capacity, int keyval_size, int key_size) {
	int cap = capacity(*ptable);
	if (4 * min_capacity > 3 * cap) {
		int capacity_for_load_factor = (4 * min_capacity) / 3;
		int new_capacity = 64;
		while (new_capacity < capacity_for_load_factor)
			new_capacity *= 2;
		int num_keyvals = new_capacity + 1; // We need 1 keyval for scratch space.

		void *new_memory = malloc(sizeof(struct header) + num_keyvals * (keyval_size + sizeof(unsigned char)));
		struct header *new_header = new_memory;
		char *new_keyvals = (char *)(new_header + 1);
		unsigned char *new_metadata = (unsigned char *)(new_keyvals + num_keyvals * keyval_size);
		memset(new_metadata, 0, (size_t)num_keyvals);

		new_header->count = count(*ptable);
		if (*ptable)
			*new_header = ((struct header *)(*ptable))[-1];
		else {
			new_header->hash = default_hash;
			new_header->copy = default_copy;
			new_header->equal = default_compare;
			new_header->hash_context = NULL;
			new_header->copy_context = NULL;
			new_header->equal_context = NULL;
		}
		new_header->slab = NULL;
		new_header->metadata = new_metadata;
		new_header->capacity = new_capacity;

		struct header *old_header = NULL;
		unsigned char *old_metadata = NULL;
		char *old_keyvals = *ptable;
		if (*ptable) {
			old_header = ((struct header *)(*ptable)) - 1;
			old_metadata = old_header->metadata;
		}

		unsigned mask = (unsigned)new_capacity - 1;
		for (int i = 0; i < cap; ++i) {
			if (old_metadata[i] && old_metadata[i] != TOMBSTONE) {
				unsigned long long hash = new_header->hash(new_header->hash_context, old_keyvals + i * keyval_size, key_size);
				unsigned char metadata = hash & 0xFF;
				metadata += (metadata == 0);
				metadata -= (metadata == TOMBSTONE);
				for (unsigned j = (unsigned)hash & mask;; j = (j + 1) & mask) {
					if (!new_metadata[j]) {
						new_metadata[j] = metadata;
						new_header->copy(new_header->copy_context, new_keyvals + j * keyval_size, old_keyvals + i * keyval_size, keyval_size, &new_header->slab);
						break;
					}
				}
			}
		}

		if (old_header) {
			freeall(&old_header->slab);
			free(old_header);
		}
		*ptable = new_header + 1;
	}
}

void private__add(table(void) *ptable, const void *keyval, int keyval_size, int key_size) {
	struct header *header = (struct header *)(*ptable) - 1;
	char *keyvals = *ptable;
	unsigned long long hash = header->hash(header->hash_context, keyval, key_size);
	unsigned mask = (unsigned)header->capacity - 1;
	for (unsigned i = (unsigned)hash & mask;; i = (i + 1) & mask) {
		if (!header->metadata[i] || header->metadata[i] == TOMBSTONE) {
			unsigned char metadata = hash & 0xFF;
			metadata += (metadata == 0);
			metadata -= (metadata == TOMBSTONE);
			header->metadata[i] = metadata;
			header->copy(header->copy_context, keyvals + i * keyval_size, keyval, keyval_size, &header->slab);
			header->count++;
			return;
		}
	}
}

int private__get(const table(void) table, const void *key, int keyval_size, int key_size) {
	struct header *header = (struct header *)table - 1;
	unsigned long long hash = header->hash(header->hash_context, key, key_size);
	unsigned metadata = hash & 0xFF;
	metadata += (metadata == 0);
	metadata -= (metadata == TOMBSTONE);
	unsigned mask = (unsigned)header->capacity - 1;
	for (unsigned i = (unsigned)hash & mask; header->metadata[i]; i = (i + 1) & mask)
		if (header->metadata[i] == metadata && header->equal(header->equal_context, key, (char *)table + i * keyval_size, key_size))
			return i;
	return -1;
}

void private__remove(table(void) *ptable, const void *key, int keyval_size, int key_size) {
	int cap = capacity(*ptable);
	if (!cap)
		return; // Tried to remove from an empty table.
	
	struct header *header = ((struct header *)*ptable) - 1;
	unsigned long long hash = header->hash(header->hash_context, key, key_size);
	unsigned char metadata = hash & 0xFF;
	metadata += (metadata == 0);
	metadata -= (metadata == TOMBSTONE);
	unsigned mask = (unsigned)header->capacity - 1;
	for (unsigned i = (unsigned)hash & mask; header->metadata[i]; i = (i + 1) & mask) {
		if (header->metadata[i] == metadata && header->equal(header->equal_context, key, ((char *)*ptable) + i * keyval_size, key_size)) {
			header->metadata[i] = TOMBSTONE;
			header->count--;
			return;
		}
	}
	// Tried to remove non-existent element.
}

#include <assert.h>
uint64_t hash_string(void *context, const void *key, int key_size) {
	(void)context; (void)key_size;
	const char *string = key;
	unsigned long long hash = 14695981039346656037u;
	for (int i = 0; string[i]; ++i)
		hash = (hash ^ string[i]) * 1099511628211u;
	return hash;
}
int equal_strings(void *context, const void *key_a, const void *key_b, int key_size) {
	(void)context; (void)key_size;
	return strcmp(*(const char **)key_a, *(const char **)key_b) == 0;
}
void copy_strings(void *context, void *destination, const void *keyval, int keyval_size, struct slab **slab) {
	(void)context; (void)keyval_size;
	const char **src = keyval;
	char **dst = destination;
	int key_size = 1 + (int)strlen(src[0]);
	int val_size = 1 + (int)strlen(src[1]);
	dst[0] = allocate(slab, key_size, 1);
	dst[1] = allocate(slab, val_size, 1);
	memcpy(dst[0], src[0], key_size);
	memcpy(dst[1], src[1], val_size);
}
int main(void) {
	struct int_int { int key; int val; };
	struct str_str { char *key; char *val; };

	{
		table(struct int_int) table = NULL;
		assert(!count(table));
		assert(!capacity(table));
		assert(get(table, 0) == -1);
		assert(!contains(table, 1));
		destroy(&table);
	}
	
	{
		table(struct int_int) table = NULL;
		for (int i = 0; i < 16; ++i)
			add(&table, i, i);
	
		assert(count(table) == 16);
		for (int i = 0; i < 16; ++i)
			assert(contains(table, i));
	
		int total[16] = { 0 };
		for (int i = first_index(table); i >= 0; i = next_index(table, i))
			total[table[i].key]++;
		for (int i = 0; i < 16; ++i)
			assert(total[i] == 1);
	
		destroy(&table);
	}

	{
		static int total_keys[1048576];
		static int total_vals[1048576];
		int n = sizeof total_keys / sizeof total_keys[0];

		table(struct int_int) table = NULL;
		for (int i = 0; i < n; ++i)
			add(&table, i, i);
		for (int i = 0; i < n; ++i)
			assert(contains(table, i));

		assert(count(table) == n);
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			total_keys[table[i].key]++;
			total_vals[table[i].val]++;
			assert(table[i].key == table[i].val);
		}
		for (int i = 0; i < n; ++i)
			assert(total_keys[i] == 1 && total_vals[i] == 1);

		for (int i = 0; i < n / 2; ++i)
			remove(&table, i);
		assert(count(table) == n / 2);
		for (int i = 0; i < n / 2; ++i)
			assert(!contains(table, i));
		for (int i = n / 2; i < n; ++i)
			assert(contains(table, i));

		memset(total_keys, 0, sizeof total_keys);
		memset(total_vals, 0, sizeof total_vals);
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			total_keys[table[i].key]++;
			total_vals[table[i].val]++;
			assert(table[i].key == table[i].val);
		}
		for (int i = 0; i < n / 2; ++i)
			assert(total_keys[i] == 0 && total_vals[i] == 0);
		for (int i = n / 2; i < n; ++i)
			assert(total_keys[i] == 1 && total_vals[i] == 1);

		destroy(&table);
	}

	{
		table(struct str_str) table = NULL;
		struct header *header = get_header(&table);
		header->hash = hash_string;
		header->equal = equal_strings;
		header->copy = copy_strings;
		add(&table, "Key0", "Val0");
		assert(contains(table, "Key0"));
		add(&table, "Key1", "Val1");
		add(&table, "Key2", "Val2");
		add(&table, "Key3", "Val3");
		assert(strcmp(table[get(table, "Key0")].val, "Val0") == 0);
		assert(strcmp(table[get(table, "Key1")].val, "Val1") == 0);
		assert(strcmp(table[get(table, "Key2")].val, "Val2") == 0);
		assert(strcmp(table[get(table, "Key3")].val, "Val3") == 0);

		int total[4] = { 0 };
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			char *val = table[i].val;
			total[val[3] - '0']++;
		}
		assert(total[0] == 1 && total[1] == 1 && total[2] == 1 && total[3] == 1);

		destroy(&table);
		assert(!capacity(table) && !count(table) && !table);
	}

	{
		static char keys[1048576][9];
		static char vals[1048576][9];
		int n = sizeof keys / sizeof keys[0];
		for (int i = 0; i < n; ++i) {
			keys[i][0] = 'k';
			vals[i][0] = 'v';
			int x = i;
			for (int j = 0; j < 7; ++j) {
				keys[i][7 - j] = '0' + x % 10;
				vals[i][7 - j] = '0' + x % 10;
				x /= 10;
			}
			keys[i][8] = 0;
			vals[i][8] = 0;
		}

		table(struct str_str) table = NULL;
		for (int i = 0; i < n; ++i)
			add(&table, keys[i], vals[i]);
		assert(count(table) == n);

		static int total[sizeof keys / sizeof keys[0]] = {0};
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			char *key = table[i].key;
			char *val = table[i].val;
			assert(key[0] == 'k' && val[0] == 'v');
			++key;
			++val;
			assert(strcmp(key, val) == 0);
			int x = 0;
			for (int j = 0; j < 7; ++j) {
				x *= 10;
				x += key[j] - '0';
			}
			total[x]++;
		}
		for (int i = 0; i < n; ++i)
			assert(total[i] == 1);

		for (int i = 0; i < n / 2; ++i)
			remove(&table, keys[i]);
		assert(count(table) == n / 2);
		memset(total, 0, sizeof total);
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			char *key = table[i].key;
			char *val = table[i].val;
			assert(key[0] == 'k' && val[0] == 'v');
			++key;
			++val;
			assert(strcmp(key, val) == 0);
			int x = 0;
			for (int j = 0; j < 7; ++j) {
				x *= 10;
				x += key[j] - '0';
			}
			total[x]++;
		}
		for (int i = 0; i < n / 2; ++i)
			assert(total[i] == 0);
		for (int i = n / 2; i < n; ++i)
			assert(total[i] == 1);

		for (int i = 0; i < n / 2; ++i)
			add(&table, keys[i], vals[i]);
		assert(count(table) == n);

		memset(total, 0, sizeof total);
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			char *key = table[i].key;
			char *val = table[i].val;
			assert(key[0] == 'k' && val[0] == 'v');
			++key;
			++val;
			assert(strcmp(key, val) == 0);
			int x = 0;
			for (int j = 0; j < 7; ++j) {
				x *= 10;
				x += key[j] - '0';
			}
			total[x]++;
		}
		for (int i = 0; i < n; ++i)
			assert(total[i] == 1);

		destroy(&table);
	}
}