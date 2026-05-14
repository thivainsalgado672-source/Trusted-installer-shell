#pragma comment(linker, "/SECTION:.text,ERW")
#pragma comment(linker, "/MERGE:.rdata=.text")
#pragma comment(linker, "/MERGE:.data=.text")


#include <windows.h>
#include <tlhelp32.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "kernel32.lib")

const wchar_t* seDebugPrivilege = L"SeDebugPrivilege";
const wchar_t* tiServiceName = L"TrustedInstaller";
const wchar_t* tiExecutableName = L"trustedinstaller.exe";

// Look at me. I am the C Runtime now.
#pragma function(memset)
extern "C" void* __cdecl memset(void* dest, int c, size_t count) {
    char* bytes = (char*)dest;
    while (count--) {
        *bytes++ = (char)c;
    }
    return dest;
}

BOOL checkIfAdmin() {
    HANDLE hToken;
    BOOL isElevated = FALSE;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD size;
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &size)) {
            isElevated = elevation.TokenIsElevated != 0;
        }
        CloseHandle(hToken);
    }
    return isElevated;
}

void elevate() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_NORMAL;

    ShellExecuteExW(&sei);
    ExitProcess(0);
}

BOOL enableSeDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return FALSE;

    LUID luid;
    if (!LookupPrivilegeValueW(nullptr, seDebugPrivilege, &luid)) {
        CloseHandle(hToken);
        return FALSE;
    }

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL result = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(hToken);
    return result;
}

DWORD getProcessPid(LPCWSTR name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W procEntry = { sizeof(procEntry) };
    if (Process32FirstW(snapshot, &procEntry)) {
        do {
            // Using Win32 lstrcmpiW instead of CRT _wcsicmp
            if (lstrcmpiW(procEntry.szExeFile, name) == 0) {
                CloseHandle(snapshot);
                return procEntry.th32ProcessID;
            }
        } while (Process32NextW(snapshot, &procEntry));
    }
    CloseHandle(snapshot);
    return 0;
}

BOOL startTrustedInstallerService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return FALSE;

    SC_HANDLE service = OpenServiceW(scm, tiServiceName, SERVICE_QUERY_STATUS | SERVICE_START);
    if (!service) {
        CloseServiceHandle(scm);
        return FALSE;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded;
    BOOL started = FALSE;

    while (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_RUNNING) {
            started = TRUE;
            break;
        }
        if (ssp.dwCurrentState != SERVICE_START_PENDING) {
            if (!StartServiceW(service, 0, nullptr)) break;
        }
        Sleep(100);
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return started;
}

BOOL RunAsTrustedInstaller(LPWSTR cmdLine) {
    if (!checkIfAdmin()) elevate();

    if (!enableSeDebugPrivilege()) return FALSE;
    if (!startTrustedInstallerService()) return FALSE;

    DWORD tiPid = getProcessPid(tiExecutableName);
    if (tiPid == 0) return FALSE;

    HANDLE tiHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, tiPid);
    if (!tiHandle) return FALSE;

    STARTUPINFOEXW si = { 0 };
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);

    SIZE_T attrListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrListSize);
    InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrListSize);

    UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &tiHandle, sizeof(HANDLE), nullptr, nullptr);

    PROCESS_INFORMATION pi = { 0 };
    BOOL success = CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NEW_CONSOLE, nullptr, nullptr, &si.StartupInfo, &pi);

    if (success) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
    CloseHandle(tiHandle);

    return success;
}

// Custom entry point to bypass CRT initialization entirely
void wmainCRTStartup() {
    WCHAR cmd[] = L"cmd.exe";
    RunAsTrustedInstaller(cmd);

    // You MUST call ExitProcess when bypassing the CRT, otherwise the thread just falls off a cliff.
    ExitProcess(0);
}
