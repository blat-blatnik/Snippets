// Source: https://gist.github.com/Mic92/12063527bb6d6c5a636502300d2de446

#ifdef _MSC_VER
#	include <intrin.h>
	void cpuid(int leaf, int subleaf, int *eax, int *ebx, int *ecx, int *edx) {
		int registers[4];
		if (subleaf == 0)
			__cpuid(registers, leaf);
		else
			__cpuidex(registers, leaf, subleaf);
		*eax = registers[0];
		*ebx = registers[1];
		*ecx = registers[2];
		*edx = registers[3];
	}
	int cpuid_is_supported(void) {
		// Try to set and clear bit 21 in the flags register. This indicates support for the CPUID instruction.
		// Bail out immediately if it's not supported.
		unsigned bit21 = 1u << 21;

		__writeeflags(__readeflags() | bit21);
		if ((__readeflags() & bit21) == 0)
			return 0;

		__writeeflags(__readeflags() & ~bit21);
		if ((__readeflags() & bit21) == 1)
			return 0;

		return 1;
	}
	unsigned long long rdtsc(void) {
		return __rdtsc();
	}
#else
#	include <cpuid.h>
	void cpuid(int leaf, int subleaf, int *eax, int *ebx, int *ecx, int *edx) {
		__cpuid_count(leaf, subleaf, *eax, *ebx, *ecx, *edx);
	}
	int cpuid_is_supported(void) {
		return __get_cpuid_max(0, 0) != 0;
	}
	unsigned long long rdtsc(void) {
		unsigned lo, hi;
		asm volatile("rdtsc" : "=a" (lo), "=d" (hi)); // RDTSC copies contents of 64-bit TSC into EDX:EAX
		return lo | ((unsigned long long)hi << 32);
	}
#endif

unsigned long long get_tsc_increments_per_second() {
	if (!cpuid_is_supported())
		return 0;

	// extracted from https://github.com/torvalds/linux/blob/b95fffb9b4afa8b9aa4a389ec7a0c578811eaf42/tools/power/x86/turbostat/turbostat.c
	int eax_crystal = 0;
	int ebx_tsc = 0;
	int crystal_hz = 0;
	int edx = 0;
	cpuid(0x15, 0, &eax_crystal, &ebx_tsc, &crystal_hz, &edx);
	if (!ebx_tsc) // This will not work on old Intel processors, or any AMD processor. You really need a fallback..
		return 0;

	int fms, family, model, ebx, ecx;
	cpuid(1, 0, &fms, &ebx, &ecx, &edx);
	family = (fms >> 8) & 0xf;
	model  = (fms >> 4) & 0xf;
	if (family == 0xf)
		family += (fms >> 20) & 0xff;
	if (family >= 6)
		model += ((fms >> 16) & 0xf) << 4;

	enum {
		INTEL_FAM6_SKYLAKE_L          = 0x4E,
		INTEL_FAM6_SKYLAKE            = 0x5E,
		INTEL_FAM6_KABYLAKE_L         = 0x8E,
		INTEL_FAM6_KABYLAKE           = 0x9E,
		INTEL_FAM6_COMETLAKE          = 0xA5,
		INTEL_FAM6_COMETLAKE_L        = 0xA6,
		INTEL_FAM6_ATOM_GOLDMONT      = 0x5C,
		INTEL_FAM6_ATOM_GOLDMONT_D    = 0x5F,
		INTEL_FAM6_ATOM_GOLDMONT_PLUS = 0x7A,
		INTEL_FAM6_ATOM_TREMONT_D     = 0x86,
	};

	if (!crystal_hz) {
		switch(model) {
			case INTEL_FAM6_SKYLAKE_L:
			case INTEL_FAM6_SKYLAKE:
			case INTEL_FAM6_KABYLAKE_L:
			case INTEL_FAM6_KABYLAKE:
			case INTEL_FAM6_COMETLAKE_L:
			case INTEL_FAM6_COMETLAKE:
				crystal_hz = 24000000;
				break;
			case INTEL_FAM6_ATOM_GOLDMONT_D:
			case INTEL_FAM6_ATOM_TREMONT_D:
				crystal_hz = 25000000;
				break;
			case INTEL_FAM6_ATOM_GOLDMONT:
			case INTEL_FAM6_ATOM_GOLDMONT_PLUS:
				crystal_hz = 19200000;
				break;
		}
	}

	return (unsigned long long)crystal_hz * ebx_tsc / eax_crystal;
}

#include <stdio.h>
#include <time.h>
int main(void) {
	unsigned long long tsc_hz = get_tsc_increments_per_second();
	if (!tsc_hz) {
		printf("Couldn't get TSC frequency on this CPU.\n");
		return 0;
	}

	double tsc_to_seconds = 1.0 / tsc_hz;
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	unsigned long long ts0 = (unsigned long long)ts.tv_sec * 1000000000 + ts.tv_nsec;
	unsigned long long tsc0 = rdtsc();
	for (;;) {
		timespec_get(&ts, TIME_UTC);
		unsigned long long ts1 = (unsigned long long)ts.tv_sec * 1000000000 + ts.tv_nsec;
		unsigned long long tsc1 = rdtsc();
		double tsdt = (ts1 - ts0) * 1e-9;
		double tscdt = (tsc1 - tsc0) * tsc_to_seconds;
		printf("TS %.9f - TSC %.9f\n", tsdt, tscdt);
	}
}