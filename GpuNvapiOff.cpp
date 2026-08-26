#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

using NvStatus = std::int32_t;
using NvPhysicalGpuHandle = void*;
using NvQueryInterface = void* (__cdecl*)(std::uint32_t);
using NvInitialize = NvStatus (__cdecl*)();
using NvEnumPhysicalGpus = NvStatus (__cdecl*)(NvPhysicalGpuHandle*, std::int32_t*);
using NvI2cFunction = NvStatus (__cdecl*)(NvPhysicalGpuHandle, void*, std::uint32_t*);
using NvIlluminationFunction = NvStatus (__cdecl*)(NvPhysicalGpuHandle, void*);

struct NvI2cInfoV3 {
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

static_assert(sizeof(NvI2cInfoV3) == 64);
static_assert(offsetof(NvI2cInfoV3, registerAddress) == 16);
static_assert(offsetof(NvI2cInfoV3, data) == 32);

constexpr std::uint32_t NvApiInitializeId = 0x0150E828;
constexpr std::uint32_t NvApiEnumPhysicalGpusId = 0xE5AC921F;
constexpr std::uint32_t NvApiI2cWriteExId = 0x283AC65A;
constexpr std::uint32_t NvApiI2cReadExId = 0x4D7B0709;
constexpr std::uint32_t NvApiIlluminationGetId = 0x3DBF5764;
constexpr std::uint32_t NvApiIlluminationSetId = 0x197D065E;
constexpr std::size_t IlluminationControlSize = 0x194C;
constexpr std::size_t IlluminationZoneOffset = 0x4C;
constexpr std::size_t IlluminationZoneStride = 0xC8;
constexpr std::uint32_t IlluminationControlVersion = 0x0001194C;
constexpr std::uint32_t MaximumIlluminationZones = 32;

template <typename Function>
Function query(NvQueryInterface queryInterface, std::uint32_t id) {
    return reinterpret_cast<Function>(queryInterface(id));
}

std::uint32_t readDword(const std::uint8_t* address) {
    std::uint32_t value = 0;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

void writeDword(std::uint8_t* address, std::uint32_t value) {
    std::memcpy(address, &value, sizeof(value));
}

NvStatus i2cTransfer(NvI2cFunction function, NvPhysicalGpuHandle gpu,
                     std::uint8_t device, std::uint8_t registerByte,
                     std::uint8_t* data, std::uint32_t dataSize) {
    NvI2cInfoV3 info{};
    info.version = static_cast<std::uint32_t>(sizeof(info)) | (3u << 16);
    info.deviceAddress = device;
    info.registerAddress = &registerByte;
    info.registerAddressSize = 1;
    info.data = data;
    info.dataSize = dataSize;
    info.legacySpeed = 0xFFFF;
    info.portId = 1;
    info.isPortIdSet = 1;
    std::uint32_t auxiliary[2]{1, 0};
    return function(gpu, &info, auxiliary);
}

bool readRegister(NvI2cFunction readEx, NvPhysicalGpuHandle gpu,
                  std::uint8_t device, std::uint8_t reg,
                  std::uint8_t* data, std::uint32_t size) {
    return readEx && i2cTransfer(readEx, gpu, device, reg, data, size) == 0;
}

bool writeRegister(NvI2cFunction writeEx, NvPhysicalGpuHandle gpu,
                   std::uint8_t device, std::uint8_t reg,
                   std::uint8_t* data, std::uint32_t size) {
    return writeEx && i2cTransfer(writeEx, gpu, device, reg, data, size) == 0;
}

int detectPalitProfile(NvI2cFunction readEx, NvPhysicalGpuHandle gpu) {
    std::array<std::uint8_t, 16> response{};
    if (!readRegister(readEx, gpu, 0x92, 0xF0, response.data(),
                      static_cast<std::uint32_t>(response.size()))) {
        return -1;
    }
    constexpr std::array<std::uint8_t, 5> signature{'P', 'A', 'L', 'I', 'T'};
    for (std::size_t index = 0; index < signature.size(); ++index) {
        if (response[index + 6] != signature[index]) {
            return -1;
        }
    }
    return 1000 * (response[12] & 0x0F) +
            100 * (response[13] & 0x0F) +
             10 * (response[14] & 0x0F) +
                  (response[5] & 0x1F);
}

bool rawProfileWithoutPreamble(int profile) {
    return profile >= 1000 && profile <= 2999 &&
           (profile % 10 == 0 || profile % 10 == 1);
}

bool rawProfileWithPreamble(int profile) {
    const int suffix = profile % 10;
    return (profile >= 1200 && profile <= 2999 && suffix == 2) ||
           (profile >= 2000 && profile <= 2999 && (suffix == 3 || suffix == 7)) ||
           (profile >= 2400 && profile <= 2999 && suffix == 6);
}

bool applyRawOff(NvI2cFunction writeEx, NvPhysicalGpuHandle gpu, int profile) {
    if (!rawProfileWithoutPreamble(profile) && !rawProfileWithPreamble(profile)) {
        return false;
    }
    std::uint8_t zero[]{0x00};
    if (rawProfileWithPreamble(profile)) {
        writeRegister(writeEx, gpu, 0x92, 0xE0, zero, 1);
    }
    std::uint8_t mode[]{0x01};
    if (!writeRegister(writeEx, gpu, 0x92, 0x60, mode, 1)) {
        return false;
    }
    std::uint8_t off[]{0x00, 0x00, 0x00, 0x64};
    return writeRegister(writeEx, gpu, 0x92, 0x6C, off, 4);
}

using IlluminationControl = std::array<std::uint8_t, IlluminationControlSize>;

bool getIlluminationControl(NvIlluminationFunction getControl, NvPhysicalGpuHandle gpu,
                            IlluminationControl& control, bool discovery = false) {
    if (!getControl) {
        return false;
    }
    control.fill(0);
    writeDword(control.data(), IlluminationControlVersion);
    writeDword(control.data() + 4, discovery ? 1u : 0u);
    return getControl(gpu, control.data()) == 0;
}

int detectIlluminationProfile(const IlluminationControl& control) {
    const std::uint32_t count = readDword(control.data() + 8);
    if (count < 2 || count > MaximumIlluminationZones) {
        return -1;
    }
    const std::uint8_t* first = control.data() + IlluminationZoneOffset;
    if (readDword(first + 4) != 0) {
        return -1;
    }
    const std::uint32_t firstKind = readDword(first);
    if (count == 2 && firstKind == 3) {
        const std::uint8_t* second = first + IlluminationZoneStride;
        if (readDword(second + 4) == 0 && readDword(second) == 4) {
            return 10;
        }
        return 11;
    }
    return firstKind == 1 || firstKind == 3 ? 11 : -1;
}

bool applyIlluminationOff(NvIlluminationFunction getControl,
                          NvIlluminationFunction setControl,
                          NvPhysicalGpuHandle gpu) {
    IlluminationControl control{};
    if (!setControl || !getIlluminationControl(getControl, gpu, control)) {
        return false;
    }
    const std::uint32_t count = readDword(control.data() + 8);
    if (count == 0 || count > MaximumIlluminationZones) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint8_t* zone = control.data() + IlluminationZoneOffset +
                             static_cast<std::size_t>(index) * IlluminationZoneStride;
        const std::uint32_t type = readDword(zone + 0x6C);
        writeDword(zone + 0x70, 0);
        if (type == 1) {
            zone[0x74] = 0;
            zone[0x75] = 0;
            zone[0x76] = 0;
            zone[0x77] = 100;
        } else if (type == 3) {
            zone[0x74] = 0;
            zone[0x75] = 0;
            zone[0x76] = 0;
            zone[0x77] = 0;
            zone[0x78] = 100;
        } else if (type == 4) {
            zone[0x74] = 0;
        }
    }
    return setControl(gpu, control.data()) == 0;
}

using LegacyRegisters = std::array<std::uint8_t, 16>;

bool readLegacyRegisters(NvI2cFunction readEx, NvPhysicalGpuHandle gpu,
                         LegacyRegisters& registers) {
    for (std::uint8_t reg = 0; reg < registers.size(); ++reg) {
        if (!readRegister(readEx, gpu, 0x10, reg, &registers[reg], 1)) {
            return false;
        }
    }
    return true;
}

bool hasLegacySignature(const LegacyRegisters& registers) {
    constexpr std::array<std::uint8_t, 5> signature{'P', 'A', 'L', 'I', 'T'};
    for (std::size_t index = 0; index < signature.size(); ++index) {
        if (registers[index + 7] != signature[index]) {
            return false;
        }
    }
    return true;
}

bool hasLegacyVersion(const LegacyRegisters& registers) {
    const std::array<std::array<std::uint8_t, 3>, 3> versions{{
        {' ', '1', '.'},
        {'2', '.', '0'},
        {'2', '.', '1'},
    }};
    for (const auto& version : versions) {
        if (std::equal(version.begin(), version.end(), registers.begin() + 12)) {
            return true;
        }
    }
    return false;
}

bool detectLegacyController(NvI2cFunction readEx, NvI2cFunction writeEx,
                            NvPhysicalGpuHandle gpu) {
    LegacyRegisters registers{};
    if (!readLegacyRegisters(readEx, gpu, registers)) {
        return false;
    }
    if (!hasLegacySignature(registers)) {
        constexpr std::array<std::array<std::uint8_t, 4>, 3> initialization{{
            {0x7F, 0x7F, 0x7F, 0x00},
            {0xFF, 0x00, 0xFF, 'P'},
            {'A', 'L', 'I', 'T'},
        }};
        for (std::size_t index = 0; index < initialization.size(); ++index) {
            auto block = initialization[index];
            if (!writeRegister(writeEx, gpu, 0x10,
                               static_cast<std::uint8_t>(index * block.size()),
                               block.data(), static_cast<std::uint32_t>(block.size()))) {
                break;
            }
        }
        if (!readLegacyRegisters(readEx, gpu, registers) ||
            !hasLegacySignature(registers)) {
            return false;
        }
    }
    return hasLegacyVersion(registers);
}

bool applyLegacyOff(NvI2cFunction writeEx, NvPhysicalGpuHandle gpu) {
    std::uint8_t off[]{0x00, 0x00, 0x00, 0xFF};
    return writeRegister(writeEx, gpu, 0x10, 0x03, off, 4);
}

enum class ApplyResult {
    Unsupported,
    Success,
    Failure,
};

ApplyResult applyOff(NvPhysicalGpuHandle gpu, NvI2cFunction readEx,
                     NvI2cFunction writeEx, NvIlluminationFunction getControl,
                     NvIlluminationFunction setControl) {
    IlluminationControl discovery{};
    if (getIlluminationControl(getControl, gpu, discovery, true)) {
        const int palitProfile = detectPalitProfile(readEx, gpu);
        if (palitProfile >= 0) {
            if (!rawProfileWithoutPreamble(palitProfile) &&
                !rawProfileWithPreamble(palitProfile)) {
                return ApplyResult::Unsupported;
            }
            return applyRawOff(writeEx, gpu, palitProfile) ?
                ApplyResult::Success : ApplyResult::Failure;
        }
        if (detectIlluminationProfile(discovery) < 0) {
            return ApplyResult::Unsupported;
        }
        return applyIlluminationOff(getControl, setControl, gpu) ?
            ApplyResult::Success : ApplyResult::Failure;
    }

    if (!detectLegacyController(readEx, writeEx, gpu)) {
        return ApplyResult::Unsupported;
    }
    return applyLegacyOff(writeEx, gpu) ? ApplyResult::Success : ApplyResult::Failure;
}

#ifndef GPU_NVAPI_OFF_NO_MAIN
int wmain() {
    const HMODULE nvapi = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!nvapi) {
        std::wcerr << L"Could not load nvapi64.dll.\n";
        return 1;
    }
    const auto queryInterface = reinterpret_cast<NvQueryInterface>(
        GetProcAddress(nvapi, "nvapi_QueryInterface"));
    if (!queryInterface) {
        std::wcerr << L"nvapi_QueryInterface was not found.\n";
        return 2;
    }

    const auto initialize = query<NvInitialize>(queryInterface, NvApiInitializeId);
    const auto enumerate = query<NvEnumPhysicalGpus>(queryInterface, NvApiEnumPhysicalGpusId);
    const auto readEx = query<NvI2cFunction>(queryInterface, NvApiI2cReadExId);
    const auto writeEx = query<NvI2cFunction>(queryInterface, NvApiI2cWriteExId);
    const auto getControl = query<NvIlluminationFunction>(queryInterface, NvApiIlluminationGetId);
    const auto setControl = query<NvIlluminationFunction>(queryInterface, NvApiIlluminationSetId);
    if (!initialize || !enumerate || !readEx || !writeEx || initialize() != 0) {
        std::wcerr << L"Required NVAPI functions are unavailable.\n";
        return 3;
    }

    std::array<NvPhysicalGpuHandle, 64> handles{};
    std::int32_t count = 0;
    if (enumerate(handles.data(), &count) != 0 || count <= 0) {
        std::wcerr << L"No NVIDIA GPU was found.\n";
        return 4;
    }

    int applied = 0;
    bool failed = false;
    for (std::int32_t index = 0; index < count; ++index) {
        const ApplyResult result = applyOff(handles[index], readEx, writeEx, getControl, setControl);
        applied += result == ApplyResult::Success ? 1 : 0;
        failed = failed || result == ApplyResult::Failure;
    }
    if (failed) {
        std::wcerr << L"A recognized LED controller rejected an Off command.\n";
        return 6;
    }
    if (applied == 0) {
        std::wcerr << L"No supported EXPERTool LED controller was found; nothing was written.\n";
        return 5;
    }

    std::wcout << L"LEDs off on " << applied << (applied == 1 ? L" GPU.\n" : L" GPUs.\n");
    return 0;
}
#endif
