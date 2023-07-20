// O(1) allocation and deallocation
// 1/32 memory wasted on average, good-fit
// 4 byte header
// 32/16 byte min allocation on 64/32-bit
// can be expanded at runtime

#include <stdint.h> // intptr_t
#include <string.h> // memcpy
#include <assert.h>

#define ALIGNMENT 8 // only 4, 8, or 16 allowed
#define FREE_BIT (1 << 0)
#define PREV_FREE_BIT (1 << 1)
#define SIZE_MASK (~(FREE_BIT | PREV_FREE_BIT))

struct node {
	struct node *prevnode; // this is actually at the end of the *previous* node's block
	int size; // last 2 bits of the size are used as bitfields: FREE_BIT | PREV_FREE_BIT
	struct node *next; // only valid if node is free
	struct node *prev; // only valid if node is free
};

struct heap {
	int listmap;
	int slotmaps[32];
	struct node freelists[32][4];
};

#define block2node(block) ((struct node *)((char *)(block) - (sizeof(struct node *) + ALIGNMENT)))
#define node2block(n) ((char *)&(n)->size + ALIGNMENT)
#define nextnode(n) ((struct node *)((char *)&(n)->size + ((n)->size & SIZE_MASK) - sizeof(struct node *)))

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
	(*slotid) = left >> (log2 - 2); // (4 * left) / size
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
	node->size &= ~FREE_BIT;
	node->prev->next = node->next;
	node->next->prev = node->prev;

	// if the slot becomes empty, clear it's bitmap bit
	if (list->next == list)
		(*slotmap) &= ~(1 << slotid);

	// and if the list becomes empty, clear it's bitmap bit too
	if (!(*slotmap))
		heap->listmap &= ~(1 << listid);

	// unlink the node
	node->prev->next = node->next;
	node->next->prev = node->prev;

	struct node *next = nextnode(node);
	next->size &= ~PREV_FREE_BIT;
}

void expand(struct heap *heap, void *memory, int size) {
	assert(size > sizeof(struct node));
	assert(size % sizeof(struct node) == 0);

	// carve out a dummy node at the end
	void *end = (char *)memory + size - sizeof(struct node) + sizeof(struct node *);
	struct node *footer = end;
	footer->size = 0;
	footer->next = 0;
	footer->prev = 0;

	// add the root node to the list
	void *p = (char *)memory - sizeof(struct node *);
	struct node *root = p;
	add(heap, root, size - sizeof(struct node));
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

	int needed = size + ALIGNMENT;
	if (needed < sizeof(struct node))
		needed = sizeof(struct node);

	// first check the exact size range for the needed amount
	int listid, slotid;
	findslot(needed, &listid, &slotid);
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
	}

	// merge with next free node
	struct node *next = nextnode(node);
	if (next->size & FREE_BIT) {
		remove(heap, next);
		node->size += (next->size & SIZE_MASK);
		next->size = 0;
		next = nextnode(node);
	}

	// mark on the next node that we are free
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

	int needed = size + ALIGNMENT;
	if (needed < sizeof(struct node))
		needed = sizeof(struct node);

	if (needed > (node->size & SIZE_MASK)) {
		// we need to grow, try expanding into the next block if it's free
		struct node *next = nextnode(node);
		assert(!(next->size & PREV_FREE_BIT)); // mistake, this node is not really free

		if (!(next->size & FREE_BIT) || (node->size & SIZE_MASK) + (next->size & SIZE_MASK) < needed) {
			// bad luck, we can't grow in-place
			void *copy = allocate(heap, size);
			if (!copy)
				return 0; // out of memory
			memcpy(copy, block, (size_t)node->size);
			deallocate(heap, block);
			return copy;
		}

		// good luck! we can grow in place
		remove(heap, next);
		node->size += next->size;
	}

	// trim off any excess
	int excess = node->size - needed;
	if (excess >= sizeof(struct node)) {
		node->size -= excess;
		struct node *left = nextnode(node);
		left->size = excess;
		// merge with next free node
		struct node *next = nextnode(left);
		if (next->size & FREE_BIT) {
			remove(heap, next);
			left->size += (next->size & SIZE_MASK);
			next->size = 0;
		}
		add(heap, left, excess);
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
			for (struct node *node = list->next; node != list; node = node->next) {
				// every node in the freelist should be free
				assert(node->size & FREE_BIT);

				// the next node needs to know if we're free
				struct node *next = nextnode(node);
				assert(next->size & PREV_FREE_BIT);

				// there should never be 2 consecutive free nodes - they should be combined
				assert(!(node->size & PREV_FREE_BIT));
				assert(!(next->size & FREE_BIT));
			}
		}
	}
}

int main(void) {
	struct heap heap;
	initialize(&heap);

	static char memory[1024];
	expand(&heap, memory, sizeof memory);

	char *a = allocate(&heap, 256); verify(&heap); memset(a, 1, 256);
	char *b = allocate(&heap, 256); verify(&heap); memset(b, 1, 256);
	deallocate(&heap, a); verify(&heap);
	char *c = allocate(&heap, 256); verify(&heap); memset(c, 1, 256);
	deallocate(&heap, c); verify(&heap);
	deallocate(&heap, b); verify(&heap);

	char *d = allocate(&heap, 0); verify(&heap); memset(d, 1, 0);
	char *e = allocate(&heap, 1); verify(&heap); memset(e, 1, 1);
	char *f = allocate(&heap, 2); verify(&heap); memset(f, 1, 2);
	char *g = allocate(&heap, 3); verify(&heap); memset(g, 1, 3);
	char *h = allocate(&heap, 4); verify(&heap); memset(h, 1, 4);
	char *i = allocate(&heap, 5); verify(&heap); memset(i, 1, 5);
	i = reallocate(&heap, i, 100); verify(&heap); memset(i, 1, 100);
	d = reallocate(&heap, d, 256); verify(&heap); memset(d, 1, 256);
	i = reallocate(&heap, i, 5); verify(&heap); memset(i, 1, 5);
	deallocate(&heap, d); verify(&heap);
	deallocate(&heap, i); verify(&heap);
	deallocate(&heap, e); verify(&heap);
	deallocate(&heap, h); verify(&heap);
	deallocate(&heap, f); verify(&heap);
	deallocate(&heap, g); verify(&heap);
}
