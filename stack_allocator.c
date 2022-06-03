#include <stdint.h> // uintptr_t
#include <string.h> // memcpy

struct allocator {
	void *buffer;
	int capacity;
	int cursor;
};

void *allocate(struct allocator *allocator, int size, int alignment) {
	uintptr_t mask = (uintptr_t)alignment - 1; // Alignment must be a power of 2.
	uintptr_t unaligned = (uintptr_t)allocator->buffer + allocator->cursor;
	uintptr_t aligned = (unaligned + mask) & ~mask;
	int new_cursor = allocator->cursor + size + (int)(aligned - unaligned);
	if (new_cursor > allocator->capacity)
		return 0;

	allocator->cursor = new_cursor;
	return (void *)aligned;
}

void deallocate(struct allocator *allocator, void *block, int size) {
	if (block && (char *)block + size == (char *)allocator->buffer + allocator->cursor)
		allocator->cursor -= size;
}

void *reallocate(struct allocator *allocator, void *block, int old_size, int new_size, int alignment) {
	uintptr_t mask = (uintptr_t)alignment - 1;
	if (block && (char *)block + old_size == (char *)allocator->buffer + allocator->cursor && ((uintptr_t)block & mask) == 0) {
		int new_cursor = allocator->cursor + new_size - old_size;
		if (new_cursor > allocator->capacity)
			return 0;
		allocator->cursor = new_cursor;
		return block;
	}

	void *result = allocate(allocator, new_size, alignment);
	if (result) {
		int to_copy = new_size < old_size ? new_size : old_size;
		memcpy(result, block, (size_t)to_copy);
	}
	return result;
}

#include <assert.h>
int main(void) {
	struct allocator allocator = { 0 };
	assert(!allocate(&allocator, 1, 1));
	assert(!allocate(&allocator, 1, 1));
	deallocate(&allocator, 0, 0);
	assert(!reallocate(&allocator, 0, 0, 1, 1));

	char buffer[16];
	allocator = (struct allocator){ .buffer = buffer, .capacity = sizeof buffer };
	char *c = allocate(&allocator, sizeof(char), _Alignof(char));
	short *s = allocate(&allocator, sizeof(short), _Alignof(short));
	int *i = allocate(&allocator, sizeof(int), _Alignof(int));
	long long *l = allocate(&allocator, sizeof(long long), _Alignof(long long));
	long long *null = allocate(&allocator, sizeof(long long), _Alignof(long long));
	assert(c && (uintptr_t)c % _Alignof(char) == 0);
	assert(s && (uintptr_t)s % _Alignof(short) == 0);
	assert(i && (uintptr_t)i % _Alignof(int) == 0);
	assert(l && (uintptr_t)l % _Alignof(long long) == 0);
	assert(!null);

	deallocate(&allocator, l, sizeof(long long));
	l = allocate(&allocator, sizeof(long long), _Alignof(long long));
	assert(l);

	deallocate(&allocator, l, sizeof(long long));
	deallocate(&allocator, i, sizeof(int));
	int *ints = allocate(&allocator, 3 * sizeof(int), _Alignof(int));
	assert(ints);
	ints[0] = ints[1] = ints[2] = 42;

	long long big_buffer[1024];
	allocator = (struct allocator){ .buffer = big_buffer, .capacity = sizeof big_buffer };
	assert(!allocate(&allocator, 1024 * sizeof(long long) + 1, 1));
	l = allocate(&allocator, 1024 * sizeof(long long), _Alignof(long long));
	assert(l);
	deallocate(&allocator, l, 1024 * sizeof(long long));
	l = allocate(&allocator, 1024 * sizeof(long long), _Alignof(long long));
	assert(l);
	
	i = reallocate(&allocator, l, 1024 * sizeof(long long), 0, 1);
	assert(allocator.cursor == 0);
	i = reallocate(&allocator, i, 0, sizeof(int), _Alignof(int));
	*i = 42;
	assert(allocator.cursor == sizeof(int));
	i = reallocate(&allocator, i, sizeof(int), 10 * sizeof(int), _Alignof(int));
	assert(allocator.cursor == 10 * sizeof(int));
	i = reallocate(&allocator, i, 10 * sizeof(int), 2048 * sizeof(int), _Alignof(int));
	assert(allocator.cursor == 2048 * sizeof(int));
	i = reallocate(&allocator, i, 2048 * sizeof(int), 11 * sizeof(int), _Alignof(int));
	assert(allocator.cursor == 11 * sizeof(int));
	for (int j = 0; j < 11; ++j)
		i[j] = j;
	l = reallocate(&allocator, NULL, 0, 1, _Alignof(long long));
	int *i1 = reallocate(&allocator, i, 11 * sizeof(int), 12 * sizeof(int), _Alignof(int));
	assert(i1 != i);
	for (int j = 0; j < 11; ++j)
		assert(i1[j] == j);
	allocate(&allocator, 2, _Alignof(char));
	int *i2 = reallocate(&allocator, i1, 12 * sizeof(int), 3 * sizeof(int), _Alignof(int));
	assert(i2 != i1);
	for (int j = 0; j < 3; ++j)
		assert(i2[j] == j);
	int *i3 = reallocate(&allocator, i2, 3 * sizeof(int), 3 * sizeof(int), 64);
	assert(i3 != i2);
	for (int j = 0; j < 3; ++j)
		assert(i2[j] == j);
}