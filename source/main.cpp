#include "application.hpp"
#include <chrono>
#include <thread>

#ifdef WINDOWS
#include <Windows.h>
#include <shellapi.h>
#include <iostream>
#include <DbgHelp.h>

#pragma comment(lib, "Dbghelp.lib")

static std::wstring GetDumpFileName()
{
	SYSTEMTIME st;
	GetLocalTime(&st);

	wchar_t filename[MAX_PATH];

	swprintf_s(
		filename,
		L"crash_%04d-%02d-%02d_%02d-%02d-%02d.dmp",
		st.wYear,
		st.wMonth,
		st.wDay,
		st.wHour,
		st.wMinute,
		st.wSecond);

	return filename;
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* exceptionInfo)
{
	HANDLE hFile = CreateFileW(
		GetDumpFileName().c_str(),
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
		dumpInfo.ThreadId = GetCurrentThreadId();
		dumpInfo.ExceptionPointers = exceptionInfo;
		dumpInfo.ClientPointers = FALSE;
		MINIDUMP_TYPE dumpType =
			(MINIDUMP_TYPE)(
				MiniDumpWithDataSegs |
				MiniDumpWithHandleData |
				MiniDumpWithThreadInfo |
				MiniDumpWithIndirectlyReferencedMemory |
				MiniDumpScanMemory |
				MiniDumpWithModuleHeaders |       
				MiniDumpWithFullAuxiliaryState |   
				MiniDumpWithProcessThreadData |
				MiniDumpWithFullMemoryInfo |
				MiniDumpWithUnloadedModules
				);

		MiniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			hFile,
			dumpType,
			&dumpInfo,
			nullptr,
			nullptr);

		CloseHandle(hFile);
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow) {

	SetUnhandledExceptionFilter(CrashHandler);

	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		FILE* fp;
		freopen_s(&fp, "CONOUT$", "w", stdout);
		freopen_s(&fp, "CONOUT$", "w", stderr);
		freopen_s(&fp, "CONIN$", "r", stdin);
	}

	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
	std::filesystem::current_path(exeDir);

	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::string argument1 = "";
	bool startMinimized = false;
	int delaySeconds = 0;

	for (int i = 1; i < argc; i++) {
		if (!argv[i]) continue;
		std::wstring warg(argv[i]);
		if (warg == L"--minimized" || warg == L"-m") {
			startMinimized = true;
		}
		else if (warg == L"--delay" || warg == L"-d") {
			if (i + 1 < argc && argv[i + 1]) {
				try {
					delaySeconds = std::stoi(argv[++i]);
				} catch (...) {
					delaySeconds = 15;
				}
			}
		}
		else if (warg.rfind(L"--delay=", 0) == 0) {
			try {
				delaySeconds = std::stoi(warg.substr(8));
			} catch (...) {
				delaySeconds = 15;
			}
		}
		else if (warg.rfind(L"--", 0) != 0) {
			int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
			if (size > 1) {
				argument1.resize(size - 1);
				WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, argument1.data(), size - 1, nullptr, nullptr);
			}
		}
	}

	LocalFree(argv);

	if (delaySeconds > 0) {
		std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
	}

	Application application;
	application.Run(argument1, startMinimized);
}
#else
int main(int argc, char* argv[]) {
	#ifdef LINUX
	gtk_disable_setlocale();
	gtk_init(&argc, &argv);
	#endif

	std::string argument1 = "";
	bool startMinimized = false;
	int delaySeconds = 0;

	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--minimized" || arg == "-m") {
			startMinimized = true;
		}
		else if (arg == "--delay" || arg == "-d") {
			if (i + 1 < argc) {
				try {
					delaySeconds = std::stoi(argv[++i]);
				} catch (...) {
					delaySeconds = 15;
				}
			}
		}
		else if (arg.rfind("--delay=", 0) == 0) {
			try {
				delaySeconds = std::stoi(arg.substr(8));
			} catch (...) {
				delaySeconds = 15;
			}
		}
		else if (arg.rfind("--", 0) != 0) {
			argument1 = arg;
		}
	}

	if (delaySeconds > 0) {
		std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
	}
	
	Application application;
	application.Run(argument1, startMinimized);
}
#endif
