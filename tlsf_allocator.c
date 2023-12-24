// O(1) allocation and deallocation
// 1/32 memory wasted on average, good-fit
// 4 byte header
// 32/16 byte min allocation on 64/32-bit
// can be expanded at runtime

#include <stdint.h> // intptr_t
#include <string.h> // memcpy
#include <assert.h>

#define ALIGNMENT 4 // only 4, 8, or 16 allowed
#define FREE_BIT (1 << 0)
#define PREV_FREE_BIT (1 << 1)
#define SIZE_MASK (~(FREE_BIT | PREV_FREE_BIT))

struct node {
	struct node *prevnode; // this is actually at the end of the *previous* node's block, only valid if previous node is free
	int size; // includes size of node, last 2 bits of the are used as bitfields: FREE_BIT | PREV_FREE_BIT
	struct node *next; // only valid if node is free
	struct node *prev; // only valid if node is free
};

struct heap {
	int listmap;
	int slotmaps[32];
	struct node freelists[32][4];
};

void *node2block(struct node *n) {
	return (char *)&(n)->size + ALIGNMENT;
}
struct node *block2node(void* block) {
	return (struct node *)((char *)block - (sizeof(struct node *) + ALIGNMENT));
}
struct node *nextnode(struct node *n) {
	return (struct node *)((char *)n + (n->size & SIZE_MASK));
}

int findfirstset(int x) {
	// _BitScanForward(&i, x) on msvc, __builtin_ffs(x) - 1 on gcc/clang
	for (int i = 0; i < 32; ++i)
		if (x & (1 << i))
			return i;
	return -1;
}
int floorlog2(int x) {
	// _BitScanReverse(&i, x) on msvc, __builtin_fls(x) - 1 on gcc/clang
	for (int i = 31; i >= 0; --i)
		if (x & (1 << i))
			return i;
	return -1;
}

void findslot(int size, int *listid, int *slotid) {
	int log2 = floorlog2(size);
	int pow2 = 1 << log2;
	int left = size - pow2;
	(*listid) = log2;
	(*slotid) = left >> (log2 - 2); // (4 * left) / pow2
}
void add(struct heap *heap, struct node *node, int size) {
	// mark the node as free
	node->size = size | FREE_BIT;

	// write the footer
	struct node *tail = nextnode(node);
	tail->prevnode = node;
	tail->size |= PREV_FREE_BIT;

	// find where the node goes
	int listid, slotid;
	findslot(size, &listid, &slotid);
	struct node *list = &heap->freelists[listid][slotid];

	// add the node to the list
	node->next = list->next;
	node->prev = list;
	list->next->prev = node;
	list->next = node;

	// mark the list and slot as full
	heap->listmap |= (1 << listid);
	heap->slotmaps[listid] |= (1 << slotid);
}
void remove(struct heap *heap, struct node *node) {
	// find where the node goes
	int listid, slotid;
	findslot(node->size, &listid, &slotid);
	struct node *list = &heap->freelists[listid][slotid];
	int *slotmap = &heap->slotmaps[listid];

	// remove the node from the freelist
	assert(node->size & FREE_BIT);
	node->size &= ~FREE_BIT;
	struct node *n = node->next;
	struct node *p = node->prev;
	node->prev->next = n;
	node->next->prev = p;

	// if the slot becomes empty, clear it's bitmap bit
	if (list->next == list)
		(*slotmap) &= ~(1 << slotid);

	// and if the list becomes empty, clear it's bitmap bit too
	if (!(*slotmap))
		heap->listmap &= ~(1 << listid);

	struct node *next = nextnode(node);
	assert(next->size & PREV_FREE_BIT);
	next->size &= ~PREV_FREE_BIT;
}

void expand(struct heap *heap, void *memory, int size) {
	assert(size > sizeof(struct node));
	assert(size % sizeof(struct node) == 0);

	// carve out a sentinel node with just the size flags at the end
	struct node *sentinel = block2node((char *)memory + size);
	sentinel->size = 0;

	// add the root node to the list
	void *p = (char *)memory - sizeof(struct node *);
	struct node *root = p;
	add(heap, root, size - ALIGNMENT);
}
void initialize(struct heap *heap) {
	memset(heap, 0, sizeof(struct heap));

	// clear freelists
	for (int i = 0; i < 32; ++i) {
		for (int j = 0; j < 4; ++j) {
			struct node *list = &heap->freelists[i][j];
			list->next = list;
			list->prev = list;
		}
	}
}
void *allocate(struct heap *heap, int size) {
	assert(size >= 0); // you could clamp to 0, or return NULL

	// need extra space for size and to align allocation
	int needed = size + ALIGNMENT;
	if (needed < sizeof(struct node))
		needed = sizeof(struct node);

	// align up
	needed = (needed + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

	// first check the exact size range for the needed amount
	// special findslot that rounds up instead of down
	int log2 = floorlog2(needed);
	int pow2 = 1 << log2;
	int left = needed - pow2;
	int listid = log2;
	int slotid = left >> (log2 - 2); // (4 * left / pow2)
	if (left) {
		++slotid;
		if (slotid == 4) {
			slotid = 0;
			++listid;
		}
	}

	int slotmask = ~((1 << slotid) - 1);
	if (!(heap->slotmaps[listid] & slotmask)) {
		// the best fitting size range is empty so don't consider it
		++listid;
		slotmask = 0xFFFFFFFF;
	}

	// find first free node big enough to hold the allocation
	int listmask = ~((1 << listid) - 1);
	int listmap = heap->listmap & listmask;
	listid = findfirstset(listmap);
	if (listid < 0)
		return 0; // out of memory

	int slotmap = heap->slotmaps[listid] & slotmask;
	slotid = findfirstset(slotmap);

	// remove the node from the freelist
	struct node *list = &heap->freelists[listid][slotid];
	struct node *node = list->next;
	assert(node->size >= needed);
	remove(heap, node);

	// trim the excess off
	int excess = node->size - needed;
	if (excess >= sizeof(struct node)) {
		node->size -= excess;
		struct node *leftover = nextnode(node);
		add(heap, leftover, excess);
	}

	return node2block(node);
}
void deallocate(struct heap *heap, void *block) {
	if (!block)
		return;

	struct node *node = block2node(block);
	assert(!(node->size & FREE_BIT)); // double free

	// merge with previous free node
	if (node->size & PREV_FREE_BIT) {
		struct node *prev = node->prevnode;
		assert(prev->size & FREE_BIT); // we think it's free but it disagrees
		assert(!(prev->size & PREV_FREE_BIT)); // there shouldn't be 2 consecutive free nodes
		remove(heap, prev);
		prev->size += (node->size & SIZE_MASK);
		node = prev;
		assert(!(node->size & PREV_FREE_BIT)); // 2 consecutive free nodes.
	}

	// merge with next free node
	struct node *next = nextnode(node);
	if (next->size & FREE_BIT) {
		assert(!(next->size & PREV_FREE_BIT)); // next node thinks we're free but we aren't
		remove(heap, next);
		node->size += next->size;
		next = nextnode(node);
		assert(!(next->size & FREE_BIT)); // 2 consecutive free nodes
	}

	// mark on the next node that we are free
	assert(!(next->size & PREV_FREE_BIT)); // corruption
	next->size |= PREV_FREE_BIT;

	add(heap, node, node->size);
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

	struct node *node = block2node(block);
	assert(!(node->size & FREE_BIT)); // use after free

	// need extra space for size and to align allocation
	int needed = size + ALIGNMENT;
	if (needed < sizeof(struct node))
		needed = sizeof(struct node);

	// align up
	needed = (needed + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

	if (needed > (node->size & SIZE_MASK)) {
		// we need to grow, try expanding into the next block if it's free
		struct node *next = nextnode(node);
		assert(!(next->size & PREV_FREE_BIT)); // mistake, this node is not really free

		if (!(next->size & FREE_BIT) || (node->size & SIZE_MASK) + (next->size & SIZE_MASK) < needed) {
			// bad luck, we can't grow in-place
			void *copy = allocate(heap, size);
			if (!copy)
				return 0; // out of memory
			memcpy(copy, block, (size_t)node->size - ALIGNMENT);
			deallocate(heap, block);
			return copy;
		}

		// good luck! we can grow in place
		remove(heap, next);
		node->size += next->size;
	}

	// trim off any excess
	int excess = (node->size & SIZE_MASK) - needed;
	if (excess >= sizeof(struct node)) {
		node->size -= excess;
		struct node *left = nextnode(node);
		left->size = excess;
		// merge with next free node
		struct node *next = nextnode(left);
		if (next->size & FREE_BIT) {
			remove(heap, next);
			left->size += (next->size & SIZE_MASK);
		}
		add(heap, left, left->size);
	}

	return block;
}

void verify(struct heap *heap) {
	// if a slotmap isn't empty the corresponding listmap bit should be set
	for (int i = 0; i < 32; ++i) {
		int slotmap = heap->slotmaps[i] != 0;
		int listmap = (heap->listmap & (1 << i)) != 0;
		assert(slotmap == listmap);
	}

	// the bitmaps should correspond to which freelists are empty
	for (int i = 0; i < 32; ++i) {
		int slotmap = heap->slotmaps[i];
		for (int j = 0; j < 4; ++j) {
			struct node *list = &heap->freelists[i][j];
			if (slotmap & (1 << j)) {
				assert(list->next != list);
				assert(list->prev != list);
			}
		}
	}

	for (int i = 0; i < 32; ++i) {
		for (int j = 0; j < 4; ++j) {
			struct node *list = &heap->freelists[i][j];
			int k = 0;
			for (struct node *node = list->next; node != list; node = node->next) {
				// every node in the freelist should be free
				assert(node->size & FREE_BIT);

				// the next node needs to know if we're free
				struct node *next = nextnode(node);
				assert(next->size & PREV_FREE_BIT);

				// there should never be 2 consecutive free nodes - they should be combined
				assert(!(node->size & PREV_FREE_BIT));
				assert(!(next->size & FREE_BIT));

				// the node should be properly aligned.
				uintptr_t block = (uintptr_t)node2block(node);
				uintptr_t nextblock = (uintptr_t)node2block(next);
				assert(block % ALIGNMENT == 0);
				assert(nextblock % ALIGNMENT == 0);

				++k;
			}
		}
	}
}
int equal(char *bytes, char value, int count) {
	assert(bytes);
	for (int i = 0; i < count; ++i)
		if (bytes[i] != value)
			return 0;
	return 1;
}

int main(void) {
	struct heap heap;
	initialize(&heap);

	static char memory[1024];
	expand(&heap, memory, sizeof memory);

	char *a = allocate(&heap, 256); verify(&heap); memset(a, 1, 256);
	char *b = allocate(&heap, 256); verify(&heap); memset(b, 2, 256);
	assert(equal(a, 1, 256));
	deallocate(&heap, a); verify(&heap);
	char *c = allocate(&heap, 256); verify(&heap); memset(c, 3, 256);
	deallocate(&heap, c); verify(&heap);
	assert(equal(b, 2, 256));
	deallocate(&heap, b); verify(&heap);
	
	char *d = allocate(&heap, 0); verify(&heap); memset(d, 4, 0);
	char *e = allocate(&heap, 1); verify(&heap); memset(e, 5, 1);
	char *f = allocate(&heap, 2); verify(&heap); memset(f, 6, 2);
	char *g = allocate(&heap, 3); verify(&heap); memset(g, 7, 3);
	char *h = allocate(&heap, 4); verify(&heap); memset(h, 8, 4);
	char *i = allocate(&heap, 5); verify(&heap); memset(i, 9, 5);
	char *j = allocate(&heap, 23); verify(&heap); memset(j, 10, 23);
	i = reallocate(&heap, i, 100); verify(&heap); memset(i, 11, 100);
	d = reallocate(&heap, d, 256); verify(&heap); memset(d, 12, 256);
	i = reallocate(&heap, i, 5); verify(&heap); memset(i, 13, 5);
	assert(equal(d, 12, 256));
	assert(equal(e, 5, 1));
	assert(equal(f, 6, 2));
	assert(equal(g, 7, 3));
	assert(equal(h, 8, 4));
	assert(equal(i, 13, 5));
	assert(equal(j, 10, 23));
	
	deallocate(&heap, d); verify(&heap);
	deallocate(&heap, i); verify(&heap);
	deallocate(&heap, e); verify(&heap);
	deallocate(&heap, h); verify(&heap);
	deallocate(&heap, f); verify(&heap);
	deallocate(&heap, g); verify(&heap);
	deallocate(&heap, j); verify(&heap);

	// stress tests

	int maxsize = 500;
	char *x = NULL;

	// one up
	for (int size = 0; size < maxsize; ++size) {
		x = reallocate(&heap, x, size); verify(&heap);
		assert(size == 0 || equal(x, size - 1, size - 1));
		memset(x, size, size);
		verify(&heap);
	}
	x = reallocate(&heap, x, 0);
	verify(&heap);

	// one down
	for (int size = 0; size < maxsize; ++size) {
		int ezis = maxsize - size;
		x = reallocate(&heap, x, ezis); verify(&heap);
		assert(size == 0 || equal(x, size - 1, ezis));
		memset(x, size, ezis);
		verify(&heap);
	}
	x = reallocate(&heap, x, 0);
	verify(&heap);

	// grow

	static char extra[1024];
	expand(&heap, extra, sizeof extra);
	char *y = NULL;

	// both up
	for (int size = 0; size < maxsize; ++size) {
		verify(&heap);
		x = reallocate(&heap, x, size); verify(&heap);
		assert(size == 0 || equal(x, size - 1, size - 1));
		assert(size == 0 || equal(y, size - 1, size - 1));
		y = reallocate(&heap, y, size); verify(&heap);
		assert(size == 0 || equal(x, size - 1, size - 1));
		assert(size == 0 || equal(y, size - 1, size - 1));
		memset(x, size, size);
		memset(y, size, size);
		verify(&heap);
	}
	x = reallocate(&heap, x, 0);
	y = reallocate(&heap, y, 0);
	verify(&heap);

	// both down
	for (int size = 0; size < maxsize; ++size) {
		int ezis = maxsize - size;
		x = reallocate(&heap, x, ezis); verify(&heap);
		assert(size == 0 || equal(x, size - 1, ezis));
		assert(size == 0 || equal(y, size - 1, ezis + 1));
		y = reallocate(&heap, y, ezis); verify(&heap);
		assert(size == 0 || equal(x, size - 1, ezis));
		assert(size == 0 || equal(y, size - 1, ezis));
		memset(x, size, ezis);
		memset(y, size, ezis);
		verify(&heap);
	}
	x = reallocate(&heap, x, 0);
	y = reallocate(&heap, y, 0);
	verify(&heap);

	// one up, one down
	for (int size = 0; size < maxsize; ++size) {
		int ezis = maxsize - size;
		x = reallocate(&heap, x, size); verify(&heap);
		assert(size == 0 || equal(x, size - 1, size - 1));
		assert(size == 0 || equal(y, size - 1, ezis + 1));
		y = reallocate(&heap, y, ezis); verify(&heap);
		assert(size == 0 || equal(x, size - 1, size - 1));
		assert(size == 0 || equal(y, size - 1, ezis));
		memset(x, size, size);
		memset(y, size, ezis);
		verify(&heap);
	}
	x = reallocate(&heap, x, 0);
	y = reallocate(&heap, y, 0);
	verify(&heap);
}
