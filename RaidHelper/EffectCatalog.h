#pragma once
#include <cstdint>
#include <vector>

namespace EffectCatalog {
    bool Load();
    const std::vector<int>& Ids();
    uint32_t DurationMs(int EffectId);
}
