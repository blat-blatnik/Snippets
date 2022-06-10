#include <Windows.h>
#include <stdio.h> // printf

void list_directory(const char *path) {
	char buffer[1024];
	wsprintfA(buffer, "%s/*", path);

	WIN32_FIND_DATAA data;
	HANDLE find = FindFirstFileA(buffer, &data);
	if (find == INVALID_HANDLE_VALUE)
		return;

	do {
		wsprintfA(buffer, "%s/%s", path, data.cFileName);
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			int is_special = data.cFileName[0] == '.' && (!data.cFileName[1] || (data.cFileName[1] == '.' && !data.cFileName[2]));
			if (!is_special)
				list_directory(buffer);
		} else printf("%s\n", buffer);
	} while (FindNextFileA(find, &data));
}

int main(void) {
	list_directory(".");
}