#include <string.h> // strlen, memcpy
#include <stdlib.h> // malloc, free

#define SLAB_SIZE (64*1024)

struct slab {
	struct slab *prev;
	char *buffer;
	int capacity;
	int cursor;
};

char *allocate(struct slab **slab, int size) {
	int remaining = (*slab)->capacity - (*slab)->cursor;
	if (remaining < size) {
		int capacity = SLAB_SIZE * ((size + SLAB_SIZE + 1) / SLAB_SIZE);
		struct slab *next = malloc(sizeof next[0] + capacity);
		next->prev = *slab;
		next->buffer = (char *)(next + 1);
		next->capacity = capacity;
		next->cursor = 0;
		*slab = next;
	}
	char *result = (*slab)->buffer + (*slab)->cursor;
	(*slab)->cursor += size;
	return result;
}

char *copy_string(struct slab **slab, const char *string) {
	int size = 1 + (int)strlen(string);
	char *copy = allocate(slab, size);
	memcpy(copy, string, (size_t)size);
	return copy;
}

void deallocate_all(struct slab **slab) {
	for (;;) {
		struct slab *prev = (*slab)->prev;
		if ((*slab)->capacity)
			free(*slab);
		if (!prev)
			return;
		*slab = prev;
	}
}

#include <assert.h>
int main(void) {
	struct slab *slab = &(struct slab) { 0 };
	assert(strcmp(copy_string(&slab, "Hello, sailor!"), "Hello, sailor!") == 0);
	assert(strcmp(copy_string(&slab, ""), "") == 0);

	char *large_string = malloc(2 * SLAB_SIZE + 1);
	memset(large_string, 'A', 2 * SLAB_SIZE);
	large_string[2 * SLAB_SIZE] = 0;
	assert(strcmp(copy_string(&slab, large_string), large_string) == 0);
	
	assert(slab->prev);
	deallocate_all(&slab);
	assert(!slab->prev);
}