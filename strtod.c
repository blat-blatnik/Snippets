// 180 line strtod replacement.
// - no dependencies at all.
// - supports fixed-point, scientific, and hex-float notation.
// - hex-floats can round-trip.
// - fixed-point and scientific are accurate to ~15 decimal places.
// - largest issue is that DBL_MAX parses as INFINITY.

#include <math.h> // Only for INFINITY && NAN.
#include <stdbool.h>

static double bb_strtod(const char* str, char** end) {
	if (end) *end = (char*)str;

	// Skip leading whitespace.
	while (*str == ' ' || (*str >= '\t' && *str <= '\r'))
		str++;

	// Parse optional sign.
	bool negative = *str == '-';
	str += *str == '-' || *str == '+';

	// Determine if this is a NaN, infinity, or normal number.
	double result = 0;
	if ((str[0] == 'n' || str[0] == 'N') && (str[1] == 'a' || str[1] == 'A') && (str[2] == 'n' || str[2] == 'N')) {
		str += 3;
		if (*str == '(') {
			// Parse optional NaN character sequence.
			const char* backup = str++;
			while ((*str >= '0' && *str <= '9') || (*str >= 'A' && *str <= 'Z') || (*str >= 'a' && *str <= 'z') || *str == '_')
				str++;
			if (*str == ')')
				str++;
			else
				str = backup;
		}
		result = NAN;
	}
	else if ((str[0] == 'i' || str[0] == 'I') && (str[1] == 'n' || str[1] == 'N') && (str[2] == 'f' || str[2] == 'F')) {
		if ((str[3] == 'i' || str[3] == 'I') &&
			(str[4] == 'n' || str[4] == 'N') &&
			(str[5] == 'i' || str[5] == 'I') &&
			(str[6] == 't' || str[6] == 'T') &&
			(str[7] == 'y' || str[7] == 'Y')) {
			str += 8;
		}
		else str += 3;
		result = INFINITY;
	} else {
		// This is a normal float, not a NaN or infinity.
		// Parse the base. We support decimal and hex floats.
		unsigned base;
		int max_digits;
		char exponent_separator;
		if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
			base = 16;
			max_digits = 14;
			exponent_separator = 'p';
			str += 2;
		} else {
			base = 10;
			max_digits = 19;
			exponent_separator = 'e';
		}

		// Check if there's at least 1 digit.
		if ((unsigned)*str - '0' >= base)
			return 0;

		// Skip leading zeros.
		while (*str == '0') str++;
		bool dot = *str == '.';
		str += dot;
		int num_leading_zeros_after_dot = 0;
		while (*str == '0') {
			str++;
			num_leading_zeros_after_dot++;
		}

		// Parse digits before exponent.
		unsigned long long digits = 0;
		int num_digits = 0;
		int num_digits_after_dot = 0;
		int num_truncated_digits_before_dot = 0;
		for (;;) {
			if (*str == '.') {
				if (dot) break; // Second dot.
				dot = true;
				str++;
			} else {
				unsigned digit = *str;
				if (digit >= '0' && digit <= '9') digit -= '0';
				else if (base == 16 && digit >= 'A' && digit <= 'F') digit = digit - 'A' + 10;
				else if (base == 16 && digit >= 'a' && digit <= 'f') digit = digit - 'a' + 10;
				else break;
				if (num_digits < max_digits) {
					digits = digits * base + digit;
					num_digits++;
					num_digits_after_dot += dot;
				} else num_truncated_digits_before_dot += !dot;
				str++;
			}
		}

		// Parse optional exponent.
		int exponent = 0;
		if (*str == exponent_separator || *str == exponent_separator - 'a' + 'A') {
			// Backup in case exponent parsing fails.
			const char* backup = str++;

			// Parse optional exponent sign.
			bool negative_exp = *str == '-';
			str += *str == '-' || *str == '+';

			// Check if we actually have a valid exponent.
			if (*str >= '0' && *str <= '9') {
				// Parse the exponent.
				do {
					exponent = exponent * 10 + (*str++ - '0');
					if (exponent > 9999) exponent = 9999; // Prevent overflow.
				} while (*str >= '0' && *str <= '9');
				if (negative_exp) exponent = -exponent;
			} else str = backup;
		}

		// Now assemble the result!
		if (digits != 0) {
			if (base == 16) {
				// Move dot after the first digit.
				int shift = (num_digits + num_truncated_digits_before_dot) - num_digits_after_dot - 1;
				if (num_leading_zeros_after_dot > 0)
					shift -= num_leading_zeros_after_dot;
				exponent += shift * 4;

				// Move first hex digit before floating point. The exponent was already adjusted for this.
				while (!(digits & 0xF0000000000000))
					digits <<= 4;

				// Truncate to 53 bit double mantissa.
				while (digits & 0xE0000000000000) {
					digits >>= 1;
					exponent++;
				}

				// Produce denormal floats.
				while (exponent < -1023 && digits) {
					digits >>= 1;
					exponent++;
				}

				// Check for overflow to infinity or underflow to denormal.
				if (exponent > 1023) {
					exponent = 1024;
					digits = 0;
				}
				if (exponent < -1023) exponent = -1023;

				// Assemble the float.
				unsigned long long exp = (unsigned long long)(exponent + 1023);
				union { unsigned long long u; double f; } fu = { (exp << 52) | (digits & 0xFFFFFFFFFFFFF) };
				return fu.f;
			} else {
				// Adjust exponent to account for leading zeros and truncated digits.
				exponent += num_truncated_digits_before_dot;
				exponent -= num_leading_zeros_after_dot;

				// Right shift digits to correct decimal place.
				unsigned long long shift = 1;
				for (int i = 0; i < num_digits_after_dot; i++)
					shift *= 10;
				result = (double)digits / (double)shift;

				if (exponent) {
					// Compute 10^abs(exponent) using binary exponentiation.
					int exp = exponent;
					if (exp < 0) exp = -exp;
					double scale = 1;
					static const double BINARY_POWERS_OF_10[9] = { 1e256, 1e128, 1e64, 1e32, 1e16, 1e8, 1e4, 1e2, 1e1 };
					for (int i = 0, decrement = 256; i < 9; i++, decrement >>= 1) {
						if (exp >= decrement) {
							exp -= decrement;
							scale *= BINARY_POWERS_OF_10[i];
						}
					}

					// Scale by the exponent.
					if (exponent >= 0)
						result *= scale;
					else
						result /= scale;
				}
			}
		}
	}

	if (end) *end = (char*)str;
	return negative ? -result : +result;
}

// === testing ===

#include <stdio.h>

int main(void) {
	// fixed point
	printf("%f\n", bb_strtod("123", NULL));
	printf("%f\n", bb_strtod("+123", NULL));
	printf("%f\n", bb_strtod("-123", NULL));
	printf("%f\n", bb_strtod("123.456", NULL));
	printf("%f\n", bb_strtod("0", NULL));
	printf("%.20f\n", bb_strtod("0.1234567890", NULL));
	printf("%.20f\n", bb_strtod("1234567890.0", NULL));
	printf("%.20f\n", bb_strtod("1234567890.1234567890", NULL));
	printf("%.20f\n", bb_strtod("999999999999999999999999999999999.0", NULL));
	printf("%.20f\n", bb_strtod("0.999999999999999999999999999999999", NULL));
	printf("%e\n", bb_strtod("100000000000000", NULL));
	printf("%e\n", bb_strtod("10000000000000", NULL));
	printf("%e\n", bb_strtod("1000000000000", NULL));
	printf("%e\n", bb_strtod("100000000000", NULL));
	printf("%e\n", bb_strtod("10000000000", NULL));
	printf("%e\n", bb_strtod("1000000000", NULL));
	printf("%e\n", bb_strtod("100000000", NULL));
	printf("%e\n", bb_strtod("10000000", NULL));
	printf("%e\n", bb_strtod("1000000", NULL));
	printf("%e\n", bb_strtod("100000", NULL));
	printf("%e\n", bb_strtod("10000", NULL));
	printf("%e\n", bb_strtod("1000", NULL));
	printf("%e\n", bb_strtod("100", NULL));
	printf("%e\n", bb_strtod("10", NULL));
	printf("%e\n", bb_strtod("1", NULL));
	printf("%e\n", bb_strtod("0.1", NULL));
	printf("%e\n", bb_strtod("0.01", NULL));
	printf("%e\n", bb_strtod("0.001", NULL));
	printf("%e\n", bb_strtod("0.0001", NULL));
	printf("%e\n", bb_strtod("0.00001", NULL));
	printf("%e\n", bb_strtod("0.000001", NULL));
	printf("%e\n", bb_strtod("0.0000001", NULL));
	printf("%e\n", bb_strtod("0.00000001", NULL));
	printf("%e\n", bb_strtod("0.000000001", NULL));
	printf("%e\n", bb_strtod("0.0000000001", NULL));
	printf("%e\n", bb_strtod("0.00000000001", NULL));
	printf("%e\n", bb_strtod("0.000000000001", NULL));
	printf("%e\n", bb_strtod("0.0000000000001", NULL));
	printf("%e\n", bb_strtod("0.00000000000001", NULL));
	printf("%e\n", bb_strtod("0.000000000000001", NULL));
	printf("%e\n", bb_strtod("0.0000000000000001", NULL));
	printf("%e\n", bb_strtod("0.00000000000000001", NULL));
	printf("%e\n", bb_strtod("0.000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.0000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.00000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.000000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.0000000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.00000000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.000000000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.0000000000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.00000000000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.000000000000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.0000000000000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.00000000000000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.000000000000000000000000000001", NULL));
	printf("%e\n", bb_strtod("0.0000000000000000000000000000001", NULL));
	printf("%f\n", bb_strtod("01", NULL));
	printf("%f\n", bb_strtod("001", NULL));
	printf("%f\n", bb_strtod("0001", NULL));
	printf("%f\n", bb_strtod("00001", NULL));
	printf("%f\n", bb_strtod("000001", NULL));
	printf("%f\n", bb_strtod("000001.000", NULL));
	printf("%f\n", bb_strtod("00001.000", NULL));
	printf("%f\n", bb_strtod("0001.000", NULL));
	printf("%f\n", bb_strtod("001.000", NULL));
	printf("%f\n", bb_strtod("01.000", NULL));
	printf("%f\n", bb_strtod("000.100", NULL));
	printf("%f\n", bb_strtod("000.010", NULL));
	printf("%f\n", bb_strtod("000.001", NULL));
	printf("%f\n", bb_strtod("000.101", NULL));

	// edge cases
	printf("%f\n", bb_strtod("-0", NULL));
	printf("%f\n", bb_strtod("nan", NULL));
	printf("%f\n", bb_strtod("-NAN", NULL));
	printf("%f\n", bb_strtod("inf", NULL));
	printf("%f\n", bb_strtod("-INF", NULL));
	printf("%f\n", bb_strtod("infinity", NULL));
	printf("%f\n", bb_strtod("-INFINITY", NULL));
	printf("%f\n", bb_strtod("-INFINITY", NULL));
	printf("%e\n", bb_strtod("1.7976931348623157e+308", NULL)); // Unfortunately DBL_MAX parses as INFINITY.

	// scientific notation
	printf("%e\n", bb_strtod("1e0", NULL));
	printf("%e\n", bb_strtod("1e1", NULL));
	printf("%e\n", bb_strtod("1e+1", NULL));
	printf("%e\n", bb_strtod("1e-1", NULL));
	printf("%e\n", bb_strtod("1.23e+45", NULL));
	printf("%e\n", bb_strtod("0e0", NULL));
	printf("%e\n", bb_strtod("1.234567e300", NULL));
	printf("%e\n", bb_strtod("1.234567e-300", NULL));
	printf("%e\n", bb_strtod("1e999", NULL));
	printf("%e\n", bb_strtod("-1e999", NULL));
	printf("%e\n", bb_strtod("1e-999", NULL));
	printf("%e\n", bb_strtod("1.797693e+308", NULL));
	printf("%e\n", bb_strtod("2.225073e-308", NULL));
	printf("%e\n", bb_strtod("1e-309", NULL));

	// hexfloat
	printf("%a\n", bb_strtod("0x1.FFFFFFFFFFFFFp+1023", NULL));
	printf("%a\n", bb_strtod("0x2.0000000000000p+1023", NULL));
	printf("%a\n", bb_strtod("0x1.FFFFFFFFFFFFFFp+1023", NULL));
	printf("%a\n", bb_strtod("0x1.0000000000000p+1024", NULL));
	printf("%a\n", bb_strtod("0x1.0000000000000p-1022", NULL));
	printf("%a\n", bb_strtod("0x0.0000000000001p-1023", NULL));
	printf("%a\n", bb_strtod("0x0.DE00000000000p-1023", NULL));
	printf("%a\n", bb_strtod("0x0.000DE00000000p-1023", NULL));
	printf("%a\n", bb_strtod("0x1.0000000000000p-1075", NULL));
	printf("%a\n", bb_strtod("0x10.0000000000000p-1079", NULL));
	printf("%a\n", bb_strtod("0x0.0000000000001p-1024", NULL));
	printf("%a\n", bb_strtod("0x0.00000000000001p-1023", NULL));
	printf("%a\n", bb_strtod("0x0.00000000000000001p-1023", NULL));
	printf("%a\n", bb_strtod("0x1FFFFFFFFFFFFF.0p+971", NULL));
	printf("%a\n", bb_strtod("0x1FFFFFFFFFFFFF0.0p+967", NULL));
	printf("%a\n", bb_strtod("0x1FFFFFFFFFFFFF00.0p+963", NULL));
	printf("%a\n", bb_strtod("0x1FFFFFFFFFFFFF000.0p+959", NULL));
	printf("%a\n", bb_strtod("0x1FFFFFFFFFFFFF0000.0p+955", NULL));
	printf("%a\n", bb_strtod("0x123.456p+78", NULL));
}

