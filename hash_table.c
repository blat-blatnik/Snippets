#include <stdlib.h> // malloc, free

// For simplicity and efficiency, this table doesn't actually store the keys. 
// It only stores the key hashes. You'd better have a good hash function, because 
// if two keys happen to hash to the same value you're in big trouble. They will 
// overwrite each other. In practice, if you have a decent hash function the 
// likelyhood of this happening is really small with 64-bits hashes.
struct table {
	unsigned long long *hashes;
	unsigned long long *values;
	int capacity; // Always a power of 2 or 0.
	int count;
	int num_tombstones;
};

#define TOMBSTONE 1

void resize(struct table *table, int capacity) {
	if (capacity <= table->count)
		return;
	
	int pow2; // Round up capacity to a power of 2.
	for (pow2 = 1; (1 << pow2) < capacity; ++pow2);
	capacity = (1 << pow2);

	unsigned long long *new_memory = malloc((size_t)capacity * 2 * sizeof new_memory[0]);
	unsigned long long *new_hashes = new_memory;
	unsigned long long *new_values = new_hashes + capacity;
	for (int i = 0; i < capacity; ++i)
		new_hashes[i] = 0;

	unsigned mask = (unsigned)capacity - 1;
	for (int i = 0; i < table->capacity; ++i) {
		unsigned long long hash = table->hashes[i];
		if (hash > TOMBSTONE) {
			for (unsigned j = (unsigned)hash & mask;; j = (j + 1) & mask) {
				if (!new_hashes[j]) {
					new_hashes[j] = hash;
					new_values[j] = table->values[i];
					break;
				}
			}
		}
	}

	free(table->hashes); // This also frees the values.
	table->hashes = new_hashes;
	table->values = new_values;
	table->capacity = capacity;
	table->num_tombstones = 0;
}

void reserve(struct table *table, int min_capacity) {
	if (3 * table->capacity < 4 * min_capacity) {
		int capacity = 4 * min_capacity / 3;
		if (capacity < 64)
			capacity = 64;
		resize(table, capacity);
	}
}

void add(struct table *table, unsigned long long hash, unsigned long long value) {
	hash += (hash <= TOMBSTONE) ? 2 : 0;
	reserve(table, table->count + 1);
	unsigned mask = (unsigned)table->capacity - 1;
	unsigned index = (unsigned)-1;
	for (unsigned i = (unsigned)hash & mask;; i = (i + 1) & mask) {
		if (table->hashes[i] == hash) {
			table->values[i] = value;
			return;
		}
		if (!table->hashes[i]) {
			index = min(index, i);
			break;
		}
		if (table->hashes[i] == TOMBSTONE)
			index = min(index, i);
	}

	if (table->hashes[index] == TOMBSTONE)
		table->num_tombstones--;
	table->hashes[index] = hash;
	table->values[index] = value;
	table->count++;
}

void remove(struct table *table, unsigned long long hash) {
	if (!table->count)
		return;

	hash += (hash <= TOMBSTONE) ? 2 : 0;
	unsigned mask = (unsigned)table->capacity - 1;
	for (unsigned i = (unsigned)hash & mask; table->hashes[i]; i = (i + 1) & mask) {
		if (table->hashes[i] == hash) {
			table->hashes[i] = TOMBSTONE;
			table->count--;
			table->num_tombstones++;
			if (8 * table->num_tombstones > table->capacity)
				resize(table, table->capacity); // Get rid of tombstones.
			return;
		}
	}
}

unsigned long long *get(struct table table, unsigned long long hash) {
	if (!table.count)
		return NULL;

	hash += (hash <= TOMBSTONE) ? 2 : 0;
	unsigned mask = (unsigned)table.capacity - 1;
	for (unsigned i = (unsigned)hash & mask; table.hashes[i]; i = (i + 1) & mask)
		if (table.hashes[i] == hash)
			return &table.values[i];

	return NULL;
}

int first_index(struct table table) {
	for (int i = 0; i < table.capacity; ++i)
		if (table.hashes[i] > TOMBSTONE)
			return i;
	return -1;
}

int next_index(struct table table, int index) {
	for (int i = index + 1; i < table.capacity; ++i)
		if (table.hashes[i] > TOMBSTONE)
			return i;
	return -1;
}

void destroy(struct table *table) {
	free(table->hashes); // This also frees the values.
	table->capacity = 0;
	table->count = 0;
	table->hashes = NULL;
	table->values = NULL;
}

#include <assert.h>
unsigned long long hash(const char *string) {
	// FNV-1a https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1a_hash
	unsigned long long hash = 14695981039346656037u;
	for (int i = 0; string[i]; ++i)
		hash = (hash ^ string[i]) * 1099511628211u;
	return hash;
}
int main(void) {
	{
		struct table table = { 0 };
		assert(!get(table, 123));
		assert(first_index(table) == -1);
		destroy(&table);
	}

	{
		const char *strings[4] = {
			"Hello, sailor!",
			"Three jumping wizards box quickly",
			"Third",
			"Eyyo",
		};

		struct table table = { 0 };
		for (int i = 0; i < 4; ++i)
			add(&table, hash(strings[i]), (unsigned)i);
		
		assert(table.count == 4);
		for (int i = 0; i < 4; ++i)
			assert(*get(table, hash(strings[i])) == (unsigned)i);

		int remaining[4] = { 0, 1, 2, 3 };
		int num_remaining = 4;
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			int value = (int)table.values[i];
			for (int i = 0; i < num_remaining; ++i) {
				if (remaining[i] == value) {
					remaining[i] = remaining[--num_remaining];
					break;
				}
			}
		}
		assert(num_remaining == 0);

		destroy(&table);
		assert(!table.capacity && !table.count && !table.hashes && !table.values);
	}

	{
		static unsigned long long hashes[1048576];
		int n = sizeof hashes / sizeof hashes[0];
		unsigned long long seed = 42;
		for (int i = 0; i < n; ++i) {
			seed ^= seed >> 12;
			seed ^= seed << 25;
			seed ^= seed >> 27;
			hashes[i] = seed * 0x2545F4914F6CDD1Du;
		}

		struct table table = { 0 };
		for (int i = 0; i < n; ++i)
			add(&table, hashes[i], (unsigned)i);

		assert(table.count == n);
		for (int i = 0; i < n; ++i)
			assert(*get(table, hashes[i]) == (unsigned)i);

		static int remaining[sizeof hashes / sizeof hashes[0]];
		for (int i = 0; i < n; ++i)
			remaining[i] = 1;
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			int value = (int)table.values[i];
			remaining[value] -= 1;
		}
		int num_remaining = 0;
		for (int i = 0; i < n; ++i)
			num_remaining += remaining[i];
		assert(num_remaining == 0);

		for (int i = 0; i < n / 2; ++i)
			remove(&table, hashes[i]);
		assert(table.count == n / 2);
		for (int i = n / 2; i < n; ++i)
			assert(*get(table, hashes[i]) == (unsigned)i);

		for (int i = 0; i < n; ++i)
			remaining[i] = 1;
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			int value = (int)table.values[i];
			remaining[value] -= 1;
		}
		int num_remaining1 = 0;
		int num_remaining2 = 0;
		for (int i = 0; i < n / 2; ++i)
			num_remaining1 += remaining[i];
		for (int i = n / 2; i < n; ++i)
			num_remaining2 += remaining[i];
		assert(num_remaining1 == n / 2);
		assert(num_remaining2 == 0);

		for (int i = 0; i < n / 2; ++i)
			add(&table, hashes[i], (unsigned)i);
		for (int i = 0; i < n; ++i)
			remaining[i] = 1;
		for (int i = first_index(table); i >= 0; i = next_index(table, i)) {
			int value = (int)table.values[i];
			remaining[value] -= 1;
		}
		num_remaining = 0;
		for (int i = 0; i < n; ++i)
			num_remaining += remaining[i];
		assert(num_remaining == 0);

		destroy(&table);
	}

	{
		// Potential pathological case: create a bunch of items and then delete them 
		// to leave tombstones, then lookup each item. If we don't clean tombstones this is O(n^2).
		struct table table = { 0 };
		for (unsigned i = 2; i <= 1048577; ++i)
			add(&table, i, i);
		for (unsigned i = 2; i <= 1048577; ++i)
			remove(&table, i, i);
		assert(table.count == 0);
		for (unsigned i = 2; i <= 1048577; ++i)
			assert(!get(table, i));
	}

	{
		// This shouldn't leak.
		for (int i = 0; i < 10000; ++i) {
			struct table table = { 0 };
			for (int j = 0; j < 10000; ++j)
				add(&table, (unsigned)j, (unsigned)j);
			destroy(&table);
		}
	}
}