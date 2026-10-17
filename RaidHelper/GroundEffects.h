#pragma once
#include <cstdint>

namespace GroundEffects {
    bool Init();
    bool Ready();
    bool Spawn(uint32_t Handle, int EffectId, int CellX, int CellY);
    void Remove(uint32_t Handle);
}
