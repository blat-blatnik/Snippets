// Concurrent multi-producer-multi-consumer wait-free-ish ring buffer queue (what a mouthful!).
// 
// - Wait-free unless the queue is full on write or empty on read.
// - If full on write or empty on read, caller yields to the OS scheduler. Increases latency but conserves power.
// - Only 1 atomic increment and 2 serialization points per call in the fast case.
// - Only 2 bytes overhead per queue slot.
// - Polling versions of calls are possible.
// - Queue is initialized to all 0.
// - No memory allocations or thread local storage.
// - Slightly modified version of https://github.com/rigtorp/MPMCQueue, which is battle tested.

#include <Windows.h>
#pragma comment(lib, "Synchronization.lib")

#define CAPACITY 16384 // Must be a power of 2.

struct Queue
{
	__declspec(align(64)) UINT32 WriteTicket;
	__declspec(align(64)) UINT32 ReadTicket;
	__declspec(align(64)) struct
	{
		UINT8 WriteTurn;
		UINT8 ReadTurn;
		int Item; // You can put anything you want here.
	} Slots[CAPACITY];
};

// Blocking API

void Enqueue(volatile struct Queue *queue, int item)
{
	UINT32 ticket = InterlockedIncrementNoFence((volatile LONG *)&queue->WriteTicket) - 1; // Serialization with all writers
	UINT32 slot = ticket % CAPACITY;
	UINT8 turn = (UINT8)(ticket / CAPACITY); // Write turns start at 0.

	UINT8 currentTurn;
	while ((currentTurn = queue->Slots[slot].WriteTurn) != turn) // Acquire, Serialization with 1 reader.
		WaitOnAddress(&queue->Slots[slot].WriteTurn, &currentTurn, sizeof currentTurn, INFINITE); // Block while queue is full.

	queue->Slots[slot].Item = item;
	queue->Slots[slot].ReadTurn = turn + 1; // Release, serialization with 1 reader.
	WakeByAddressAll((void *)&queue->Slots[slot].ReadTurn); // Hash table crawl.
}
int Dequeue(volatile struct Queue *queue)
{
	UINT32 ticket = InterlockedIncrementNoFence((volatile LONG *)&queue->ReadTicket) - 1; // Acquire, serialization with all readers.
	UINT32 slot = ticket % CAPACITY;
	UINT8 turn = (UINT8)(ticket / CAPACITY + 1); // Read turns start at 1.

	UINT8 currentTurn;
	while ((currentTurn = queue->Slots[slot].ReadTurn) != turn) // Acquire, serialization with 1 writer.
		WaitOnAddress(&queue->Slots[slot].ReadTurn, &currentTurn, sizeof currentTurn, INFINITE); // Block while queue is empty.

	int item = queue->Slots[slot].Item;
	queue->Slots[slot].WriteTurn = turn; // Release, serialization with 1 writer.
	WakeByAddressAll((void *)&queue->Slots[slot].WriteTurn); // Hash table crawl.
	return item;
}

// Polling API

BOOL TryEnqueue(volatile struct Queue *queue, int item)
{
	UINT32 tryTicket = queue->WriteTicket; // Atomic load relaxed. Serialization with all writers.
	for (;;)
	{
		UINT32 slot = tryTicket % CAPACITY;
		UINT8 turn = (UINT8)(tryTicket / CAPACITY); // Write turns start at 0.
		UINT8 currentTurn = queue->Slots[slot].WriteTurn; // Acquire, serialization with 1 reader.
		
		int turnsRemaining = (int)(turn - currentTurn);
		if (turnsRemaining > 0)
			return FALSE; // Queue is full.
		if (turnsRemaining == 0)
		{
			UINT32 ticket = InterlockedCompareExchangeNoFence((volatile LONG *)&queue->WriteTicket, tryTicket + 1, tryTicket); // Serialization with all readers.
			if (ticket == tryTicket)
			{
				queue->Slots[slot].Item = item;
				queue->Slots[slot].ReadTurn = turn + 1; // Release, serialization with 1 reader.
				WakeByAddressAll((void *)&queue->Slots[slot].ReadTurn); // Hash table crawl. Remove this if you only use Polling and not Blocking.
				return TRUE;
			}
			tryTicket = ticket;
		}
		else tryTicket = queue->WriteTicket; // Another writer beat us to it, try again.
	}
}
BOOL TryDequeue(volatile struct Queue *queue, int *outItem)
{
	UINT32 tryTicket = queue->ReadTicket; // Atomic load relaxed. Serialization with all readers.
	for (;;)
	{
		UINT32 slot = tryTicket % CAPACITY;
		UINT8 turn = (UINT8)(tryTicket / CAPACITY + 1); // Read turns start at 1.
		UINT8 currentTurn = queue->Slots[slot].ReadTurn; // Acquire, serialization with 1 writer.

		int turnsRemaining = (int)(turn - currentTurn);
		if (turnsRemaining > 0)
			return FALSE; // Queue is empty.
		if (turnsRemaining == 0)
		{
			UINT32 ticket = InterlockedCompareExchangeNoFence((volatile LONG *)&queue->ReadTicket, tryTicket + 1, tryTicket); // Serialization with all readers.
			if (ticket == tryTicket)
			{
				(*outItem) = queue->Slots[slot].Item;
				queue->Slots[slot].WriteTurn = turn; // Release, serialization with 1 writer.
				WakeByAddressAll((void *)&queue->Slots[slot].WriteTurn); // Hash table crawl. Remove this if you only use Polling and not Blocking.
				return TRUE;
			}
			tryTicket = ticket;
		}
		else tryTicket = queue->ReadTicket; // Another reader beat us to it, try again.
	}
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
		int item;
		if (i < 500000)
			item = Dequeue(queue);
		else
			while (!TryDequeue(queue, &item));
		int writer = item / 1000000;
		int data = item % 1000000;
		assert(writer < 3); // Ensure no data corruption.
		InterlockedIncrement(&counters[writer][data]);
		assert(lastWriterData[writer] < data); // Ensure data is correctly sequenced FIFO.
		lastWriterData[writer] = data;
	}

	// Wait for all readers to finish.
	static volatile LONG doneCounter;
	InterlockedIncrement(&doneCounter);
	WakeByAddressAll((void *)&doneCounter);
	LONG numDone;
	while ((numDone = doneCounter) != 3)
		WaitOnAddress(&doneCounter, &numDone, sizeof numDone, INFINITE);

	for (int writer = 0; writer < 3; ++writer)
		for (int i = 0; i < 1000000; ++i)
			assert(counters[writer][i] == 1); // Ensure all items have been properly received.

	return EXIT_SUCCESS;
}
DWORD __stdcall WriterThread(void *parameter)
{
	struct Queue *queue = parameter;
	static volatile LONG idDispenser;
	LONG id = InterlockedIncrement(&idDispenser) - 1;
	for (int i = 0; i < 500000; ++i)
		Enqueue(queue, id * 1000000 + i);
	for (int i = 500000; i < 1000000; ++i)
		while (!TryEnqueue(queue, id * 1000000 + i));
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
