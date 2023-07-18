// O(log N) allocation
// O(log N) deallocation
// avg 1/4 memory wasted
// 2 pointer header, 16/8 byte on 64/32-bit

#include <stdint.h> // intptr_t, uintptr_t
#include <assert.h>

union node {
	struct usednode {
		intptr_t free;
		intptr_t size;
	};
	struct freenode {
		union node *next;
		union node *prev;
	};
};

struct heap {
	int capacity;
	union node freelists[32];
};

int ceillog2(int x) {
	int log2 = 0;
	while ((1 << log2) < x)
		++log2;
	return log2;
}

void initialize(struct heap *heap, void *memory, int capacity) {
	// capacity must be a power of 2
	assert(capacity > 0 && (capacity & (capacity - 1)) == 0);
	// memory has to be properly aligned
	assert((uintptr_t)memory % capacity == 0);

	heap->capacity = capacity;
	for (int i = 0; i < 32; ++i) {
		union node *list = &heap->freelists[i];
		list->next = list;
		list->prev = list;
	}

	int available = capacity - sizeof(union node);
	int log2 = ceillog2(available);
	union node *list = &heap->freelists[log2];
	union node *node = memory;
	list->next = node;
	list->prev = node;
	node->next = list;
	node->prev = list;
}

void *allocate(struct heap *heap, int size) {
	// you could clamp to 0, or return NULL
	assert(size >= 0);

	int needed = size + sizeof(union node);
	for (int log2 = ceillog2(needed); log2 < 32; ++log2) {
		union node *list = &heap->freelists[log2];
		if (list->next == list)
			continue;

		union node *node = list->next;
		list->next = node->next;
		list->next->prev = list;
		assert(node->free);

		// split node to smallest size that fits
		while ((1 << (log2 - 1)) >= needed) {
			--log2;
			void *memory = (char *)node + ((intptr_t)1 << log2);
			union node *buddy = memory;
			list = &heap->freelists[log2];
			buddy->next = list->next;
			buddy->prev = list;
			list->next->prev = buddy;
			list->next = buddy;
		}

		node->free = 0;
		node->size = (intptr_t)1 << log2;
		return (char *)node + sizeof(union node);
	}
	return 0;
}

void deallocate(struct heap *heap, void *block) {
	if (!block)
		return;

	block = (char *)block - sizeof(union node);
	union node *node = block;
	assert(!node->free); // double free

	// combine neighboring free nodes
	while (node->size < heap->capacity) {
		// the buddy node is always just a bitflip away
		uintptr_t nodep = (uintptr_t)node;
		uintptr_t buddyp = nodep ^ node->size;
		union node *buddy = (union node *)buddyp;
		if (!buddy->free)
			break;

		buddy->next->prev = buddy->prev;
		buddy->prev->next = buddy->next;

		intptr_t size = node->size;
		node = node < buddy ? node : buddy;
		node->size = 2 * size;
	}

	int log2 = ceillog2((int)node->size);
	union node *list = &heap->freelists[log2];
	node->next = list->next;
	node->prev = list;
	list->next->prev = node;
	list->next = node;
}

int main(void) {
	static _Alignas(1024) char memory[1024];
	struct heap heap;
	initialize(&heap, memory, sizeof memory);

	char *a = allocate(&heap, 256);
	char *b = allocate(&heap, 256);
	deallocate(&heap, a);
	char *c = allocate(&heap, 256);
	deallocate(&heap, c);
	deallocate(&heap, b);

	char *d = allocate(&heap, 0);
	char *e = allocate(&heap, 1);
	char *f = allocate(&heap, 2);
	char *g = allocate(&heap, 3);
	char *h = allocate(&heap, 4);
	char *i = allocate(&heap, 5);
	deallocate(&heap, d);
	deallocate(&heap, i);
	deallocate(&heap, e);
	deallocate(&heap, h);
	deallocate(&heap, f);
	deallocate(&heap, g);

	__debugbreak();
}