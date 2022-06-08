#include <stdlib.h> // malloc, free, size_t
#include <string.h> // memcpy

#define SLAB_SIZE (64*1024)

struct allocator {
	struct slab *slab;
	int cursor;
};

struct slab {
	struct slab *prev;
	struct slab *next;
	void *memory;
	int capacity;
	int cursor;
};

void *allocate(struct allocator *allocator, int size, int alignment) {
	size_t mask = (size_t)alignment - 1;
	for (;;) {
		size_t unaligned = (size_t)allocator->slab->memory + allocator->slab->cursor;
		size_t aligned = (unaligned + mask) & ~mask;
		int needed = size + (int)(aligned - unaligned);
		int remaining = allocator->slab->capacity - allocator->slab->cursor;
		if (needed <= remaining) {
			allocator->slab->cursor += needed;
			allocator->cursor += needed;
			return (void *)aligned;
		}

		struct slab *next = allocator->slab->next;
		if (!next) {
			int worst_case = size + alignment - 1;
			int consecutive_slabs = (worst_case + SLAB_SIZE - 1) / SLAB_SIZE;
			int capacity = consecutive_slabs * SLAB_SIZE;
			next = malloc(sizeof next[0] + capacity);
			next->prev = allocator->slab;
			next->next = NULL;
			next->memory = next + 1;
			next->capacity = capacity;
			next->cursor = 0;
			allocator->slab->next = next;
		}

		allocator->cursor += remaining;
		allocator->slab->cursor += remaining;
		allocator->slab = next;
	}
}

void deallocate(struct allocator *allocator, void *block, int size) {
	char *end = (char *)block + size;
	char *top = (char *)allocator->slab->memory + allocator->slab->cursor;
	if (end == top) {
		allocator->slab->cursor -= size;
		allocator->cursor -= size;
	}
}

void *reallocate(struct allocator *allocator, void *block, int old_size, int new_size, int alignment) {
	size_t mask = (size_t)alignment - 1;
	if (!((size_t)block & mask)) {
		char *end = (char *)block + old_size;
		char *top = (char *)allocator->slab->memory + allocator->slab->cursor;
		int delta = new_size - old_size;
		if (end == top && allocator->slab->cursor + delta <= allocator->slab->capacity) {
			allocator->slab->cursor += delta;
			allocator->cursor += delta;
			return block;
		}
		if (new_size < old_size)
			return block;
	}

	void *copy = allocate(allocator, new_size, alignment);
	int to_copy = new_size;
	if (to_copy > old_size)
		to_copy = old_size;
	memcpy(copy, block, (size_t)to_copy);
	return copy;
}

void reset(struct allocator *allocator, int cursor) {
	for (;;) {
		int remaining = allocator->cursor - cursor;
		if (remaining <= allocator->slab->cursor) {
			allocator->slab->cursor -= remaining;
			allocator->cursor = cursor;
			return;
		}

		allocator->cursor -= allocator->slab->cursor;
		allocator->slab->cursor = 0;
		if (allocator->slab->prev)
			allocator->slab = allocator->slab->prev;
	}
}

void trim(struct allocator *allocator) {
	struct slab *slab = allocator->slab->next;
	allocator->slab->next = NULL;
	while (slab) {
		struct slab *next = slab->next;
		free(slab);
		slab = next;
	}
}

void destroy(struct allocator *allocator) {
	struct slab *slab = allocator->slab;
	while (slab->prev)
		slab = slab->prev;
	while (slab) {
		struct slab *next = slab->next;
		if (slab->capacity)
			free(slab);
		slab = next;
	}
}

#include <assert.h>
int main(void) {
	{
		struct allocator allocator = { .slab = &(struct slab) { 0 } };
		// None of these should crash.
		for (int i = 0; i < 2; ++i) {
			allocate(&allocator, 0, 1);
			assert(allocator.cursor == 0);
			reallocate(&allocator, NULL, 0, 0, 1);
			assert(allocator.cursor == 0);
			deallocate(&allocator, NULL, 0);
			assert(allocator.cursor == 0);
			trim(&allocator);
			destroy(&allocator);
		}
	}

	{
		struct allocator allocator = { .slab = &(struct slab) { 0 } };

		assert(allocator.cursor == 0);
		int ne = 999;
		int nb = ne * sizeof(int);
		int *a = allocate(&allocator, nb, _Alignof(int));
		int marka = allocator.cursor;
		assert(marka >= nb && marka < nb + _Alignof(int) && marka == allocator.cursor);
		for (int i = 0; i < ne; ++i)
			a[i] = i;

		int *b = allocate(&allocator, nb, _Alignof(int));
		int markb = allocator.cursor;
		assert(markb >= 2 * nb && markb < 2 * (nb + _Alignof(int)));
		for (int i = 0; i < ne; ++i) {
			assert(a[i] == i);
			b[i] = 2 * i;
		}

		int *c = allocate(&allocator, nb, _Alignof(int));
		int markc = allocator.cursor;
		assert(markc >= 3 * nb && markc < 3 * (nb + _Alignof(int)));
		for (int i = 0; i < ne; ++i) {
			assert(a[i] == i);
			assert(b[i] == 2 * i);
			c[i] = 3 * i;
		}

		int mark = allocator.cursor;
		int *d = reallocate(&allocator, a, nb, 2 * nb, _Alignof(int));
		int markd = allocator.cursor;
		assert(markd >= 5 * nb && markd < 5 * nb + 4 * _Alignof(int));
		for (int i = 0; i < ne; ++i) {
			assert(a[i] == i);
			assert(b[i] == 2 * i);
			assert(c[i] == 3 * i);
			assert(d[i] == i);
		}
		for (int i = ne; i < 2 * ne; ++i)
			d[i] = i;

		int *e = reallocate(&allocator, d, 2 * nb, 3 * nb, _Alignof(int));
		int marke = allocator.cursor;
		assert(marke >= 6 * nb && marke < 6 * nb + 4 * _Alignof(int));
		assert(e == d);
		for (int i = 0; i < 2 * ne; ++i)
			assert(e[i] == i);
		for (int i = 2 * ne; i < 3 * ne; ++i)
			e[i] = i;

		int mark1 = allocator.cursor;
		deallocate(&allocator, c, nb);
		assert(allocator.cursor == mark1);
		for (int i = 0; i < ne; ++i) {
			assert(a[i] == i);
			assert(b[i] == 2 * i);
		}
		for (int i = 0; i < 3 * ne; ++i)
			assert(e[i] == i);

		reset(&allocator, mark);
		assert(allocator.cursor == mark);
		for (int i = 0; i < ne; ++i)
			assert(b[i] == 2 * i);

		reset(&allocator, 0);
		assert(allocator.cursor == 0);

		char *f = allocate(&allocator, SLAB_SIZE + 1024, 1);
		int fmark = allocator.cursor;
		assert(fmark >= 2 * SLAB_SIZE + 1024 && fmark <= 2 * SLAB_SIZE + 1025);
		memset(f, 'f', SLAB_SIZE + 1024);

		char *g = allocate(&allocator, 2 * SLAB_SIZE + 1024, 1);
		int gmark = allocator.cursor;
		assert(gmark >= 5 * SLAB_SIZE + 1024 && gmark < 5 * SLAB_SIZE + 1024 + 64);
		memset(g, 'g', 2 * SLAB_SIZE + 1024);

		char *h = reallocate(&allocator, f, SLAB_SIZE + 1024, 3 * SLAB_SIZE + 1024, 1);
		int hmark = allocator.cursor;
		for (int i = 0; i < SLAB_SIZE + 1024; ++i)
			assert(f[i] == 'f');
		for (int i = 0; i < 2 * SLAB_SIZE + 1024; ++i)
			assert(g[i] == 'g');
		for (int i = 0; i < SLAB_SIZE + 1024; ++i)
			assert(h[i] == 'f');
		memset(h, 'h', 3 * SLAB_SIZE + 1024);

		deallocate(&allocator, h, 3 * SLAB_SIZE + 1024);
		assert(allocator.cursor >= gmark && allocator.cursor < hmark);

		char *k = allocate(&allocator, SLAB_SIZE, 1);
		assert(k == h);
		memset(k, 'k', SLAB_SIZE);

		reset(&allocator, gmark);
		assert(allocator.cursor == gmark);

		allocate(&allocator, 2 * SLAB_SIZE, 2);
		reset(&allocator, 0);
		assert(allocator.cursor == 0);

		for (int i = 0; i < 1000; ++i) {
			for (int align = 2048; align >= 1; align /= 2) {
				void *ptr = allocate(&allocator, 1, align);
				assert(!((size_t)ptr & (size_t)(align - 1)));
			}
		}

		reset(&allocator, 0);
		assert(allocator.cursor == 0);
		trim(&allocator);
		destroy(&allocator);
	}
}