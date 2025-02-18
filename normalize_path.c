// Normalizes file paths into a canonical form by
// - removing '.' components
// - resolving '..' components
// - replacing backslashes with forward slashes
// - merge consecutive path separators
// - removing trailing slashes
// Modifies the input string in place, the resulting string is always the same length or shorter.
void normalize(char path[]) {
	char* src = path;
	char* dst = path;
	char* start = dst;
	for (;;) {
		if (*src == '/' || *src == '\\' || !*src) {
			int slash = *src == '/' || *src == '\\';
			while (*src == '/' || *src == '\\') src++; // merge consecutive path separators
			int exit = *src == '\0'; // we might temporarily replace this with '/'.
			if (start + 1 == dst && start[0] == '.') { // remove '.' component
				dst = start;
			}
			else if (start + 2 == dst && start[0] == '.' && start[1] == '.' && start > path) { // resolve '..' component
				start--; // skip over last separator
				if (start > path && start[-1] != ':') while (start > path && start[-1] != '/') start--; // find the separator before that, and continue from there
				if (start[0] == '.' && start[1] == '.' && start[2] == '/') { // don't remove leading '..'
					start += 5; 
					*start++ = '/';
				}
				else if ((start == path && *start == '/') || (start > path && start[-1] == ':')) start++; // don't remove absolute path
				dst = start;
			}
			else if (slash) { // replace windows '\' with unix '/' separators
				*dst++ = '/';
			}
			if (exit) break;
			start = dst;
		}
		else *dst++ = *src++;
	}
	if (dst > path + 1 && dst[-1] == '/' && dst[-2] != ':') dst--; // remove trailing separator
	*dst = '\0';
}

// === TESTING ===

#include <assert.h>
#include <string.h>
#include <stdlib.h>

char* normalize_alloc(const char* path) {
	char* copy = malloc(strlen(path) + 1);
	memcpy(copy, path, strlen(path) + 1);
	normalize(copy);
	return copy;
}

int main(void) {
	// already canonicized
	assert(!strcmp(normalize_alloc("file"), "file"));
	assert(!strcmp(normalize_alloc("dir/subdir/file"), "dir/subdir/file"));
	
	// basic usage
	assert(!strcmp(normalize_alloc("dir\\subdir\\file"), "dir/subdir/file"));
	assert(!strcmp(normalize_alloc("dir/subdir/../file"), "dir/file"));
	assert(!strcmp(normalize_alloc("dir/subdir/../../file"), "file"));
	assert(!strcmp(normalize_alloc("dir/subdir/./file"), "dir/subdir/file"));
	assert(!strcmp(normalize_alloc("dir/subdir///file"), "dir/subdir/file"));
	assert(!strcmp(normalize_alloc("dir/subdir/file/"), "dir/subdir/file"));

	// unix absolute paths
	assert(!strcmp(normalize_alloc("/file"), "/file"));
	assert(!strcmp(normalize_alloc("/dir/subdir/file"), "/dir/subdir/file"));
	assert(!strcmp(normalize_alloc("/"), "/"));
	assert(!strcmp(normalize_alloc("/.."), "/"));
	assert(!strcmp(normalize_alloc("/../.."), "/"));

	// windows absolute paths
	assert(!strcmp(normalize_alloc("C:/file"), "C:/file"));
	assert(!strcmp(normalize_alloc("C:/"), "C:/"));
	assert(!strcmp(normalize_alloc("C:/.."), "C:/"));
	assert(!strcmp(normalize_alloc("C:/../.."), "C:/"));

	// edge cases
	assert(!strcmp(normalize_alloc(""), ""));
	assert(!strcmp(normalize_alloc("."), ""));
	assert(!strcmp(normalize_alloc(".."), ".."));
	assert(!strcmp(normalize_alloc("./"), ""));
	assert(!strcmp(normalize_alloc("../"), ".."));
	assert(!strcmp(normalize_alloc("/."), "/"));
	assert(!strcmp(normalize_alloc("/.."), "/"));
	assert(!strcmp(normalize_alloc(".a"), ".a"));
	assert(!strcmp(normalize_alloc("a."), "a."));
	assert(!strcmp(normalize_alloc("..a"), "..a"));
	assert(!strcmp(normalize_alloc("a.."), "a.."));
	assert(!strcmp(normalize_alloc("../.."), "../.."));
	assert(!strcmp(normalize_alloc("../../.."), "../../.."));
	assert(!strcmp(normalize_alloc("a/b/c/../../../../../"), "../.."));
	assert(!strcmp(normalize_alloc("C:"), "C:")); // adding a trailing slash would make the string longer
}
