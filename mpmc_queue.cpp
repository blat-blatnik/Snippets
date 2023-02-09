// Concurrent multi-producer-multi-consumer wait-free-ish ring buffer queue (what a mouthful!).
// 
// - Wait-free unless the queue is full on write or empty on read.
// - If full on write or empty on read, caller yields to the OS scheduler. Increases latency but conserves power.
// - Only 1 atomic increment and 2 serialization points per call in the fast case.
// - Only 1 byte overhead per queue slot.
// - Polling versions of calls are possible.
// - Queue is initialized to all 0.
// - No memory allocations or thread local storage.
// - Slightly modified version of https://github.com/rigtorp/MPMCQueue, which is battle tested.

#include <stdint.h>
#include <atomic>

#define CAPACITY 16384 // Must be a power of 2.

struct Queue {
	alignas(64) std::atomic<uint32_t> write_ticket = 0;
	alignas(64) std::atomic<uint32_t> read_ticket = 0;
	struct {
		alignas(64)
		std::atomic<uint8_t> write_turn = 0;
		std::atomic<uint8_t> read_turn = 0;
		int item = 0;
	} slots[CAPACITY];
};

// Blocking API

void enqueue(Queue *queue, int item) {
	uint32_t ticket = queue->write_ticket.fetch_add(1, std::memory_order::relaxed); // Serialization with all writers.
	uint32_t slot = ticket % CAPACITY;
	uint8_t turn = (uint8_t)(ticket / CAPACITY); // Write turns start at 0.

	uint8_t current_turn;
	while ((current_turn = queue->slots[slot].write_turn.load(std::memory_order::acquire)) != turn) // Serialization with 1 reader.
		queue->slots[slot].write_turn.wait(current_turn, std::memory_order::acquire);

	queue->slots[slot].item = item;
	queue->slots[slot].read_turn.store(turn + 1, std::memory_order::release); // Serialization with 1 reader.
	queue->slots[slot].read_turn.notify_all(); // Hash table crawl.
}
int dequeue(Queue *queue) {
	uint32_t ticket = queue->read_ticket.fetch_add(1, std::memory_order::relaxed); // Serialization with all readers.
	uint32_t slot = ticket % CAPACITY;
	uint8_t turn = (uint8_t)(ticket / CAPACITY + 1); // Write turns start at 1.

	uint8_t current_turn;
	while ((current_turn = queue->slots[slot].read_turn.load(std::memory_order::acquire)) != turn) // Serialization with 1 writer.
		queue->slots[slot].read_turn.wait(current_turn, std::memory_order::acquire);

	int item = queue->slots[slot].item;
	queue->slots[slot].write_turn.store(turn, std::memory_order::release); // Serialization with 1 writer.
	queue->slots[slot].write_turn.notify_all(); // Hash table crawl.
	return item;
}

// Polling API (untested)

bool try_enqueue(Queue *queue, int item) {
	uint32_t try_ticket = queue->write_ticket.load(std::memory_order::relaxed); // Serialization with all writers.
	for (;;) {
		uint32_t slot = try_ticket % CAPACITY;
		uint8_t turn = (uint8_t)(try_ticket / CAPACITY); // Write turns start at 0.
		uint8_t current_turn = queue->slots[slot].write_turn.load(std::memory_order::acquire); // Serialization with 1 reader.

		int turns_remaining = (int)(turn - current_turn);
		if (turns_remaining > 0)
			return false;
		else if (turns_remaining < 0)
			try_ticket = queue->write_ticket.load(std::memory_order::relaxed);
		else if (queue->write_ticket.compare_exchange_weak(try_ticket, try_ticket + 1, std::memory_order::relaxed)) {
			queue->slots[slot].item = item;
			queue->slots[slot].read_turn.store(turn + 1, std::memory_order::release); // Serialization with 1 reader.
			queue->slots[slot].read_turn.notify_all(); // Hash table crawl.
			return true;
		}
	}
}
bool try_dequeue(Queue *queue, int *out_item) {
	uint32_t try_ticket = queue->read_ticket.load(std::memory_order::relaxed); // Serialization with all readers.
	for (;;) {
		uint32_t slot = try_ticket % CAPACITY;
		uint8_t turn = (uint8_t)(try_ticket / CAPACITY + 1); // Read turns start at 1.
		uint8_t current_turn = queue->slots[slot].read_turn.load(std::memory_order::acquire); // Serialization with 1 writer.

		int turns_remaining = (int)(turn - current_turn);
		if (turns_remaining > 0)
			return false;
		else if (turns_remaining < 0)
			try_ticket = queue->read_ticket.load(std::memory_order::relaxed);
		else if (queue->read_ticket.compare_exchange_weak(try_ticket, try_ticket + 1, std::memory_order::relaxed)) {
			(*out_item) = queue->slots[slot].item;
			queue->slots[slot].write_turn.store(turn, std::memory_order::release); // Serialization with 1 writer.
			queue->slots[slot].write_turn.notify_all(); // Hash table crawl.
			return true;
		}
	}
}

// Test

#include <thread>
#include <assert.h>

void reader_thread(Queue *queue) {
	static std::atomic<int> counters[3][1000000];
	int last_writer_data[3] = { -1, -1, -1 };
	for (int i = 0; i < 1000000; ++i) {
		int item;
		if (i < 500000)
			item = dequeue(queue);
		else
			while (!try_dequeue(queue, &item));
		int writer_id = item / 1000000;
		int data = item % 1000000;
		assert(writer_id < 3); // Ensure no data corruption corruption.
		counters[writer_id][data].fetch_add(1);
		assert(last_writer_data[writer_id] < data); // Ensure data is correctly sequenced FIFO.
		last_writer_data[writer_id] = data;
	}

	// Wait for all readers to finish.
	static std::atomic<int> done_counter;
	done_counter.fetch_add(1);
	done_counter.notify_all();
	int num_done;
	while ((num_done = done_counter.load()) != 3)
		done_counter.wait(num_done);

	for (int writer_id = 0; writer_id < 3; ++writer_id)
		for (int i = 0; i < 1000000; ++i)
			assert(counters[writer_id][i] == 1); // Ensure all items have been properly received.
}
void writer_thread(Queue *queue) {
	static std::atomic<int> id_dispenser;
	int id = id_dispenser.fetch_add(1);
	for (int i = 0; i < 500000; ++i)
		enqueue(queue, id * 1000000 + i);
	for (int i = 500000; i < 1000000; ++i)
		while (!try_enqueue(queue, id * 1000000 + i));
}
int main() {
	static Queue queue;
	std::thread reader0(reader_thread, &queue);
	std::thread reader1(reader_thread, &queue);
	std::thread reader2(reader_thread, &queue);
	std::thread writer0(writer_thread, &queue);
	std::thread writer1(writer_thread, &queue);
	std::thread writer2(writer_thread, &queue);
	reader0.join();
	reader1.join();
	reader2.join();
	writer0.join();
	writer1.join();
	writer2.join();
}