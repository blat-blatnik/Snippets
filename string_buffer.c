#include <stdio.h> // snprintf
#include <string.h> // memcpy
#include <stdarg.h> // va_list, va_start, va_end

struct buffer {
	char *buffer; // Always kept null terminated.
	int cursor; 
	int capacity;
	int bytes_needed; // Includes null terminator.
};

struct buffer create(char *buffer, int capacity) {
	if (capacity > 0)
		buffer[0] = 0;
	return (struct buffer) {
		.buffer = buffer,
		.capacity = capacity,
		.bytes_needed = 1,
	};
}

void append_char(struct buffer *buffer, char c) {
	++buffer->bytes_needed;
	if (buffer->cursor + 1 < buffer->capacity) {
		buffer->buffer[buffer->cursor++] = c;
		buffer->buffer[buffer->cursor] = 0;
	}
}

void append_char_repeated(struct buffer *buffer, char c, int count) {
	int remaining_chars = buffer->capacity - buffer->cursor - 1;
	if (remaining_chars < 0)
		remaining_chars = 0;
	int bytes_to_set = count;
	if (bytes_to_set > remaining_chars)
		bytes_to_set = remaining_chars;
	memset(buffer->buffer + buffer->cursor, c, (size_t)bytes_to_set);
	buffer->bytes_needed += count;
	buffer->cursor += bytes_to_set;
	if (bytes_to_set > 0)
		buffer->buffer[buffer->cursor] = 0;
}

void append_bytes(struct buffer *buffer, const void *bytes, int size) {
	int remaining_chars = buffer->capacity - buffer->cursor - 1;
	if (remaining_chars < 0)
		remaining_chars = 0;
	int bytes_to_copy = size;
	if (bytes_to_copy > remaining_chars)
		bytes_to_copy = remaining_chars;
	memcpy(buffer->buffer + buffer->cursor, bytes, (size_t)bytes_to_copy);
	buffer->bytes_needed += size;
	buffer->cursor += bytes_to_copy;
	if (bytes_to_copy > 0)
		buffer->buffer[buffer->cursor] = 0;
}

void append_string(struct buffer *buffer, const char *string) {
	int remaining_chars = buffer->capacity - buffer->cursor - 1;
	if (remaining_chars < 0)
		remaining_chars = 0;
	int length;
	for (length = 0; length < remaining_chars && string[length]; ++length)
		buffer->buffer[buffer->cursor++] = string[length];
	buffer->bytes_needed += length + (int)strlen(string + length);
	if (remaining_chars > 0)
		buffer->buffer[buffer->cursor] = 0;
}

void append_format_va(struct buffer *buffer, const char *format, va_list args) {
	int remaining_bytes = buffer->capacity - buffer->cursor;
	int chars_needed = vsnprintf(buffer->buffer + buffer->cursor, (size_t)remaining_bytes, format, args);
	int chars_written = chars_needed;
	if (chars_written > remaining_bytes - 1)
		chars_written = remaining_bytes - 1;
	if (chars_written > 0)
		buffer->cursor += chars_written;
	buffer->bytes_needed += chars_needed;
}

void append_format(struct buffer *buffer, const char *format, ...) {
	va_list args;
	va_start(args, format);
	append_format_va(buffer, format, args);
	va_end(args);
}

#include <assert.h>
#define BUFFER_ON_STACK(capacity) create((char[capacity]){0},(capacity))
int main(void) {
	// create
	{
		struct buffer sb;

		sb = create(NULL, 0);
		assert(!sb.buffer && sb.capacity == 0 && sb.cursor == 0 && sb.bytes_needed == 1);

		char a[3] = { 1, 2, 3 };
		sb = create(a + 1, 1);
		assert(sb.buffer == a + 1 && sb.capacity == 1 && sb.cursor == 0 && sb.bytes_needed == 1);
		assert(a[1] == 0);

		sb = BUFFER_ON_STACK(1);
		assert(sb.buffer && sb.buffer[0] == 0 && sb.capacity == 1 && sb.cursor == 0 && sb.bytes_needed == 1);

		sb = BUFFER_ON_STACK(42);
		assert(sb.buffer && sb.buffer[0] == 0 && sb.capacity == 42 && sb.cursor == 0 && sb.bytes_needed == 1);
	}

	// append_char
	{
		struct buffer sb;

		sb = BUFFER_ON_STACK(4);
		append_char(&sb, 'a');
		assert(!strcmp(sb.buffer, "a"));
		assert(sb.cursor == 1 && sb.bytes_needed == 2);

		append_char(&sb, 'b');
		append_char(&sb, 'c');
		assert(!strcmp(sb.buffer, "abc"));
		assert(sb.cursor == 3 && sb.bytes_needed == 4);

		append_char(&sb, 'd');
		append_char(&sb, 'e');
		append_char(&sb, 'f');
		assert(!strcmp(sb.buffer, "abc"));
		assert(sb.cursor == 3 && sb.bytes_needed == 7);

		sb = create(NULL, 0);
		for (int i = 0; i < 100; ++i)
			append_char(&sb, (char)i);
		assert(sb.cursor == 0 && sb.bytes_needed == 101);

		sb = BUFFER_ON_STACK(8);
		append_char(&sb, 'a');
		append_char(&sb, 'b');
		append_char(&sb, 'c');
		append_char(&sb, '\0');
		assert(!strcmp(sb.buffer, "abc"));
		assert(sb.cursor == 4 && sb.bytes_needed == 5);

		append_char(&sb, 'd');
		append_char(&sb, 'e');
		assert(!strcmp(sb.buffer, "abc"));
		assert(!strcmp(sb.buffer + 4, "de"));
		assert(sb.cursor == 6 && sb.bytes_needed == 7);
	}

	// append_char_repeated
	{
		struct buffer sb;

		sb = BUFFER_ON_STACK(8);
		append_char_repeated(&sb, 'a', 3);
		assert(!strcmp(sb.buffer, "aaa"));
		assert(sb.cursor == 3 && sb.bytes_needed == 4);

		append_char_repeated(&sb, 'b', 1);
		assert(!strcmp(sb.buffer, "aaab"));
		assert(sb.cursor == 4 && sb.bytes_needed == 5);

		append_char_repeated(&sb, 'c', 0);
		assert(!strcmp(sb.buffer, "aaab"));
		assert(sb.cursor == 4 && sb.bytes_needed == 5);

		append_char_repeated(&sb, 'd', 4);
		assert(!strcmp(sb.buffer, "aaabddd"));
		assert(sb.cursor == 7 && sb.bytes_needed == 9);

		append_char_repeated(&sb, 'e', 100);
		assert(!strcmp(sb.buffer, "aaabddd"));
		assert(sb.cursor == 7 && sb.bytes_needed == 109);

		sb = create(NULL, 0);
		for (int i = 0; i < 100; ++i)
			append_char_repeated(&sb, (char)i, 100);
		assert(sb.cursor == 0 && sb.bytes_needed == 100 * 100 + 1);

		sb = BUFFER_ON_STACK(8);
		append_char_repeated(&sb, 'a', 3);
		append_char_repeated(&sb, '\0', 3);
		assert(sb.cursor == 6 && sb.bytes_needed == 7);
		assert(!strcmp(sb.buffer, "aaa") && !memcmp(sb.buffer + 3, "\0\0\0\0", 4));
		append_char_repeated(&sb, 'b', 3);
		assert(sb.cursor == 7 && sb.bytes_needed == 10);
		assert(!strcmp(sb.buffer + 6, "b"));
	}

	// append_bytes
	{
		struct buffer sb;

		sb = BUFFER_ON_STACK(8);
		append_bytes(&sb, "123", 3);
		assert(!strcmp(sb.buffer, "123"));
		assert(sb.cursor == 3 && sb.bytes_needed == 4);

		append_bytes(&sb, "", 0);
		assert(!strcmp(sb.buffer, "123"));
		assert(sb.cursor == 3 && sb.bytes_needed == 4);

		append_bytes(&sb, "4567", 4);
		assert(!strcmp(sb.buffer, "1234567"));
		assert(sb.cursor == 7 && sb.bytes_needed == 8);

		append_bytes(&sb, "890", 3);
		assert(!strcmp(sb.buffer, "1234567"));
		assert(sb.cursor == 7 && sb.bytes_needed == 11);

		sb = create(NULL, 0);
		for (int i = 0; i < 100; ++i)
			append_bytes(&sb, "1234", 4);
		assert(sb.cursor == 0 && sb.bytes_needed == 401);

		sb = BUFFER_ON_STACK(12);
		append_bytes(&sb, "12345", 5);
		append_bytes(&sb, "\0\0\0\0\0", 5);
		assert(!strcmp(sb.buffer, "12345"));
		assert(!memcmp(sb.buffer + 5, "\0\0\0\0\0\0", 6));
		assert(sb.cursor == 10 && sb.bytes_needed == 11);

		append_bytes(&sb, "6789", 4);
		assert(!strcmp(sb.buffer + 10, "6"));
		assert(sb.cursor == 11 && sb.bytes_needed == 15);
	}

	// append_string
	{
		struct buffer sb;

		sb = BUFFER_ON_STACK(8);
		append_string(&sb, "123");
		assert(!strcmp(sb.buffer, "123"));
		assert(sb.cursor == 3 && sb.bytes_needed == 4);

		append_string(&sb, "");
		assert(!strcmp(sb.buffer, "123"));
		assert(sb.cursor == 3 && sb.bytes_needed == 4);

		append_string(&sb, "4567");
		assert(!strcmp(sb.buffer, "1234567"));
		assert(sb.cursor == 7 && sb.bytes_needed == 8);

		append_string(&sb, "890");
		assert(!strcmp(sb.buffer, "1234567"));
		assert(sb.cursor == 7 && sb.bytes_needed == 11);

		sb = create(NULL, 0);
		for (int i = 0; i < 100; ++i)
			append_string(&sb, "1234");
		assert(sb.cursor == 0 && sb.bytes_needed == 401);

		sb = BUFFER_ON_STACK(7);
		append_string(&sb, "123456789");
		assert(!strcmp(sb.buffer, "123456"));
		assert(sb.cursor == 6 && sb.bytes_needed == 10);
	}

	// append_format
	{
		struct buffer sb;

		sb = BUFFER_ON_STACK(8);
		append_format(&sb, "123");
		assert(!strcmp(sb.buffer, "123"));
		assert(sb.cursor == 3 && sb.bytes_needed == 4);

		append_format(&sb, "");
		assert(!strcmp(sb.buffer, "123"));
		assert(sb.cursor == 3 && sb.bytes_needed == 4);

		append_format(&sb, "%d", 4567);
		assert(!strcmp(sb.buffer, "1234567"));
		assert(sb.cursor == 7 && sb.bytes_needed == 8);

		append_format(&sb, "890");
		assert(!strcmp(sb.buffer, "1234567"));
		assert(sb.cursor == 7 && sb.bytes_needed == 11);

		sb = create(NULL, 0);
		for (int i = 0; i < 100; ++i)
			append_format(&sb, "1234");
		assert(sb.cursor == 0 && sb.bytes_needed == 401);

		sb = BUFFER_ON_STACK(7);
		append_format(&sb, "%s", "123456789");
		assert(!strcmp(sb.buffer, "123456"));
		assert(sb.cursor == 6 && sb.bytes_needed == 10);

		sb = BUFFER_ON_STACK(256);
		append_format(&sb, "Hello%c ", '!');
		append_format(&sb, "You are '%s' number %d.", "sailor", 42);
		assert(!strcmp(sb.buffer, "Hello! You are 'sailor' number 42."));
	}
}