#include "debug.h"

#include <stdarg.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>

static uint32_t s_mask = 0xffffffff;

static LONG debug_exception_handler(LPEXCEPTION_POINTERS ExceptionInfo)
{
	debug_print(k_print_error, "Caught exception!\n");

	HANDLE file = CreateFile(L"ga2022-crash.dmp", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION mini_exception = { 0 };
		mini_exception.ThreadId = GetCurrentThreadId();
		mini_exception.ExceptionPointers = ExceptionInfo;
		mini_exception.ClientPointers = FALSE;

		MiniDumpWriteDump(GetCurrentProcess(),
			GetCurrentProcessId(),
			file,
			MiniDumpWithThreadInfo,
			&mini_exception,
			NULL,
			NULL);

		CloseHandle(file);
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

void debug_install_exception_handler()
{
	AddVectoredExceptionHandler(TRUE, debug_exception_handler);
}

void debug_set_print_mask(uint32_t mask)
{
	s_mask = mask;
}

void debug_print(uint32_t type, _Printf_format_string_ const char* format, ...)
{
	if ((s_mask & type) == 0)
	{
		return;
	}

	va_list args;
	va_start(args, format);
	char buffer[256];
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	OutputDebugStringA(buffer);

	DWORD bytes = (DWORD)strlen(buffer);
	DWORD written = 0;
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	WriteConsoleA(out, buffer, bytes, &written, NULL);
}

int debug_backtrace(void** stack, int stack_capacity)
{
	return CaptureStackBackTrace(2, stack_capacity, stack, NULL);
}

void symbol_init() {
	SymSetOptions(SYMOPT_LOAD_LINES);
	if (!SymInitialize(GetCurrentProcess(), 0, TRUE)) {
		debug_print(k_print_error, "Cannot initialized Symbol\n");
	}
}

void symbol_clean() {
	SymCleanup(GetCurrentProcess());
}

void callstack_print(void* stack[], int stack_count, heap_t* heap) {
	SYMBOL_INFO* sym_info = (SYMBOL_INFO*)heap_alloc(heap, sizeof(SYMBOL_INFO) + 254, 8);
	sym_info->SizeOfStruct = sizeof(SYMBOL_INFO);
	sym_info->MaxNameLen = 255;
	sym_info->Flags = SYMFLAG_FUNCTION;

	IMAGEHLP_LINE* line_info = (IMAGEHLP_LINE*)heap_alloc(heap, sizeof(IMAGEHLP_LINE), 8);
	line_info->SizeOfStruct = sizeof(IMAGEHLP_LINE);

	DWORD displace = 0;
	for (int i = 0; i < stack_count; i++) {
		if (!stack[i])break;
		if (SymFromAddr(GetCurrentProcess(), (DWORD64)stack[i], 0, sym_info))
		{
			debug_print(k_print_warning, "[%d] %p %s", i, stack[i], sym_info->Name);
				
			if (SymGetLineFromAddr64(GetCurrentProcess(), (DWORD64)stack[i], &displace, line_info)) {
				char* file_name = strrchr(line_info->FileName, '\\')+1;
				debug_print(k_print_warning, " at %s:%d", file_name, line_info->LineNumber);
			}
			
			debug_print(k_print_warning, "\n");

			// stop callstack print at main, could be deleted
			if (!strcmp(sym_info->Name, "main")) break;
		}
		else debug_print(k_print_warning, "[%d] Cannot retrive function name %p\n", i, stack[i]);
	}
	debug_print(k_print_warning, "\n");

	heap_free(heap, sym_info);
	heap_free(heap, line_info);
}
