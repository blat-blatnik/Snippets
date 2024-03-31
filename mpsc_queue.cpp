#include <atomic>
#include <stdint.h>
using namespace std;
using enum std::memory_order;

#define CAPACITY 16384 // Must be a power of 2.

struct Queue {
	alignas(64) atomic<uint32_t> write_ticket = 0;
	alignas(64) uint32_t read_ticket = 0;
	struct { 
		alignas(64)
		atomic<uint8_t> turn = 0;
		atomic<bool> full = false; 
		int item = 0; 
	} slots[CAPACITY];
};

// Blocking API

void enqueue(Queue *queue, int item) {
	uint32_t ticket = queue->write_ticket.fetch_add(1, relaxed); // Serialization with writers. 
	uint32_t slot = ticket % CAPACITY;
	uint8_t turn = (uint8_t)(ticket / CAPACITY);

	uint8_t current_turn;
	while ((current_turn = queue->slots[slot].turn.load(acquire)) != turn) // Serialization with reader.
		queue->slots[slot].turn.wait(current_turn, acquire); // Block while queue is full.
	
	queue->slots[slot].item = item;
	queue->slots[slot].full.store(true, release); // Serialization with reader.
	queue->slots[slot].full.notify_one();
}
int dequeue(Queue *queue) {
	uint32_t ticket = queue->read_ticket++;
	uint32_t slot = ticket % CAPACITY;
	uint8_t turn = (uint8_t)(ticket / CAPACITY);
	queue->slots[slot].full.wait(false, acquire); // Block while queue is empty.
	int item = queue->slots[slot].item;
	queue->slots[slot].full.store(false, relaxed);
	queue->slots[slot].turn.store(turn + 1, release); // Serialization with 1 writer.
	queue->slots[slot].turn.notify_all();
	return item;
}

// Polling API

bool try_enqueue(Queue *queue, int item) {
	uint32_t try_ticket = queue->write_ticket.load(relaxed); // Serialization with writers.
	for (;;) {
		uint32_t slot = try_ticket % CAPACITY;
		uint8_t turn = (uint8_t)(try_ticket / CAPACITY);
		uint8_t current_turn = queue->slots[slot].turn.load(acquire); // Serialization with reader.
		int turns_remaining = (int)turn - (int)current_turn;
		if (turns_remaining > 0)
			return false; // Queue is full.
		else if (turns_remaining < 0)
			try_ticket = queue->write_ticket; // Another writer lapped us, try again.
		else if (queue->write_ticket.compare_exchange_weak(try_ticket, try_ticket + 1, relaxed)) {
			queue->slots[slot].item = item;
			queue->slots[slot].full.store(true, release); // Serialization with reader.
			queue->slots[slot].full.notify_one(); // Hash table lookup. Remove this if you only use Polling and not Blocking.
			return true;
		}
	}
}
bool try_dequeue(Queue *queue, int *out_item) {
	uint32_t ticket = queue->read_ticket;
	uint32_t slot = ticket % CAPACITY;
	if (!queue->slots[slot].full.load(acquire)) // Serialization with 1 writer.
		return false; // Queue is empty.

	uint8_t turn = (uint8_t)(ticket / CAPACITY);
	(*out_item) = queue->slots[slot].item;
	queue->slots[slot].full.store(false, relaxed);
	queue->slots[slot].turn.store(turn + 1, release); // Serialization with 1 writer.
	queue->slots[slot].turn.notify_all(); // Hash table crawl. Remove this if you only use Polling and not Blocking.
	++(queue->read_ticket);
	return true;
}

// Test

#include <thread>
#include <assert.h>

void reader_thread(Queue *queue) {
	static int counters[5][1000000];
	int last_writer_data[5] = { -1, -1, -1, -1, -1 };
	for (int i = 0; i < 5000000; ++i) {
		int item;
		if (i < 2500000)
			item = dequeue(queue);
		else
			while (!try_dequeue(queue, &item));
		int writer = item / 1000000;
		int data = item % 1000000;
		assert(writer < 5); // Ensure no data corruption corruption.
		++(counters[writer][data]);
		assert(last_writer_data[writer] < data); // Ensure data is correctly sequenced FIFO.
		last_writer_data[writer] = data;
	}
	for (int writer = 0; writer < 5; ++writer)
		for (int i = 0; i < 1000000; ++i)
			assert(counters[writer][i] == 1); // Ensure all items have been properly received.
}
void writer_thread(Queue *queue) {
	static atomic<int> id_dispenser;
	int id = id_dispenser.fetch_add(1);
	for (int i = 0; i < 500000; ++i)
		enqueue(queue, id * 1000000 + i);
	for (int i = 500000; i < 1000000; ++i)
		while (!try_enqueue(queue, id * 1000000 + i));
}
int main(void) {
	static struct Queue queue;
	thread reader(reader_thread, &queue);
	thread writer0(writer_thread, &queue);
	thread writer1(writer_thread, &queue);
	thread writer2(writer_thread, &queue);
	thread writer3(writer_thread, &queue);
	thread writer4(writer_thread, &queue);
	reader.join();
	writer0.join();
	writer1.join();
	writer2.join();
	writer3.join();
	writer4.join();
}
