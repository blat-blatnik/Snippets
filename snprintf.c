// 500 line snprintf replacement.
// - no dependencies except for va_start,va_arg,va_end.
// - almost completely standards compliant.
// - supports UTF8.
// - supports all float specifiers but not correctly rounded.
// - all in a single function.
//
// The main difference from standard snprintf is that this
// one returns the actual number of characters written to
// the buffer, rather than the number of characters that
// *would* have been written if the buffer was large enough:
//      snprintf(buf, 3, "12345") writes 2 chars and returns 5.
//   bb_snprintf(buf, 3, "12345") writes 2 chars and returns 2.
//
// The floating point specifiers are correct to ~16 decimal digits.
// The hex-float specifier is completely correct and can round-trip.

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static int bb_vsnprintf(char dst[], int max, const char* fmt, va_list args) {
	// This function can be called from a lot of strange debugging contexts
	// so it's important that it handles all possible input values correctly.
	// This includes crazy stuff like an INT_MAX width or precision field.
	if (!dst || max <= 0) return 0;
	if (!fmt) fmt = "(null)";

	int write = 0;
	int max_write = max - 1;
	while (write < max_write && *fmt) {
		if (*fmt != '%')
			dst[write++] = *fmt++;
		else {
			// The full specifier looks like this: %[flags][width][.precision][length]specifier.
			fmt++;
			
			// Parse flags.
			bool minus = false;
			bool plus = false;
			bool space = false;
			bool hash = false;
			bool zero = false;
			while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' || *fmt == '0') {
				minus |= *fmt == '-';
				plus |= *fmt == '+';
				space |= *fmt == ' ';
				hash |= *fmt == '#';
				zero |= *fmt == '0';
				fmt++;
			}
			
			// Parse width.
			int width = 0;
			if (*fmt == '*') {
				fmt++;
				width = va_arg(args, int);
				if (width < 0)
					width = 0;
			}
			else if (*fmt >= '1' && *fmt <= '9') {
				unsigned u = 0;
				do {
					unsigned prev = u;
					u *= 10;
					u += *fmt++ - '0';
					if (prev && u <= prev) u = UINT_MAX;
				} while (*fmt >= '0' && *fmt <= '9');
				if (u > INT_MAX) u = INT_MAX;
				width = (int)u;
			}
			
			// Parse precision.
			int precision = -1;
			if (*fmt == '.') {
				fmt++;
				precision = 0;
				if (*fmt == '*') {
					fmt++;
					precision = va_arg(args, int);
				}
				else if (*fmt >= '0' && *fmt <= '9') {
					unsigned u = 0;
					do {
						unsigned prev = u;
						u *= 10;
						u += *fmt++ - '0';
						if (prev && u <= prev) u = UINT_MAX;
					} while (*fmt >= '0' && *fmt <= '9');
					if (u > INT_MAX) u = INT_MAX;
					precision = (int)u;
				}
			}
			
			// Parse length.
			enum { none, hh, h, l, ll, j, z, t, L } length = none;
			if (*fmt == 'h') {
				fmt++;
				if (*fmt == 'h') {
					fmt++;
					length = hh;
				}
				else length = h;
			}
			else if (*fmt == 'l') {
				fmt++;
				if (*fmt == 'l') {
					fmt++;
					length = ll;
				}
				else length = l;
			}
			else if (*fmt == 'j') {
				fmt++;
				length = j;
			}
			else if (*fmt == 'z') {
				fmt++;
				length = z;
			}
			else if (*fmt == 't') {
				fmt++;
				length = t;
			}
			else if (*fmt == 'L') {
				fmt++;
				length = L;
			}
			
			// Process the specifier.
			int start = write;
			char specifier = *fmt++;
			switch (specifier) {
				case 'c': {
					unsigned int c = va_arg(args, int);
					if (length == l) {
						//@non-standard: %lc appends unicode codepoint as UTF8 instead of wchar_t.
						if (c > 0x10FFFF)
							c = '?'; // Fallback for invalid codepoints.
						
						// Append unicode codepoint.
						if (c <= 0x7F) {
							dst[write++] = (char)c;
						} else if (c <= 0x7FF) {
							if (write < max_write) dst[write++] = (char)(0xC0 | (c >> 6));
							if (write < max_write) dst[write++] = (char)(0x80 | (c & 0x3F));
						} else if (c <= 0xFFFF) {
							if (write < max_write) dst[write++] = (char)(0xE0 | (c >> 12));
							if (write < max_write) dst[write++] = (char)(0x80 | ((c >> 6) & 0x3F));
							if (write < max_write) dst[write++] = (char)(0x80 | (c & 0x3F));
						} else if (c <= 0x10FFFF) {
							if (write < max_write) dst[write++] = (char)(0xF0 | (c >> 18));
							if (write < max_write) dst[write++] = (char)(0x80 | ((c >> 12) & 0x3F));
							if (write < max_write) dst[write++] = (char)(0x80 | ((c >> 6) & 0x3F));
							if (write < max_write) dst[write++] = (char)(0x80 | (c & 0x3F));
						}
					}
					else dst[write++] = (char)c;
				} break;
				case 's': {
					//@non-standard: %ls is not supported.
					const char* s = va_arg(args, const char*);
					if (!s)
						s = "(null)";
					if (precision < 0)
						precision = INT_MAX;
					int limit = max_write - write;
					if (limit > precision)
						limit = precision;
					for (int i = 0; i < limit && s[i]; i++)
						dst[write++] = s[i];
				} break;
				case 'd':
				case 'i':
				case 'u':
				case 'o':
				case 'x':
				case 'X':
				case 'p': {
					// First extract value and whether it's negative.
					bool negative;
					unsigned long long u = 0;
					if (specifier == 'd' || specifier == 'i') {
						// Signed integer.
						long long i;
						if (length == j)
							i = va_arg(args, intmax_t);
						else if (length == z)
							i = va_arg(args, size_t);
						else if (length == t)
							i = va_arg(args, ptrdiff_t);
						else if (length == ll)
							i = va_arg(args, long long);
						else if (length == l)
							i = va_arg(args, long);
						else
							i = va_arg(args, int);
						negative = i < 0;
						if (negative) {
							// -INT_MIN is undefined behavior so be careful.
							if (i < -LLONG_MAX)
								u = (unsigned long long)LLONG_MAX + 1;
							else if (i < 0)
								u = (unsigned long long)-i;
						}
						else u = (unsigned long long)i;
					} else if (specifier == 'p') {
						// Pointer.
						negative = false;
						u = (unsigned long long)va_arg(args, void*);
					} else {
						// Unsigned integer.
						negative = false;
						if (length == j)
							u = va_arg(args, uintmax_t);
						else if (length == z)
							u = va_arg(args, size_t);
						else if (length == t)
							u = va_arg(args, ptrdiff_t);
						else if (length == ll)
							u = va_arg(args, unsigned long long);
						else if (length == l)
							u = va_arg(args, unsigned long);
						else if (length == h)
							u = (unsigned short)va_arg(args, unsigned int);
						else if (length == hh)
							u = (unsigned char)va_arg(args, unsigned int);
						else
							u = va_arg(args, unsigned int);
					}
					
					// Precision of 0 means no output for 0.
					if (precision != 0 || u != 0) {
						if (precision < 0)
							precision = 1;
						
						// Prepend sign.
						if (negative) dst[write++] = '-';
						else if (plus) dst[write++] = '+';
						else if (space) dst[write++] = ' ';
						
						// Prepend 0x for hex or 0 for octal.
						unsigned char base;
						if (specifier == 'o' || specifier == 'x' || specifier == 'X' || specifier == 'p') {
							base = specifier == 'o' ? 8 : 16;
							if (specifier == 'p' || hash) {
								if (write < max_write) dst[write++] = '0';
								if (write < max_write && specifier != 'o') dst[write++] = specifier == 'X' ? 'X' : 'x';
							}
						}
						else base = 10;
						
						// Use lowercase hex characters for %x.
						const char* digits;
						if (specifier == 'x')
							digits = "0123456789abcdef";
						else
							digits = "0123456789ABCDEF";
						
						// Write out number in reverse.
						char reverse[22];
						int reverse_length = 0;
						do {
							reverse[reverse_length++] = digits[u % base];
							u /= base;
						} while (u != 0);
						
						// Pad with leading 0s to fill up precision.
						int num_leading_zeros = precision - reverse_length;
						for (int i = 0; i < num_leading_zeros && write < max_write; i++)
							dst[write++] = '0';
						
						// Append the number in correct order.
						for (int i = reverse_length - 1; i >= 0 && write < max_write; i--)
							dst[write++] = reverse[i];
					}
				} break;
				case 'f':
				case 'F':
				case 'e':
				case 'E':
				case 'g':
				case 'G':
				case 'a':
				case 'A': {
					double f;
					if (length == L)
						f = (double)va_arg(args, long double);
					else
						f = (double)va_arg(args, double);
					
					// Check if negative and then take absolute value.
					// Test the sign bit explicitly to preserve -0.
					union { double f; unsigned long long u; } fu = { f };
					bool negative = fu.u & 0x8000000000000000;
					fu.u &= 0x7FFFFFFFFFFFFFFF;
					
					// Test for NaN. Test the bit pattern explictly to prevent compiler meddling.
					if (fu.u > 0x7FF0000000000000)
					{
						if (specifier == 'f' || specifier == 'e' || specifier == 'g' || specifier == 'a') {
							if (write < max_write) dst[write++] = 'n';
							if (write < max_write) dst[write++] = 'a';
							if (write < max_write) dst[write++] = 'n';
						} else {
							if (write < max_write) dst[write++] = 'N';
							if (write < max_write) dst[write++] = 'A';
							if (write < max_write) dst[write++] = 'N';
						}
						break;
					}
					
					// Output sign.
					if (negative) dst[write++] = '-';
					else if (plus) dst[write++] = '+';
					else if (space) dst[write++] = ' ';
					
					// Test for infinity. Test the bit pattern explicitly to prevent compiler meddling.
					if (fu.u == 0x7FF0000000000000) {
						if (specifier == 'f' || specifier == 'e' || specifier == 'g' || specifier == 'a') {
							if (write < max_write) dst[write++] = 'i';
							if (write < max_write) dst[write++] = 'n';
							if (write < max_write) dst[write++] = 'f';
						} else {
							if (write < max_write) dst[write++] = 'I';
							if (write < max_write) dst[write++] = 'N';
							if (write < max_write) dst[write++] = 'F';
						}
						break;
					}

					// Are we doing decimal or hex floats?
					if (specifier == 'f' || specifier == 'F' || specifier == 'e' || specifier == 'E' || specifier == 'g' || specifier == 'G') {
						//@non-standard: Decimal float output is not correctly rounded.
						//               But it should be precise enough for most uses.
						//               Output should be correct to ~16 decimal places.

						// Default precision for all decimal float formats.
						if (precision < 0)
							precision = 6;
						
						// Normalize to d.fff*10^eee.
						// https://blog.benoitblanchon.fr/lightweight-float-to-string/
						f = fu.f;
						int exp10 = 0;
						static const double BINARY_POWERS_OF_10[9] = { 1e256, 1e128, 1e64, 1e32, 1e16, 1e8, 1e4, 1e2, 1e1 };
						for (int i = 0, increment = 256; i < 9; i++, increment >>= 1) {
							double pow10 = BINARY_POWERS_OF_10[i];
							if (f >= pow10) {
								f /= pow10;
								exp10 += increment;
							}
						}
						if (f > 0) {
							static const double NEGATIVE_BINARY_POWERS_OF_10[9] = { 1e-255, 1e-127, 1e-63, 1e-31, 1e-15, 1e-7, 1e-3, 1e-1, 1e-0 };
							for (int i = 0, decrement = 256; i < 9; i++, decrement >>= 1) {
								if (f < NEGATIVE_BINARY_POWERS_OF_10[i]) {
									f *= BINARY_POWERS_OF_10[i];
									exp10 -= decrement;
								}
							}
						}
						
						// Decide whether we're doing scientific or fixed-point notation.
						bool scientific = specifier == 'e' || specifier == 'E';
						if (specifier == 'g' || specifier == 'G') {
							// https://en.cppreference.com/w/c/io/fprintf
							int p = precision == 0 ? 1 : precision;
							int x = exp10;
							if (p > x && x >= -4)
								precision = p - 1 - x;
							else
								scientific = true;
						}
						
						// Decide how many significant digits to extract.
						int left_shift_count = precision;
						if (!scientific) {
							if (exp10 > 0) {
								int overflow_protection = INT_MAX - exp10;
								if (left_shift_count > overflow_protection)
									left_shift_count = overflow_protection;
							}
							left_shift_count += exp10;
						}
						if (left_shift_count > 18)
							left_shift_count = 18;
						
						// Extract significant digits.
						unsigned long long shift = 1;
						for (int i = 0; i < left_shift_count; i++)
							shift *= 10;
						f *= (double)shift;
						unsigned long long u = (unsigned long long)f;
						f -= (double)u;
						
						// Round to nearest.
						if (f >= 0.5) {
							u += 1;
							if (u >= shift * 10) {
								u /= 10;
								exp10++;
							}
						}
						
						// Extract significant digits.
						// Remember that leftShiftCount can be almost arbitrarily negative.
						int num_significant_digits = 1 + left_shift_count;
						if (num_significant_digits < 0)
							num_significant_digits = 0;
						char digits[19];
						for (int i = num_significant_digits - 1; i >= 0; i--) {
							digits[i] = '0' + u % 10;
							u /= 10;
						}
						int digit_cursor = 0;
						
						// Decide how many integer digits go before the decimal point.
						int num_integer_digits = 1;
						if (!scientific)
							num_integer_digits += exp10;
						
						// Write integer part.
						if (num_integer_digits > 0) {
							int num_leading_digits = num_integer_digits;
							if (num_leading_digits > num_significant_digits)
								num_leading_digits = num_significant_digits;
							for (int i = 0; i < num_leading_digits && write < max_write; i++)
								dst[write++] = digits[digit_cursor++];
							
							int num_trailing_zeros = num_integer_digits - num_leading_digits;
							for (int i = 0; i < num_trailing_zeros && write < max_write; i++)
								dst[write++] = '0';
						}
						else if (write < max_write)
							dst[write++] = '0';
						
						// Write fractional part.
						bool have_dot = write < max_write && (precision > 0 || hash);
						if (have_dot) {
							dst[write++] = '.';
				
							// Decide how many leading zeros to prepend.
							int num_leading_zeros = 0;
							if (!scientific) {
								// 1.23e-4 = 0.000123 = 3 leading zeros.
								num_leading_zeros = -exp10 - 1;
								if (num_leading_zeros < 0)
									num_leading_zeros = 0;
								if (num_leading_zeros > precision)
									num_leading_zeros = precision;
							}
							
							// Append leading zeros.
							for (int i = 0; i < num_leading_zeros && write < max_write; i++)
								dst[write++] = '0';
							
							// Append significant digits.
							int num_digits = num_significant_digits - digit_cursor;
							for (int i = 0; i < num_digits && write < max_write; i++)
								dst[write++] = digits[digit_cursor++];
							
							// Append trailing zeros.
							int num_trailing_zeros = precision - num_digits - num_leading_zeros;
							for (int i = 0; i < num_trailing_zeros && write < max_write; i++)
								dst[write++] = '0';
						}
						
						// Write the exponent.
						if (scientific) {
							// Write exponent in reverse.
							bool negative_exp = exp10 < 0;
							unsigned uexp = negative_exp ? -exp10 : +exp10;
							char reverse[3];
							int reverse_length = 0;
							do {
								reverse[reverse_length++] = '0' + uexp % 10;
								uexp /= 10;
							} while (uexp > 0);
							
							// Write exponent in correct order.
							if (write < max_write) dst[write++] = specifier == 'E' || specifier == 'G' ? 'E' : 'e';
							if (write < max_write) dst[write++] = negative_exp ? '-' : '+';
							for (int i = reverse_length - 1; i >= 0 && write < max_write; i--)
								dst[write++] = reverse[i];
						}
						
						// Trim trailing zeros and dot.
						if ((specifier == 'g' || specifier == 'G') && !hash && have_dot) {
							while (dst[write - 1] == '0')
								dst[--write] = '\0';
							if (dst[write - 1] == '.')
								dst[--write] = '\0';
						}
					}
					else if (specifier == 'a' || specifier == 'A') {
						if (precision < 0) precision = 13; // Default precision is 13. Enough to fully represent any double.
						int prec = precision;
						if (prec > 13) prec = 13; // Don't overflow scratch.
						
						// Split the double into it's 11-bit exponent and 52-bit fraction.
						unsigned long long exponent = (fu.u & 0x7FF0000000000000) >> 52;
						unsigned long long fraction = (fu.u & 0x000FFFFFFFFFFFFF) >> 0;
						int exp2 = (int)exponent - 1023; // Subtract exponent bias.
						fraction >>= 52 - prec * 4;
						
						// Prepend 0x to all hex floats.
						if (write < max_write) dst[write++] = '0';
						if (write < max_write) dst[write++] = specifier == 'A' ? 'X' : 'x';
						
						// Denormals start with '0', normals with '1'.
						if (write < max_write) dst[write++] = exponent == 0 ? '0' : '1';
						
						// Append fraction.
						// Precision 0 means no '.' unless the '#' flag was specified.
						if (prec > 0 || hash) {
							if (write < max) dst[write++] = '.';
							const char* digits = specifier == 'A' ? "0123456789ABCDEF" : "0123456789abcdef";
							for (int shift = 4 * (prec - 1); shift >= 0 && write < max_write; shift -= 4)
								dst[write++] = digits[(fraction >> shift) & 0xF];
							
							// Pad with 0 up to precision.
							for (int i = prec; i < precision && write < max_write; i++)
								dst[write++] = '0';
						}
						
						// Append the exponent separator p.
						if (write < max) dst[write++] = specifier == 'A' ? 'P' : 'p';
						if (write < max) dst[write++] = exp2 >= 0 ? '+' : '-';
						
						// Append exponent in reverse order.
						unsigned int u = exp2 >= 0 ? exp2 : -exp2;
						char reverse[10];
						int reverse_length = 0;
						do {
							reverse[reverse_length++] = '0' + u % 10;
							u /= 10;
						} while (u > 0);
						
						// Output exponent in correct order.
						for (int i = reverse_length - 1; i >= 0 && write < max_write; i--)
							dst[write++] = reverse[i];
					}
				} break;
				case 'n': {
					if (length == none) *va_arg(args, int*) = write;
					else if (length == hh) *va_arg(args, signed char*) = (signed char)(write < -128 ? -128 : write > +127 ? +127 : write);
					else if (length == h) *va_arg(args, short*) = (short)(write < -32768 ? -32768 : write > +32767 ? +32767 : write);
					else if (length == l) *va_arg(args, long*) = write;
					else if (length == ll) *va_arg(args, long long*) = write;
					else if (length == t) *va_arg(args, ptrdiff_t*) = write;
					else if (length == z) *va_arg(args, size_t*) = write;
					else if (length == j) *va_arg(args, intmax_t*) = write;
				} break;
				default: {
					// Simply output the specifier character.
					// This handles %% but also invalid specifiers.
					dst[write++] = specifier;
				} break;
			}
			
			// Pad output to specified width.
			char padding = zero ? '0' : ' ';
			int written = write - start;
			int pad = width - written;
			if (minus) { // Left-justify (padding on the right).
				for (int i = 0; i < pad && write < max_write; ++i)
					dst[write++] = padding;
			}
			else if (pad > 0) { // Right-justify (padding on the left).
				// First, shift the written characters over to the right.
				// Be careful because these could go past the end of the buffer.
				//
				// 0123456789
				// xABCDE |
				// x___ABC|DE
				//
				int shift = written;
				if (shift > max_write - start - pad)
					shift = max_write - start - pad;
				for (int i = shift; i >= 0; --i)
					dst[start + pad + i] = dst[start + i];

				// Now put in padding.
				for (int i = 0; i < pad && start + i < max_write; ++i)
					dst[start + i] = padding;
				
				write += pad;
				if (write > max_write)
					write = max_write;
			}
		}
	}
	dst[write] = '\0';
	return write;
}
static int bb_snprintf(char dst[], int max, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int length = bb_vsnprintf(dst, max, fmt, args);
	va_end(args);
	return length;
}

// === testing ===

#include <math.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void bb_printf(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	char message[4096];
	bb_vsnprintf(message, sizeof message, fmt, args);
	va_end(args);
	fputs(message, stdout);
	fputc('\n', stdout);
}

int main(void) {
	bb_printf("Hello %s%c %d(%#x) < %llu(%#llo) < %.12g", "sailor", '!', 123, 123, ULLONG_MAX, ULLONG_MAX, 1.23456789e+123);
	
	// strings
	bb_printf("%lc %lc %lc", 0x1F60D, 0x1F924, 0x1F643); // Should print "😍 🤤 🙃" if your console supports it.
	bb_printf("%.3s", "12345");
	bb_printf("%%");
	bb_printf("%?");

	// integer
	bb_printf("%lld %lld", LONG_MIN, LONG_MAX);
	bb_printf("%.0d %.0d", 0, 1);
	bb_printf("%.10d", 123);
	bb_printf("%hhx %hx %x", SCHAR_MIN, SHRT_MIN, INT_MIN);
	bb_printf("%jd %zu %td", PTRDIFF_MAX, SIZE_MAX, INTMAX_MAX);

	// %f
	bb_printf("%f", 123.0);
	bb_printf("%f", 12.0);
	bb_printf("%f", 1.0);
	bb_printf("%f", 0.000123);
	bb_printf("%f", 0.00123);
	bb_printf("%f", 0.0123);
	bb_printf("%f", 0.123);
	bb_printf("%f %f", INFINITY, NAN);
	bb_printf("%f_%+f_% f", 1.0, 2.0, 3.0);
	bb_printf("%.17f", DBL_MAX);
	bb_printf("%f", -0.0);
	bb_printf("%f", 12345600000.00000000000000);
	bb_printf("%f", 01234560000.00000000000000);
	bb_printf("%f", 00123456000.00000000000000);
	bb_printf("%f", 00012345600.00000000000000);
	bb_printf("%f", 00001234560.00000000000000);
	bb_printf("%f", 00000123456.00000000000000);
	bb_printf("%f", 00000012345.60000000000000);
	bb_printf("%f", 00000001234.56000000000000);
	bb_printf("%f", 00000000123.45600000000000);
	bb_printf("%f", 00000000012.34560000000000);
	bb_printf("%f", 00000000001.23456000000000);
	bb_printf("%f", 00000000000.12345600000000);
	bb_printf("%f", 00000000000.01234560000000);
	bb_printf("%f", 00000000000.00123456000000);
	bb_printf("%f", 00000000000.00012345600000);
	bb_printf("%f", 00000000000.00001234560000);
	bb_printf("%f", 00000000000.00000123456000);
	bb_printf("%f", 00000000000.00000012345600);
	bb_printf("%f", 00000000000.00000001234560);
	bb_printf("%f", 00000000000.00000000123456);
	bb_printf("%.20f", 1234567890123456789000000.00000000000000000000000000000000000000000);
	bb_printf("%.20f", 0123456789012345678900000.00000000000000000000000000000000000000000);
	bb_printf("%.20f", 0012345678901234567890000.00000000000000000000000000000000000000000);
	bb_printf("%.20f", 0001234567890123456789000.00000000000000000000000000000000000000000);
	bb_printf("%.20f", 0000123456789012345678900.00000000000000000000000000000000000000000);
	bb_printf("%.20f", 0000012345678901234567890.00000000000000000000000000000000000000000);
	bb_printf("%.20f", 0000001234567890123456789.00000000000000000000000000000000000000000);
	bb_printf("%.20f", 0000000123456789012345678.90000000000000000000000000000000000000000);
	bb_printf("%.20f", 0000000012345678901234567.89000000000000000000000000000000000000000);
	bb_printf("%.20f", 0000000001234567890123456.78900000000000000000000000000000000000000);
	bb_printf("%.20f", 0000000000123456789012345.67890000000000000000000000000000000000000);
	bb_printf("%.20f", 0000000000012345678901234.56789000000000000000000000000000000000000);
	bb_printf("%.20f", 0000000000001234567890123.45678900000000000000000000000000000000000);
	bb_printf("%.20f", 0000000000000123456789012.34567890000000000000000000000000000000000);
	bb_printf("%.20f", 0000000000000012345678901.23456789000000000000000000000000000000000);
	bb_printf("%.20f", 0000000000000001234567890.12345678900000000000000000000000000000000);
	bb_printf("%.20f", 0000000000000000123456789.01234567890000000000000000000000000000000);
	bb_printf("%.20f", 0000000000000000012345678.90123456789000000000000000000000000000000);
	bb_printf("%.20f", 0000000000000000001234567.89012345678900000000000000000000000000000);
	bb_printf("%.20f", 0000000000000000000123456.78901234567890000000000000000000000000000);
	bb_printf("%.20f", 0000000000000000000012345.67890123456789000000000000000000000000000);
	bb_printf("%.20f", 0000000000000000000001234.56789012345678900000000000000000000000000);
	bb_printf("%.20f", 0000000000000000000000123.45678901234567890000000000000000000000000);
	bb_printf("%.20f", 0000000000000000000000012.34567890123456789000000000000000000000000);
	bb_printf("%.20f", 0000000000000000000000001.23456789012345678900000000000000000000000);
	bb_printf("%.20f", 0000000000000000000000000.12345678901234567890000000000000000000000);
	bb_printf("%.20f", 0000000000000000000000000.01234567890123456789000000000000000000000);
	bb_printf("%.20f", 0000000000000000000000000.00123456789012345678900000000000000000000);
	bb_printf("%.20f", 0000000000000000000000000.00012345678901234567890000000000000000000);
	bb_printf("%.20f", 0000000000000000000000000.00001234567890123456789000000000000000000);
	bb_printf("%.20f", 0000000000000000000000000.00000123456789012345678900000000000000000);
	bb_printf("%.20f", 0000000000000000000000000.00000012345678901234567890000000000000000);
	bb_printf("%.20f", 0000000000000000000000000.00000001234567890123456789000000000000000);
	bb_printf("%.20f", 0000000000000000000000000.00000000123456789012345678900000000000000);
	bb_printf("%.20f", 0000000000000000000000000.00000000012345678901234567890000000000000);
	bb_printf("%.20f", 0000000000000000000000000.00000000001234567890123456789000000000000);
	bb_printf("%.20f", 0000000000000000000000000.00000000000123456789012345678900000000000);
	bb_printf("%.20f", 0000000000000000000000000.00000000000012345678901234567890000000000);
	bb_printf("%.20f", 0000000000000000000000000.00000000000001234567890123456789000000000);
	bb_printf("%.20f", 0000000000000000000000000.00000000000000123456789012345678900000000);
	bb_printf("%.20f", 0000000000000000000000000.00000000000000012345678901234567890000000);
	bb_printf("%.20f", 0000000000000000000000000.00000000000000001234567890123456789000000);
	bb_printf("%.20f", 0000000000000000000000000.00000000000000000123456789012345678900000);
	bb_printf("%.20f", 0000000000000000000000000.00000000000000000012345678901234567890000);
	bb_printf("%.20f", 0000000000000000000000000.00000000000000000001234567890123456789000);
	bb_printf("%.20f", 0000000000000000000000000.00000000000000000000123456789012345678900);
	bb_printf("%.20f", 0000000000000000000000000.00000000000000000000012345678901234567890);
	bb_printf("%.20f", 0000000000000000000000000.00000000000000000000001234567890123456789);
	bb_printf("%.20f", 0.123456789012345678901234567890);
	bb_printf("%.20f", 0.0123456789012345678901234567890);
	bb_printf("%.20f", 0.00123456789012345678901234567890);
	bb_printf("%.20f", 0.000123456789012345678901234567890);
	bb_printf("%.20f", 0.0000123456789012345678901234567890);
	bb_printf("%.20f", 0.00000123456789012345678901234567890);
	bb_printf("%.20f", 0.000000123456789012345678901234567890);
	bb_printf("%.20f", 0.0000000123456789012345678901234567890);
	bb_printf("%.20f", 0.00000000123456789012345678901234567890);
	bb_printf("%.20f", 0.000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.0000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.00000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.000000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.0000000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.00000000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.000000000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.0000000000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.00000000000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.000000000000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.0000000000000000000123456789012345678901234567890);
	bb_printf("%.20f", 0.00000000000000000000123456789012345678901234567890);
	bb_printf("%f", 1234560000.0);
	bb_printf("%f", 123456000.00);
	bb_printf("%f", 12345600.00);
	bb_printf("%f", 1234560.0000);
	bb_printf("%f", 123456.00000);
	bb_printf("%f", 12345.600000);
	bb_printf("%f", 1234.5600000);
	bb_printf("%f", 123.45600000);
	bb_printf("%f", 12.345600000);
	bb_printf("%f", 1.2345600000);
	bb_printf("%f", 0.1234560000);
	bb_printf("%f", 0.0123456000000);
	bb_printf("%f", 0.0012345600000);
	bb_printf("%f", 0.0001234560000);
	bb_printf("%f", 0.0000123456000);
	bb_printf("%f", 0.0000012345600);
	bb_printf("%f", 0.0000001234560);
	bb_printf("%f", 0.0000000123456);
	bb_printf("%f", 123456789012345678901234567890.0);
	bb_printf("%.0f", 123.456);
	bb_printf("%#.0f", 123.456);

	// %e
	bb_printf("%#.0e", 456.789);
	bb_printf("%.9e", 1234560000.0);
	bb_printf("%.9e", 123456000.00);
	bb_printf("%.9e", 12345600.000);
	bb_printf("%.9e", 1234560.0000);
	bb_printf("%.9e", 123456.00000);
	bb_printf("%.9e", 12345.600000);
	bb_printf("%.9e", 1234.5600000);
	bb_printf("%.9e", 123.45600000);
	bb_printf("%.9e", 12.345600000);
	bb_printf("%.9e", 1.2345600000);
	bb_printf("%.9e", 0.1234560000);
	bb_printf("%.9e", 0.0123456000);
	bb_printf("%.9e", 0.0012345600);
	bb_printf("%.9e", 0.0001234560);
	bb_printf("%.9e", 0.0000123456);
	bb_printf("%.17e", DBL_MAX);
	bb_printf("%.0e", 1.23e+45);
	bb_printf("%#.0e", 1.23e-45);

	// %g
	bb_printf("%g", 0000000001.1000000000);
	bb_printf("%g", 0000000012.1200000000);
	bb_printf("%g", 0000000123.1230000000);
	bb_printf("%g", 0000001234.1234000000);
	bb_printf("%g", 0000012345.1234500000);
	bb_printf("%g", 0000123456.1234560000);
	bb_printf("%g", 0001234567.1234567000);
	bb_printf("%g", 0012345678.1234567800);
	bb_printf("%g", 0123456789.1234567890);
	bb_printf("%g", 1234567890.1234567890);
	bb_printf("%.12g", 1234567890.1234567890);
	bb_printf("%g", DBL_MAX);
	bb_printf("%.0g", 123.456);
	bb_printf("%#.0g", 123.456);

	// %a
	bb_printf("%a", DBL_MAX); // 0x1.FFFFFFFFFFFFFp+1023
	bb_printf("%a", DBL_MIN); // 0x1.0000000000000p-1022
	bb_printf("%a", DBL_TRUE_MIN); // 0x0.0000000000001p-1023

	// padding
	bb_printf("%12s", "123456");
	bb_printf("%012s", "123456");
	bb_printf("%-12s", "123456");
	bb_printf("%-012s", "123456");
	bb_printf("%05d", 123);
	
	// Make sure theres no buffer overflows.
	char buf[5];
	assert(bb_snprintf(buf, sizeof buf, "123456789") == 4 && strcmp(buf, "1234") == 0);
	assert(bb_snprintf(buf, sizeof buf, "%s", "123456789") == 4 && strcmp(buf, "1234") == 0);
	assert(bb_snprintf(buf, sizeof buf, "123%lc", 0x1F618) == 4);
	assert(bb_snprintf(buf, sizeof buf, "1234%s", "outside") == 4 && strcmp(buf, "1234") == 0);
	assert(bb_snprintf(buf, sizeof buf, "%d", 123456) == 4 && strcmp(buf, "1234") == 0);
	char buf2[8];
	assert(bb_snprintf(buf2, sizeof buf2, "x%8s", "ABCDE") == 7 && strcmp(buf2, "x   ABC") == 0);
}
