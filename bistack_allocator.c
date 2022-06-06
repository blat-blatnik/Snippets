#include <stdint.h> // uintptr_t
#include <string.h> // memcpy - only needed for realloc

struct allocator {
	void *buffer;
	int capacity;
	int lcursor;
	int rcursor;
};

void *allocate_left(struct allocator *allocator, int size, int alignment) {
	uintptr_t mask = (uintptr_t)alignment - 1; // Alignment must be a power of 2.
	uintptr_t unaligned = (uintptr_t)allocator->buffer + allocator->lcursor;
	uintptr_t aligned = (unaligned + mask) & ~mask;
	int new_lcursor = allocator->lcursor + size + (int)(aligned - unaligned);
	if (new_lcursor >= allocator->capacity - allocator->rcursor)
		return 0;

	allocator->lcursor = new_lcursor;
	return (void *)aligned;
}

void *allocate_right(struct allocator *allocator, int size, int alignment) {
	uintptr_t mask = (uintptr_t)alignment - 1; // Alignment must be a power of 2.
	uintptr_t unaligned = (uintptr_t)allocator->buffer + allocator->capacity - allocator->rcursor - size - alignment;
	uintptr_t aligned = (unaligned + alignment) & ~mask;
	int new_rcursor = allocator->rcursor + size + (int)(unaligned + alignment - aligned);
	if (allocator->lcursor >= allocator->capacity - new_rcursor)
		return 0;

	allocator->rcursor = new_rcursor;
	return (void *)aligned;
}

void deallocate_left(struct allocator *allocator, void *block, int size) {
	if ((char *)block + size == (char *)allocator->buffer + allocator->lcursor)
		allocator->lcursor -= size;
}

void deallocate_right(struct allocator *allocator, void *block, int size) {
	if (block == (char *)allocator->buffer + allocator->capacity - allocator->rcursor)
		allocator->rcursor -= size;
}

void *reallocate_left(struct allocator *allocator, void *block, int old_size, int new_size, int alignment) {
	uintptr_t mask = (uintptr_t)alignment - 1;
	if ((char *)block + old_size == (char *)allocator->buffer + allocator->lcursor && ((uintptr_t)block & mask) == 0) {
		int new_lcursor = allocator->lcursor + new_size - old_size;
		if (new_lcursor >= allocator->capacity - allocator->rcursor)
			return 0;
		allocator->lcursor = new_lcursor;
		return block;
	}

	void *result = allocate_left(allocator, new_size, alignment);
	if (result) {
		int to_copy = new_size < old_size ? new_size : old_size;
		memcpy(result, block, (size_t)to_copy);
	}
	return result;
}

void *reallocate_right(struct allocator *allocator, void *block, int old_size, int new_size, int alignment) {
	uintptr_t mask = (uintptr_t)alignment - 1;
	if (block == (char *)allocator->buffer + allocator->capacity - allocator->rcursor && ((uintptr_t)block & mask) == 0) {
		int new_rcursor = allocator->rcursor + new_size - old_size;
		if (allocator->lcursor >= allocator->capacity - new_rcursor)
			return 0;
		allocator->rcursor = new_rcursor;
		return block;
	}

	void *result = allocate_right(allocator, new_size, alignment);
	if (result) {
		int to_copy = new_size < old_size ? new_size : old_size;
		memcpy(result, block, (size_t)to_copy);
	}
	return result;
}

#include <assert.h>
int main(void) {
	{
		struct allocator allocator = { 0 };
		assert(!allocate_left(&allocator, 1, 1));
		assert(!allocate_right(&allocator, 1, 1));
		assert(!reallocate_left(&allocator, 0, 0, 1, 1));
		assert(!reallocate_right(&allocator, 0, 0, 1, 1));
		deallocate_left(&allocator, NULL, 0);
		deallocate_right(&allocator, NULL, 0);
	}

	{
		_Alignas(16) char buffer[17];
		struct allocator allocator = { .buffer = buffer, .capacity = sizeof buffer };

		char *c = allocate_left(&allocator, sizeof(char), _Alignof(char));
		short *s = allocate_left(&allocator, sizeof(short), _Alignof(short));
		int *i = allocate_left(&allocator, sizeof(int), _Alignof(int));
		long long *l = allocate_left(&allocator, sizeof(long long), _Alignof(long long));
		long long *null = allocate_left(&allocator, sizeof(long long), _Alignof(long long));
		assert(c && (uintptr_t)c % _Alignof(char) == 0);
		assert(s && (uintptr_t)s % _Alignof(short) == 0);
		assert(i && (uintptr_t)i % _Alignof(int) == 0);
		assert(l && (uintptr_t)l % _Alignof(long long) == 0);
		assert(!null);
	}

	{
		_Alignas(16) char buffer[23];
		struct allocator allocator = { .buffer = buffer, .capacity = sizeof buffer };

		char *c = allocate_right(&allocator, sizeof(char), _Alignof(char));
		short *s = allocate_right(&allocator, sizeof(short), _Alignof(short));
		int *i = allocate_right(&allocator, sizeof(int), _Alignof(int));
		long long *l = allocate_right(&allocator, sizeof(long long), _Alignof(long long));
		long long *null = allocate_right(&allocator, sizeof(long long), _Alignof(long long));
		assert(c && (uintptr_t)c % _Alignof(char) == 0);
		assert(s && (uintptr_t)s % _Alignof(short) == 0);
		assert(i && (uintptr_t)i % _Alignof(int) == 0);
		assert(l && (uintptr_t)l % _Alignof(long long) == 0);
		assert(!null);
	}

	{
		_Alignas(16) char buffer[40];
		struct allocator allocator = { .buffer = buffer, .capacity = sizeof buffer };

		char *lc = allocate_left(&allocator, sizeof(char), _Alignof(char));
		char *rc = allocate_right(&allocator, sizeof(char), _Alignof(char));
		short *ls = allocate_left(&allocator, sizeof(short), _Alignof(short));
		short *rs = allocate_right(&allocator, sizeof(short), _Alignof(short));
		int *li = allocate_left(&allocator, sizeof(int), _Alignof(int));
		int *ri = allocate_right(&allocator, sizeof(int), _Alignof(int));
		long long *ll = allocate_left(&allocator, sizeof(long long), _Alignof(long long));
		long long *rl = allocate_right(&allocator, sizeof(long long), _Alignof(long long));
		long long *lnull = allocate_left(&allocator, sizeof(long long), _Alignof(long long));
		long long *rnull = allocate_right(&allocator, sizeof(long long), _Alignof(long long));
		assert(lc && (uintptr_t)lc % _Alignof(char) == 0);
		assert(rc && (uintptr_t)rc % _Alignof(char) == 0);
		assert(ls && (uintptr_t)ls % _Alignof(short) == 0);
		assert(rs && (uintptr_t)rs % _Alignof(short) == 0);
		assert(li && (uintptr_t)li % _Alignof(int) == 0);
		assert(ri && (uintptr_t)ri % _Alignof(int) == 0);
		assert(ll && (uintptr_t)ll % _Alignof(long long) == 0);
		assert(rl && (uintptr_t)rl % _Alignof(long long) == 0);
		assert(!lnull);
		assert(!rnull);
	}

	{
		char buffer[3];
		struct allocator allocator = { .buffer = buffer, .capacity = sizeof buffer };
		char *l = allocate_left(&allocator, 1, 1);
		char *r = allocate_right(&allocator, 1, 1);
		assert(l && r && l != r);
	}

	{
		_Alignas(8) char buffer[17];
		struct allocator allocator = { .buffer = buffer + 1, .capacity = sizeof buffer - 1 };
		char *c = reallocate_left(&allocator, NULL, 0, sizeof(char), _Alignof(char));
		assert(c && (uintptr_t)c % _Alignof(char) == 0);
		short *s = reallocate_left(&allocator, c, sizeof(char), sizeof(short), _Alignof(short));
		assert(s && (uintptr_t)s % _Alignof(short) == 0);
		int *i = reallocate_left(&allocator, s, sizeof(short), sizeof(int), _Alignof(int));
		assert(i && (uintptr_t)i % _Alignof(int) == 0);
		long *l = reallocate_left(&allocator, i, sizeof(int), sizeof(long long), _Alignof(long long));
		assert(l && (uintptr_t)l % _Alignof(long long) == 0);
		int mark = allocator.lcursor;
		deallocate_left(&allocator, l, sizeof(long long));
		assert(mark - allocator.lcursor >= sizeof(long long));
	}

	{
		_Alignas(8) char buffer[32];
		struct allocator allocator = { .buffer = buffer, .capacity = sizeof buffer };
		char *c = reallocate_right(&allocator, NULL, 0, sizeof(char), _Alignof(char));
		assert(c && (uintptr_t)c % _Alignof(char) == 0);
		short *s = reallocate_right(&allocator, c, sizeof(char), sizeof(short), _Alignof(short));
		assert(s && (uintptr_t)s % _Alignof(short) == 0);
		int *i = reallocate_right(&allocator, s, sizeof(short), sizeof(int), _Alignof(int));
		assert(i && (uintptr_t)i % _Alignof(int) == 0);
		long *l = reallocate_right(&allocator, i, sizeof(int), sizeof(long long), _Alignof(long long));
		assert(l && (uintptr_t)l % _Alignof(long long) == 0);
		int mark = allocator.rcursor;
		deallocate_right(&allocator, l, sizeof(long long));
		assert(mark - allocator.rcursor >= sizeof(long long));
	}
}