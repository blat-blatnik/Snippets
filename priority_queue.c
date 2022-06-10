#include <stdlib.h> // realloc, free

struct queue { // max heap
	struct item *items;
	int capacity;
	int count;
};

struct item {
	int priority;
	int value;
};

#define LEFT_CHILD(index) (2*(index)+1)
#define RIGHT_CHILD(index) (2*(index)+2)
#define PARENT(index) ((index-1)/2)

void upheap(struct item *items, int index) {
	for (; index > 0 && items[index].priority > items[PARENT(index)].priority; index = PARENT(index)) {
		struct item temp = items[index];
		items[index] = items[PARENT(index)];
		items[PARENT(index)] = temp;
	}
}

void downheap(struct item *items, int index, int count) {
	while (LEFT_CHILD(index) < count) {
		int l = LEFT_CHILD(index);
		int r = RIGHT_CHILD(index);
		int max_child = l;
		if (r < count && items[r].priority >= items[l].priority)
			max_child = r;

		if (items[index].priority >= items[max_child].priority)
			break;

		struct item temp = items[max_child];
		items[max_child] = items[index];
		items[index] = temp;
		index = max_child;
	}
}

void reserve(struct queue *queue, int min_capacity) {
	if (queue->capacity < min_capacity) {
		int new_capacity = 2 * queue->capacity;
		if (new_capacity < 128)
			new_capacity = 128;
		while (new_capacity < min_capacity)
			new_capacity *= 2;
		
		queue->items = realloc(queue->items, (int)new_capacity * sizeof queue->items[0]);
		queue->capacity = new_capacity;
	}
}

void push(struct queue *queue, int item, int priority) {
	reserve(queue, queue->count + 1);
	int index = queue->count++;
	queue->items[index].priority = priority;
	queue->items[index].value = item;
	upheap(queue->items, index);
}

int pop(struct queue *queue) {
	if (!queue->count)
		return 0; // Tried to pop from an empty queue.
	
	int result = queue->items[0].value;
	queue->items[0] = queue->items[--queue->count];
	downheap(queue->items, 0, queue->count);
	return result;
}

int push_pop(struct queue *queue, int item, int priority) {
	if (!queue->count || priority >= queue->items[0].priority)
		return item;

	int result = queue->items[0].value;
	queue->items[0].priority = priority;
	queue->items[0].value = item;
	downheap(queue->items, 0, queue->count);
	return result;
}

int pop_push(struct queue *queue, int item, int priority) {
	if (!queue->count) {
		push(queue, item, priority);
		return 0; // Tried to pop from an empty queue.
	}

	int result = queue->items[0].value;
	queue->items[0].value = item;
	queue->items[0].priority = priority;
	downheap(queue->items, 0, queue->count);
	return result;
}

void change_priority(struct queue *queue, int index, int new_priority) {
	if (index < queue->count) {
		int old_priority = queue->items[index].priority;
		queue->items[index].priority = new_priority;
		if (new_priority > old_priority)
			upheap(queue->items, index);
		else if (new_priority < old_priority)
			downheap(queue->items, index, queue->count);
	}
}

void destroy(struct queue *queue) {
	free(queue->items);
	queue->items = NULL;
	queue->capacity = 0;
	queue->count = 0;
}

#include <assert.h>
int main(void) {
	{
		struct queue queue = { 0 };
		for (int i = 0; i < 10; ++i)
			push(&queue, i, i);
		assert(queue.count == 10);
		
		for (int i = 9; i >= 0; --i)
			assert(pop(&queue) == i);
		assert(queue.count == 0);

		destroy(&queue);
	}

	{
		static int priorities[10000];
		for (int i = 0; i < 10000; ++i)
			priorities[i] = rand();

		struct queue queue = { 0 };
		for (int i = 0; i < 10000; ++i)
			push(&queue, i, priorities[i]);
		assert(queue.count == 10000);

		int prev = -1;
		while (queue.count > 0) {
			int index = pop(&queue);
			assert(prev == -1 || priorities[prev] >= priorities[index]);
		}

		destroy(&queue);
	}

	{
		struct queue queue = { 0 };
		push(&queue, 0, 0);
		push(&queue, 1, 1);
		push(&queue, 2, 2);
		change_priority(&queue, 0, -99);
		change_priority(&queue, 1, 99);
		assert(pop(&queue) == 0);
		assert(pop(&queue) == 1);
		assert(pop(&queue) == 2);
		destroy(&queue);
	}
}