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

#include <Windows.h>
#pragma comment(lib, "Synchronization.lib")

#define CAPACITY 16384 // Must be a power of 2.

struct Queue
{
	__declspec(align(64)) UINT32 WriteCursor;
	__declspec(align(64)) UINT32 ReadCursor;
	__declspec(align(64)) struct
	{
		UINT8 Cycle;
		int Item; // You can put anything you want here.
	} Slots[CAPACITY];
};

// Blocking API

void Enqueue(volatile struct Queue *queue, int item)
{
	UINT32 ticket = InterlockedIncrementNoFence((volatile LONG *)&queue->WriteCursor) - 1; // Serialization with all writers
	UINT32 slot = ticket % CAPACITY;
	UINT8 cycle = (UINT8)((ticket / CAPACITY) * 2 + 0); // Writes happen on even cycles.

	UINT8 currentCycle;
	while ((currentCycle = queue->Slots[slot].Cycle) != cycle) // Serialization with 1 reader.
		WaitOnAddress(&queue->Slots[slot].Cycle, &currentCycle, sizeof currentCycle, INFINITE);

	queue->Slots[slot].Item = item;
	queue->Slots[slot].Cycle = cycle + 1; // Release, serialization with 1 reader.
	WakeByAddressAll((void *)&queue->Slots[slot].Cycle); // Hash table crawl.
}
int Dequeue(volatile struct Queue *queue)
{
	UINT32 ticket = InterlockedIncrementNoFence((volatile LONG *)&queue->ReadCursor) - 1; // Serialization with all readers.
	UINT32 slot = ticket % CAPACITY;
	UINT8 cycle = (UINT8)((ticket / CAPACITY) * 2 + 1); // Reads happen on odd cycles.

	UINT8 currentCycle;
	while ((currentCycle = queue->Slots[slot].Cycle) != cycle) // Acquire, serialization with 1 writer.
		WaitOnAddress(&queue->Slots[slot].Cycle, &currentCycle, sizeof currentCycle, INFINITE);

	int item = queue->Slots[slot].Item;
	queue->Slots[slot].Cycle = cycle + 1; // Serialization with 1 writer.
	WakeByAddressAll((void *)&queue->Slots[slot].Cycle); // Hash table crawl.
	return item;
}

// Polling API

BOOL TryEnequeue(volatile struct Queue *queue, int item)
{
	UINT32 cursor = queue->WriteCursor; // Atomic load relaxed. Serialization with all writers.
	for (;;)
	{
		UINT32 slot = cursor % CAPACITY;
		UINT8 cycle = (UINT8)((cursor / CAPACITY) * 2 + 0);
		UINT8 currentCycle = queue->Slots[slot].Cycle; // Serialization with 1 reader.
		
		int cyclesRemaining = (int)(cycle - currentCycle);
		if (cyclesRemaining > 0)
			return FALSE;
		if (cyclesRemaining < 0)
		{
			UINT32 newCursor = InterlockedCompareExchangeNoFence((volatile LONG *)&queue->WriteCursor, cursor + 1, cursor); // Serialization with all readers.
			if (newCursor == cursor)
			{
				queue->Slots[slot].Item = item;
				queue->Slots[slot].Cycle = cycle + 1; // Release, serialization with 1 reader.
				WakeByAddressAll((void *)&queue->Slots[slot]); // Hash table crawl. Remove this if you only use Polling and not Blocking.
				return TRUE;
			}
			cursor = newCursor;
		}
		else cursor = queue->WriteCursor;
	}
}
BOOL TryDequeue(volatile struct Queue *queue, int *outItem)
{
	UINT32 cursor = queue->ReadCursor; // Atomic load relaxed. Serialization with all readers.
	for (;;)
	{
		UINT32 slot = cursor % CAPACITY;
		UINT8 cycle = (UINT8)((cursor / CAPACITY) * 2 + 1);
		UINT8 currentCycle = queue->Slots[slot].Cycle; // Acquire, serialization with 1 writer.

		int cyclesRemaining = (int)(cycle - currentCycle);
		if (cyclesRemaining > 0)
			return FALSE;
		if (cyclesRemaining < 0)
		{
			UINT32 newCursor = InterlockedCompareExchangeNoFence((volatile LONG *)&queue->WriteCursor, cursor + 1, cursor); // Serialization with all readers.
			if (newCursor == cursor)
			{
				(*outItem) = queue->Slots[slot].Item;
				queue->Slots[slot].Cycle = cycle + 1; // Serialization with 1 writer.
				WakeByAddressAll((void *)&queue->Slots[slot]); // Hash table crawl. Remove this if you only use Polling and not Blocking.
				return TRUE;
			}
			cursor = newCursor;
		}
		else cursor = queue->WriteCursor;
	}
}

// Transactional API

UINT32 ReserveEnqueue(volatile struct Queue *queue)
{
	// Once you call ReserveEnqueue, you *MUST* wait until CanEnqueue returns true, and then call CommitEnqueue.
	return InterlockedIncrementNoFence((volatile LONG *)&queue->WriteCursor) - 1; // Serialization with all writers.
}
BOOL CanEnqueue(volatile struct Queue *queue, UINT32 ticket)
{
	UINT32 slot = ticket % CAPACITY;
	UINT8 cycle = (UINT8)((ticket / CAPACITY) * 2 + 0);
	return queue->Slots[slot].Cycle == cycle; // Serialization with 1 reader.
}
void WaitEnqueue(volatile struct Queue *queue, UINT32 ticket)
{
	UINT32 slot = ticket % CAPACITY;
	UINT8 cycle = (UINT8)((ticket / CAPACITY) * 2 + 0);
	UINT8 currentCycle;
	while ((currentCycle = queue->Slots[slot].Cycle) != cycle) // Serialization with 1 reader.
		WaitOnAddress(&queue->Slots[slot].Cycle, &currentCycle, sizeof currentCycle, INFINITE);
}
void CommitEnqueue(volatile struct Queue *queue, UINT32 ticket, int item)
{
	UINT32 slot = ticket % CAPACITY;
	UINT8 cycle = (UINT8)((ticket / CAPACITY) * 2 + 0);
	queue->Slots[slot].Item = item;
	queue->Slots[slot].Cycle = cycle + 1; // Release, serialization with 1 reader.
	WakeByAddressAll((void *)&queue->Slots[slot]); // Hash table crawl.
}
UINT32 ReserveDequeue(volatile struct Queue *queue)
{
	// Once you call ReserveDequeue, you *MUST* wait until CanDequeue returns true, and then call CommitDequeue.
	return InterlockedIncrementNoFence((volatile LONG *)&queue->ReadCursor) - 1; // Serialization with all writers.
}
BOOL CanDequeue(volatile struct Queue *queue, UINT32 ticket)
{
	UINT32 slot = ticket % CAPACITY;
	UINT8 cycle = (UINT8)((ticket / CAPACITY) * 2 + 1);
	return queue->Slots[slot].Cycle == cycle; // Acquire, serialization with 1 writer.
}
void WaitDequeue(volatile struct Queue *queue, UINT32 ticket)
{
	UINT32 slot = ticket % CAPACITY;
	UINT8 cycle = (UINT8)((ticket / CAPACITY) * 2 + 1);
	UINT8 currentCycle;
	while ((currentCycle = queue->Slots[slot].Cycle) != cycle) // Acquire, serialization with 1 writer.
		WaitOnAddress(&queue->Slots[slot].Cycle, &currentCycle, sizeof currentCycle, INFINITE);
}
int CommitDequeue(volatile struct Queue *queue, UINT32 ticket)
{
	UINT32 slot = ticket % CAPACITY;
	UINT8 cycle = (UINT8)((ticket / CAPACITY) * 2 + 1);
	int item = queue->Slots[slot].Item;
	queue->Slots[slot].Cycle = cycle + 1; // Serialization with 1 writer.
	WakeByAddressAll((void *)&queue->Slots[slot]); // Hash table crawl.
	return item;
}

// Auxilary API

int ApproximateCount(volatile struct Queue *queue)
{
	UINT32 write = queue->WriteCursor;
	UINT32 read = queue->ReadCursor;
	int count = (int)(write - read);
	count = count < 0 ? 0 : count; // Count can be negative if there are outstanding reads.
	count = count > CAPACITY ? CAPACITY : count; // Count can overflow if there are outstanding writes.
	return count;
}
BOOL ApproximatelyEmpty(volatile struct Queue *queue)
{
	UINT32 write = queue->WriteCursor;
	UINT32 read = queue->ReadCursor;
	return read >= write;
}
BOOL ApproximatelyFull(volatile struct Queue *queue)
{
	UINT32 write = queue->WriteCursor;
	UINT32 read = queue->ReadCursor;
	int count = (int)(write - read);
	return count >= CAPACITY;
}

// Test

#include <assert.h>

DWORD __stdcall ReaderThread(void *parameter)
{
	struct Queue *queue = parameter;
	static volatile LONG counters[3][1000000];
	int lastWriterData[3] = { -1, -1, -1 };
	for (int i = 0; i < 1000000; ++i)
	{
		int item = Dequeue(queue);
		int writerId = item / 1000000;
		int data = item % 1000000;
		assert(writerId < 3); // Ensure no data corruption corruption.
		InterlockedIncrement(&counters[writerId][data]);
		assert(lastWriterData[writerId] < data); // Ensure data is correctly sequenced FIFO.
		lastWriterData[writerId] = data;
	}

	// Wait for all readers to finish.
	static volatile LONG doneCounter;
	InterlockedIncrement(&doneCounter);
	WakeByAddressAll((void *)&doneCounter);
	LONG numDone;
	while ((numDone = doneCounter) != 3)
		WaitOnAddress(&doneCounter, &numDone, sizeof numDone, INFINITE);

	for (int writerId = 0; writerId < 3; ++writerId)
		for (int i = 0; i < 1000000; ++i)
			assert(counters[writerId][i] == 1); // Ensure all items have been properly received.

	return EXIT_SUCCESS;
}
DWORD __stdcall WriterThread(void *parameter)
{
	struct Queue *queue = parameter;
	static volatile LONG idDispenser;
	LONG id = InterlockedIncrement(&idDispenser) - 1;
	for (int i = 0; i < 1000000; ++i)
		Enqueue(queue, id * 1000000 + i);
	return EXIT_SUCCESS;
}
int main(void)
{
	static struct Queue queue;
	HANDLE threads[6];
	threads[0] = CreateThread(NULL, 0, ReaderThread, &queue, 0, NULL);
	threads[1] = CreateThread(NULL, 0, ReaderThread, &queue, 0, NULL);
	threads[2] = CreateThread(NULL, 0, ReaderThread, &queue, 0, NULL);
	threads[3] = CreateThread(NULL, 0, WriterThread, &queue, 0, NULL);
	threads[4] = CreateThread(NULL, 0, WriterThread, &queue, 0, NULL);
	threads[5] = CreateThread(NULL, 0, WriterThread, &queue, 0, NULL);
	WaitForMultipleObjects(6, threads, TRUE, INFINITE);
	__debugbreak();
}
