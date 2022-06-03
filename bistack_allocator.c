#include <stdint.h> // uintptr_t
#include <string.h> // memcpy

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
	int new_rcursor = allocator->rcursor + size + (int)(alignment - unaligned & mask);
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

}