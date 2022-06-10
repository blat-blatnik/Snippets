#include <Windows.h>
#include <TlHelp32.h>
#include <ImageHlp.h>
#include <stdio.h> // printf

typedef BOOL(WINAPI *SymGetSymFromAddr64_Func)(HANDLE hProcess, DWORD64 qwAddr, PDWORD64 pdwDisplacement, PIMAGEHLP_SYMBOL64 Symbol);
typedef BOOL(WINAPI *SymGetSymFromAddr64_Func)(HANDLE hProcess, DWORD64 qwAddr, PDWORD64 pdwDisplacement, PIMAGEHLP_SYMBOL64 Symbol);
typedef DWORD(WINAPI *UnDecorateSymbolName_Func)(PCSTR name, PSTR outputString, DWORD maxStringLength, DWORD flags);
typedef BOOL(WINAPI *SymGetLineFromAddr64_Func)(HANDLE hProcess, DWORD64 qwAddr, PDWORD pdwDisplacement, PIMAGEHLP_LINE64 Line64);
typedef BOOL(WINAPI *SymGetModuleInfo64_Func)(HANDLE hProcess, DWORD64 qwAddr, PIMAGEHLP_MODULE64 ModuleInfo);
typedef BOOL(WINAPI *StackWalk64_Func)(
	DWORD MachineType,
	HANDLE hProcess,
	HANDLE hThread,
	LPSTACKFRAME64 StackFrame,
	PVOID ContextRecord,
	PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
	PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
	PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
	PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);
SymGetSymFromAddr64_Func SymGetSymFromAddr64_func;
UnDecorateSymbolName_Func UnDecorateSymbolName_func;
SymGetLineFromAddr64_Func SymGetLineFromAddr64_func;
SymGetModuleInfo64_Func SymGetModuleInfo64_func;
StackWalk64_Func StackWalk64_func;
#define SymGetSymFromAddr64 SymGetSymFromAddr64_func
#define UnDecorateSymbolName UnDecorateSymbolName_func
#define SymGetLineFromAddr64 SymGetLineFromAddr64_func
#define SymGetModuleInfo64 SymGetModuleInfo64_func
#define StackWalk64 StackWalk64_func

void init_dbghelp_dll(void) {
	static BOOL already_tried_to_init;
	if (already_tried_to_init)
		return;
	already_tried_to_init = TRUE;

	HMODULE dbghelp_dll = LoadLibraryA("DbgHelp.dll");
	if (!dbghelp_dll)
		return;

	typedef BOOL(WINAPI *SymInitialize_Func)(HANDLE hProcess, PCSTR UserSearchPath, BOOL fInvadeProcess);
	typedef DWORD(WINAPI *SymGetOptions_Func)(void);
	typedef DWORD(WINAPI *SymSetOptions_Func)(DWORD SymOptions);
	typedef DWORD64(WINAPI *SymLoadModule64_Func)(HANDLE hProcess, HANDLE hFile, PCSTR ImageName, PCSTR ModuleName, DWORD64 BaseOfDll, DWORD SizeOfDll);
	SymInitialize_Func SymInitialize_func = NULL;
	SymGetOptions_Func SymGetOptions_func = NULL;
	SymSetOptions_Func SymSetOptions_func = NULL;
	SymLoadModule64_Func SymLoadModule64_func = NULL;
	#define SymInitialize SymInitialize_func
	#define SymGetOptions SymGetOptions_func
	#define SymSetOptions SymSetOptions_func
	#define SymLoadModule64 SymLoadModule64_func

	SymInitialize = (SymInitialize_Func)GetProcAddress(dbghelp_dll, "SymInitialize");
	SymGetOptions = (SymGetOptions_Func)GetProcAddress(dbghelp_dll, "SymGetOptions");
	SymSetOptions = (SymSetOptions_Func)GetProcAddress(dbghelp_dll, "SymSetOptions");
	SymLoadModule64 = (SymLoadModule64_Func)GetProcAddress(dbghelp_dll, "SymLoadModule64");
	SymGetSymFromAddr64 = (SymGetSymFromAddr64_Func)GetProcAddress(dbghelp_dll, "SymGetSymFromAddr64");
	UnDecorateSymbolName = (UnDecorateSymbolName_Func)GetProcAddress(dbghelp_dll, "UnDecorateSymbolName");
	SymGetLineFromAddr64 = (SymGetLineFromAddr64_Func)GetProcAddress(dbghelp_dll, "SymGetLineFromAddr64");
	SymGetModuleInfo64 = (SymGetModuleInfo64_Func)GetProcAddress(dbghelp_dll, "SymGetModuleInfo64");
	StackWalk64 = (StackWalk64_Func)GetProcAddress(dbghelp_dll, "StackWalk64");

	HANDLE process = GetCurrentProcess();
	if (SymInitialize && SymGetOptions && SymSetOptions && SymLoadModule64 && SymInitialize(process, NULL, FALSE)) {
		DWORD options = SymGetOptions();
		options |= SYMOPT_LOAD_LINES;
		options |= SYMOPT_FAIL_CRITICAL_ERRORS;
		options |= SYMOPT_DEFERRED_LOADS;
		options = SymSetOptions(options);

		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
		if (snapshot != INVALID_HANDLE_VALUE) {
			#undef MODULEENTRY32 // Windows.h literally renames these to their wchar counterpart.
			#undef Module32First
			#undef Module32Next
			MODULEENTRY32 entry = { sizeof(entry) };
			for (BOOL keep_going = Module32First(snapshot, &entry); keep_going; keep_going = Module32Next(snapshot, &entry))
				SymLoadModule64(process, NULL, entry.szExePath, entry.szModule, (DWORD64)entry.modBaseAddr, entry.modBaseSize);
			CloseHandle(snapshot);
		}
	}
}

int generate_stacktrace(void *buffer[], int buffer_count, int frames_to_skip) {
	// RtlCaptureStackBackTrace is orders of magnitude faster than StackWalk64, but it's
	// undocumented and sometimes unreliable. Therefore we always try RtlCaptureStackBackTrace
	// first, and then fall back on StackWalk64.
	int num_frames = RtlCaptureStackBackTrace((DWORD)frames_to_skip + 1, buffer_count, buffer, NULL);
	if (num_frames <= 1) {
		init_dbghelp_dll();
		if (!StackWalk64)
			return 0;

		CONTEXT context = { .ContextFlags = CONTEXT_FULL }; // Or CONTEXT_ALL? Or is there even any difference??
		RtlCaptureContext(&context);

		DWORD machine_type;
		STACKFRAME64 frame = {
			.AddrPC.Mode    = AddrModeFlat,
			.AddrFrame.Mode = AddrModeFlat,
			.AddrStack.Mode = AddrModeFlat,
		};
		#ifdef _M_X64
		{
			machine_type = IMAGE_FILE_MACHINE_AMD64;
			frame.AddrPC.Offset    = context.Rip;
			frame.AddrFrame.Offset = context.Rbp;
			frame.AddrStack.Offset = context.Rsp;

			// Apparently StackWalk64 doesn't capture the frame of the functio that calls 
			// it in 32-bit code and so we only need to do this in x64. I'm not sure why.
			frames_to_skip += 1;
		}
		#elif defined _M_IX86
		{
			machine_type = IMAGE_FILE_MACHINE_I386;
			frame.AddrPC.Offset    = context.Eip;
			frame.AddrFrame.Offset = context.Ebp;
			frame.AddrStack.Offset = context.Esp;
		}
		#else
		{
			return 0; // Stacktraces not supported on ARM.
		}
		#endif

		HANDLE process = GetCurrentProcess();
		HANDLE thread = GetCurrentThread();
		num_frames = 0;
		for (int i = 0; StackWalk64(machine_type, process, thread, &frame, &context, NULL, NULL, NULL, NULL); ++i) {
			if (frame.AddrPC.Offset == 0)
				break;
			if (i >= (int)frames_to_skip && num_frames < buffer_count)
				buffer[num_frames++] = (void *)(uintptr_t)frame.AddrPC.Offset;
		}
	}

	// The PC will have advanced by 1 (or more) by the point we get the stack trace - we have to undo that otherwise we get wrong info!
	for (int i = 0; i < num_frames; ++i)
		buffer[i] = (char *)buffer[i] + 1;
	return num_frames;
}

void print_stacktrace(void *const stackframes[], int num_frames) {
	init_dbghelp_dll();
	HANDLE process = GetCurrentProcess();
	for (int i = 0; i < num_frames; ++i) {
		// Print either:
		// 1) function() in file, line x
		// 2) function() in !module
		// 3) 0xaddress in file, line x
		// 4) 0xaddress in !module
		// 5) 0xaddress

		DWORD64 address = (DWORD64)stackframes[i];
		DWORD64 symbol_buffer[64] = { 0 };
		IMAGEHLP_SYMBOL64 *symbol = (IMAGEHLP_SYMBOL64 *)symbol_buffer;
		symbol->SizeOfStruct = sizeof symbol[0];
		symbol->MaxNameLength = sizeof symbol_buffer - sizeof symbol[0];

		if (SymGetSymFromAddr64 && SymGetSymFromAddr64(process, address, &(DWORD64){0}, symbol)) {
			const char *function = symbol->Name;
			char undecorated[512];
			if (UnDecorateSymbolName) {
				UnDecorateSymbolName(function, undecorated, sizeof undecorated, UNDNAME_NAME_ONLY);
				undecorated[sizeof undecorated - 1] = 0;
				function = undecorated;
			}
			printf("%s()", function);
		} else printf("0x%p", stackframes[i]);

		IMAGEHLP_LINE64 line_info = { .SizeOfStruct = sizeof line_info };
		IMAGEHLP_MODULE64 module_info = { .SizeOfStruct = sizeof module_info };
		if (SymGetLineFromAddr64 && SymGetLineFromAddr64(process, address, &(DWORD){0}, &line_info)) {
			int line = (int)line_info.LineNumber;
			const char *file = line_info.FileName;
			printf(" in %s, line %d", file, line);
		} else if (SymGetModuleInfo64 && SymGetModuleInfo64(process, address, &module_info)) {
			const char *module = module_info.ModuleName;
			printf(" in !%s", module);
		}

		printf("\n");
	}
}

int main(void) {
	void *frames[128];
	int num_frames = generate_stacktrace(frames, 128, 0);
	print_stacktrace(frames, num_frames);
}