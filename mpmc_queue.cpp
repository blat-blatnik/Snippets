// Concurrent multi-producer-multi-consumer wait-free-ish ring buffer queue (what a mouthful!).
// 
// - Wait-free unless the queue is full on write or empty on read.
// - If full on write or empty on read, caller yields to the OS scheduler. Increases latency but conserves power.
// - Only 1 atomic increment and 2 serialization points per call in the fast case.
// - Only 1 byte overhead per queue slot.
// - Polling versions of calls are possible.
// - Queue is initialized to all 0.
// - No memory allocations or thread local storage.
// - Basically the exact same algorithm as https://github.com/rigtorp/MPMCQueue, which is battle tested.

#include <stdint.h>
#include <atomic>
#include <new>

template<typename T, int Log2Capacity> struct Queue {
	static constexpr int Capacity = 1 << Log2Capacity;
	alignas(std::hardware_destructive_interference_size) std::atomic<uint32_t> write_cursor = 0;
	alignas(std::hardware_destructive_interference_size) std::atomic<uint32_t> read_cursor = 0;
	alignas(std::hardware_destructive_interference_size) struct {
		std::atomic<uint8_t> cycle = 0;
		T item = T();
	} slots[Capacity];

	// Blocking API

	void enqueue(T item) {
		uint32_t ticket = write_cursor.fetch_add(1, std::memory_order_relaxed); // Serialization with all writers.
		uint32_t slot = ticket % Capacity;
		uint8_t cycle = (uint8_t)((ticket / Capacity) * 2 + 0); // Writes happen on even cycles.

		uint8_t current_cycle;
		while ((current_cycle = slots[slot].cycle.load(std::memory_order_relaxed)) != cycle) // Serialization with 1 reader.
			slots[slot].cycle.wait(current_cycle, std::memory_order_relaxed);

		slots[slot].item = item;
		slots[slot].cycle.store(cycle + 1, std::memory_order_release); // Serialization with 1 reader.
		slots[slot].cycle.notify_all(); // Hash table crawl.
	}
	T dequeue() {
		uint32_t ticket = read_cursor.fetch_add(1, std::memory_order_relaxed); // Serialization with all readers.
		uint32_t slot = ticket % Capacity;
		uint8_t cycle = (uint8_t)((ticket / Capacity) * 2 + 1); // Reads happen on odd cycles.

		uint8_t current_cycle;
		while ((current_cycle = slots[slot].cycle.load(std::memory_order_acquire)) != cycle) // Serialization with 1 writer.
			slots[slot].cycle.wait(current_cycle, std::memory_order_acquire);

		T item = slots[slot].item;
		slots[slot].cycle.store(cycle + 1, std::memory_order_relaxed); // Serialization with 1 writer.
		slots[slot].cycle.notify_all(); // Hash table crawl.
		return item;
	}

	// Polling API

	bool try_enqueue(T item) {
		uint32_t cursor = write_cursor.load(std::memory_order_relaxed); // Serialization with all writers.
		for (;;) {
			uint32_t slot = cursor % Capacity;
			uint8_t cycle = (uint8_t)((cursor / Capacity) * 2 + 0);
			uint8_t current_cycle = slots[slot].cycle.load(std::memory_order_relaxed); // Serialization with 1 reader.

			int cycles_remaining = (int)(cycle - current_cycle);
			if (cycles_remaining > 0)
				return false;
			if (cycles_remaining < 0) {
				uint32_t new_cursor = cursor; 
				if (write_cursor.compare_exchange_weak(new_cursor, cursor, std::memory_order_relaxed)) { // Serialization with all readers.
					slots[slot].item = item;
					slots[slot].cycle.store(cycle + 1, std::memory_order_release); // Serialization with 1 reader.
					slots[slot].cycle.notify_all(); // Hash table crawl.
					return true;
				}
				cursor = new_cursor;
			}
			else cursor = write_cursor.load(std::memory_order_relaxed);
		}
	}
	bool try_dequeue(T *out_item) {
		uint32_t cursor = write_cursor.load(std::memory_order_relaxed); // Serialization with all readers.
		for (;;) {
			uint32_t slot = cursor % Capacity;
			uint8_t cycle = (uint8_t)((cursor / Capacity) * 2 + 1);
			uint8_t current_cycle = slots[slot].cycle.load(std::memory_order_acquire); // Serialization with 1 writer.

			int cycles_remaining = (int)(cycle - current_cycle);
			if (cycles_remaining > 0)
				return false;
			if (cycles_remaining < 0) {
				uint32_t new_cursor = cursor;
				if (write_cursor.compare_exchange_weak(new_cursor, cursor, std::memory_order_relaxed)) { // Serialization with all readers.
					(*out_item) = slots[slot].item;
					slots[slot].cycle.store(cycle + 1, std::memory_order_relaxed); // Serialization with 1 writer.
					slots[slot].cycle.notify_all(); // Hash table crawl.
					return true;
				}
				cursor = new_cursor;
			}
			else cursor = write_cursor.load(std::memory_order_relaxed);
		}
	}

	// Transactional API

	uint32_t reserve_enqueue() {
		// Once you reserve, you *MUST* wait for your turn to enqueue (can_enqueue), and then commit it (commit_enqueue).
		return write_cursor.fetch_add(1, std::memory_order_relaxed); // Serialization with all writers.
	}
	bool can_enqueue(uint32_t ticket) const {
		uint32_t slot = ticket % Capacity;
		uint8_t cycle = (uint8_t)((ticket / Capacity) * 2 + 0);
		return slots[slot].cycle.load(std::memory_order_relaxed) == cycle; // Serialization with 1 reader.
	}
	void commit_enqueue(uint32_t ticket, T item) {
		uint32_t slot = ticket % Capacity;
		uint8_t cycle = (uint8_t)((ticket / Capacity) * 2 + 0);
		slots[slot].item = item;
		slots[slot].cycle.store(cycle + 1, std::memory_order_release); // Serialization with 1 reader.
		slots[slot].cycle.notify_all(); // Hash table crawl.
	}
	uint32_t reserve_dequeue() {
		// Once you reserve, you *MUST* wait for your turn to dequeue (can_dequeue), and then commit it (commit_dequeue).
		return read_cursor.fetch_add(1, std::memory_order_relaxed); // Serialization with all readers.
	}
	bool can_dequeue(uint32_t ticket) const {
		uint32_t slot = ticket % Capacity;
		uint8_t cycle = (uint8_t)((ticket / Capacity) * 2 + 1);
		return slots[slot].cycle.load(std::memory_order_acquire) == cycle; // Serialization with 1 writer.
	}
	T commit_dequeue(uint32_t ticket) {
		uint32_t slot = ticket % Capacity;
		uint8_t cycle = (uint8_t)((ticket / Capacity) * 2 + 1);
		T item = slots[slot].item;
		slots[slot].cycle.store(cycle + 1, std::memory_order_relaxed); // Serialization with 1 writer.
		slots[slot].cycle.notify_all(); // Hash table crawl.
		return item;
	}

	// Misc API
	
	int approximate_count() const {
		uint32_t write = write_cursor.load(std::memory_order_relaxed);
		uint32_t read = read_cursor.load(std::memory_order_relaxed);
		int count = (int)(write - read);
		count = count < 0 ? 0 : count; // Count can be negative if there are outstanding reads.
		count = count > Capacity ? Capacity : count; // Count can overflow if there are outstanding writes.
		return count;
	}
	bool approximately_empty(volatile struct Queue *queue) const {
		uint32_t write = write_cursor.load(std::memory_order_relaxed);
		uint32_t read = read_cursor.load(std::memory_order_relaxed);
		return read >= write;
	}
	bool approximately_full() const {
		uint32_t write = write_cursor.load(std::memory_order_relaxed);
		uint32_t read = read_cursor.load(std::memory_order_relaxed);
		int count = (int)(write - read);
		return count >= Capacity;
	}
};

// Test

#include <thread>
#include <assert.h>

template<class T, int Log2Capacity>
void reader_thread(Queue<T, Log2Capacity> *queue) {
	static std::atomic<int> counters[3][1000000];
	int last_writer_data[3] = { -1, -1, -1 };
	for (int i = 0; i < 1000000; ++i) {
		int item = queue->dequeue();
		int writer_id = item / 1000000;
		int data = item % 1000000;
		assert(writer_id <= 3); // Ensure no data corruption corruption.
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
template<class T, int Log2Capacity>
void writer_thread(Queue<T, Log2Capacity> *queue) {
	static std::atomic<int> id_dispenser;
	int id = id_dispenser.fetch_add(1);
	for (int i = 0; i < 1000000; ++i)
		queue->enqueue(id * 1000000 + i);
}
int main() {
	static Queue<int, 16> queue;
	std::thread reader0(reader_thread<int, 16>, &queue);
	std::thread reader1(reader_thread<int, 16>, &queue);
	std::thread reader2(reader_thread<int, 16>, &queue);
	std::thread writer0(writer_thread<int, 16>, &queue);
	std::thread writer1(writer_thread<int, 16>, &queue);
	std::thread writer2(writer_thread<int, 16>, &queue);
	reader0.join();
	reader1.join();
	reader2.join();
	writer0.join();
	writer1.join();
	writer2.join();
}