#pragma once
#include "ModContract.h"
#include "TEWControlWidget.h"

#include <unordered_set>
#include <vector>

namespace MapOverlay {
    struct Options {
        bool Center = false;
        int Opacity = 60;
        bool Minimap = true;
    };

    struct Mark {
        long Id;
        int X;
        int Y;
        int32_t Image;
        AtlasFrame Frame;
        int16_t Width;
        int16_t Height;
        bool Boss;
    };

    void OnMapChange();
    void OnMonsterDied(long Id);
    void OnMonsterIn(long Id);

    void Tick(const ModHost* Host, TLBSWidget* Root, const TickContext& Context, bool Visible, const Options& Settings,
              const std::vector<Mark>& Marks);
    void Forget();
    void Remove(const ModHost* Host);
}
