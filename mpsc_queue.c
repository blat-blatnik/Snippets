#include <Windows.h>
#pragma comment(lib, "Synchronization.lib")

#define CAPACITY 16384 // Must be a power of 2.

struct Queue
{
	__declspec(align(64)) UINT32 WriteTicket;
	__declspec(align(64)) UINT32 ReadTicket;
	__declspec(align(64)) struct { UINT8 Turn, Full; int Item; } Slots[CAPACITY];
};

// Blocking API

void Enqueue(volatile struct Queue *queue, int item)
{
	UINT32 ticket = InterlockedIncrementNoFence((volatile long *)&queue->WriteTicket) - 1; // Serialization with writers. 
	UINT32 slot = ticket % CAPACITY;
	UINT8 turn = (UINT8)(ticket / CAPACITY);

	UINT8 currentTurn;
	while ((currentTurn = queue->Slots[slot].Turn) != turn) // Acquire, serialization with reader.
		WaitOnAddress(&queue->Slots[slot].Turn, &currentTurn, sizeof currentTurn, INFINITE);
	
	queue->Slots[slot].Item = item;
	queue->Slots[slot].Full = TRUE; // Release, serialization with reader.
	WakeByAddressSingle((void *)&queue->Slots[slot].Full); // Hash table lookup.
}
int Dequeue(volatile struct Queue *queue)
{
	UINT32 ticket = queue->ReadTicket++;
	UINT32 slot = ticket % CAPACITY;
	UINT8 turn = (UINT8)(ticket / CAPACITY);

	UINT8 notFull = FALSE;
	while (!queue->Slots[slot].Full) // Acquire, serialization with 1 writer.
		WaitOnAddress(&queue->Slots[slot].Full, &notFull, sizeof notFull, INFINITE);
	
	int item = queue->Slots[slot].Item;
	queue->Slots[slot].Full = FALSE;
	queue->Slots[slot].Turn = turn + 1; // Release, serialization with 1 writer.
	WakeByAddressAll((void *)&queue->Slots[slot].Turn); // Hash table crawl.
	return item;
}

// Polling API

BOOL TryEnqueue(volatile struct Queue *queue, int item)
{
	UINT32 tryTicket = queue->WriteTicket; // Atomic load relaxed. Serialization with writers.
	for (;;)
	{
		UINT32 slot = tryTicket % CAPACITY;
		UINT8 turn = (UINT8)(tryTicket / CAPACITY);
		UINT8 currentTurn = queue->Slots[slot].Turn; // Acquire, serialization with reader.

		int turnsRemaining = (int)turn - (int)currentTurn;
		if (turnsRemaining > 0)
			return FALSE; // Queue is full.
		else if (turnsRemaining < 0)
			tryTicket = queue->WriteTicket; // Turn increased in between us getting the ticket and now: someone lapped us.
		else
		{
			UINT32 ticket = InterlockedCompareExchangeNoFence((volatile LONG *)&queue->WriteTicket, tryTicket + 1, tryTicket); // Serialization with writers.
			if (ticket == tryTicket)
			{
				queue->Slots[slot].Item = item;
				queue->Slots[slot].Full = TRUE; // Release, serialization with reader.
				WakeByAddressSingle((void *)&queue->Slots[slot].Full); // Hash table lookup. Remove this if you only use Polling and not Blocking.
				return TRUE;
			}
			tryTicket = ticket;
		}
	}
}
BOOL TryDequeue(volatile struct Queue *queue, int *outItem)
{
	UINT32 ticket = queue->ReadTicket;
	UINT32 slot = ticket % CAPACITY;
	if (!queue->Slots[slot].Full) // Acquire, serialization with 1 writer.
		return FALSE;

	UINT8 turn = (UINT8)(ticket / CAPACITY);
	(*outItem) = queue->Slots[slot].Item;
	queue->Slots[slot].Full = FALSE;
	queue->Slots[slot].Turn = turn + 1; // Release, serialization with 1 writer.
	WakeByAddressAll((void *)&queue->Slots[slot].Turn); // Hash table crawl.
	++(queue->ReadTicket);
	return TRUE;
}

// Test

#include <assert.h>

DWORD __stdcall ReaderThread(void *parameter)
{
	struct Queue *queue = parameter;
	static LONG counters[5][1000000];
	int lastWriterData[5] = { -1, -1, -1, -1, -1 };
	for (int i = 0; i < 5000000; ++i)
	{
		int item;
		if (i < 2500000)
			item = Dequeue(queue);
		else
			while (!TryDequeue(queue, &item));
		int writer = item / 1000000;
		int data = item % 1000000;
		assert(writer < 5); // Ensure no data corruption corruption.
		++(counters[writer][data]);
		assert(lastWriterData[writer] < data); // Ensure data is correctly sequenced FIFO.
		lastWriterData[writer] = data;
	}
	for (int writerId = 0; writerId < 5; ++writerId)
		for (int i = 0; i < 1000000; ++i)
			assert(counters[writerId][i] == 1); // Ensure all items have been properly received.

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
	threads[1] = CreateThread(NULL, 0, WriterThread, &queue, 0, NULL);
	threads[2] = CreateThread(NULL, 0, WriterThread, &queue, 0, NULL);
	threads[3] = CreateThread(NULL, 0, WriterThread, &queue, 0, NULL);
	threads[4] = CreateThread(NULL, 0, WriterThread, &queue, 0, NULL);
	threads[5] = CreateThread(NULL, 0, WriterThread, &queue, 0, NULL);
	WaitForMultipleObjects(6, threads, TRUE, INFINITE);
	__debugbreak();
}
