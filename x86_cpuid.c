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
#else
#	include <cpuid.h>
	void cpuid(int leaf, int subleaf, int *eax, int *ebx, int *ecx, int *edx) {
		__cpuid_count(leaf, subleaf, *eax, *ebx, *ecx, *edx);
	}
	int cpuid_is_supported(void) {
		return __get_cpuid_max(0, 0) != 0;
	}
#endif

#include <stdio.h>
#include <string.h>
int extract_bits(int x, int highest, int lowest) {
	unsigned u = (int)x;
	u <<=  31 - highest;
	u >>= (31 - highest) + lowest;
	return (int)u;
}
int extract_bit(int x, int index) {
	return (int)(((unsigned)x >> index) & 1);
}
int main(void) {
	if (!cpuid_is_supported()) {
		printf("CPUID is not supported on this CPU!\n");
		return 0;
	}

	int eax, ebx, ecx, edx;
	cpuid(0, 0, &eax, &ebx, &ecx, &edx);

	int max_cpuid = eax;
	printf("Max CPUID leaf: %d.\n", max_cpuid);

	char vendor[13];
	memcpy(vendor + 0, &ebx, 4);
	memcpy(vendor + 4, &edx, 4); // Note that the string is in ebx:eDx:eCx.
	memcpy(vendor + 8, &ecx, 4);
	vendor[12] = 0;
	printf("Vendor: '%s'.\n", vendor);

	if (max_cpuid < 1)
		return 0;

	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	int stepping_id   = extract_bits(eax,  3, 0);
	int model_id      = extract_bits(eax,  7, 4);
	int family_id     = extract_bits(eax, 11, 8);
	int ext_model_id  = extract_bits(eax, 19, 16);
	int ext_family_id = extract_bits(eax, 27, 20);
	
	int actual_family_id = family_id;
	if (family_id == 15)
		actual_family_id += ext_family_id;

	int actual_model_id = model_id;
	if (family_id == 6 || family_id == 15)
		actual_model_id += (ext_model_id << 4);

	printf("Family: %d.\n", actual_family_id);
	printf("Model: %d.\n", actual_model_id);
	printf("Stepping: %d.\n", stepping_id);

	int supports_hyperthreading_in_theory = extract_bit(edx, 28); // Doesn't mean the CPU is actually hyperthreaded..
	int has_clflush = extract_bit(edx, 19);
	int cache_line_size = 0;
	if (has_clflush)
		cache_line_size = 8 * extract_bits(ebx, 15, 8);

	cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
	int max_cpuid_ex = eax;
	char name[48] = "Unknown";
	if (max_cpuid_ex >= 0x80000004) {
		cpuid(0x80000002, 0, (int *)name + 0, (int *)name + 1, (int *)name +  2, (int *)name +  3);
		cpuid(0x80000003, 0, (int *)name + 4, (int *)name + 5, (int *)name +  6, (int *)name +  7);
		cpuid(0x80000004, 0, (int *)name + 8, (int *)name + 9, (int *)name + 10, (int *)name + 11);
	}
	printf("Name: %s\n", name);
	printf("Cache line size: %d bytes.\n", cache_line_size);

	int num_logical_cores = 1;
	int num_physical_cores = 1;
	int l1d_cache_size = 0;
	int l1i_cache_size = 0;
	int l2_cache_size = 0;
	int l3_cache_size = 0;
	if (strstr(vendor, "AMD")) {
		if (max_cpuid_ex >= 0x80000008) {
			cpuid(0x80000008, 0, &eax, &ebx, &ecx, &edx);
			num_logical_cores = 1 + extract_bits(ecx, 7, 0);
		} else {
			cpuid(1, 0, &eax, &ebx, &ecx, &edx);
			num_logical_cores = extract_bits(ebx, 23, 16);
		}

		// This really isn't a great indication. Many sources say that CPUID reports hyperthreading even when the processor 
		// doesn't actually support it. But I can't test this right now since I don't have a non-hyperthreaded AMD chip.
		if (supports_hyperthreading_in_theory)
			num_physical_cores = num_logical_cores / 2;
		else
			num_physical_cores = num_logical_cores;

		if (max_cpuid_ex >= 0x80000005) {
			cpuid(0x80000005, 0, &eax, &ebx, &ecx, &edx);
			l1d_cache_size = extract_bits(ecx, 31, 24);
			l1i_cache_size = extract_bits(edx, 31, 24);
		}

		if (max_cpuid_ex >= 0x80000006) {
			cpuid(0x80000006, 0, &eax, &ebx, &ecx, &edx);
			l2_cache_size = extract_bits(ecx, 31, 16);
			l3_cache_size = 512 * extract_bits(edx, 31, 18); // This is reported in units of 512kB.
		}
	} else if (strstr(vendor, "Intel")) {
		if (max_cpuid >= 4) {
			cpuid(4, 0, &eax, &ebx, &ecx, &edx);

			// The value reported here is not accurate (I'm not sure if that's always the case).
			// On an i5-7300HQ it reports 8 logical cores with hyperthreading, even though that CPU
			// doesn't have hyperthreading.. Still this is a decent approximation at least.
			num_logical_cores  = 1 + extract_bits(eax, 31, 26);
			num_physical_cores = num_logical_cores;
			if (supports_hyperthreading_in_theory)
				num_physical_cores /= 2;

			// Enumerate all caches to find out sizes.
			for (int index = 0;; ++index) {
				cpuid(4, index, &eax, &ebx, &ecx, &edx);
				int type = extract_bits(eax, 4, 0); // 0 - invalid, 1 - data cache, 2 - instruction cache, 3 - unified cache.
				if (type == 0)
					break;

				int level = extract_bits(eax, 7, 5);
				int ways       = 1 + extract_bits(ebx, 31, 22);
				int partitions = 1 + extract_bits(ebx, 21, 12);
				int line_size  = 1 + extract_bits(ebx, 11, 0);
				int sets       = 1 + extract_bits(ecx, 31, 0);
				int cache_size = ways * partitions * line_size * sets / 1024;

				if (level == 1) {
					if (type == 1)
						l1d_cache_size = cache_size;
					else if (type == 2)
						l1i_cache_size = cache_size;
					else if (type == 3) {
						// For unified L1 caches, set instruction cache size to 0 and set data cache size to the actual cache size.
						l1i_cache_size = 0;
						l1d_cache_size = cache_size;
					}
				}
				else if (level == 2) 
					l2_cache_size = cache_size;
				else if (level == 3)
					l3_cache_size = cache_size;
			}
		}

		if (max_cpuid >= 0xB) {
			// This is a much better way of checking the number of cores than with cpuid(4) above.
			// At least this one is accurate on a i5-7300HQ and i7-8550U.
			cpuid(0xB, 0, &eax, &ebx, &ecx, &edx);
			int num_logical_processors_per_physical_core = extract_bits(ebx, 15, 0);
			cpuid(0xB, 1, &eax, &ebx, &ecx, &edx);
			num_logical_cores  = extract_bits(ebx, 15, 0);
			num_physical_cores = num_logical_cores / num_logical_processors_per_physical_core;
		}
	}
	printf("Logical cores: %d.\n", num_logical_cores);
	printf("Physical cores: %d.\n", num_physical_cores);
	printf("L1i cache size: %d kB.\n", l1i_cache_size);
	printf("L1d cache size: %d kB.\n", l1d_cache_size);
	printf("L2 cahce size: %d kB.\n", l2_cache_size);
	printf("L3 cahce size: %d kB.\n", l3_cache_size);

	printf("Feature flags: ");
	cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	if (extract_bit(edx,  8)) printf("cx8 ");
	if (extract_bit(ecx, 13)) printf("cx16 ");
	if (extract_bit(edx,  4)) printf("tsc ");
	if (extract_bit(edx, 15)) printf("cmov ");
	if (extract_bit(edx, 23)) printf("mmx ");
	if (extract_bit(edx, 25)) printf("sse ");
	if (extract_bit(edx, 26)) printf("sse2 ");
	if (extract_bit(ecx,  0)) printf("sse3 ");
	if (extract_bit(ecx,  9)) printf("ssse3 ");
	if (extract_bit(ecx, 19)) printf("sse41 ");
	if (extract_bit(ecx, 20)) printf("sse42 ");
	if (extract_bit(ecx, 28)) printf("avx ");
	if (extract_bit(ecx, 12)) printf("fma ");
	if (extract_bit(ecx, 29)) printf("f16c ");
	if (extract_bit(ecx,  1)) printf("pclmulqdq ");
	if (extract_bit(ecx, 22)) printf("movbe ");
	if (extract_bit(ecx, 23)) printf("popcnt ");
	if (extract_bit(ecx, 25)) printf("aes ");
	if (extract_bit(ecx, 30)) printf("rdrnd ");

	eax = ebx = ecx = edx = 0;
	if (max_cpuid >= 7)
		cpuid(7, 0, &eax, &ebx, &ecx, &edx);
	int max_cpuid_7 = eax;

	if (extract_bit(ebx,  5)) printf("avx2 ");
	if (extract_bit(ebx, 16)) printf("avx512_f ");
	if (extract_bit(ebx, 17)) printf("avx512_dq ");
	if (extract_bit(ebx, 21)) printf("avx512_ifma ");
	if (extract_bit(ebx, 26)) printf("avx512_pf ");
	if (extract_bit(ebx, 27)) printf("avx512_er ");
	if (extract_bit(ebx, 28)) printf("avx512_cd ");
	if (extract_bit(ebx, 30)) printf("avx512_bw ");
	if (extract_bit(ebx, 31)) printf("avx512_vl ");
	if (extract_bit(ecx,  1)) printf("avx512_vbmi ");
	if (extract_bit(ecx,  6)) printf("avx512_vbmi2 ");
	if (extract_bit(ecx, 11)) printf("avx512_vnni ");
	if (extract_bit(ecx, 12)) printf("avx512_bitalg ");
	if (extract_bit(ecx, 14)) printf("avx512_vpopcntdq ");
	if (extract_bit(edx,  2)) printf("avx512_4vnniw ");
	if (extract_bit(edx,  3)) printf("avx512_4fmaps ");
	if (extract_bit(edx,  8)) printf("avx512_vp2intersect ");
	if (extract_bit(edx, 23)) printf("avx512_fp16 ");
	if (extract_bit(ebx,  3)) printf("bmi1 ");
	if (extract_bit(ebx,  8)) printf("bmi2 ");
	if (extract_bit(ebx, 29)) printf("sha ");
	if (extract_bit(ebx, 18)) printf("rdseed ");
}