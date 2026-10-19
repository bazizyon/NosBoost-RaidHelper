#pragma once
#include <cstdint>
#include <vector>

namespace MapImage {
    struct Grid {
        int Width = 0;
        int Height = 0;
        std::vector<uint8_t> Walkable;
    };

    struct Picture {
        int Width = 0;
        int Height = 0;
        std::vector<uint8_t> Rgba;
    };

    std::vector<uint8_t> EncodePng(const Picture& Image);

    std::vector<float> Field(const Grid& Map);

    Picture Overlay(const Grid& Map, const std::vector<float>& Values, float Scale);
    Picture Minimap(const Grid& Map, const std::vector<float>& Values, int Size);
    Picture MarkerSheet();
    constexpr int ArrowSize = 32;
    constexpr int ArrowFrames = 16;
    constexpr int DotSize = 16;
    constexpr int DotLeft = ArrowSize * ArrowFrames;
    constexpr int DiamondLeft = DotLeft + DotSize;

    void StartPlaceholderScan();
    void StopPlaceholderScan();
    bool PlaceholdersReady();
    bool HasPlaceholder(int MapId);
}
