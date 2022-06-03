struct manager {
	void *items; // items[0] is a reserved sentinel.
	struct metadata *metadata;
	unsigned short freelist;
	unsigned short num_items;
	unsigned short item_size;
};

struct metadata {
	unsigned short generation;
	unsigned short prev;
	unsigned short next;
};

union handle {
	unsigned value;
	struct { unsigned short index, generation; } fields;
};

struct manager create(void *items, struct metadata *metadata, unsigned num_items, unsigned item_size) {
	metadata[0].prev = 0;
	metadata[0].next = 0;
	metadata[0].generation = 0;
	for (unsigned i = 1; i < num_items - 1; ++i) {
		metadata[i].prev = 0;
		metadata[i].next = (unsigned short)(i + 1);
		metadata[i].generation = 0;
	}
	metadata[num_items - 1].prev = 0;
	metadata[num_items - 1].next = 0;
	metadata[num_items - 1].generation = 0;
	
	return (struct manager) {
		.items = items,
		.metadata = metadata,
		.freelist = 1,
		.num_items = (unsigned short)num_items,
		.item_size = (unsigned short)item_size,
	};
}

union handle allocate(struct manager *manager) {
	unsigned short index = manager->freelist;
	if (index)
		manager->freelist = manager->metadata[index].next;
	
	manager->metadata[index].prev = manager->metadata[0].prev;
	manager->metadata[index].next = 0;
	manager->metadata[manager->metadata[0].prev].next = index;
	manager->metadata[0].prev = index;
	
	return (union handle) { .fields = { 
		.index = (unsigned short)index, 
		.generation = manager->metadata[index].generation 
	}};
}

void deallocate(struct manager *manager, union handle handle) {
	unsigned short index = handle.fields.index;
	if (!handle.value)
		return;
	if (index >= manager->num_items || handle.fields.generation != manager->metadata[index].generation)
		return; // Handle is invalid.

	unsigned short next = manager->metadata[index].next;
	unsigned short prev = manager->metadata[index].prev;
	manager->metadata[prev].next = next;
	manager->metadata[next].prev = prev;
	++manager->metadata[index].generation;
	manager->metadata[index].next = (unsigned short)manager->freelist;
	manager->freelist = index;
}

int is_valid(struct manager *manager, union handle handle) {
	unsigned index = handle.fields.index;
	return index < manager->num_items && handle.fields.generation == manager->metadata[index].generation;
}

void *get_item_from_handle(struct manager *manager, union handle handle) {
	unsigned index = handle.fields.index;
	if (index >= manager->num_items || handle.fields.generation != manager->metadata[index].generation)
		index = 0; // Handle is invalid.

	return (char *)manager->items + index * manager->item_size;
}

#include <assert.h>
int main(void) {
	int items[10] = { -999, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	struct metadata metadata[10];
	struct manager manager = create(items, metadata, 10, sizeof items[0]);
	union handle handles[10];

	for (int i = 1; i < 10; ++i) {
		handles[i] = allocate(&manager);
		assert(is_valid(&manager, handles[i]));
		int *item = get_item_from_handle(&manager, handles[i]);
		assert(*item == items[i]);
	}
	
	for (int i = 1; i < 10; ++i) {
		assert(is_valid(&manager, handles[i]));
		deallocate(&manager, handles[i]);
		assert(!is_valid(&manager, handles[i]));
	}
	
	for (int i = 0; i < 10; ++i) {
		union handle handle = allocate(&manager);
		assert(is_valid(&manager, handle));
		int item = *(int *)get_item_from_handle(&manager, handle);
		deallocate(&manager, handle);
		assert(!is_valid(&manager, handle));
		union handle new_handle = allocate(&manager);
		assert(!is_valid(&manager, handle));
		assert(item == *(int *)get_item_from_handle(&manager, new_handle));
		deallocate(&manager, new_handle);
	}

	for (int i = 1; i < 10; ++i)
		handles[i] = allocate(&manager);
	assert(*(int *)get_item_from_handle(&manager, allocate(&manager)) == items[0]);
	assert(*(int *)get_item_from_handle(&manager, allocate(&manager)) == items[0]);
	assert(*(int *)get_item_from_handle(&manager, allocate(&manager)) == items[0]);
	for (int i = 1; i < 10; ++i)
		deallocate(&manager, handles[i]);

	for (int i = 1; i < 5; ++i)
		handles[i] = allocate(&manager);

	for (unsigned index = metadata[0].next, i = 1; index; index = metadata[index].next, ++i) {
		assert(i < 5);
		assert(items[index] == i);
	}

	for (int i = 5; i < 10; ++i)
		handles[i] = allocate(&manager);
	for (unsigned index = metadata[0].next, i = 1; index; index = metadata[index].next, ++i) {
		assert(i < 10);
		assert(items[index] == i);
	}
	for (int i = 1; i < 10; ++i)
		deallocate(&manager, handles[i]);

	for (unsigned index = metadata[0].next; index; index = metadata[index].next)
		assert(0);
}