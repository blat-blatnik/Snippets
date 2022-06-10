#include <Windows.h>
#include <stdio.h>

volatile int cursor;
BOOL is_prime[1048576];

BOOL prime(int x) {
	if (x == 2)
		return TRUE;
	if (x <= 1 || !(x % 2))
		return FALSE;
	for (INT64 i = 3; i * i <= x; i += 2)
		if (!(x % i))
			return FALSE;
	return TRUE;
}
DWORD CALLBACK thread_function(void *param) {
	for (;;) {
		int index = InterlockedIncrement(&cursor) - 1;
		if (index >= _countof(is_prime))
			return 0;
		is_prime[index] = prime(index);
	}
}

int main(void) {
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	
	HANDLE threads[MAXIMUM_WAIT_OBJECTS];
	int num_extra_threads = (int)info.dwNumberOfProcessors - 1;
	if (num_extra_threads > _countof(threads))
		num_extra_threads = _countof(threads);
	
	printf("Creating %d worker threads.\n", num_extra_threads);
	for (int i = 0; i < num_extra_threads; ++i)
		threads[i] = CreateThread(NULL, 0, thread_function, NULL, 0, NULL);
	
	thread_function(NULL);
	WaitForMultipleObjects((DWORD)num_extra_threads, threads, TRUE, INFINITE);

	for (int i = 0; i < _countof(is_prime); ++i)
		if (is_prime[i])
			printf("%d is prime.\n", i);
}