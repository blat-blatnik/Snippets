#include <stdlib.h> // malloc, free
#include <string.h> // strlen, strcmp, memcpy, memset

struct set {
	char **items;
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

void resize(struct set *set, int capacity) {
	if (capacity <= set->count)
		capacity = set->count + 1;
	
	int pow2;
	for (pow2 = 0; (1 << pow2) < capacity; ++pow2);
	capacity = 1 << pow2;

	int total_string_size = 0;
	for (struct slab *slab = set->slab; slab; slab = slab->prev)
		total_string_size += slab->cursor;

	int first_slab_capacity = 1024;
	while (first_slab_capacity < total_string_size)
		first_slab_capacity *= 2;

	void *new_memory = malloc(capacity * sizeof set->items[0] + sizeof set->slab[0] + first_slab_capacity);
	char **new_items = new_memory;
	memset(new_items, 0, capacity * sizeof set->items[0]);
	struct slab *new_slab = (struct slab *)(new_items + capacity);
	new_slab->prev = NULL;
	new_slab->capacity = first_slab_capacity;
	new_slab->cursor = 0;

	unsigned mask = capacity - 1;
	for (int i = 0; i < set->capacity; ++i) {
		if ((size_t)set->items[i] > TOMBSTONE) {
			char *item = copy_string(&new_slab, set->items[i]);
			unsigned long long hash = hash_string(item);
			for (unsigned j = (unsigned)hash & mask;; j = (j + 1) & mask) {
				if (!new_items[j]) {
					new_items[j] = item;
					break;
				}
			}
		}
	}

	for (struct slab *slab = set->slab; slab && slab->prev;) {
		struct slab *prev = slab->prev;
		free(slab);
		slab = prev;
	}
	free(set->items); // This also frees the metadata, and slab.
	set->items = new_items;
	set->slab = new_slab;
	set->capacity = capacity;
	set->num_tombstones = 0;
}

void reserve(struct set *set, int min_capacity) {
	if (2 * set->capacity < 3 * min_capacity) {
		int new_capacity = 2 * set->capacity;
		if (new_capacity < 64)
			new_capacity = 64;
		while (2 * new_capacity < 3 * min_capacity)
			new_capacity *= 2;
		resize(set, new_capacity);
	}
}

void add(struct set *set, const char *item) {
	reserve(set, set->count + 1);
	unsigned long long hash = hash_string(item);
	unsigned mask = (unsigned)set->capacity - 1;
	unsigned index = (unsigned)-1;
	for (unsigned i = (unsigned)hash & mask;; i = (i + 1) & mask) {
		if (!set->items[i]) {
			index = min(index, i);
			break;
		}
		if (set->items[i] == (void *)TOMBSTONE)
			index = min(index, i);
		else if (strcmp(set->items[i], item) == 0)
			return;
	}
	if (set->items[index] == (void *)TOMBSTONE)
		--set->num_tombstones;
	set->count++;
	set->items[index] = copy_string(&set->slab, item);
}

void remove(struct set *set, const char *item) {
	if (!set->count)
		return;
	
	unsigned long long hash = hash_string(item);
	unsigned mask = (unsigned)set->capacity - 1;
	for (unsigned i = (unsigned)hash & mask; set->items[i]; i = (i + 1) & mask) {
		if (set->items[i] != (void *)TOMBSTONE && strcmp(set->items[i], item) == 0) {
			set->items[i] = (void *)TOMBSTONE;
			set->count--;
			set->num_tombstones++;
			if (8 * set->num_tombstones > set->capacity)
				resize(set, set->capacity); // Get rid of tombstones.
			return;
		}
	}
}

int contains(struct set set, const char *item) {
	if (!set.count)
		return 0;

	unsigned long long hash = hash_string(item);
	unsigned mask = (unsigned)set.capacity - 1;
	for (unsigned i = (unsigned)hash & mask; set.items[i]; i = (i + 1) & mask)
		if (set.items[i] != (void *)TOMBSTONE && strcmp(set.items[i], item) == 0)
			return 1;
	return 0;
}

int first_index(struct set set) {
	for (int i = 0; i < set.capacity; ++i)
		if ((size_t)set.items[i] > TOMBSTONE)
			return i;
	return -1;
}

int next_index(struct set set, int index) {
	for (int i = index + 1; i < set.capacity; ++i)
		if ((size_t)set.items[i] > TOMBSTONE)
			return i;
	return -1;
}

void destroy(struct set *set) {
	for (struct slab *slab = set->slab; slab && slab->prev;) {
		struct slab *prev = slab->prev;
		free(slab);
		slab = prev;
	}
	free(set->items); // This also frees the slab.
	memset(set, 0, sizeof set[0]);
}

#include <assert.h>
int main(void) {
	static char items[1048576][8] = { 0 };
	int n = sizeof items / sizeof items[0];
	for (int i = 0; i < n; ++i) {
		int x = i;
		for (int j = 0; j < 7; ++j) {
			items[i][6 - j] = '0' + x % 10;
			x /= 10;
		}
	}

	{
		struct set set = { 0 };
		assert(!contains(set, "Hi"));
		assert(first_index(set) < 0);
		remove(&set, "Hi");
		destroy(&set);
	}

	{
		struct set set = { 0 };

		add(&set, "abcd");
		add(&set, "efgh");
		add(&set, "ijkl");
		add(&set, "mnop");
		assert(contains(set, "abcd"));
		assert(contains(set, "efgh"));
		assert(contains(set, "ijkl"));
		assert(contains(set, "mnop"));
		assert(!contains(set, "qrst"));

		remove(&set, "abcd");
		assert(!contains(set, "abcd"));
		assert(contains(set, "efgh"));
		assert(contains(set, "ijkl"));
		assert(contains(set, "mnop"));

		remove(&set, "abcd");
		assert(!contains(set, "abcd"));
		assert(contains(set, "efgh"));
		assert(contains(set, "ijkl"));
		assert(contains(set, "mnop"));

		remove(&set, "efgh");
		remove(&set, "ijkl");
		remove(&set, "mnop");
		assert(!contains(set, "abcd"));
		assert(!contains(set, "efgh"));
		assert(!contains(set, "ijkl"));
		assert(!contains(set, "mnop"));

		destroy(&set);
	}

	{
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

		static int total[1048576] = { 0 };
		for (int i = first_index(set); i >= 0; i = next_index(set, i)) {
			char *item = set.items[i];
			int x = 0;
			for (int j = 0; j < 7; ++j) {
				x *= 10;
				x += item[j] - '0';
			}
			total[x]++;
		}
		for (int i = 0; i < n; ++i)
			assert(total[i] == 1);

		destroy(&set);
	}

	{
		// Potential pathological case: create a bunch of items and then delete them 
		// to leave tombstones, then lookup each item. If we don't clean tombstones this is O(n^2).
		struct set set = { 0 };
		for (int i = 0; i < n - 1; ++i)
			add(&set, items[i]);
		resize(&set, set.count + 1);
		for (int i = 1; i < n - 1; ++i)
			remove(&set, items[i]);
		assert(set.count == 1);
		for (int i = 1; i < n - 1; ++i)
			assert(!contains(set, items[i]));
		destroy(&set);
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
				add(&set, item);
			}
			destroy(&set);
		}
	}
}