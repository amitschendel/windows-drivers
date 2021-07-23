#include <iostream>
#include <Windows.h>
#include <string>
#include "../ProcessGuard/ProcessGuardCommon.h"
using namespace std;

#define ERROR_MSG(msg) ::cout << msg << "Error code: " << GetLastError()

int main() {
	auto hFile = ::CreateFile(L"\\\\.\\ProcessGuard", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		ERROR_MSG("Invalid handle value");
		return 1;
	}
	PCWSTR path = L"\\??\\C:\\Windows\\system32\\notepad.exe";
	PCWSTR processName = L"notepad.exe";
	ProcessData data;
	
	data.ProcessPath = path;
	data.ProcessName = processName;

	DWORD returned;
	BOOL success = DeviceIoControl(hFile, IOCTL_PROCESS_GUARD_FORBIDDEN_PROCESS, &data, sizeof(data), nullptr, 0, &returned, nullptr);

	if (success)
		printf("Process added succeeded!\n");
	else
		ERROR_MSG("Process add failed!");

	CloseHandle(hFile);

	return 0;
}