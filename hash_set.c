#include <stdlib.h> // calloc, free

// For simplicity and efficiency, this set doesn't actually store the items. 
// It only stores the item hashes. You'd better have a good hash function, because 
// if two items happen to hash to the same value you're in big trouble. They will 
// overwrite each other. In practice, if you have a decent hash function the 
// likelyhood of this happening is really small with 64-bits hashes. Note that
// this also means that you cannot iterate over all of the items in the set, since
// we only store the hashes.
struct set {
	unsigned long long *hashes;
	int capacity; // Always a power of 2 or 0.
	int count;
	int num_tombstones;
};

#define TOMBSTONE 1

void resize(struct set *set, int capacity) {
	if (capacity <= set->count)
		capacity = set->count + 1;
	
	int pow2; // Round up capacity to a power of 2.
	for (pow2 = 1; (1 << pow2) < capacity; ++pow2);
	capacity = (1 << pow2);

	unsigned long long *new_hashes = calloc((size_t)capacity, sizeof new_hashes[0]);
	unsigned mask = (unsigned)capacity - 1;
	for (int i = 0; i < set->capacity; ++i) {
		unsigned long long hash = set->hashes[i];
		if (hash > TOMBSTONE) {
			for (unsigned j = (unsigned)hash & mask;; j = (j + 1) & mask) {
				if (!new_hashes[j]) {
					new_hashes[j] = hash;
					break;
				}
			}
		}
	}

	free(set->hashes);
	set->hashes = new_hashes;
	set->capacity = capacity;
	set->num_tombstones = 0;
}

void reserve(struct set *set, int min_capacity) {
	if (3 * set->capacity < 4 * min_capacity) {
		int capacity = 4 * min_capacity / 3;
		if (capacity < 64)
			capacity = 64;
		resize(set, capacity);
	}
}

void add(struct set *set, unsigned long long hash) {
	hash += (hash <= TOMBSTONE) ? 2 : 0;
	reserve(set, set->count + 1);
	unsigned mask = (unsigned)set->capacity - 1;
	unsigned index = (unsigned)-1;
	for (unsigned i = (unsigned)hash & mask;; i = (i + 1) & mask) {
		if (set->hashes[i] == hash)
			return;
		if (!set->hashes[i]) {
			index = min(index, i);
			break;
		}
		if (set->hashes[i] == TOMBSTONE)
			index = min(index, i);
	}
	if (set->hashes[index] == TOMBSTONE)
		--set->num_tombstones;
	set->hashes[index] = hash;
	set->count++;
}

void remove(struct set *set, unsigned long long hash) {
	if (!set->count)
		return;

	hash += (hash <= TOMBSTONE) ? 2 : 0;
	unsigned mask = (unsigned)set->capacity - 1;
	for (unsigned i = (unsigned)hash & mask; set->hashes[i]; i = (i + 1) & mask) {
		if (set->hashes[i] == hash) {
			set->hashes[i] = TOMBSTONE;
			set->num_tombstones++;
			set->count--;
			if (8 * set->num_tombstones > set->capacity)
				resize(set, set->capacity); // Get rid of tombstones.
			return;
		}
	}
}

int contains(struct set set, unsigned long long hash) {
	if (!set.count)
		return 0;
	
	hash += (hash <= TOMBSTONE) ? 2 : 0;
	unsigned mask = (unsigned)set.capacity - 1;
	for (unsigned i = (unsigned)hash & mask; set.hashes[i]; i = (i + 1) & mask)
		if (set.hashes[i] == hash)
			return 1;
	
	return 0;
}

void destroy(struct set *set) {
	free(set->hashes);
	set->capacity = 0;
	set->count = 0;
	set->hashes = NULL;
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
		struct set set = { 0 };
		assert(!contains(set, hash("Hi")));
		remove(&set, hash("Hi"));
		destroy(&set);
	}

	{
		struct set set = { 0 };

		add(&set, hash("abcd"));
		add(&set, hash("efgh"));
		add(&set, hash("ijkl"));
		add(&set, hash("mnop"));
		assert(contains(set, hash("abcd")));
		assert(contains(set, hash("efgh")));
		assert(contains(set, hash("ijkl")));
		assert(contains(set, hash("mnop")));
		assert(!contains(set, hash("qrst")));

		remove(&set, hash("abcd"));
		assert(!contains(set, hash("abcd")));
		assert(contains(set, hash("efgh")));
		assert(contains(set, hash("ijkl")));
		assert(contains(set, hash("mnop")));

		remove(&set, hash("abcd"));
		assert(!contains(set, hash("abcd")));
		assert(contains(set, hash("efgh")));
		assert(contains(set, hash("ijkl")));
		assert(contains(set, hash("mnop")));

		remove(&set, hash("efgh"));
		remove(&set, hash("ijkl"));
		remove(&set, hash("mnop"));
		assert(!contains(set, hash("abcd")));
		assert(!contains(set, hash("efgh")));
		assert(!contains(set, hash("ijkl")));
		assert(!contains(set, hash("mnop")));

		destroy(&set);
	}

	{
		static unsigned long long items[1048576];
		int n = sizeof items / sizeof items[0];
		for (int i = 0; i < n; ++i) {
			int x = i;
			char key[8] = { 0 };
			for (int j = 0; j < 7; ++j) {
				key[6 - j] = '0' + x % 10;
				x /= 10;
			}
			items[i] = hash(key);
		}

		struct set set = { 0 };
		for (int i = 0; i < n; ++i)
			assert(!contains(set, items[i]));
		for (int i = 0; i < n; ++i)
			add(&set, items[i]);
		for (int i = 0; i < n; ++i)
			assert(contains(set, items[i]));
		for (int i = 0; i < n; ++i)
			add(&set, items[i]);
		for (int i = 0; i < n; ++i)
			remove(&set, items[i]);
		for (int i = 0; i < n; ++i)
			assert(!contains(set, items[i]));
		for (int i = 0; i < n; ++i)
			add(&set, items[i]);
		for (int i = 0; i < n; ++i)
			assert(contains(set, items[i]));

		destroy(&set);
		for (int i = 0; i < n / 2; ++i)
			add(&set, items[i]);
		for (int i = n / 2; i < n; ++i)
			assert(!contains(set, items[i]));
		for (int i = 0; i < n / 2; ++i)
			assert(contains(set, items[i]));
		for (int i = 0; i < n / 4; ++i)
			remove(&set, items[i]);
		for (int i = 0; i < n; ++i)
			assert(contains(set, items[i]) == (i >= n / 4 && i < n / 2));

		for (int i = 0; i < n; ++i)
			remove(&set, items[i]);
		assert(set.count == 0);
		for (int i = 0; i < n; ++i)
			add(&set, items[i]);

		destroy(&set);
	}

	{
		// Potential pathological case: create a bunch of items and then delete them 
		// to leave tombstones, then lookup each item. If we don't clean tombstones this is O(n^2).
		struct set set = { 0 };
		for (unsigned long long i = 2; i <= 1048577; ++i)
			add(&set, i);
		//resize(&set, set.count + 1);
		for (unsigned long long i = 3; i <= 1048577; ++i)
			remove(&set, i);
		assert(set.count == 1);
		for (unsigned long long i = 3; i <= 1048577; ++i)
			assert(!contains(set, i));
	}

	{
		// This shouldn't leak.
		for (int i = 0; i < 10000; ++i) {
			struct set set = { 0 };
			for (int j = 0; j < 10000; ++j) {
				char item[5] = { 0 };
				int x = j;
				item[3] = x % 10; x /= 10;
				item[2] = x % 10; x /= 10;
				item[1] = x % 10; x /= 10;
				item[0] = x % 10; x /= 10;
				add(&set, hash(item));
			}
			destroy(&set);
		}
	}
}