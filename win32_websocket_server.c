// Minimal websocket server setup using HTTP.sys and the Windows websocket API.
// This is just a minimal example using synchronous calls and minimal error checking.
// Don't use this in production, it's just for reference.
// 
// You can test it with this python program:
// 
// $ pip install websockets
// 
// import websockets.sync.client
// with websockets.sync.client.connect("ws://localhost:9999/server") as websocket:
//   message = websocket.recv()
//   print(f"Received: {message}")
//   websocket.send("Hello from client!")

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <websocket.h>
#include <http.h>
#include <stdio.h>
#include <assert.h>
#pragma comment(lib, "httpapi.lib")
#pragma comment(lib, "websocket.lib")

void checkHr(HRESULT hr) {
	if (FAILED(hr)) {
		char message[256] = { 0 };
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, hr, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), message, 256, NULL);
		printf("HRESULT = 0x%08X: %s\n", hr, message);
		__debugbreak();
	}
}
void check(unsigned error) {
	HRESULT hr = HRESULT_FROM_WIN32(error);
	checkHr(hr);
}

int main(void) {
	HTTPAPI_VERSION version = HTTPAPI_VERSION_2;
	check(HttpInitialize(version, HTTP_INITIALIZE_SERVER, NULL));

	HTTP_SERVER_SESSION_ID session = 0;
	check(HttpCreateServerSession(version, &session, 0));

	HTTP_URL_GROUP_ID urlGroup = 0;
	check(HttpCreateUrlGroup(session, &urlGroup, 0));
	check(HttpAddUrlToUrlGroup(urlGroup, L"http://localhost:9999/server", 0, 0));

	HANDLE requestQueue = NULL;
	check(HttpCreateRequestQueue(version, NULL, NULL, 0, &requestQueue));

	HTTP_BINDING_INFO binding = { 0 };
	binding.Flags.Present = TRUE;
	binding.RequestQueueHandle = requestQueue;
	check(HttpSetUrlGroupProperty(urlGroup, HttpServerBindingProperty, &binding, sizeof binding));

	printf("Waiting for connection.\n");
	__declspec(align(8)) char requestBuffer[4096] = { 0 };
	HTTP_REQUEST_V2* request = (HTTP_REQUEST_V2*)requestBuffer;
	unsigned long requestSize = 0;
	check(HttpReceiveHttpRequest(requestQueue, HTTP_NULL_ID, 0, request, sizeof requestBuffer, &requestSize, NULL));
	printf("Received HTTP request.\n");

	WEB_SOCKET_HTTP_HEADER wsRequestHeaders[99] = { 0 };
	unsigned long numRequestHeaders = 0;
	for (unsigned i = 0; i < HttpHeaderRequestMaximum; i++) {
		HTTP_KNOWN_HEADER* src = &request->Headers.KnownHeaders[i];
		if (src->RawValueLength) {
			static const char* const REQUEST_HEADER_NAMES[HttpHeaderRequestMaximum] = {
				"CacheControl",
				"Connection",
				"Date",
				"KeepAlive",
				"Pragma",
				"Trailer",
				"TransferEncoding",
				"Upgrade",
				"Via",
				"Warning",
				"Allow",
				"ContentLength",
				"ContentType",
				"ContentEncoding",
				"ContentLanguage",
				"ContentLocation",
				"ContentMd5",
				"ContentRange",
				"Expires",
				"LastModified",
				"Accept",
				"AcceptCharset",
				"AcceptEncoding",
				"AcceptLanguage",
				"Authorization",
				"Cookie",
				"Expect",
				"From",
				"Host",
				"IfMatch",
				"IfModifiedSince",
				"IfNoneMatch",
				"IfRange",
				"IfUnmodifiedSince",
				"MaxForwards",
				"ProxyAuthorization",
				"Referer",
				"Range",
				"Te",
				"Translate",
				"UserAgent",
			};

			WEB_SOCKET_HTTP_HEADER* dst = &wsRequestHeaders[numRequestHeaders++];
			dst->pcName = (char*)REQUEST_HEADER_NAMES[i];
			dst->ulNameLength = (unsigned)strlen(dst->pcName);
			dst->pcValue = (char*)src->pRawValue;
			dst->ulValueLength = src->RawValueLength;
		}
	}
	for (unsigned i = 0; i < request->Headers.UnknownHeaderCount; i++) {
		HTTP_UNKNOWN_HEADER* src = &request->Headers.pUnknownHeaders[i];
		WEB_SOCKET_HTTP_HEADER* dst = &wsRequestHeaders[numRequestHeaders++];
		dst->pcName = (char*)src->pName;
		dst->ulNameLength = src->NameLength;
		dst->pcValue = (char*)src->pRawValue;
		dst->ulValueLength = src->RawValueLength;
	}

	WEB_SOCKET_HANDLE websocket = NULL;
	checkHr(WebSocketCreateServerHandle(NULL, 0, &websocket));

	printf("Performing websocket handshake.\n");
	WEB_SOCKET_HTTP_HEADER* wsResponseHeaders = NULL;
	unsigned long numResponseHeaders = 0;
	checkHr(WebSocketBeginServerHandshake(websocket, NULL, NULL, 0, wsRequestHeaders, numRequestHeaders, &wsResponseHeaders, &numResponseHeaders));

	HTTP_UNKNOWN_HEADER responseHeaders[99] = { 0 };
	HTTP_RESPONSE_V2 response = { 0 };
	response.StatusCode = 101;
	response.pReason = "Switching Protocols";
	response.ReasonLength = sizeof "Switching Protocols" - 1;
	response.Headers.pUnknownHeaders = responseHeaders;
	for (unsigned i = 0; i < numResponseHeaders; i++) {
		WEB_SOCKET_HTTP_HEADER* src = &wsResponseHeaders[i];
		BOOL isKnownHeader = FALSE;
		for (int j = 0; j < HttpHeaderResponseMaximum; j++) {
			static const char* const RESPONSE_HEADER_NAMES[HttpHeaderResponseMaximum] = {
				"CacheControl",
				"Connection",
				"Date",
				"KeepAlive",
				"Pragma",
				"Trailer",
				"TransferEncoding",
				"Upgrade",
				"Via",
				"Warning",
				"Allow",
				"ContentLength",
				"ContentType",
				"ContentEncoding",
				"ContentLanguage",
				"ContentLocation",
				"ContentMd5",
				"ContentRange",
				"Expires",
				"LastModified",
				"AcceptRanges",
				"Age",
				"Etag",
				"Location",
				"ProxyAuthenticate",
				"RetryAfter",
				"Server",
				"SetCookie",
				"Vary",
				"WwwAuthenticate",
			};
			const char* name = RESPONSE_HEADER_NAMES[j];
			size_t length = strlen(name);
			if (src->ulNameLength == length && memcmp(src->pcName, name, length) == 0) {
				isKnownHeader = TRUE;
				HTTP_KNOWN_HEADER* dst = &response.Headers.KnownHeaders[j];
				dst->pRawValue = src->pcValue;
				dst->RawValueLength = (unsigned short)src->ulValueLength;
				break;
			}
		}
		if (!isKnownHeader) {
			HTTP_UNKNOWN_HEADER* dst = &response.Headers.pUnknownHeaders[response.Headers.UnknownHeaderCount++];
			dst->pName = src->pcName;
			dst->NameLength = (unsigned short)src->ulNameLength;
			dst->pRawValue = src->pcValue;
			dst->RawValueLength = (unsigned short)src->ulValueLength;
		}
	}

	//@HACK For some reason HttpSendResponse doesn't seem to send the Connection: Upgrade header unless it's set
	//      as both an HTTP_KNOWN_HEADER and an HTTP_UNKNOWN_HEADER. No idea why, but it just ignores it.
	//      We already set it as a known header in the loop above, so now just add it as an unknown header.
	HTTP_UNKNOWN_HEADER* connectionHeader = &response.Headers.pUnknownHeaders[response.Headers.UnknownHeaderCount++];
	connectionHeader->pName = "Connection";
	connectionHeader->NameLength = sizeof "Connection" - 1;
	connectionHeader->pRawValue = "Upgrade";
	connectionHeader->RawValueLength = sizeof "Upgrade" - 1;

	HTTP_REQUEST_ID requestId = request->RequestId;
	unsigned long responseBytesSent = 0;
	check(HttpSendHttpResponse(requestQueue, requestId, HTTP_SEND_RESPONSE_FLAG_OPAQUE | HTTP_SEND_RESPONSE_FLAG_MORE_DATA, &response, NULL, &responseBytesSent, NULL, 0, NULL, NULL));

	checkHr(WebSocketEndServerHandshake(websocket));
	printf("Websocket handshake complete.\n");

	WEB_SOCKET_BUFFER sendData = { 0 };
	sendData.Data.pbBuffer = (BYTE*)"Hello from server!";
	sendData.Data.ulBufferLength = sizeof "Hello from server!" - 1;
	checkHr(WebSocketSend(websocket, WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, &sendData, NULL));
	for (;;) {
		WEB_SOCKET_BUFFER buffer = { 0 };
		unsigned long numBuffers = 1;
		WEB_SOCKET_ACTION action = 0;
		WEB_SOCKET_BUFFER_TYPE bufferType = 0;
		void* context = NULL;
		checkHr(WebSocketGetAction(websocket, WEB_SOCKET_ALL_ACTION_QUEUE, &buffer, &numBuffers, &action, &bufferType, NULL, &context));

		unsigned long bytesTransferred = 0;
		if (action == WEB_SOCKET_SEND_TO_NETWORK_ACTION) {
			assert(numBuffers == 1);
			HTTP_DATA_CHUNK chunk = { 0 };
			chunk.DataChunkType = HttpDataChunkFromMemory;
			chunk.FromMemory.pBuffer = buffer.Data.pbBuffer;
			chunk.FromMemory.BufferLength = buffer.Data.ulBufferLength;
			check(HttpSendResponseEntityBody(requestQueue, requestId, HTTP_SEND_RESPONSE_FLAG_MORE_DATA, 1, &chunk, &bytesTransferred, NULL, 0, NULL, NULL));
			printf("Sent %d bytes.\n", bytesTransferred);
		}
		else {
			assert(action == WEB_SOCKET_INDICATE_SEND_COMPLETE_ACTION);
			assert(numBuffers == 0);
			printf("Send completed.\n");
		}

		WebSocketCompleteAction(websocket, context, bytesTransferred);
		if (action == WEB_SOCKET_INDICATE_SEND_COMPLETE_ACTION) break;
	}

	checkHr(WebSocketReceive(websocket, NULL, NULL));
	for (;;) {
		WEB_SOCKET_BUFFER buffer = { 0 };
		unsigned long numBuffers = 1;
		WEB_SOCKET_ACTION action = 0;
		WEB_SOCKET_BUFFER_TYPE bufferType = 0;
		void* context = NULL;
		checkHr(WebSocketGetAction(websocket, WEB_SOCKET_ALL_ACTION_QUEUE, &buffer, &numBuffers, &action, &bufferType, NULL, &context));

		unsigned long bytesTransferred = 0;
		if (action == WEB_SOCKET_RECEIVE_FROM_NETWORK_ACTION) {
			assert(numBuffers == 1);
			check(HttpReceiveRequestEntityBody(requestQueue, requestId, 0, buffer.Data.pbBuffer, buffer.Data.ulBufferLength, &bytesTransferred, NULL));
			printf("Received %d bytes.\n", bytesTransferred);
		}
		else {
			assert(action == WEB_SOCKET_INDICATE_RECEIVE_COMPLETE_ACTION);
			assert(numBuffers == 1);
			printf("Receive completed: \"%.*s\"\n", buffer.Data.ulBufferLength, buffer.Data.pbBuffer);
		}

		WebSocketCompleteAction(websocket, context, bytesTransferred);
		if (action == WEB_SOCKET_INDICATE_RECEIVE_COMPLETE_ACTION) break;
	}

	printf("Done.\n");
}
