#include <Windows.h>
#include <winhttp.h>
#include <stdio.h>
#pragma comment(lib, "winhttp.lib")

void checkHr(HRESULT hr) {
	if (FAILED(hr)) {
		char message[256] = { 0 };
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, hr, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), message, 256, NULL);
		printf("HRESULT = 0x%08X: %s\n", hr, message);
		__debugbreak();
	}
}
void checkCond(BOOL cond) {
	if (!cond) {
		DWORD error = GetLastError();
		HRESULT hr = HRESULT_FROM_WIN32(error);
		checkHr(hr);
	}
}
void check(DWORD error) {
	HRESULT hr = HRESULT_FROM_WIN32(error);
	checkHr(hr);
}

int main(void) {
	HINTERNET session = WinHttpOpen(L"Websocket Client Test User Agent", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	checkCond(session != NULL);

	HINTERNET connection = WinHttpConnect(session, L"localhost", 9999, 0);
	checkCond(connection != NULL);
	printf("Connected to server.\n");

	HINTERNET request = WinHttpOpenRequest(connection, L"GET", L"", L"HTTP/1.1", WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	checkCond(request != NULL);

	printf("Starting websocket upgrade handshake.\n");
	checkCond(WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0));
	checkCond(WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0));
	checkCond(WinHttpReceiveResponse(request, NULL));

	HINTERNET websocket = WinHttpWebSocketCompleteUpgrade(request, NULL);
	checkCond(websocket != NULL);
	checkCond(WinHttpCloseHandle(request));
	printf("Websocket upgrade completed.\n");

	const char* message = "Hello, sailor!";
	check(WinHttpWebSocketSend(websocket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, message, strlen(message)));
	printf("Sent message to server.\n");

	char buffer[999] = { 0 };
	int length = 0;
	for (;;) {
		unsigned long bytesRead = 0;
		WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType = 0;
		check(WinHttpWebSocketReceive(websocket, buffer + length, sizeof buffer - length, &bytesRead, &bufferType));
		length += bytesRead;
		if (bufferType != WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) break;
	}
	printf("Received response from server: \"%.*s\".\n", length, buffer);

	check(WinHttpWebSocketClose(websocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0));
	unsigned short status = 0;
	char reason[999];
	unsigned long reasonLength = 0;
	WinHttpWebSocketQueryCloseStatus(websocket, &status, reason, sizeof reason, &reasonLength);
	printf("Closed connection with status %d and reason \"%.*s\".", status, reasonLength, reason);

	printf("Done");
}
