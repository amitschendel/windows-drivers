#include <iostream>
#include <Windows.h>
using namespace std;

#define ERROR_MSG(msg) ::cout << msg << "Error code: " << GetLastError()

int main() {
	auto hFile = ::CreateFile(L"\\\\.\\TokenElevator", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		ERROR_MSG("Invalid handle value\n");
		return 1;
	}

    DWORD ProcessId, write;
    ProcessId = 0;
    while (ProcessId != -1)
    {   
        cout << "Enter PID: ";
        cin >> ProcessId;
        cout << endl;
        if (!WriteFile(hFile, &ProcessId, sizeof(DWORD), &write, NULL))
        {
            ERROR_MSG("Error: Unable to replace process token\n");
        }
        else
        {
            cout << "Successfully replaced process token with system token." << endl;
        }
    }
    CloseHandle(hFile);
    return 0;
}