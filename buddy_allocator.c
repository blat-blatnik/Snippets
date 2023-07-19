// O(log N) allocation
// O(log N) deallocation
// avg 1/4 memory wasted
// 2 pointer header, 16/8 byte on 64/32-bit

#include <stdint.h> // intptr_t, uintptr_t
#include <string.h> // memcpy
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
	void *memory;
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

	heap->memory = memory;
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

	assert(block >= heap->memory); // block isn't from this heap
	
	void *header = (char *)block - sizeof(union node);
	union node *node = header;
	
	assert(!node->free); // double free
	assert((char *)node + node->size <= (char *)heap->memory + heap->capacity); // block isn't from this heap.

	// combine neighboring free nodes
	while (node->size < heap->capacity) {
		// the buddy node is always just a bitflip away
		uintptr_t base = (uintptr_t)heap->memory;
		uintptr_t nodep = (uintptr_t)node - base;
		uintptr_t buddyp = nodep ^ node->size;
		union node *buddy = (union node *)(buddyp + base);
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

void *reallocate(struct heap *heap, void *block, int size) {
	// you could clamp to 0, or return NULL
	assert(size >= 0);

	if (!block)
		return allocate(heap, size);
	if (!size) {
		deallocate(heap, block);
		return 0;
	}

	assert(block >= heap->memory); // block isn't from this heap
	
	void *header = (char *)block - sizeof(union node);
	union node *node = header;
	
	assert(!node->free); // double free
	assert((char *)node + node->size <= (char *)heap->memory + heap->capacity); // block isn't from this heap.

	int needed = size + sizeof(union node);
	if (needed > node->size) {
		if (needed > heap->capacity)
			return 0; // allocation doesn't fit in the heap

		// try to merge with neighboring free buddies
		int oldsize = (int)node->size;
		for (;;) {
			// we can only merge with the buddy if we are the "left" buddy
			uintptr_t base = (uintptr_t)heap->memory;
			uintptr_t nodep = (uintptr_t)node - base;
			if (nodep & node->size)
				break; // we are the "right" buddy so we can't merge

			uintptr_t buddyp = nodep ^ node->size;
			union node *buddy = (union node *)(buddyp + base);
			if (!buddy->free)
				break; // buddy isn't free so we can't merge

			// ok we can merge with this buddy
			buddy->next->prev = buddy->prev;
			buddy->prev->next = buddy->next;
			node->size *= 2;

			if (node->size >= needed)
				return block;
		}

		// we couldn't reallocate in-place so undo any growth we've done
		while (node->size > oldsize) {
			node->size /= 2;
			
			void *memory = (char *)node + node->size;
			union node *buddy = memory;
			buddy->free = 1;
			buddy->size = node->size;

			// add buddy back to the freelist
			int log2 = ceillog2((int)node->size);
			union node *list = &heap->freelists[log2];
			buddy->next = list->next;
			buddy->prev = list;
			list->next->prev = buddy;
			list->next = buddy;
		}

		// make a new allocation and copy the old one
		void *copy = allocate(heap, size);
		memcpy(copy, block, (size_t)node->size);
		deallocate(heap, block);
		return copy;
	}
	else {
		// split off as many buddies from the node as we can
		int log2 = ceillog2((int)node->size);
		while ((1 << (log2 - 1)) >= needed) {
			--log2;
			void *memory = (char *)node + ((intptr_t)1 << log2);
			union node *buddy = memory;
			union node *list = &heap->freelists[log2];
			buddy->next = list->next;
			buddy->prev = list;
			list->next->prev = buddy;
			list->next = buddy;
		}
		return block;
	}
}

int main(void) {
	static char memory[1024];
	struct heap heap;
	initialize(&heap, memory, sizeof memory);

	char *a = allocate(&heap, 256); memset(a, 1, 256);
	char *b = allocate(&heap, 256); memset(b, 1, 256);
	deallocate(&heap, a);
	char *c = allocate(&heap, 256); memset(c, 1, 256);
	deallocate(&heap, c);
	deallocate(&heap, b);

	char *d = allocate(&heap, 0); memset(d, 1, 0);
	char *e = allocate(&heap, 1); memset(e, 1, 1);
	char *f = allocate(&heap, 2); memset(f, 1, 2);
	char *g = allocate(&heap, 3); memset(g, 1, 3);
	char *h = allocate(&heap, 4); memset(h, 1, 4);
	char *i = allocate(&heap, 5); memset(i, 1, 5);
	d = reallocate(&heap, d, 256); memset(d, 1, 256);
	i = reallocate(&heap, i, 100); memset(i, 1, 100);
	deallocate(&heap, d);
	deallocate(&heap, i);
	deallocate(&heap, e);
	deallocate(&heap, h);
	deallocate(&heap, f);
	deallocate(&heap, g);

	__debugbreak();
}