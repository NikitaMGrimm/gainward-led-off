#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <iostream>

using QueryInterface = void* (__cdecl*)(std::uint32_t);
using NvInitialize = std::int32_t (__cdecl*)();

int wmain() {
    HMODULE nvapi = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!nvapi) {
        return 1;
    }
    auto query = reinterpret_cast<QueryInterface>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    if (!query) {
        return 2;
    }
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(query), &owner)) {
        return 3;
    }
    wchar_t ownerPath[MAX_PATH]{};
    GetModuleFileNameW(owner, ownerPath, MAX_PATH);
    std::wcout << L"QueryInterface owner: " << ownerPath << L"\n";
    if (wcsstr(ownerPath, L"NvapiTraceHook.dll") == nullptr) {
        return 4;
    }
    auto initialize = reinterpret_cast<NvInitialize>(query(0x0150E828));
    const std::int32_t status = initialize ? initialize() : -1;
    std::wcout << L"NvAPI_Initialize through injected hook: " << status << L"\n";
    return status == 0 ? 0 : 5;
}
