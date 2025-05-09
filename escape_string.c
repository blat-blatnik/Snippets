// escape C string, source and destination cannot overlap, does NOT zero terminate.
int escape(char* restrict dst, const char* restrict src, int len) {
	int cursor = 0;
	for (int i = 0; i < len; i++) {
		unsigned char c = src[i];
		if (c >= ' ' && c <= '~' && c != '"' && c != '\'' && c != '\\')
			dst[cursor++] = c;
		else {
			dst[cursor++] = '\\';
			switch (c) {
				case '\a': dst[cursor++] = 'a'; break;
				case '\b': dst[cursor++] = 'b'; break;
				case '\t': dst[cursor++] = 't'; break;
				case '\n': dst[cursor++] = 'n'; break;
				case '\v': dst[cursor++] = 'v'; break;
				case '\f': dst[cursor++] = 'f'; break;
				case '\r': dst[cursor++] = 'r'; break;
				case '\"': dst[cursor++] = '"'; break;
				case '\'': dst[cursor++] = '\''; break;
				case '\\': dst[cursor++] = '\\'; break;
				default:
					dst[cursor++] = 'x';
					dst[cursor++] = "0123456789ABCDEF"[(c >> 4) & 0xF];
					dst[cursor++] = "0123456789ABCDEF"[(c >> 0) & 0xF];
					break;
			}
		}
	}
	return cursor;
}

// unescape C string, source and destination can overlap, does NOT zero terminate.
int unescape(char* dst, const char* src, int len) {
	int cursor = 0;
	for (int i = 0; i < len; i++) {
		if (src[i] == '\\' && i < len - 1) {
			switch (src[++i]) {
				case 'a': dst[cursor++] = '\a'; break;
				case 'b': dst[cursor++] = '\b'; break;
				case 'e': dst[cursor++] = '\x1B'; break;
				case 'f': dst[cursor++] = '\f'; break;
				case 'n': dst[cursor++] = '\n'; break;
				case 'r': dst[cursor++] = '\r'; break;
				case 't': dst[cursor++] = '\t'; break;
				case 'v': dst[cursor++] = '\v'; break;
				case 'x': {
					int one = 0; // track if we have at least one valid hex char.
					int hex = 0;
					for (; i < len - 1; i++) {
						char c = src[i + 1];
						int dig;
						if (c >= '0' && c <= '9')
							dig = c - '0';
						else if (c >= 'A' && c <= 'F')
							dig = c - 'A' + 10;
						else if (c >= 'a' && c <= 'f')
							dig = c - 'a' + 10;
						else
							break;
						hex = (hex << 4) | dig;
						if (hex > 0xFF)
							hex = 0xFF;
						one = 1;
					}
					dst[cursor++] = one ? (char)hex : 'x'; // "\x" without any following hex chars unescapes to "x".
				} break;
				case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
					int oct = src[i] - '0';
					int max = i + 2;
					if (max > len - 1)
						max = len - 1;
					for (; i < max; i++) {
						unsigned dig = src[i + 1] - '0';
						if (dig >= 8)
							break;
						oct = (oct << 3) | dig;
					}
					if (oct > 0xFF)
						oct = 0xFF;
					dst[cursor++] = (char)oct;
				} break;
				default: // handles \" \' \? \\ and invalid escapes
					dst[cursor++] = src[i];
					break;
			}
		}
		else dst[cursor++] = src[i];
	}
	return cursor;
}

// === TESTS ===

#include <assert.h>
#include <string.h>
#include <stdbool.h>

bool escape_equal(const char* string, int string_length, const char* expected, int expected_length) {
	char escaped[9999];
	int escaped_length = escape(escaped, string, string_length);
	return escaped_length == expected_length && !memcmp(escaped, expected, expected_length);
}
bool unescape_equal(const char* string, int string_length, const char* expected, int expected_length) {
	char unescaped[9999];
	int unescaped_length = unescape(unescaped, string, string_length);
	return unescaped_length == expected_length && !memcmp(unescaped, expected, expected_length);
}

int main(void) {
	// exhaustively test all possible bytes
	char ascii[256];
	for (int i = 0; i <= 255; i++)
		ascii[i] = (char)i;
	char expected[] =
		"\\x00\\x01\\x02\\x03\\x04\\x05\\x06\\a\\b\\t\\n\\v\\f\\r\\x0E\\x0F"
		"\\x10\\x11\\x12\\x13\\x14\\x15\\x16\\x17\\x18\\x19\\x1A\\x1B\\x1C\\x1D\\x1E\\x1F"
		" !\\\"#$%&\\'()*+,-./"
		"0123456789:;<=>?"
		"@ABCDEFGHIJKLMNO"
		"PQRSTUVWXYZ[\\\\]^_"
		"`abcdefghijklmno"
		"pqrstuvwxyz{|}~\\x7F"
		"\\x80\\x81\\x82\\x83\\x84\\x85\\x86\\x87\\x88\\x89\\x8A\\x8B\\x8C\\x8D\\x8E\\x8F"
		"\\x90\\x91\\x92\\x93\\x94\\x95\\x96\\x97\\x98\\x99\\x9A\\x9B\\x9C\\x9D\\x9E\\x9F"
		"\\xA0\\xA1\\xA2\\xA3\\xA4\\xA5\\xA6\\xA7\\xA8\\xA9\\xAA\\xAB\\xAC\\xAD\\xAE\\xAF"
		"\\xB0\\xB1\\xB2\\xB3\\xB4\\xB5\\xB6\\xB7\\xB8\\xB9\\xBA\\xBB\\xBC\\xBD\\xBE\\xBF"
		"\\xC0\\xC1\\xC2\\xC3\\xC4\\xC5\\xC6\\xC7\\xC8\\xC9\\xCA\\xCB\\xCC\\xCD\\xCE\\xCF"
		"\\xD0\\xD1\\xD2\\xD3\\xD4\\xD5\\xD6\\xD7\\xD8\\xD9\\xDA\\xDB\\xDC\\xDD\\xDE\\xDF"
		"\\xE0\\xE1\\xE2\\xE3\\xE4\\xE5\\xE6\\xE7\\xE8\\xE9\\xEA\\xEB\\xEC\\xED\\xEE\\xEF"
		"\\xF0\\xF1\\xF2\\xF3\\xF4\\xF5\\xF6\\xF7\\xF8\\xF9\\xFA\\xFB\\xFC\\xFD\\xFE\\xFF";
	assert(escape_equal(ascii, sizeof ascii, expected, sizeof expected - 1));

	#define test_unescape(string, expected) assert(unescape_equal(string, sizeof(string) - 1, expected, sizeof(expected) - 1))
	test_unescape("\\a", "\a");
	test_unescape("\\b", "\b");
	test_unescape("\\e", "\x1B");
	test_unescape("\\f", "\f");
	test_unescape("\\n", "\n");
	test_unescape("\\r", "\r");
	test_unescape("\\t", "\t");
	test_unescape("\\v", "\v");
	test_unescape("\\\\", "\\");
	test_unescape("\\\'", "\'");
	test_unescape("\\\"", "\"");
	test_unescape("\\?", "?");
	test_unescape("\\", "\\");
	test_unescape("\\%", "%");
	test_unescape("\\0", "\0");
	test_unescape("\\00", "\0");
	test_unescape("\\000", "\0");
	test_unescape("\\0000", "\x00\x30");
	test_unescape("\\123", "\123");
	test_unescape("\\777", "\xFF");
	test_unescape("\\8", "8");
	test_unescape("\\78", "\7\x38");
	test_unescape("\\x", "x");
	test_unescape("\\X", "X");
	test_unescape("\\x0", "\x0");
	test_unescape("\\x00", "\x00");
	test_unescape("\\x000", "\x00");
	test_unescape("\\x1", "\x1");
	test_unescape("\\x11", "\x11");
	test_unescape("\\x111", "\xFF");
	test_unescape("\\xF", "\xF");
	test_unescape("\\xFF", "\xFF");
	test_unescape("\\xFFF", "\xFF");
	test_unescape("\\x01\\x23\\x45\\x67\\x89", "\x01\x23\x45\x67\x89");
	test_unescape("\\xAB\\xCD\\xEF", "\xAB\xCD\xEF");
	test_unescape("\\xab\\xcd\\xef", "\xab\xcd\xef");
	test_unescape("\\xFG", "\xFG");
	test_unescape("\\xfg", "\xfg");
	test_unescape("abcABC123+-( ~{}", "abcABC123+-( ~{}");
	test_unescape("abc\\", "abc\\");
	test_unescape("abc\\r\\n\\a\\\\\\123\\xF\\xfa", "abc\r\n\a\\\123\xF\xfa");
}