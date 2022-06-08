#include <stdlib.h> // malloc, free
#include <string.h> // strlen, strcmp, memcpy, memset

struct table {
	char **keys;
	char **vals;
	struct slab *slab;
	int count;
	int capacity;
	int num_tombstones;
};

struct slab {
	struct slab *prev;
	int cursor;
	int capacity;
	// Memory comes right after this.
};

#define TOMBSTONE 1

unsigned long long hash_string(const char *string) {
	unsigned long long hash = 14695981039346656037u;
	for (int i = 0; string[i]; ++i)
		hash = (hash ^ string[i]) * 1099511628211u;
	return hash;
}

char *copy_string(struct slab **slab, const char *string) {
	int size = 1 + (int)strlen(string);
	if ((*slab)->capacity - (*slab)->cursor < size) {
		int new_capacity = 1024;
		while (new_capacity < size)
			new_capacity *= 2;
		struct slab *new_slab = malloc(sizeof new_slab[0] + new_capacity);
		new_slab->capacity = new_capacity;
		new_slab->cursor = 0;
		new_slab->prev = *slab;
		*slab = new_slab;
	}
	char *copy = (char *)(*slab + 1) + (*slab)->cursor;
	(*slab)->cursor += size;
	memcpy(copy, string, (size_t)size);
	return copy;
}

void resize(struct table *table, int capacity) {
	if (capacity <= table->count)
		capacity = table->count + 1;
	
	int pow2;
	for (pow2 = 0; (1 << pow2) < capacity; ++pow2);
	capacity = 1 << pow2;

	int total_string_size = 0;
	for (struct slab *slab = table->slab; slab; slab = slab->prev)
		total_string_size += slab->cursor;

	int first_slab_capacity = 1024;
	while (first_slab_capacity < total_string_size)
		first_slab_capacity *= 2;

	void *new_memory = malloc(capacity * (sizeof table->keys[0] + sizeof table->vals[0]) + sizeof table->slab[0] + first_slab_capacity);
	char **new_keys = new_memory;
	char **new_vals = new_keys + capacity;
	memset(new_keys, 0, (size_t)capacity * sizeof new_keys[0]);
	struct slab *new_slab = (struct slab *)(new_vals + capacity);
	new_slab->prev = NULL;
	new_slab->capacity = first_slab_capacity;
	new_slab->cursor = 0;

	unsigned mask = capacity - 1;
	for (int i = 0; i < table->capacity; ++i) {
		if ((size_t)table->keys[i] > TOMBSTONE) {
			char *key = copy_string(&new_slab, table->keys[i]);
			char *val = copy_string(&new_slab, table->vals[i]);
			unsigned long long hash = hash_string(key);
			for (unsigned j = (unsigned)hash & mask;; j = (j + 1) & mask) {
				if (!new_keys[j]) {
					new_keys[j] = key;
					new_vals[j] = val;
					break;
				}
			}
		}
	}

	for (struct slab *slab = table->slab; slab && slab->prev;) {
		struct slab *prev = slab->prev;
		free(slab);
		slab = prev;
	}
	free(table->keys); // This also frees the values, metadata, and slab.
	table->keys = new_keys;
	table->vals = new_vals;
	table->slab = new_slab;
	table->capacity = capacity;
	table->num_tombstones = 0;
}

void reserve(struct table *table, int min_capacity) {
	if (2 * table->capacity < 3 * min_capacity) {
		int new_capacity = 2 * table->capacity;
		if (new_capacity < 64)
			new_capacity = 64;
		while (2 * new_capacity < 3 * min_capacity)
			new_capacity *= 2;
		resize(table, new_capacity);
	}
}

void add(struct table *table, const char *key, const char *val) {
	reserve(table, table->count + 1);
	unsigned long long hash = hash_string(key);
	unsigned mask = (unsigned)table->capacity - 1;
	unsigned index = (unsigned)-1;
	for (unsigned i = (unsigned)hash & mask;; i = (i + 1) & mask) {
		if (!table->keys[i]) {
			index = min(index, i);
			break;
		}
		if (table->keys[i] == (void *)TOMBSTONE)
			index = min(index, i);
		else if (strcmp(table->keys[i], key) == 0) {
			table->vals[i] = copy_string(&table->slab, val);
			return;
		}
	}
	table->count++;
	table->keys[index] = copy_string(&table->slab, key);
	table->vals[index] = copy_string(&table->slab, val);
}

void remove(struct table *table, const char *key) {
	if (!table->count)
		return;

	unsigned long long hash = hash_string(key);
	unsigned mask = (unsigned)table->capacity - 1;
	for (unsigned i = (unsigned)hash & mask; table->keys[i]; i = (i + 1) & mask) {
		if (table->keys[i] != (void *)TOMBSTONE && strcmp(table->keys[i], key) == 0) {
			table->keys[i] = (void *)TOMBSTONE;
			table->count--;
			table->num_tombstones++;
			if (8 * table->num_tombstones > table->capacity)
				resize(table, table->capacity); // Get rid of tombstones.
		}
	}
}

const char *get(struct table table, const char *key) {
	if (!table.count)
		return NULL;

	unsigned long long hash = hash_string(key);
	unsigned mask = (unsigned)table.capacity - 1;
	for (unsigned i = (unsigned)hash & mask; table.keys[i]; i = (i + 1) & mask)
		if (table.keys[i] != (void *)TOMBSTONE && strcmp(table.keys[i], key) == 0)
			return table.vals[i];

	return NULL;
}

int first_index(struct table table) {
	for (int i = 0; i < table.capacity; ++i)
		if ((size_t)table.keys[i] > TOMBSTONE)
			return i;
	return -1;
}

int next_index(struct table table, int index) {
	for (int i = index + 1; i < table.capacity; ++i)
		if ((size_t)table.keys[i] > TOMBSTONE)
			return i;
	return -1;
}

void destroy(struct table *table) {
	for (struct slab *slab = table->slab; slab && slab->prev;) {
		struct slab *prev = slab->prev;
		free(slab);
		slab = prev;
	}
	free(table->keys); // This also frees the values, metadata, and slab.
	memset(table, 0, sizeof table[0]);
}

#include <assert.h>
int main(void) {
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

	{
		struct table table = { 0 };
		assert(!get(table, ""));
		assert(first_index(table) < 0);
		destroy(&table);
	}

	{
		struct table table = { 0 };
		add(&table, "Key0", "Val0");
		add(&table, "Key1", "Val1");
		add(&table, "Key2", "Val2");
		add(&table, "Key3", "Val3");
		assert(strcmp(get(table, "Key0"), "Val0") == 0);
		assert(strcmp(get(table, "Key1"), "Val1") == 0);
		assert(strcmp(get(table, "Key2"), "Val2") == 0);
		assert(strcmp(get(table, "Key3"), "Val3") == 0);

		int remaining[4] = { 1, 1, 1, 1 };
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			char *val = table.vals[i];
			remaining[val[3] - '0']--;
		}
		assert(remaining[0] == 0 && remaining[1] == 0 && remaining[2] == 0 && remaining[3] == 0);

		destroy(&table);
		assert(!table.capacity && !table.count && !table.keys && !table.vals && !table.slab);
	}

	{
		struct table table = { 0 };
		for (int i = 0; i < n; ++i)
			add(&table, keys[i], vals[i]);
		assert(table.count == n);

		static int remaining[sizeof keys / sizeof keys[0]];
		for (int i = 0; i < n; ++i)
			remaining[i] = 1;
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			char *key = table.keys[i];
			char *val = table.vals[i];
			assert(key[0] == 'k' && val[0] == 'v');
			++key;
			++val;
			assert(strcmp(key, val) == 0);
			int x = 0;
			for (int j = 0; j < 7; ++j) {
				x *= 10;
				x += key[j] - '0';
			}
			remaining[x] -= 1;
		}
		for (int i = 0; i < n; ++i)
			assert(!remaining[i]);

		for (int i = 0; i < n / 2; ++i)
			remove(&table, keys[i]);
		assert(table.count == n / 2);
		for (int i = 0; i < n; ++i)
			remaining[i] = 1;
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			char *key = table.keys[i];
			char *val = table.vals[i];
			assert(key[0] == 'k' && val[0] == 'v');
			++key;
			++val;
			assert(strcmp(key, val) == 0);
			int x = 0;
			for (int j = 0; j < 7; ++j)
			{
				x *= 10;
				x += key[j] - '0';
			}
			remaining[x] -= 1;
		}
		for (int i = 0; i < n / 2; ++i)
			assert(remaining[i] == 1);
		for (int i = n / 2; i < n; ++i)
			assert(!remaining[i]);

		for (int i = 0; i < n / 2; ++i)
			add(&table, keys[i], vals[i]);
		assert(table.count == n);
		for (int i = 0; i < n; ++i)
			remaining[i] = 1;
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			char *key = table.keys[i];
			char *val = table.vals[i];
			assert(key[0] == 'k' && val[0] == 'v');
			++key;
			++val;
			assert(strcmp(key, val) == 0);
			int x = 0;
			for (int j = 0; j < 7; ++j) {
				x *= 10;
				x += key[j] - '0';
			}
			remaining[x] -= 1;
		}
		for (int i = 0; i < n; ++i)
			assert(!remaining[i]);

		destroy(&table);
	}

	{
		// Potential pathological case: create a bunch of items and then delete them 
		// to leave tombstones, then lookup each item. If we don't clean tombstones this is O(n^2).
		struct table table = { 0 };
		for (int i = 0; i < n - 1; ++i)
			add(&table, keys[i], vals[i]);
		//resize(&table, table.count + 1);
		for (int i = 1; i < n - 1; ++i)
			remove(&table, keys[i]);
		assert(table.count == 1);
		for (int i = 1; i < n - 1; ++i)
			assert(!get(table, keys[i]));
		destroy(&table);
	}

	{
		// This shouldn't leak.
		for (int i = 0; i < 10000; ++i) {
			struct table table = { 0 };
			for (int j = 0; j < 10000; ++j) {
				char keyval[5] = { 0 };
				int x = j;
				keyval[3] = x % 10; x /= 10;
				keyval[2] = x % 10; x /= 10;
				keyval[1] = x % 10; x /= 10;
				keyval[0] = x % 10; x /= 10;
				add(&table, keyval, keyval);
			}
			destroy(&table);
		}
	}
}