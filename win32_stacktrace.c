#include <Windows.h>
#include <TlHelp32.h>
#include <ImageHlp.h>
#include <stdio.h> // printf

int generate_stacktrace(void *buffer[], int buffer_count, int frames_to_skip) {
	int num_frames = RtlCaptureStackBackTrace((DWORD)frames_to_skip, buffer_count, buffer, NULL);
	if (num_frames <= 1) {		
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

		static BOOL already_tried_to_load;
		static HMODULE dbghelp_dll;
		static StackWalk64_Func StackWalk64_func;
		#define StackWalk64 StackWalk64_func

		if (!already_tried_to_load) {
			already_tried_to_load = TRUE;
			dbghelp_dll = LoadLibraryA("DbgHelp.dll");
			if (dbghelp_dll)
				StackWalk64 = (StackWalk64_Func)GetProcAddress(dbghelp_dll, "StackWalk64");
		}
		if (!StackWalk64)
			return 0;

		CONTEXT context = { .ContextFlags = CONTEXT_FULL }; // Or CONTEXT_ALL? Or is there even any difference??
		RtlCaptureContext(&context);

		DWORD image_type;
		STACKFRAME64 frame;
		#ifdef _M_X64
		{
			image_type = IMAGE_FILE_MACHINE_AMD64;
			frame.AddrPC.Offset    = context.Rip;
			frame.AddrPC.Mode      = AddrModeFlat;
			frame.AddrFrame.Offset = context.Rbp;
			frame.AddrFrame.Mode   = AddrModeFlat;
			frame.AddrStack.Offset = context.Rsp;
			frame.AddrStack.Mode   = AddrModeFlat;

			// Apparently StackWalk64 doesn't capture the frame of the functio that calls 
			// it in 32-bit code and so we only need to do this in x64. I'm not sure why.
			frames_to_skip += 1;
		}
		#elif defined _M_IX86
		{
			image_type = IMAGE_FILE_MACHINE_I386;
			frame.AddrPC.Offset    = context.Eip;
			frame.AddrPC.Mode      = AddrModeFlat;
			frame.AddrFrame.Offset = context.Ebp;
			frame.AddrFrame.Mode   = AddrModeFlat;
			frame.AddrStack.Offset = context.Esp;
			frame.AddrStack.Mode   = AddrModeFlat;
		}
		#else
		{
			return 0; // Stacktraces not supported on ARM.
		}
		#endif

		HANDLE process = GetCurrentProcess();
		HANDLE thread = GetCurrentThread();
		num_frames = 0;
		for (int i = 0; StackWalk64(image_type, process, thread, &frame, &context, NULL, NULL, NULL, NULL); ++i) {
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
	//DEF_DLL_FUNCTION(BOOL, SymInitialize, HANDLE hProcess, PCSTR UserSearchPath, BOOL fInvadeProcess);
	//DEF_DLL_FUNCTION(DWORD, SymGetOptions, void);
	//DEF_DLL_FUNCTION(DWORD, SymSetOptions, DWORD SymOptions);
	//DEF_DLL_FUNCTION(BOOL, SymGetSymFromAddr64, HANDLE hProcess, DWORD64 qwAddr, PDWORD64 pdwDisplacement, PIMAGEHLP_SYMBOL64 Symbol);
	//DEF_DLL_FUNCTION(DWORD, UnDecorateSymbolName, PCSTR name, PSTR outputString, DWORD maxStringLength, DWORD flags);
	//DEF_DLL_FUNCTION(BOOL, SymGetLineFromAddr64, HANDLE hProcess, DWORD64 qwAddr, PDWORD pdwDisplacement, PIMAGEHLP_LINE64 Line64);
	//DEF_DLL_FUNCTION(BOOL, SymGetModuleInfo64, HANDLE hProcess, DWORD64 qwAddr, PIMAGEHLP_MODULE64 ModuleInfo);
	//DEF_DLL_FUNCTION(DWORD64, SymLoadModule64, HANDLE hProcess, HANDLE hFile, PCSTR ImageName, PCSTR ModuleName, DWORD64 BaseOfDll, DWORD SizeOfDll);

	static BOOL already_tried_to_load;
	static HMODULE dbghelp_dll;

	/*
	LOAD_DLL_FUNCTION(dbghelp, SymInitialize);
	LOAD_DLL_FUNCTION(dbghelp, SymGetOptions);
	LOAD_DLL_FUNCTION(dbghelp, SymSetOptions);
	LOAD_DLL_FUNCTION(dbghelp, SymLoadModule64);
	LOAD_DLL_FUNCTION(dbghelp, SymGetSymFromAddr64);
	LOAD_DLL_FUNCTION(dbghelp, UnDecorateSymbolName);
	LOAD_DLL_FUNCTION(dbghelp, SymGetLineFromAddr64);
	LOAD_DLL_FUNCTION(dbghelp, SymGetModuleInfo64);
	LOAD_DLL_FUNCTION(dbghelp, StackWalk64);

	if (SymInitialize && SymGetOptions && SymSetOptions && SymLoadModule64 && SymInitialize(process, NULL, FALSE))
	{
		DWORD options = SymGetOptions();
		options |= SYMOPT_LOAD_LINES;
		options |= SYMOPT_FAIL_CRITICAL_ERRORS;
		options |= SYMOPT_DEFERRED_LOADS;
		options = SymSetOptions(options);

		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
		if (snapshot != INVALID_HANDLE_VALUE)
		{
			#undef MODULEENTRY32 // Windows literally renames these to their wchar counterpart..
			#undef Module32First
			#undef Module32Next
			MODULEENTRY32 entry = { sizeof(entry) };
			for (BOOL keep_going = Module32First(snapshot, &entry); keep_going; keep_going = Module32Next(snapshot, &entry))
				SymLoadModule64(process, NULL, entry.szExePath, entry.szModule, (DWORD64)(uptr)entry.modBaseAddr, entry.modBaseSize);

			CloseHandle(snapshot);
		}
	}
	*/

	/*
	for (int i = 0; i < num_frames; ++i)
	{
		append_char_repeated(&trace, ' ', 2);

		// Print one of 3 lines for each frame:
		//   [function] --- [file] line [line]
		//   [address]  --- ![module]
		//   [address]

		DWORD64 address = (DWORD64)(uptr)stackframes[i];

		_Alignas(IMAGEHLP_SYMBOL64) u8 symbol_buffer[512] = { 0 };
		IMAGEHLP_SYMBOL64 *symbol = (IMAGEHLP_SYMBOL64 *)symbol_buffer;
		symbol->SizeOfStruct = sizeof symbol[0];
		symbol->MaxNameLength = sizeof symbol_buffer - sizeof symbol[0];
		if (SymGetSymFromAddr64 && SymGetSymFromAddr64(process, address, &(DWORD64){0}, symbol))
		{
			char function[256];
			function[255] = 0;
			if (UnDecorateSymbolName)
				UnDecorateSymbolName(symbol->Name, function, sizeof function - 1, UNDNAME_NAME_ONLY);
			else
				copy_string(function, symbol->Name, sizeof function);
			
			append_string(&trace, function);
			append_string(&trace, "()");
		}
		else append_format(&trace, "0x%p", stackframes[i]);

		IMAGEHLP_LINE64 line_info = { .SizeOfStruct = sizeof line_info };
		IMAGEHLP_MODULE64 module_info = { .SizeOfStruct = sizeof module_info };
		if (SymGetLineFromAddr64 && SymGetLineFromAddr64(process, address, &(DWORD){0}, &line_info))
		{
			int line = (int)line_info.LineNumber;
			const char *file = path_skip_to_filename(line_info.FileName);
			append_format(&trace, " in %s, line %d", file, line);
		}
		else if (SymGetModuleInfo64 && SymGetModuleInfo64(process, address, &module_info))
		{
			const char *module = module_info.ModuleName;
			append_format(&trace, " in !%s", module);
		}

		if (i != num_frames - 1)
			append_char(&trace, '\n');
	}
	*/
}

int main(void) {

}