#include <stdlib.h>
#include <string.h>

#define PAGE_GRANULARITY (64 * 1024)

typedef struct Page {
	struct Page* prev;
	struct Page* next;
	ptrdiff_t capacity;
	ptrdiff_t mark;
	char memory[];
} Page;

typedef struct Cleanup {
	struct Cleanup* prev;
	void(*cleanup)(void* param);
	void* context;
} Cleanup;

typedef struct Stack {
	Page* page;
	Cleanup* cleanup;
	ptrdiff_t mark;
} Stack;

void* stackPush(Stack* stack, ptrdiff_t size, ptrdiff_t alignment) {
	ptrdiff_t mask = alignment - 1;
	for (;;) {
		ptrdiff_t unaligned = (ptrdiff_t)stack->page->memory + stack->page->mark;
		ptrdiff_t aligned = (unaligned + mask) & ~mask;
		ptrdiff_t needed = size + (aligned - unaligned);
		ptrdiff_t remaining = stack->page->capacity - stack->page->mark;
		if (needed <= remaining) {
			stack->page->mark += needed;
			stack->mark += needed;
			return (void*)aligned;
		}

		Page* next = stack->page->next;
		if (!next) {
			ptrdiff_t worst_case = size + alignment - 1;
			ptrdiff_t capacity = (worst_case + PAGE_GRANULARITY - 1) / PAGE_GRANULARITY * PAGE_GRANULARITY;
			next = malloc(sizeof(Page) + capacity);
			if (!next)
				return NULL;
			next->prev = stack->page;
			next->next = NULL;
			next->capacity = capacity;
			next->mark = 0;
			stack->page->next = next;
		}

		stack->mark += remaining;
		stack->page->mark += remaining;
		stack->page = next;
	}
}
void* stackResize(Stack* stack, void* block, ptrdiff_t old_size, ptrdiff_t new_size, ptrdiff_t alignment) {
	ptrdiff_t mask = alignment - 1;
	if (!((ptrdiff_t)block & mask)) {
		char* end = (char*)block + old_size;
		char* top = (char*)stack->page->memory + stack->page->mark;
		ptrdiff_t delta = new_size - old_size;
		if (end == top && stack->page->mark + delta <= stack->page->capacity) {
			stack->page->mark += delta;
			stack->mark += delta;
			return block;
		}
		if (new_size < old_size)
			return block;
	}

	void *copy = stackPush(stack, new_size, alignment);
	if (copy) {
		ptrdiff_t to_copy = new_size;
		if (to_copy > old_size)
			to_copy = old_size;
		memcpy(copy, block, to_copy);
	}
	return copy;
}
void stackCleanup(Stack* stack, void* context, void cleanup(void* context)) {
	Cleanup* prev = stack->cleanup;
	stack->cleanup = stackPush(stack, sizeof(Cleanup), _Alignof(Cleanup));
	stack->cleanup->prev = prev;
	stack->cleanup->cleanup = cleanup;
	stack->cleanup->context = context;
}
void stackReset(Stack* stack, ptrdiff_t mark) {
	for (;;) {
		ptrdiff_t remaining = stack->mark - mark;
		if (remaining <= stack->page->mark) {
			stack->page->mark -= remaining;
			stack->mark = mark;
			if ((ptrdiff_t)stack->cleanup > (ptrdiff_t)stack->page->memory + stack->page->mark && 
				(ptrdiff_t)stack->cleanup < (ptrdiff_t)stack->page->memory + stack->page->capacity)
			{
				stack->cleanup->cleanup(stack->cleanup->context);
				stack->cleanup = stack->cleanup->prev;
			}
			return;
		}

		stack->mark -= stack->page->mark;
		stack->page->mark = 0;
		if (stack->page->prev)
			stack->page = stack->page->prev;
		if ((ptrdiff_t)stack->cleanup > (ptrdiff_t)stack->page->memory && 
			(ptrdiff_t)stack->cleanup < (ptrdiff_t)stack->page->memory + stack->page->capacity)
		{
			stack->cleanup->cleanup(stack->cleanup->context);
			stack->cleanup = stack->cleanup->prev;
		}
	}
}
void stackReset2(Stack* stack, ptrdiff_t mark) {
	for (;;) {
		ptrdiff_t shrink = stack->mark - mark;
		if (shrink > stack->page->mark)
			shrink = stack->page->mark;
		if (shrink <= 0)
			return;
		stack->mark -= shrink;
		stack->page->mark -= shrink;
		ptrdiff_t beg = (ptrdiff_t)stack->page->memory + stack->page->mark;
		ptrdiff_t end = (ptrdiff_t)stack->page->memory + stack->page->capacity;
		if (stack->cleanup >= beg && stack->cleanup < end) {
			stack->cleanup->cleanup(stack->cleanup->context);
			stack->cleanup = stack->cleanup->prev;
		}
		if (stack->page->mark == 0)
			stack->page = stack->page->prev;
	}
}
