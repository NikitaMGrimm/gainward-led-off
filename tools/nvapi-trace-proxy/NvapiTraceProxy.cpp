#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using NvStatus = std::int32_t;
using NvPhysicalGpuHandle = void*;
using QueryInterface = void* (__cdecl*)(std::uint32_t);
using I2cFunction = NvStatus (__cdecl*)(NvPhysicalGpuHandle, void*, std::uint32_t*);
using IllumFunction = NvStatus (__cdecl*)(NvPhysicalGpuHandle, void*);
using GetPciIdentifiers = NvStatus (__cdecl*)(NvPhysicalGpuHandle, std::uint32_t*,
                                              std::uint32_t*, std::uint32_t*, std::uint32_t*);

extern "C" IMAGE_DOS_HEADER __ImageBase;

constexpr std::uint32_t I2cWriteExId = 0x283AC65A;
constexpr std::uint32_t I2cReadExId = 0x4D7B0709;
constexpr std::uint32_t IllumGetControlId = 0x3DBF5764;
constexpr std::uint32_t IllumSetControlId = 0x197D065E;
constexpr std::uint32_t GetPciIdentifiersId = 0x2DDFB66E;

struct I2cInfoV3 {
    std::uint32_t version;
    std::uint32_t displayMask;
    std::uint8_t isDdcPort;
    std::uint8_t deviceAddress;
    std::uint8_t padding0[6];
    std::uint8_t* registerAddress;
    std::uint32_t registerAddressSize;
    std::uint32_t padding1;
    std::uint8_t* data;
    std::uint32_t dataSize;
    std::uint32_t legacySpeed;
    std::uint32_t speedKhz;
    std::uint8_t portId;
    std::uint8_t padding2[3];
    std::uint32_t isPortIdSet;
};

static_assert(sizeof(I2cInfoV3) == 64);

INIT_ONCE initialization = INIT_ONCE_STATIC_INIT;
INIT_ONCE logInitialization = INIT_ONCE_STATIC_INIT;
HMODULE realNvapi = nullptr;
QueryInterface realQuery = nullptr;
QueryInterface realDirect = nullptr;
HANDLE logFile = INVALID_HANDLE_VALUE;
SRWLOCK logLock = SRWLOCK_INIT;
std::uint64_t logSequence = 0;
using GetProcAddressFunction = FARPROC (WINAPI*)(HMODULE, LPCSTR);
GetProcAddressFunction originalGetProcAddress = nullptr;

extern "C" __declspec(dllexport) void* __cdecl nvapi_QueryInterface(std::uint32_t id);
extern "C" __declspec(dllexport) void* __cdecl nvapi_Direct_GetMethod(std::uint32_t id);

std::wstring moduleDirectory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) {
        slash[1] = L'\0';
    }
    return path;
}

BOOL CALLBACK initializeRealNvapi(PINIT_ONCE, PVOID, PVOID*) {
    const std::wstring path = moduleDirectory() + L"nvapi64.real.dll";
    realNvapi = LoadLibraryW(path.c_str());
    if (!realNvapi) {
        return FALSE;
    }
    realQuery = reinterpret_cast<QueryInterface>(GetProcAddress(realNvapi, "nvapi_QueryInterface"));
    realDirect = reinterpret_cast<QueryInterface>(GetProcAddress(realNvapi, "nvapi_Direct_GetMethod"));
    return realQuery != nullptr;
}

bool ensureInitialized() {
    return InitOnceExecuteOnce(&initialization, initializeRealNvapi, nullptr, nullptr) != FALSE;
}

BOOL CALLBACK initializeLog(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t localAppData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    std::wstring directory;
    if (length && length < MAX_PATH) {
        directory = std::wstring(localAppData) + L"\\GainwardLedTrace";
        CreateDirectoryW(directory.c_str(), nullptr);
        directory += L"\\";
    } else {
        directory = moduleDirectory();
    }
    const std::wstring path = directory + L"NvApiLedTrace.log";
    logFile = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return TRUE;
}

void append(const std::string& text) {
    InitOnceExecuteOnce(&logInitialization, initializeLog, nullptr, nullptr);
    if (logFile == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("NvapiTraceProxy: could not open the trace log.\n");
        return;
    }
    AcquireSRWLockExclusive(&logLock);
    const std::string line = "#" + std::to_string(++logSequence) + " " + text;
    DWORD written = 0;
    WriteFile(logFile, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    ReleaseSRWLockExclusive(&logLock);
}

std::string hex(const std::uint8_t* bytes, std::size_t size, std::size_t limit = 0x2000) {
    if (!bytes) {
        return "<null>";
    }
    static constexpr char digits[] = "0123456789ABCDEF";
    const std::size_t count = std::min(size, limit);
    std::string result;
    result.reserve(count * 3 + 24);
    for (std::size_t index = 0; index < count; ++index) {
        if (index) {
            result.push_back(' ');
        }
        result.push_back(digits[bytes[index] >> 4]);
        result.push_back(digits[bytes[index] & 0x0F]);
    }
    if (count != size) {
        result += " ...";
    }
    return result;
}

bool copyMemory(const void* source, void* destination, std::size_t size) {
    if (!source || !destination || size == 0) {
        return false;
    }
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), source, destination, size, &copied) != FALSE &&
           copied == size;
}

template <typename T>
bool readValue(const T* source, T& destination) {
    return copyMemory(source, &destination, sizeof(T));
}

std::string hexMemory(const std::uint8_t* bytes, std::size_t size, std::size_t limit = 0x2000) {
    if (!bytes) {
        return "<null>";
    }
    const std::size_t count = (std::min)(size, limit);
    std::vector<std::uint8_t> copy(count);
    if (count && !copyMemory(bytes, copy.data(), count)) {
        return "<unreadable>";
    }
    std::string result = hex(copy.data(), copy.size(), copy.size());
    if (count != size) {
        result += " ...";
    }
    return result;
}

std::string timestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char output[64]{};
    std::snprintf(output, sizeof(output), "%04u-%02u-%02u %02u:%02u:%02u.%03u ",
                  time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                  time.wSecond, time.wMilliseconds);
    return output;
}

FARPROC WINAPI traceGetProcAddress(HMODULE module, LPCSTR name) {
    FARPROC result = originalGetProcAddress ? originalGetProcAddress(module, name) : nullptr;
    if (!result || reinterpret_cast<std::uintptr_t>(name) <= 0xFFFF) {
        return result;
    }
    wchar_t modulePath[MAX_PATH]{};
    if (!GetModuleFileNameW(module, modulePath, MAX_PATH)) {
        return result;
    }
    const wchar_t* fileName = wcsrchr(modulePath, L'\\');
    fileName = fileName ? fileName + 1 : modulePath;
    if (_wcsicmp(fileName, L"nvapi64.dll") != 0) {
        return result;
    }
    if (std::strcmp(name, "nvapi_QueryInterface") == 0) {
        return reinterpret_cast<FARPROC>(&nvapi_QueryInterface);
    }
    if (std::strcmp(name, "nvapi_Direct_GetMethod") == 0) {
        return reinterpret_cast<FARPROC>(&nvapi_Direct_GetMethod);
    }
    return result;
}

extern "C" __declspec(dllexport) BOOL WINAPI InstallTraceHook() {
    HMODULE executable = GetModuleHandleW(nullptr);
    if (!executable) {
        return FALSE;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(executable);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return FALSE;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const std::uint8_t*>(executable) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return FALSE;
    }
    const IMAGE_DATA_DIRECTORY& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) {
        return FALSE;
    }

    originalGetProcAddress = &GetProcAddress;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        reinterpret_cast<std::uint8_t*>(executable) + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            reinterpret_cast<std::uint8_t*>(executable) + descriptor->FirstThunk);
        for (; thunk->u1.Function; ++thunk) {
            void** slot = reinterpret_cast<void**>(&thunk->u1.Function);
            if (*slot != reinterpret_cast<void*>(originalGetProcAddress)) {
                continue;
            }
            DWORD oldProtection = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection)) {
                return FALSE;
            }
            InterlockedExchangePointer(slot, reinterpret_cast<void*>(&traceGetProcAddress));
            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(*slot), oldProtection, &ignored);
            append(timestamp() + "HOOK GetProcAddress installed\r\n");
            return TRUE;
        }
    }
    return FALSE;
}

I2cFunction realI2c(std::uint32_t id) {
    return ensureInitialized() ? reinterpret_cast<I2cFunction>(realQuery(id)) : nullptr;
}

NvStatus __cdecl traceI2cWrite(NvPhysicalGpuHandle gpu, void* rawInfo, std::uint32_t* auxiliary) {
    I2cFunction function = realI2c(I2cWriteExId);
    if (!function) {
        return -3;
    }
    const NvStatus status = function(gpu, rawInfo, auxiliary);
    if (!rawInfo) {
        append(timestamp() + "I2C_WRITE status=" + std::to_string(status) + " info=<null>\r\n");
        return status;
    }
    std::uint32_t version = 0;
    if (!readValue(static_cast<std::uint32_t*>(rawInfo), version)) {
        append(timestamp() + "I2C_WRITE status=" + std::to_string(status) + " info=<unreadable>\r\n");
        return status;
    }
    if ((version & 0xFFFF) < sizeof(I2cInfoV3)) {
        append(timestamp() + "I2C_WRITE status=" + std::to_string(status) +
               " unsupportedVersion=" + std::to_string(version) + "\r\n");
        return status;
    }
    I2cInfoV3 info{};
    if (!copyMemory(rawInfo, &info, sizeof(info))) {
        append(timestamp() + "I2C_WRITE status=" + std::to_string(status) + " info=<unreadable>\r\n");
        return status;
    }
    char header[256]{};
    std::snprintf(header, sizeof(header),
                  "I2C_WRITE status=%d gpu=%p version=%08X port=%u portSet=%u device=%02X regSize=%u dataSize=%u reg=[",
                  status, gpu, info.version, info.portId, info.isPortIdSet,
                  info.deviceAddress, info.registerAddressSize, info.dataSize);
    append(timestamp() + header + hexMemory(info.registerAddress, info.registerAddressSize, 32) +
           "] data=[" + hexMemory(info.data, info.dataSize) + "]\r\n");
    return status;
}

NvStatus __cdecl traceI2cRead(NvPhysicalGpuHandle gpu, void* rawInfo, std::uint32_t* auxiliary) {
    I2cFunction function = realI2c(I2cReadExId);
    if (!function) {
        return -3;
    }
    const NvStatus status = function(gpu, rawInfo, auxiliary);
    if (!rawInfo) {
        append(timestamp() + "I2C_READ status=" + std::to_string(status) + " info=<null>\r\n");
        return status;
    }
    std::uint32_t version = 0;
    if (!readValue(static_cast<std::uint32_t*>(rawInfo), version)) {
        append(timestamp() + "I2C_READ status=" + std::to_string(status) + " info=<unreadable>\r\n");
        return status;
    }
    if ((version & 0xFFFF) < sizeof(I2cInfoV3)) {
        append(timestamp() + "I2C_READ status=" + std::to_string(status) +
               " unsupportedVersion=" + std::to_string(version) + "\r\n");
        return status;
    }
    I2cInfoV3 info{};
    if (!copyMemory(rawInfo, &info, sizeof(info))) {
        append(timestamp() + "I2C_READ status=" + std::to_string(status) + " info=<unreadable>\r\n");
        return status;
    }
    char header[256]{};
    std::snprintf(header, sizeof(header),
                  "I2C_READ status=%d gpu=%p version=%08X port=%u portSet=%u device=%02X regSize=%u dataSize=%u reg=[",
                  status, gpu, info.version, info.portId, info.isPortIdSet,
                  info.deviceAddress, info.registerAddressSize, info.dataSize);
    append(timestamp() + header + hexMemory(info.registerAddress, info.registerAddressSize, 32) +
           "] data=[" + hexMemory(info.data, info.dataSize) + "]\r\n");
    return status;
}

NvStatus traceIllum(std::uint32_t id, const char* operation,
                    NvPhysicalGpuHandle gpu, void* buffer) {
    if (!ensureInitialized()) {
        return -3;
    }
    IllumFunction function = reinterpret_cast<IllumFunction>(realQuery(id));
    if (!function) {
        return -3;
    }
    const bool isGet = id == IllumGetControlId;
    NvStatus status = 0;
    if (isGet) {
        status = function(gpu, buffer);
    }
    std::uint32_t version = 0;
    readValue(static_cast<std::uint32_t*>(buffer), version);
    const std::size_t size = version & 0xFFFF;
    const std::string snapshot = hexMemory(static_cast<std::uint8_t*>(buffer), size);
    if (!isGet) {
        status = function(gpu, buffer);
    }
    char header[160]{};
    std::snprintf(header, sizeof(header), "%s status=%d gpu=%p version=%08X size=%zu data=[",
                  operation, status, gpu, version, size);
    append(timestamp() + header + snapshot + "]\r\n");
    return status;
}

NvStatus __cdecl traceIllumGet(NvPhysicalGpuHandle gpu, void* buffer) {
    return traceIllum(IllumGetControlId, "ILLUM_GET", gpu, buffer);
}

NvStatus __cdecl traceIllumSet(NvPhysicalGpuHandle gpu, void* buffer) {
    return traceIllum(IllumSetControlId, "ILLUM_SET", gpu, buffer);
}

NvStatus __cdecl traceGetPci(NvPhysicalGpuHandle gpu, std::uint32_t* device,
                             std::uint32_t* subsystem, std::uint32_t* revision,
                             std::uint32_t* externalDevice) {
    if (!ensureInitialized()) {
        return -3;
    }
    auto function = reinterpret_cast<GetPciIdentifiers>(realQuery(GetPciIdentifiersId));
    if (!function) {
        return -3;
    }
    const NvStatus status = function(gpu, device, subsystem, revision, externalDevice);
    std::uint32_t deviceValue = 0;
    std::uint32_t subsystemValue = 0;
    std::uint32_t revisionValue = 0;
    std::uint32_t externalValue = 0;
    if (status == 0) {
        readValue(device, deviceValue);
        readValue(subsystem, subsystemValue);
        readValue(revision, revisionValue);
        readValue(externalDevice, externalValue);
    }
    char line[256]{};
    std::snprintf(line, sizeof(line),
                  "PCI status=%d gpu=%p device=%08X subsystem=%08X revision=%08X external=%08X\r\n",
                  status, gpu, deviceValue, subsystemValue, revisionValue, externalValue);
    append(timestamp() + line);
    return status;
}

extern "C" __declspec(dllexport) void* __cdecl nvapi_QueryInterface(std::uint32_t id) {
    if (!ensureInitialized()) {
        return nullptr;
    }
    void* result = realQuery(id);
    if (!result) {
        char line[128]{};
        std::snprintf(line, sizeof(line), "QUERY id=%08X result=%p\r\n", id, result);
        append(timestamp() + line);
        return nullptr;
    }
    switch (id) {
    case I2cWriteExId:
        result = reinterpret_cast<void*>(&traceI2cWrite);
        break;
    case I2cReadExId:
        result = reinterpret_cast<void*>(&traceI2cRead);
        break;
    case IllumGetControlId:
        result = reinterpret_cast<void*>(&traceIllumGet);
        break;
    case IllumSetControlId:
        result = reinterpret_cast<void*>(&traceIllumSet);
        break;
    case GetPciIdentifiersId:
        result = reinterpret_cast<void*>(&traceGetPci);
        break;
    default:
        break;
    }
    char line[128]{};
    std::snprintf(line, sizeof(line), "QUERY id=%08X result=%p\r\n", id, result);
    append(timestamp() + line);
    return result;
}

extern "C" __declspec(dllexport) void* __cdecl nvapi_Direct_GetMethod(std::uint32_t id) {
    return ensureInitialized() && realDirect ? realDirect(id) : nullptr;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
