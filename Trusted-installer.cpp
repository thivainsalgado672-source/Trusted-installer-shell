#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>

#pragma comment(lib, "advapi32.lib")

const wchar_t* seDebugPrivilege = L"SeDebugPrivilege";
const wchar_t* tiServiceName = L"TrustedInstaller";
const wchar_t* tiExecutableName = L"trustedinstaller.exe";

// Helper: Check for Admin by attempting to open a raw handle to the physical drive
bool checkIfAdmin() {
    HANDLE h = CreateFileW(L"\\\\.\\PHYSICALDRIVE0", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(h);
    return true;
}

// Helper: Standard UAC "runas" elevation
void elevate() {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        throw std::runtime_error("Failed to get executable path");
    }

    wchar_t cwd[MAX_PATH];
    if (!GetCurrentDirectoryW(MAX_PATH, cwd)) {
        throw std::runtime_error("Failed to get current directory");
    }

    // Build command line arguments string
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        throw std::runtime_error("Failed to parse command line");
    }

    std::wstring args;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) args += L" ";
        // Quote arguments if they contain spaces
        if (wcschr(argv[i], L' ')) {
            args += L"\"";
            args += argv[i];
            args += L"\"";
        }
        else {
            args += argv[i];
        }
    }
    LocalFree(argv);

    HINSTANCE result = ShellExecuteW(nullptr, L"runas", exePath, args.c_str(), cwd, SW_NORMAL);
    if ((INT_PTR)result <= 32) {
        throw std::runtime_error("Failed to elevate process");
    }
    ExitProcess(0);
}

// Helper: Enable SeDebugPrivilege so we can open SYSTEM processes
void enableSeDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        throw std::runtime_error("OpenProcessToken failed");
    }

    LUID luid;
    if (!LookupPrivilegeValueW(nullptr, seDebugPrivilege, &luid)) {
        CloseHandle(hToken);
        throw std::runtime_error("LookupPrivilegeValue failed");
    }

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr)) {
        CloseHandle(hToken);
        throw std::runtime_error("AdjustTokenPrivileges failed");
    }

    CloseHandle(hToken);
}

// Helper: Find PID by name using Toolhelp32Snapshot
DWORD getProcessPid(const std::wstring& name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateToolhelp32Snapshot failed");
    }

    PROCESSENTRY32W procEntry;
    procEntry.dwSize = sizeof(procEntry);

    if (!Process32FirstW(snapshot, &procEntry)) {
        CloseHandle(snapshot);
        throw std::runtime_error("Process32First failed");
    }

    do {
        std::wstring exeName = procEntry.szExeFile;
        if (_wcsicmp(exeName.c_str(), name.c_str()) == 0) {
            CloseHandle(snapshot);
            return procEntry.th32ProcessID;
        }
    } while (Process32NextW(snapshot, &procEntry));

    CloseHandle(snapshot);
    throw std::runtime_error("process " + std::string(name.begin(), name.end()) + " not found");
}

// Helper: Open SCM and start TrustedInstaller service if not running
void startTrustedInstallerService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        throw std::runtime_error("OpenSCManager failed");
    }

    SC_HANDLE service = OpenServiceW(scm, tiServiceName, SERVICE_QUERY_STATUS | SERVICE_START);
    if (!service) {
        CloseServiceHandle(scm);
        throw std::runtime_error("OpenService failed");
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        throw std::runtime_error("QueryServiceStatusEx failed");
    }

    if (ssp.dwCurrentState != SERVICE_RUNNING) {
        if (!StartServiceW(service, 0, nullptr)) {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            throw std::runtime_error("StartService failed");
        }
        // Wait for service to start
        do {
            Sleep(500);
            if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
                CloseServiceHandle(service);
                CloseServiceHandle(scm);
                throw std::runtime_error("QueryServiceStatusEx failed");
            }
        } while (ssp.dwCurrentState == SERVICE_START_PENDING);
        if (ssp.dwCurrentState != SERVICE_RUNNING) {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            throw std::runtime_error("TrustedInstaller service failed to start");
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
}

// Main Logic: The Hijack
void RunAsTrustedInstaller(const std::wstring& path, const std::vector<std::wstring>& args) {
    if (!checkIfAdmin()) {
        elevate();
    }

    enableSeDebugPrivilege();

    startTrustedInstallerService();

    DWORD tiPid = getProcessPid(tiExecutableName);

    HANDLE tiHandle = OpenProcess(PROCESS_CREATE_PROCESS | PROCESS_DUP_HANDLE | PROCESS_SET_INFORMATION, FALSE, tiPid);
    if (!tiHandle) {
        throw std::runtime_error("OpenProcess on TrustedInstaller failed");
    }

    // Prepare command line
    std::wstring cmdLine = L"\"" + path + L"\"";
    for (const auto& arg : args) {
        cmdLine += L" ";
        // Quote argument if contains spaces
        if (arg.find(L' ') != std::wstring::npos) {
            cmdLine += L"\"" + arg + L"\"";
        }
        else {
            cmdLine += arg;
        }
    }

    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);

    SIZE_T attrListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrListSize);
    if (!si.lpAttributeList) {
        CloseHandle(tiHandle);
        throw std::runtime_error("HeapAlloc failed");
    }
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrListSize)) {
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        CloseHandle(tiHandle);
        throw std::runtime_error("InitializeProcThreadAttributeList failed");
    }

    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &tiHandle, sizeof(HANDLE), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        CloseHandle(tiHandle);
        throw std::runtime_error("UpdateProcThreadAttribute failed");
    }

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL success = CreateProcessW(
        nullptr,
        &cmdLine[0],
        nullptr,
        nullptr,
        FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_NEW_CONSOLE,
        nullptr,
        nullptr,
        &si.StartupInfo,
        &pi);

    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
    CloseHandle(tiHandle);

    if (!success) {
        throw std::runtime_error("CreateProcess failed");
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

int wmain(int argc, wchar_t* argv[]) {
    try {
        std::wstring path = L"cmd.exe";
        std::vector<std::wstring> args;
        // If you want to pass arguments from command line, you can parse here
        RunAsTrustedInstaller(path, args);
    }
    catch (const std::exception& ex) {
        std::wcerr << L"Fatal: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
