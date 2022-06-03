#include <stdint.h> // uintptr_t

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
	if (aligned + size >= (uintptr_t)allocator->buffer + allocator->capacity - allocator->rcursor)
		return 0;

	allocator->lcursor += (int)(aligned - unaligned) + size;
	return (void *)aligned;
}

void *allocate_right(struct allocator *allocator, int size, int alignment) {
	uintptr_t mask = (uintptr_t)alignment - 1; // Alignment must be a power of 2.
	uintptr_t unaligned = (uintptr_t)allocator->buffer + allocator->capacity - allocator->rcursor - size - alignment;
	uintptr_t aligned = (unaligned + alignment) & ~mask;
	if (aligned <= (uintptr_t)allocator->buffer + allocator->lcursor)
		return 0;

	allocator->rcursor += (int)(alignment - unaligned & mask) + size;
	return (void *)aligned;
}

void deallocate(struct allocator *allocator, void *block, int size) {
	if (block) {
		if ((char *)block + size == (char *)allocator->buffer + allocator->lcursor)
			allocator->lcursor -= size;
		else if (block == (char *)allocator->buffer + allocator->capacity - allocator->rcursor)
			allocator->rcursor -= size;
	}
}