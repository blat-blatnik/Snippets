#include <stdlib.h> // malloc, free

// For simplicity and efficiency, this table doesn't actually store the keys. 
// It only stores the key hashes. You'd better have a good hash function, because 
// if two keys happen to hash to the same value you're in big trouble. They will 
// overwrite each other. In practice, if you do have a decent hash function, 
// then the likelyhood of this happening is really small with 64-bits hashes.
// The hash 0 is reserved as a free value.
// The hash 0xFFFFFFFFFFFFFFFF (all bits set) is reserved as a tombstone value.
// Additionally, the highest bit of the hash
struct table {
	unsigned long long *hashes;
	unsigned long long *values;
	int capacity; // Always a power of 2 or 0.
	int count;
};

#define TOMBSTONE ((unsigned long long)-1)

void resize(struct table *table, int capacity) {
	if (capacity <= table->count)
		return;
	
	int pow2; // Round up capacity to a power of 2.
	for (pow2 = 1; (1 << pow2) < capacity; ++pow2);
	capacity = (1 << pow2);
	if (capacity < 128)
		capacity = 128;

	unsigned long long *new_memory = malloc((size_t)capacity * 2 * sizeof new_memory[0]);
	unsigned long long *new_hashes = new_memory;
	unsigned long long *new_values = new_hashes + capacity;
	for (int i = 0; i < capacity; ++i)
		new_hashes[i] = 0;

	unsigned mask = (unsigned)capacity - 1;
	for (int i = 0; i < table->capacity; ++i) {
		unsigned long long hash = table->hashes[i];
		if (hash && hash != TOMBSTONE) {
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
}

void reserve(struct table *table, int min_capacity) {
	if (table->capacity < min_capacity) {
		int capacity = 2 * table->capacity;
		if (capacity < 128)
			capacity = 128;
		while (capacity < min_capacity)
			capacity *= 2;
		resize(table, capacity);
	}
}

void add(struct table *table, unsigned long long hash, unsigned long long value) {
	// assert(hash != 0 && hash != TOMBSTONE);
	// assert(!get(table, hash));
	reserve(table, table->count + 1);
	unsigned mask = (unsigned)table->capacity - 1;
	unsigned i = (unsigned)hash & mask;
	while (table->hashes[i] && table->hashes[i] != TOMBSTONE)
		i = (i + 1) & mask;
	table->hashes[i] = hash;
	table->values[i] = value;
	++table->count;
}

void remove(struct table *table, unsigned long long hash) {
	// assert(hash != 0 && hash != TOMBSTONE);
	if (!table->count)
		return; // Tried to remove from an empty table.
	unsigned mask = (unsigned)table->capacity - 1;
	for (unsigned i = (unsigned)hash & mask; table->hashes[i]; i = (i + 1) & mask) {
		if (table->hashes[i] == hash) {
			table->hashes[i] = TOMBSTONE;
			table->count--;
			return;
		}
	}
	// Tried to remove non-existent key.
}

unsigned long long *get(struct table table, unsigned long long hash) {
	// assert(hash != 0 && hash != TOMBSTONE);
	if (!table.capacity)
		return NULL;
	unsigned mask = (unsigned)table.capacity - 1;
	for (unsigned i = (unsigned)hash & mask; table.hashes[i]; i = (i + 1) & mask)
		if (table.hashes[i] == hash)
			return &table.values[i];
	return NULL;
}

int first_index(struct table table) {
	if (!table.count)
		return -1;
	for (int i = 0; i < table.capacity; ++i)
		if (table.hashes[i] && table.hashes[i] != TOMBSTONE)
			return i;
	return -1; // This should never happen.
}

int next_index(struct table table, int index) {
	for (int i = index + 1; i < table.capacity; ++i)
		if (table.hashes[i] && table.hashes[i] != TOMBSTONE)
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
			remaining[value] = 0;
		}
		int num_remaining = 0;
		for (int i = 0; i < n; ++i)
			num_remaining += remaining[i];
		assert(num_remaining == 0);

		destroy(&table);
	}
}