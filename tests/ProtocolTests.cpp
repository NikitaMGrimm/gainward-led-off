#define GPU_NVAPI_OFF_NO_MAIN
#include "../GpuNvapiOff.cpp"

#include <cstdlib>
#include <vector>

enum class Scenario {
    Raw,
    Illumination,
    IlluminationUnknown,
    Legacy,
    LegacyUninitialized,
    LegacyUnknownVersion,
    Unknown,
};

struct WriteCall {
    std::uint8_t device;
    std::uint8_t reg;
    std::vector<std::uint8_t> data;
};

Scenario scenario = Scenario::Unknown;
std::vector<WriteCall> writes;
std::vector<std::uint32_t> illuminationFlags;
IlluminationControl submittedControl{};
int setCalls = 0;
LegacyRegisters legacyRegisters{};

void requireAt(bool condition, int line) {
    if (!condition) {
        std::cerr << "Requirement failed at line " << line << ".\n";
        std::abort();
    }
}

#define require(condition) requireAt((condition), __LINE__)

NvStatus __cdecl mockRead(NvPhysicalGpuHandle, void* rawInfo, std::uint32_t*) {
    auto* info = static_cast<NvI2cInfoV3*>(rawInfo);
    const std::uint8_t reg = *info->registerAddress;
    if (scenario == Scenario::Raw && info->deviceAddress == 0x92 && reg == 0xF0 &&
        info->dataSize == 16) {
        const std::uint8_t response[]{
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x06,
            'P', 'A', 'L', 'I', 'T', ' ', '2', '5', '0', 0x00,
        };
        std::memcpy(info->data, response, sizeof(response));
        return 0;
    }
    if ((scenario == Scenario::Legacy || scenario == Scenario::LegacyUninitialized ||
         scenario == Scenario::LegacyUnknownVersion) && info->deviceAddress == 0x10 &&
        info->dataSize == 1 && reg < 16) {
        *info->data = legacyRegisters[reg];
        return 0;
    }
    return -1;
}

NvStatus __cdecl mockWrite(NvPhysicalGpuHandle, void* rawInfo, std::uint32_t*) {
    auto* info = static_cast<NvI2cInfoV3*>(rawInfo);
    writes.push_back({info->deviceAddress, *info->registerAddress,
                      std::vector<std::uint8_t>(info->data, info->data + info->dataSize)});
    if (scenario == Scenario::LegacyUninitialized && info->deviceAddress == 0x10 &&
        static_cast<std::size_t>(*info->registerAddress) + info->dataSize <=
            legacyRegisters.size()) {
        std::memcpy(legacyRegisters.data() + *info->registerAddress,
                    info->data, info->dataSize);
    }
    return 0;
}

NvStatus __cdecl mockIlluminationGet(NvPhysicalGpuHandle, void* rawControl) {
    auto* control = static_cast<std::uint8_t*>(rawControl);
    illuminationFlags.push_back(readDword(control + 4));
    if (scenario == Scenario::Legacy || scenario == Scenario::LegacyUninitialized ||
        scenario == Scenario::LegacyUnknownVersion) {
        return -1;
    }
    if (scenario != Scenario::Illumination && scenario != Scenario::IlluminationUnknown) {
        return 0;
    }
    writeDword(control + 8, 2);
    std::uint8_t* first = control + IlluminationZoneOffset;
    std::uint8_t* second = first + IlluminationZoneStride;
    writeDword(first, 3);
    writeDword(first + 4, 0);
    writeDword(second, 4);
    writeDword(second + 4, 0);
    writeDword(first + 0x6C, scenario == Scenario::Illumination ? 3u : 99u);
    writeDword(second + 0x6C, 4);
    writeDword(first + 0x70, 0xAAAAAAAA);
    writeDword(second + 0x70, 0xBBBBBBBB);
    control[IlluminationControlSize - 1] = 0x5A;
    return 0;
}

NvStatus __cdecl mockIlluminationSet(NvPhysicalGpuHandle, void* rawControl) {
    std::memcpy(submittedControl.data(), rawControl, submittedControl.size());
    ++setCalls;
    return 0;
}

void reset(Scenario next) {
    scenario = next;
    writes.clear();
    illuminationFlags.clear();
    submittedControl.fill(0);
    setCalls = 0;
    legacyRegisters.fill(0);
    if (next == Scenario::Legacy || next == Scenario::LegacyUnknownVersion) {
        const std::uint8_t signature[]{'P', 'A', 'L', 'I', 'T'};
        std::memcpy(legacyRegisters.data() + 7, signature, sizeof(signature));
    }
    if (next == Scenario::Legacy || next == Scenario::LegacyUninitialized) {
        const std::uint8_t version[]{'2', '.', '1'};
        std::memcpy(legacyRegisters.data() + 12, version, sizeof(version));
    } else if (next == Scenario::LegacyUnknownVersion) {
        const std::uint8_t version[]{'9', '.', '9'};
        std::memcpy(legacyRegisters.data() + 12, version, sizeof(version));
    }
}

void testRaw() {
    reset(Scenario::Raw);
    require(applyOff(nullptr, mockRead, mockWrite, mockIlluminationGet,
                     mockIlluminationSet) == ApplyResult::Success);
    require(illuminationFlags == std::vector<std::uint32_t>{1});
    require(writes.size() == 3);
    require(writes[0].device == 0x92 && writes[0].reg == 0xE0 &&
            writes[0].data == std::vector<std::uint8_t>{0});
    require(writes[1].reg == 0x60 && writes[1].data == std::vector<std::uint8_t>{1});
    require(writes[2].reg == 0x6C &&
            writes[2].data == std::vector<std::uint8_t>({0, 0, 0, 100}));
}

void testIllumination() {
    reset(Scenario::Illumination);
    require(applyOff(nullptr, mockRead, mockWrite, mockIlluminationGet,
                     mockIlluminationSet) == ApplyResult::Success);
    require(illuminationFlags == std::vector<std::uint32_t>({1, 0}));
    require(writes.empty() && setCalls == 1);
    const std::uint8_t* first = submittedControl.data() + IlluminationZoneOffset;
    const std::uint8_t* second = first + IlluminationZoneStride;
    require(readDword(first + 0x70) == 0 && first[0x74] == 0 &&
            first[0x77] == 0 && first[0x78] == 100);
    require(readDword(second + 0x70) == 0 && second[0x74] == 0);
    require(submittedControl.back() == 0x5A);
}

void testIlluminationUnknown() {
    reset(Scenario::IlluminationUnknown);
    require(applyOff(nullptr, mockRead, mockWrite, mockIlluminationGet,
                     mockIlluminationSet) == ApplyResult::Success);
    require(setCalls == 1);
    const std::uint8_t* first = submittedControl.data() + IlluminationZoneOffset;
    const std::uint8_t* second = first + IlluminationZoneStride;
    require(readDword(first + 0x70) == 0);
    require(readDword(second + 0x70) == 0);
}

void testLegacy() {
    reset(Scenario::Legacy);
    require(applyOff(nullptr, mockRead, mockWrite, mockIlluminationGet,
                     mockIlluminationSet) == ApplyResult::Success);
    require(illuminationFlags == std::vector<std::uint32_t>{1});
    require(writes.size() == 1 && writes[0].device == 0x10 && writes[0].reg == 0x03);
    require(writes[0].data == std::vector<std::uint8_t>({0, 0, 0, 0xFF}));
}

void testLegacyUninitialized() {
    reset(Scenario::LegacyUninitialized);
    require(applyOff(nullptr, mockRead, mockWrite, mockIlluminationGet,
                     mockIlluminationSet) == ApplyResult::Success);
    require(writes.size() == 4);
    require(writes[0].device == 0x10 && writes[0].reg == 0x00 &&
            writes[0].data == std::vector<std::uint8_t>({0x7F, 0x7F, 0x7F, 0x00}));
    require(writes[1].reg == 0x04 &&
            writes[1].data == std::vector<std::uint8_t>({0xFF, 0x00, 0xFF, 'P'}));
    require(writes[2].reg == 0x08 &&
            writes[2].data == std::vector<std::uint8_t>({'A', 'L', 'I', 'T'}));
    require(writes[3].reg == 0x03 &&
            writes[3].data == std::vector<std::uint8_t>({0, 0, 0, 0xFF}));
}

void testLegacyUnknownVersion() {
    reset(Scenario::LegacyUnknownVersion);
    require(applyOff(nullptr, mockRead, mockWrite, mockIlluminationGet,
                     mockIlluminationSet) == ApplyResult::Unsupported);
    require(writes.empty());
}

void testUnknown() {
    reset(Scenario::Unknown);
    require(applyOff(nullptr, mockRead, mockWrite, mockIlluminationGet,
                     mockIlluminationSet) == ApplyResult::Unsupported);
    require(writes.empty() && setCalls == 0);
}

int main() {
    require(rawProfileWithoutPreamble(1000));
    require(rawProfileWithoutPreamble(2991));
    require(rawProfileWithPreamble(1202));
    require(rawProfileWithPreamble(2003));
    require(rawProfileWithPreamble(2007));
    require(rawProfileWithPreamble(2506));
    require(!rawProfileWithPreamble(2004));
    require(!rawProfileWithPreamble(3002));
    testRaw();
    testIllumination();
    testIlluminationUnknown();
    testLegacy();
    testLegacyUninitialized();
    testLegacyUnknownVersion();
    testUnknown();
    std::cout << "Protocol tests passed.\n";
    return 0;
}
