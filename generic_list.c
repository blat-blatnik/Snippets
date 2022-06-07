#include <stdlib.h> // realloc, free

#define list(T) T*

#define reserve(plist, num_items)\
	private__reserve((plist), (num_items), sizeof *(*plist))

#define add(plist, item) do{\
	int private__index = count(*(plist));\
	reserve((plist), private__index + 1);\
	(*plist)[private__index] = (item);\
	(plist)[++((int*)(*(plist)))[-1]];\
}while(0)

#define pop(plist)\
	(((int*)(*(plist)))[-1]--, (*(plist))[((int*)(*(plist)))[-1]])

#define swap_delete(list, index)do{\
	if(index < count(list))\
		(list)[index] = (list)[--((int*)(list))[-1]];\
}while(0)

int count(const list(void) list) {
	return list ? ((int *)list)[-1] : 0;
}

int capacity(const list(void) list) {
	return list ? ((int *)list)[-2] : 0;
}

void destroy(list(void) *plist) {
	free((int *)(*plist) - 4);
	*plist = NULL;
}

static void private__reserve(list(void) *plist, int min_capacity, int item_size) {
	int cap = capacity(*plist);
	if (cap < min_capacity) {
		cap *= 2;
		if (cap < 64)
			cap = 64;
		while (cap < min_capacity)
			cap *= 2;
		// Overallocate by 4 ints to keep overall alignment to 16 bytes.
		int cnt = count(*plist);
		int *newlist = (int *)realloc(*plist ? (int *)(*plist) - 4 : NULL, cap * item_size + 4 * sizeof(int)) + 4;
		newlist[-2] = cap;
		newlist[-1] = cnt;
		*plist = newlist;
	}
}

#include <assert.h>
int main(void) {
	list(long long) * ints = NULL;
	assert(count(ints) == 0);
	assert(capacity(ints) == 0);

	for (int i = 0; i < 1024; ++i)
		add(&ints, i);
	assert(count(ints) == 1024);

	for (int i = 0; i < 1024; ++i)
		assert(ints[i] == i);

	for (int i = 1023; i >= 0; --i)
		assert(pop(&ints) == i);
	assert(count(ints) == 0);

	destroy(&ints);
	assert(!ints);
}