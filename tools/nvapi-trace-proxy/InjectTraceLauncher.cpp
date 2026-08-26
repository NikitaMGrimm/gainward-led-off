#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <iostream>
#include <string>

std::wstring directoryOf(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring fileNameOf(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::uintptr_t remoteModuleBase(DWORD processId, const wchar_t* moduleName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    MODULEENTRY32W entry{sizeof(entry)};
    std::uintptr_t result = 0;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, moduleName) == 0) {
                result = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

bool runRemoteThread(HANDLE process, LPTHREAD_START_ROUTINE function, void* argument,
                     DWORD* exitCode = nullptr) {
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, function, argument, 0, nullptr);
    if (!thread) {
        return false;
    }
    const bool completed = WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0;
    DWORD result = 0;
    const bool readResult = completed && GetExitCodeThread(thread, &result) != FALSE;
    CloseHandle(thread);
    if (exitCode && readResult) {
        *exitCode = result;
    }
    return readResult;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::wcerr << L"Usage: InjectTraceLauncher.exe <TBPanel.exe> <nvapi64.dll>\n";
        return 2;
    }
    const std::wstring executable = argv[1];
    const std::wstring proxy = argv[2];
    const std::wstring workingDirectory = directoryOf(executable);

    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + executable + L"\"";
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, workingDirectory.c_str(), &startup, &process)) {
        std::wcerr << L"Could not start EXPERTool: " << GetLastError() << L"\n";
        return 3;
    }
    BOOL targetIsWow64 = FALSE;
    if (!IsWow64Process(process.hProcess, &targetIsWow64) || targetIsWow64) {
        std::wcerr << L"The trace launcher requires a 64-bit target.\n";
        TerminateProcess(process.hProcess, 4);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 4;
    }

    const std::size_t pathBytes = (proxy.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process.hProcess, nullptr, pathBytes,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool injected = remotePath &&
        WriteProcessMemory(process.hProcess, remotePath, proxy.c_str(), pathBytes, nullptr) != FALSE;
    if (injected) {
        injected = runRemoteThread(process.hProcess,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(&LoadLibraryW), remotePath);
    }
    if (remotePath) {
        VirtualFreeEx(process.hProcess, remotePath, 0, MEM_RELEASE);
    }

    const std::wstring proxyName = fileNameOf(proxy);
    std::uintptr_t remoteProxy = injected ?
        remoteModuleBase(process.dwProcessId, proxyName.c_str()) : 0;
    HMODULE localProxy = LoadLibraryExW(proxy.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    FARPROC localInstall = localProxy ? GetProcAddress(localProxy, "InstallTraceHook") : nullptr;
    DWORD hookResult = 0;
    if (remoteProxy && localInstall) {
        const auto offset = reinterpret_cast<std::uintptr_t>(localInstall) -
                            reinterpret_cast<std::uintptr_t>(localProxy);
        injected = runRemoteThread(process.hProcess,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteProxy + offset), nullptr, &hookResult) &&
            hookResult != 0;
    } else {
        injected = false;
    }
    if (localProxy) {
        FreeLibrary(localProxy);
    }

    if (!injected) {
        std::wcerr << L"Could not install the trace hook. Error: " << GetLastError() << L"\n";
        TerminateProcess(process.hProcess, 5);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 5;
    }

    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    std::wcout << L"Target exit code: " << exitCode << L"\n";
    return 0;
}
