#define NOMINMAX
#include <windows.h>

#include "EffectCatalog.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace EffectCatalog {
    namespace {
        constexpr uint32_t FallbackMs = 3000;
        constexpr int MaxListedId = 100000;

        std::filesystem::path FilePath;
        std::unordered_map<int, uint32_t> Offsets;
        std::vector<int> SortedIds;
        std::unordered_map<int, uint32_t> Durations;
        bool Loaded = false;

        template <typename T>
        T Get(const std::vector<uint8_t>& Bytes, const size_t At) {
            T Value{};
            std::memcpy(&Value, Bytes.data() + At, sizeof(T));
            return Value;
        }

        bool ReadEntry(const int Id, std::vector<uint8_t>& Out) {
            const auto Found = Offsets.find(Id);
            if (Found == Offsets.end()) return false;
            std::ifstream File(FilePath, std::ios::binary);
            if (!File) return false;
            File.seekg(Found->second);
            uint8_t Head[13];
            if (!File.read(reinterpret_cast<char*>(Head), sizeof(Head))) return false;
            uint32_t Size = 0;
            std::memcpy(&Size, Head + 4, 4);
            if (Head[12] != 0 || Size > (16u << 20)) return false;
            Out.resize(Size);
            return static_cast<bool>(File.read(reinterpret_cast<char*>(Out.data()), Size));
        }

        uint32_t Measure(const std::vector<uint8_t>& Definition) {
            constexpr size_t PartSize = 0xC0;
            constexpr struct { size_t Field; size_t ValueSize; } Tracks[] = {
                {0x60, 16}, {0x6C, 12}, {0x78, 12}, {0x84, 4}, {0x90, 4}, {0x9C, 16}, {0xA8, 12}, {0xB4, 12},
            };
            if (Definition.size() < 0x18) return FallbackMs;
            const uint16_t Parts = Get<uint16_t>(Definition, 0x0C);
            size_t At = 0x18 + Parts * PartSize;
            if (At > Definition.size()) return FallbackMs;
            uint32_t Longest = 0;
            for (uint16_t i = 0; i < Parts; i++) {
                const size_t Part = 0x18 + i * PartSize;
                const int32_t Lifetime = Get<int32_t>(Definition, Part + 0x54);
                if (Lifetime < 0) return 0;
                Longest = std::max<uint32_t>(Longest, Get<uint16_t>(Definition, Part + 0x50) + static_cast<uint32_t>(Lifetime));
                for (const auto& Track : Tracks) {
                    const uint8_t Keys = Definition[Part + Track.Field];
                    if (Keys && At + Keys * 2 <= Definition.size()) {
                        Longest = std::max<uint32_t>(Longest, Get<uint16_t>(Definition, At + (Keys - 1) * 2));
                    }
                    At += Keys * (2 + Track.ValueSize);
                }
            }
            if (At != Definition.size()) return FallbackMs;
            return Longest ? Longest : FallbackMs;
        }
    }

    bool Load() {
        if (Loaded) return !Offsets.empty();
        Loaded = true;
        wchar_t Exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, Exe, MAX_PATH);
        FilePath = std::filesystem::path(Exe).parent_path() / L"NostaleData" / L"NSeffData.NOS";
        std::ifstream File(FilePath, std::ios::binary);
        if (!File) return false;
        uint8_t Header[0x15];
        if (!File.read(reinterpret_cast<char*>(Header), sizeof(Header))) return false;
        if (std::memcmp(Header, "NT Data", 7) != 0) return false;
        uint32_t Count = 0;
        std::memcpy(&Count, Header + 0x10, 4);
        if (Count > 1000000) return false;
        std::vector<uint32_t> Pairs(static_cast<size_t>(Count) * 2);
        if (!File.read(reinterpret_cast<char*>(Pairs.data()), static_cast<std::streamsize>(Pairs.size() * 4))) return false;
        for (uint32_t i = 0; i < Count; i++) {
            const int Id = static_cast<int>(Pairs[i * 2]);
            Offsets[Id] = Pairs[i * 2 + 1];
            if (Id > 0 && Id < MaxListedId) SortedIds.push_back(Id);
        }
        std::sort(SortedIds.begin(), SortedIds.end());
        return true;
    }

    const std::vector<int>& Ids() {
        return SortedIds;
    }

    uint32_t DurationMs(const int EffectId) {
        const auto Cached = Durations.find(EffectId);
        if (Cached != Durations.end()) return Cached->second;
        std::vector<uint8_t> Definition;
        const uint32_t Result = ReadEntry(EffectId, Definition) ? Measure(Definition) : FallbackMs;
        Durations[EffectId] = Result;
        return Result;
    }
}
