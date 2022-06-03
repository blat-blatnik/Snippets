void *allocate(void **freelist) {
	void *result = *freelist;
	if (*freelist)
		*freelist = **(void ***)freelist;
	return result;
}

void deallocate(void **freelist, void *item) {
	*(void **)item = *freelist;
	*freelist = item;
}

#include <assert.h>
int main(void) {
	void *items[10];
	void *list = 0;
	assert(!allocate(&list));
	
	for (int i = 0; i < 10; ++i)
		deallocate(&list, &items[i]);
	for (int i = 9; i >= 0; --i) {
		void **item = allocate(&list);
		int index = (int)(item - items);
		assert(index == i);
	}
	assert(!allocate(&list));
	assert(!allocate(&list));
	
	for (int i = 0; i < 10; ++i) {
		deallocate(&list, &items[i]);
		void **item = allocate(&list);
		int index = (int)(item - items);
		assert(index == i);
		assert(!allocate(&list));
	}
}