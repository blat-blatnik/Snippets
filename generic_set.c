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
	int(*equal)(void *context, const void *a, const void *b, int size);
	void(*copy)(void *context, void *destination, const void *item, int size, struct slab **slab);
	unsigned long long(*hash)(void *context, const void *item, int size);
	void *equal_context;
	void *copy_context;
	void *hash_context;
	struct slab *slab;
	unsigned char *metadata;
	int count;
	int capacity;
	int num_tombstones;
};

#define TOMBSTONE 1

#define set(T) T*

#define resize(pset, capacity)\
	private__resize((pset),(capacity),sizeof*(*(pset)))

#define reserve(pset, min_capacity)\
	private__reserve((pset),(min_capacity),sizeof*(*(pset)))

#define get_header(pset)\
	((!*(pset)?(reserve((pset),64),0):0),(struct header*)(*(pset))-1)

#define add(pset, item)do{\
	reserve((pset), 1 + count(*(pset)));\
	(*(pset))[capacity(*(pset))] = (item);\
	private__add((void **)(pset), *(pset) + capacity(*(pset)), sizeof*(*(pset)));\
}while(0)

#define get_index(set, item)(\
	count(set)?\
		((set)[capacity(set)]=(item), private__get((set),&(set)[capacity(set)], sizeof*(set)))\
		:-1)

#define contains(set, item)\
	(get_index((set),(item))>=0)

#define remove(pset, item)do{\
	if (!count(*(pset))) break;\
	(*(pset))[capacity(*(pset))] = (item);\
	private__remove((void **)(pset), *(pset) + capacity(*(pset)), sizeof*(*(pset)));\
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

int count(const set(void) set) {
	return set ? ((struct header *)set)[-1].count : 0;
}

int capacity(const set(void) set) {
	return set ? ((struct header *)set)[-1].capacity : 0;
}

void destroy(set(void) *set) {
	if (*set) {
		struct header *header = ((struct header *)*set) - 1;
		freeall(&header->slab);
		free(header);
		*set = NULL;
	}
}

int first_index(const set(void) set) {
	if (set) {
		struct header *header = (struct header *)set - 1;
		for (int i = 0; i < header->capacity; ++i)
			if (header->metadata[i] > TOMBSTONE)
				return i;
	}
	return -1;
}

int next_index(const set(void) set, int index) {
	if (set) {
		struct header *header = (struct header *)set - 1;
		for (int i = index + 1; i < header->capacity; ++i)
			if (header->metadata[i] > TOMBSTONE)
				return i;
	}
	return -1;
}

int default_compare(void *context, const void *a, const void *b, int size) {
	(void)context;
	return memcmp(a, b, (size_t)size) == 0;
}

void default_copy(void *context, void *destination, const void *item, int size, struct slab **slab) {
	(void)context; (void)slab;
	memcpy(destination, item, (size_t)size);
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

void private__resize(set(void) *pset, int new_capacity, int item_size) {
	int old_count = count(*pset);
	int old_capacity = capacity(*pset);
	if (new_capacity <= old_count)
		new_capacity = old_count + 1;

	int pow2;
	for (pow2 = 0; (1 << pow2) < new_capacity; ++pow2);
	new_capacity = 1 << pow2;
	int num_items = new_capacity + 1; //1 extra item at the end for temporary storage that we can take the address of in macros.

	void *new_memory = malloc(sizeof(struct header) + num_items * (item_size + sizeof(unsigned char)));
	struct header *new_header = new_memory;
	char *new_items = (char *)(new_header + 1);
	unsigned char *new_metadata = (unsigned char *)(new_items + num_items * item_size);
	memset(new_metadata, 0, (size_t)num_items);

	new_header->count = old_count;
	if (*pset)
		*new_header = ((struct header *)(*pset))[-1];
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
	new_header->num_tombstones = 0;

	struct header *old_header = NULL;
	unsigned char *old_metadata = NULL;
	char *old_items = *pset;
	if (*pset) {
		old_header = ((struct header *)(*pset)) - 1;
		old_metadata = old_header->metadata;
	}

	unsigned mask = (unsigned)new_capacity - 1;
	for (int i = 0; i < old_capacity; ++i) {
		if (old_metadata[i] > TOMBSTONE) {
			unsigned long long hash = new_header->hash(new_header->hash_context, old_items + i * item_size, item_size);
			for (unsigned j = (unsigned)hash & mask;; j = (j + 1) & mask) {
				if (!new_metadata[j]) {
					new_metadata[j] = old_metadata[i];
					new_header->copy(new_header->copy_context, new_items + j * item_size, old_items + i * item_size, item_size, &new_header->slab);
					break;
				}
			}
		}
	}

	if (old_header) {
		freeall(&old_header->slab);
		free(old_header);
	}
	*pset = new_header + 1;
}

void private__reserve(set(void) *pset, int min_capacity, int item_size) {
	if (4 * min_capacity > 3 * capacity(*pset)) {
		int new_capacity = 4 * min_capacity / 3;
		if (new_capacity < 64)
			new_capacity = 64;
		private__resize(pset, new_capacity, item_size);
	}
}

void private__add(set(void) *pset, const void *item, int item_size) {
	struct header *header = (struct header *)(*pset) - 1;
	char *items = *pset;
	unsigned long long hash = header->hash(header->hash_context, item, item_size);
	unsigned char metadata = hash & 0xFF;
	metadata += (metadata <= TOMBSTONE) ? 2 : 0;
	unsigned mask = (unsigned)header->capacity - 1;
	unsigned index = (unsigned)-1;
	for (unsigned i = (unsigned)hash & mask;; i = (i + 1) & mask) {
		if (!header->metadata[i]) {
			index = min(index, i);
			break;
		}
		if (header->metadata[i] == TOMBSTONE)
			index = min(index, i);
		else if (header->metadata[i] == metadata && header->equal(header->equal_context, items + i * item_size, item, item_size)) {
			header->copy(header->copy_context, items + i * item_size, item, item_size, &header->slab);
			return;
		}
	}
	if (header->metadata[index] == TOMBSTONE)
		header->num_tombstones--;
	header->metadata[index] = metadata;
	header->copy(header->copy_context, items + index * item_size, item, item_size, &header->slab);
	header->count++;
}

void private__remove(set(void) *pset, const void *item, int item_size) {
	struct header *header = ((struct header *)*pset) - 1;
	unsigned long long hash = header->hash(header->hash_context, item, item_size);
	unsigned char metadata = hash & 0xFF;
	metadata += (metadata <= TOMBSTONE) ? 2 : 0;
	unsigned mask = (unsigned)header->capacity - 1;
	for (unsigned i = (unsigned)hash & mask; header->metadata[i]; i = (i + 1) & mask) {
		if (header->metadata[i] == metadata && header->equal(header->equal_context, item, ((char *)*pset) + i * item_size, item_size)) {
			header->metadata[i] = TOMBSTONE;
			header->count--;
			header->num_tombstones++;
			if (4 * header->count < header->capacity)
				private__resize(pset, 2 * header->count, item_size);
			else if (8 * header->num_tombstones > header->capacity)
				private__resize(pset, header->capacity, item_size); // Get rid of tombstones.
			return;
		}
	}
}

int private__get(const set(void) set, const void *item, int item_size) {
	struct header *header = (struct header *)set - 1;
	unsigned long long hash = header->hash(header->hash_context, item, item_size);
	unsigned char metadata = hash & 0xFF;
	metadata += (metadata <= TOMBSTONE) ? 2 : 0;
	unsigned mask = (unsigned)header->capacity - 1;
	for (unsigned i = (unsigned)hash & mask; header->metadata[i]; i = (i + 1) & mask)
		if (header->metadata[i] == metadata && header->equal(header->equal_context, item, (char *)set + i * item_size, item_size))
			return i;
	return -1;
}

#undef NDEBUG
#include <assert.h>
uint64_t hash_string(void *context, const void *item, int item_size) {
	(void)context; (void)item_size;
	const char *string = item;
	unsigned long long hash = 14695981039346656037u;
	for (int i = 0; string[i]; ++i)
		hash = (hash ^ string[i]) * 1099511628211u;
	return hash;
}
int equal_strings(void *context, const void *a, const void *b, int size) {
	(void)context; (void)size;
	return strcmp(*(const char **)a, *(const char **)b) == 0;
}
void copy_string(void *context, void *destination, const void *item, int size, struct slab **slab) {
	(void)context; (void)size;
	const char **src = item;
	char **dst = destination;
	int string_size = 1 + (int)strlen(*src);
	*dst = allocate(slab, string_size, 1);
	memcpy(*dst, *src, string_size);
}
int main(void) {
	{
		set(int) set = NULL;
		assert(!count(set));
		assert(!capacity(set));
		assert(get_index(set, 0) == -1);
		assert(!contains(set, 1));
		assert(first_index(set) == -1);
		destroy(&set);
	}

	{
		set(int) set = NULL;
		for (int i = 0; i < 16; ++i)
			add(&set, i);
	
		assert(count(set) == 16);
		for (int i = 0; i < 16; ++i)
			assert(contains(set, i));
	
		int total[16] = { 0 };
		for (int i = first_index(set); i >= 0; i = next_index(set, i))
			total[set[i]]++;
		for (int i = 0; i < 16; ++i)
			assert(total[i] == 1);
	
		destroy(&set);
	}

	{
		static int total_items[1048576];
		int n = sizeof total_items / sizeof total_items[0];

		set(int) set = NULL;
		for (int i = 0; i < n; ++i)
			add(&set, i);
		for (int i = 0; i < n; ++i)
			assert(contains(set, i));

		assert(count(set) == n);
		for (int i = first_index(set); i >= 0; i = next_index(set, i))
			total_items[set[i]]++;
		for (int i = 0; i < n; ++i)
			assert(total_items[i] == 1);

		for (int i = 0; i < n / 2; ++i)
			remove(&set, i);
		assert(count(set) == n / 2);
		for (int i = 0; i < n / 2; ++i)
			assert(!contains(set, i));
		for (int i = n / 2; i < n; ++i)
			assert(contains(set, i));

		memset(total_items, 0, sizeof total_items);
		for (int i = first_index(set); i >= 0; i = next_index(set, i))
			total_items[set[i]]++;
		for (int i = 0; i < n / 2; ++i)
			assert(total_items[i] == 0);
		for (int i = n / 2; i < n; ++i)
			assert(total_items[i] == 1);

		destroy(&set);
	}

	{
		set(char *) set = NULL;
		struct header *header = get_header(&set);
		header->hash = hash_string;
		header->equal = equal_strings;
		header->copy = copy_string;
		add(&set, "Key0");
		assert(contains(set, "Key0"));
		add(&set, "Key1");
		add(&set, "Key2");
		add(&set, "Key3");
		assert(contains(set, "Key0"));
		assert(contains(set, "Key1"));
		assert(contains(set, "Key2"));
		assert(contains(set, "Key3"));
		assert(!contains(set, "Key4"));
		assert(strcmp(set[get_index(set, "Key0")], "Key0") == 0);
		assert(strcmp(set[get_index(set, "Key1")], "Key1") == 0);
		assert(strcmp(set[get_index(set, "Key2")], "Key2") == 0);
		assert(strcmp(set[get_index(set, "Key3")], "Key3") == 0);

		int total[4] = { 0 };
		for (int i = first_index(set); i >= 0; i = next_index(set, i))
			total[set[i][3] - '0']++;
		assert(total[0] == 1 && total[1] == 1 && total[2] == 1 && total[3] == 1);

		destroy(&set);
		assert(!capacity(set) && !count(set) && !set);
	}

	{
		static char items[1048576][8];
		int n = sizeof items / sizeof items[0];
		for (int i = 0; i < n; ++i) {
			int x = i;
			for (int j = 0; j < 7; ++j) {
				items[i][6 - j] = '0' + x % 10;
				x /= 10;
			}
			items[i][7] = 0;
		}

		set(char *) set = NULL;
		for (int i = 0; i < n; ++i)
			add(&set, items[i]);
		assert(count(set) == n);

		static int total[sizeof items / sizeof items[0]] = {0};
		for (int i = first_index(set); i >= 0; i = next_index(set, i)) {
			char *item = set[i];
			int x = 0;
			for (int j = 0; j < 7; ++j) {
				x *= 10;
				x += item[j] - '0';
			}
			total[x]++;
		}
		for (int i = 0; i < n; ++i)
			assert(total[i] == 1);

		for (int i = 0; i < n / 2; ++i)
			remove(&set, items[i]);
		assert(count(set) == n / 2);
		memset(total, 0, sizeof total);
		for (int i = first_index(set); i >= 0; i = next_index(set, i)) {
			char *item = set[i];
			int x = 0;
			for (int j = 0; j < 7; ++j) {
				x *= 10;
				x += item[j] - '0';
			}
			total[x]++;
		}
		for (int i = 0; i < n / 2; ++i)
			assert(total[i] == 0);
		for (int i = n / 2; i < n; ++i)
			assert(total[i] == 1);

		for (int i = 0; i < n / 2; ++i)
			add(&set, items[i]);
		assert(count(set) == n);

		memset(total, 0, sizeof total);
		for (int i = first_index(set); i >= 0; i = next_index(set, i)) {
			char *item = set[i];
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
		set(int) set = NULL;
		for (int i = 0; i < 1048575; ++i)
			add(&set, i);
		resize(&set, count(set) + 1);
		assert(capacity(set) == count(set) + 1);
		for (int i = 1; i < 1048575; ++i)
			remove(&set, i);
		assert(count(set) == 1);
		assert(contains(set, 0));
		for (int i = 1; i < 1048575; ++i)
			assert(!contains(set, i));
		destroy(&set);
	}

	{
		// This shouldn't leak
		for (int i = 0; i < 10000; ++i) {
			set(int) set = NULL;
			for (int j = 0; j < 10000; ++j)
				add(&set, j);
			destroy(&set);
		}
	}
}