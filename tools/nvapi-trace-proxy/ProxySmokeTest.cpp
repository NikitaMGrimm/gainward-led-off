#include <windows.h>

#include <array>
#include <cstdint>
#include <iostream>

using QueryInterface = void* (__cdecl*)(std::uint32_t);
using NvInitialize = std::int32_t (__cdecl*)();
using NvPhysicalGpuHandle = void*;
using NvEnumPhysicalGpus = std::int32_t (__cdecl*)(NvPhysicalGpuHandle*, std::int32_t*);
using NvGetPciIdentifiers = std::int32_t (__cdecl*)(NvPhysicalGpuHandle, std::uint32_t*,
                                                    std::uint32_t*, std::uint32_t*, std::uint32_t*);

int wmain() {
    HMODULE module = LoadLibraryW(L".\\nvapi64.dll");
    if (!module) {
        std::wcerr << L"Could not load proxy: " << GetLastError() << L"\n";
        return 1;
    }
    auto query = reinterpret_cast<QueryInterface>(GetProcAddress(module, "nvapi_QueryInterface"));
    if (!query) {
        std::wcerr << L"Proxy export missing.\n";
        return 2;
    }
    auto initialize = reinterpret_cast<NvInitialize>(query(0x0150E828));
    if (!initialize) {
        std::wcerr << L"Real NVAPI function was not forwarded.\n";
        return 3;
    }
    const std::int32_t status = initialize();
    std::wcout << L"NvAPI_Initialize through proxy: " << status << L"\n";
    if (status != 0) {
        return 4;
    }

    auto enumerate = reinterpret_cast<NvEnumPhysicalGpus>(query(0xE5AC921F));
    auto getPci = reinterpret_cast<NvGetPciIdentifiers>(query(0x2DDFB66E));
    if (!enumerate || !getPci) {
        return 5;
    }
    std::array<NvPhysicalGpuHandle, 64> handles{};
    std::int32_t count = 0;
    if (enumerate(handles.data(), &count) != 0 || count <= 0) {
        return 6;
    }
    std::uint32_t device = 0;
    std::uint32_t subsystem = 0;
    std::uint32_t revision = 0;
    std::uint32_t external = 0;
    if (getPci(handles[0], &device, &subsystem, &revision, &external) != 0) {
        return 7;
    }
    std::wcout << L"PCI through traced wrapper: " << std::hex << device << L" / " << subsystem << L"\n";
    return 0;
}
