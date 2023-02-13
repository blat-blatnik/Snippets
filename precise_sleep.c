// The PERFECT sleeping function for Windows.
// - Sleep times accurate to 1 microsecond
// - Low CPU usage
// - Runs on Windows Vista and up

#include <Windows.h>
#include <stdio.h>
#pragma comment(lib, "Winmm.lib") // timeGetDevCaps, timeBeginPeriod

HANDLE Timer;
int SchedulerPeriodMs;
INT64 QpcPerSecond;

void PreciseSleep(double seconds)
{
	LARGE_INTEGER qpc;
	QueryPerformanceCounter(&qpc);
	INT64 targetQpc = (INT64)(qpc.QuadPart + seconds * QpcPerSecond);

	if (Timer) // Try using a high resolution timer first.
	{
		const double TOLERANCE = 0.001'02;
		INT64 maxTicks = (INT64)SchedulerPeriodMs * 9'500;
		for (;;) // Break sleep up into parts that are lower than scheduler period.
		{
			double remainingSeconds = (targetQpc - qpc.QuadPart) / (double)QpcPerSecond;
			INT64 sleepTicks = (INT64)((remainingSeconds - TOLERANCE) * 10'000'000);
			if (sleepTicks <= 0)
				break;

			LARGE_INTEGER due;
			due.QuadPart = -(sleepTicks > maxTicks ? maxTicks : sleepTicks);
			SetWaitableTimerEx(Timer, &due, 0, NULL, NULL, NULL, 0);
			WaitForSingleObject(Timer, INFINITE);
			QueryPerformanceCounter(&qpc);
		}
	}
	else // Fallback to Sleep.
	{
		const double TOLERANCE = 0.000'02;
		double sleepMs = (seconds - TOLERANCE) * 1000 - SchedulerPeriodMs; // Sleep for 1 scheduler period less than requested.
		int sleepSlices = (int)(sleepMs / SchedulerPeriodMs);
		if (sleepSlices > 0)
			Sleep((DWORD)sleepSlices * SchedulerPeriodMs);
		QueryPerformanceCounter(&qpc);
	}

	while (qpc.QuadPart < targetQpc) // Spin for any remaining time.
	{
		YieldProcessor();
		QueryPerformanceCounter(&qpc);
	}
}

int main(void)
{
	// Initialization
	Timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	TIMECAPS caps;
	timeGetDevCaps(&caps, sizeof caps);
	timeBeginPeriod(caps.wPeriodMin);
	SchedulerPeriodMs = (int)caps.wPeriodMin;
	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);
	QpcPerSecond = qpf.QuadPart;

	// Game loop
	for (int i = 0; i < 100; ++i)
	{
		LARGE_INTEGER qpc0, qpc1;
		QueryPerformanceCounter(&qpc0);
		PreciseSleep(1 / 60.0);
		QueryPerformanceCounter(&qpc1);
		double dt = (qpc1.QuadPart - qpc0.QuadPart) / (double)QpcPerSecond;
		printf("Slept for %.2f ms\n", 1000 * dt);
	}
}
