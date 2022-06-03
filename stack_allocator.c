#include <stdint.h> // uintptr_t

struct allocator {
	void *buffer;
	int capacity;
	int cursor;
};

void *allocate(struct allocator *allocator, int size, int alignment) {
	uintptr_t mask = (uintptr_t)alignment - 1; // Alignment must be a power of 2.
	uintptr_t unaligned = (uintptr_t)allocator->buffer + allocator->cursor;
	uintptr_t aligned = (unaligned + mask) & ~mask;
	if (aligned + size > (uintptr_t)allocator->buffer + allocator->capacity)
		return 0;

	allocator->cursor += (int)(aligned - unaligned) + size;
	return (void *)aligned;
}

void deallocate(struct allocator *allocator, void *block, int size) {
	if (block && (char *)block + size == (char *)allocator->buffer + allocator->cursor)
		allocator->cursor -= size;
}

void *reallocate(struct allocator *allocator, void *block, int old_size, int new_size, int alignment) {
	deallocate(allocator, block, old_size);
	return allocate(allocator, new_size, alignment);
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
	int *i3 = allocate(&allocator, 3 * sizeof(int), _Alignof(int));
	assert(i3);
	i3[0] = i3[1] = i3[2] = 42;

	long long big_buffer[1024];
	allocator = (struct allocator){ .buffer = big_buffer, .capacity = sizeof big_buffer };
	assert(!allocate(&allocator, 1024 * sizeof(long long) + 1, 1));
	l = allocate(&allocator, 1024 * sizeof(long long), _Alignof(long long));
	assert(l);
	deallocate(&allocator, l, 1024 * sizeof(long long));
	l = allocate(&allocator, 1024 * sizeof(long long), _Alignof(long long));
	assert(l);
}