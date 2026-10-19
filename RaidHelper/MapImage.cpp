#define NOMINMAX
#include <windows.h>

#include "MapImage.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace MapImage {
    namespace {
        constexpr uint32_t MinimapImageBase = 0x5551D000;
        constexpr uint32_t MinimapIdRange = 0x10000;
        constexpr float EdgeSlope = 0.5f;
        constexpr float EdgeHalfWidth = 1.1f;

        uint32_t Crc32(const uint8_t* Data, const size_t Size) {
            uint32_t Crc = 0xFFFFFFFFu;
            for (size_t i = 0; i < Size; i++) {
                Crc ^= Data[i];
                for (int Bit = 0; Bit < 8; Bit++) Crc = (Crc >> 1) ^ (0xEDB88320u & (0u - (Crc & 1u)));
            }
            return ~Crc;
        }

        void PushBig(std::vector<uint8_t>& Out, const uint32_t Value) {
            for (int Shift = 24; Shift >= 0; Shift -= 8) Out.push_back(static_cast<uint8_t>(Value >> Shift));
        }

        struct Bits {
            const uint8_t* Data;
            size_t Size;
            size_t Pos = 0;
            uint32_t Held = 0;
            int Count = 0;

            int Bit() {
                if (!Count) {
                    if (Pos >= Size) throw 0;
                    Held = Data[Pos++];
                    Count = 8;
                }
                const int Value = static_cast<int>(Held & 1);
                Held >>= 1;
                Count--;
                return Value;
            }

            int Read(const int Number) {
                int Value = 0;
                for (int i = 0; i < Number; i++) Value |= Bit() << i;
                return Value;
            }
        };

        struct Huffman {
            uint16_t Counts[16]{};
            uint16_t Symbols[320]{};
        };

        void Build(Huffman& Table, const uint8_t* Lengths, const int Number) {
            std::fill(std::begin(Table.Counts), std::end(Table.Counts), uint16_t{0});
            for (int i = 0; i < Number; i++) Table.Counts[Lengths[i]]++;
            Table.Counts[0] = 0;
            uint16_t Offsets[16]{};
            for (int i = 1; i < 15; i++) Offsets[i + 1] = static_cast<uint16_t>(Offsets[i] + Table.Counts[i]);
            for (int i = 0; i < Number; i++) {
                if (Lengths[i]) Table.Symbols[Offsets[Lengths[i]]++] = static_cast<uint16_t>(i);
            }
        }

        int Decode(Bits& Input, const Huffman& Table) {
            int Code = 0, First = 0, Index = 0;
            for (int Length = 1; Length < 16; Length++) {
                Code |= Input.Bit();
                const int Count = Table.Counts[Length];
                if (Code - Count < First) return Table.Symbols[Index + (Code - First)];
                Index += Count;
                First = (First + Count) << 1;
                Code <<= 1;
            }
            throw 0;
        }

        constexpr uint16_t LengthBase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                           35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        constexpr uint8_t LengthExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                           3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        constexpr uint16_t DistanceBase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                             257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                             8193, 12289, 16385, 24577};
        constexpr uint8_t DistanceExtra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                             7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

        void Inflate(Bits& Input, const Huffman& Literals, const Huffman& Distances, std::vector<uint8_t>& Out) {
            while (true) {
                const int Symbol = Decode(Input, Literals);
                if (Symbol < 256) {
                    Out.push_back(static_cast<uint8_t>(Symbol));
                    continue;
                }
                if (Symbol == 256) return;
                const int Code = Symbol - 257;
                if (Code >= 29) throw 0;
                const int Length = LengthBase[Code] + Input.Read(LengthExtra[Code]);
                const int DistanceCode = Decode(Input, Distances);
                if (DistanceCode >= 30) throw 0;
                const size_t Distance = DistanceBase[DistanceCode] + Input.Read(DistanceExtra[DistanceCode]);
                if (Distance > Out.size()) throw 0;
                for (int i = 0; i < Length; i++) Out.push_back(Out[Out.size() - Distance]);
            }
        }

        bool Unzip(const uint8_t* Data, const size_t Size, std::vector<uint8_t>& Out) {
            if (Size < 2) return false;
            Bits Input{Data + 2, Size - 2};
            try {
                int Final = 0;
                while (!Final) {
                    Final = Input.Bit();
                    const int Type = Input.Read(2);
                    if (Type == 0) {
                        Input.Count = 0;
                        if (Input.Pos + 4 > Input.Size) return false;
                        const size_t Length = Data[2 + Input.Pos] | Data[2 + Input.Pos + 1] << 8;
                        Input.Pos += 4;
                        if (Input.Pos + Length > Input.Size) return false;
                        Out.insert(Out.end(), Data + 2 + Input.Pos, Data + 2 + Input.Pos + Length);
                        Input.Pos += Length;
                    } else if (Type == 1) {
                        uint8_t Lengths[288 + 30];
                        std::fill(Lengths, Lengths + 144, uint8_t{8});
                        std::fill(Lengths + 144, Lengths + 256, uint8_t{9});
                        std::fill(Lengths + 256, Lengths + 280, uint8_t{7});
                        std::fill(Lengths + 280, Lengths + 288, uint8_t{8});
                        std::fill(Lengths + 288, Lengths + 318, uint8_t{5});
                        Huffman Literals, Distances;
                        Build(Literals, Lengths, 288);
                        Build(Distances, Lengths + 288, 30);
                        Inflate(Input, Literals, Distances, Out);
                    } else if (Type == 2) {
                        const int LiteralCount = Input.Read(5) + 257;
                        const int DistanceCount = Input.Read(5) + 1;
                        const int CodeCount = Input.Read(4) + 4;
                        constexpr uint8_t Order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                        uint8_t CodeLengths[19]{};
                        for (int i = 0; i < CodeCount; i++) CodeLengths[Order[i]] = static_cast<uint8_t>(Input.Read(3));
                        Huffman Codes;
                        Build(Codes, CodeLengths, 19);
                        uint8_t Lengths[288 + 32]{};
                        int At = 0;
                        while (At < LiteralCount + DistanceCount) {
                            const int Symbol = Decode(Input, Codes);
                            if (Symbol < 16) {
                                Lengths[At++] = static_cast<uint8_t>(Symbol);
                                continue;
                            }
                            uint8_t Value = 0;
                            int Repeat;
                            if (Symbol == 16) {
                                if (At == 0) return false;
                                Value = Lengths[At - 1];
                                Repeat = 3 + Input.Read(2);
                            } else if (Symbol == 17) {
                                Repeat = 3 + Input.Read(3);
                            } else {
                                Repeat = 11 + Input.Read(7);
                            }
                            if (At + Repeat > LiteralCount + DistanceCount) return false;
                            while (Repeat--) Lengths[At++] = Value;
                        }
                        Huffman Literals, Distances;
                        Build(Literals, Lengths, LiteralCount);
                        Build(Distances, Lengths + LiteralCount, DistanceCount);
                        Inflate(Input, Literals, Distances, Out);
                    } else {
                        return false;
                    }
                }
            } catch (int) {
                return false;
            }
            return true;
        }

        uint64_t Hash(const uint8_t* Data, const size_t Size) {
            uint64_t Value = 1469598103934665603ull;
            for (size_t i = 0; i < Size; i++) Value = (Value ^ Data[i]) * 1099511628211ull;
            return Value;
        }

        std::string DataFolder() {
            char Path[MAX_PATH]{};
            GetModuleFileNameA(nullptr, Path, MAX_PATH);
            const std::string Exe(Path);
            return Exe.substr(0, Exe.find_last_of("\\/") + 1) + "NostaleData\\";
        }

        std::vector<uint8_t> ReadFile(const std::string& Path) {
            std::ifstream File(Path, std::ios::binary);
            return {std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
        }

        template <typename Visit>
        void ForEachEntry(const std::vector<uint8_t>& File, const std::atomic<bool>& Stop, Visit&& Callback) {
            if (File.size() < 0x15) return;
            uint32_t Count;
            std::memcpy(&Count, File.data() + 0x10, 4);
            if (Count > 100000 || 0x15 + static_cast<size_t>(Count) * 8 > File.size()) return;
            for (uint32_t i = 0; i < Count && !Stop; i++) {
                uint32_t Id, Offset, Size;
                std::memcpy(&Id, File.data() + 0x15 + i * 8, 4);
                std::memcpy(&Offset, File.data() + 0x19 + i * 8, 4);
                if (static_cast<size_t>(Offset) + 13 > File.size()) continue;
                std::memcpy(&Size, File.data() + Offset + 8, 4);
                if (static_cast<size_t>(Offset) + 13 + Size > File.size()) continue;
                Callback(Id, File[Offset + 12] != 0, File.data() + Offset + 13, static_cast<size_t>(Size));
            }
        }

        std::thread Scanner;
        std::atomic<bool> StopScan{false};
        std::atomic<bool> ScanDone{false};
        std::mutex ResultLock;
        std::unordered_set<int> Placeholders;

        void Scan() {
            std::unordered_map<int, uint64_t> Layouts;
            const std::string Folder = DataFolder();
            ForEachEntry(ReadFile(Folder + "NStcData.NOS"), StopScan,
                         [&](const uint32_t Id, const bool Packed, const uint8_t* Data, const size_t Size) {
                std::vector<uint8_t> Plain;
                if (Packed ? !Unzip(Data, Size, Plain) : (Plain.assign(Data, Data + Size), false)) return;
                Layouts[static_cast<int>(Id)] = Hash(Plain.data(), Plain.size());
            });

            std::unordered_map<uint64_t, std::vector<int>> ByPicture;
            for (int Part = 0; Part < 32 && !StopScan; Part++) {
                char Name[32];
                std::snprintf(Name, sizeof(Name), "NStpData%02X.NOS", Part);
                ForEachEntry(ReadFile(Folder + Name), StopScan,
                             [&](const uint32_t Id, bool, const uint8_t* Data, const size_t Size) {
                    if (Id < MinimapImageBase || Id >= MinimapImageBase + MinimapIdRange) return;
                    ByPicture[Hash(Data, Size)].push_back(static_cast<int>(Id - MinimapImageBase));
                });
            }

            std::unordered_set<int> Found;
            for (const auto& [Picture, Maps] : ByPicture) {
                if (Maps.size() < 2) continue;
                std::unordered_set<uint64_t> Shapes;
                for (const int Map : Maps) {
                    if (const auto It = Layouts.find(Map); It != Layouts.end()) Shapes.insert(It->second);
                }
                if (Shapes.size() < 2) continue;
                Found.insert(Maps.begin(), Maps.end());
            }
            {
                std::lock_guard Lock(ResultLock);
                Placeholders = std::move(Found);
            }
            ScanDone = true;
        }

        float Sample(const Grid& Map, const std::vector<float>& Values, const float X, const float Y) {
            const float Fx = X - 0.5f, Fy = Y - 0.5f;
            const int X0 = static_cast<int>(std::floor(Fx)), Y0 = static_cast<int>(std::floor(Fy));
            const float Tx = Fx - static_cast<float>(X0), Ty = Fy - static_cast<float>(Y0);
            auto At = [&](const int Cx, const int Cy) {
                return Cx < 0 || Cy < 0 || Cx >= Map.Width || Cy >= Map.Height ? 0.0f : Values[Cy * Map.Width + Cx];
            };
            const float Top = At(X0, Y0) + (At(X0 + 1, Y0) - At(X0, Y0)) * Tx;
            const float Bottom = At(X0, Y0 + 1) + (At(X0 + 1, Y0 + 1) - At(X0, Y0 + 1)) * Tx;
            return Top + (Bottom - Top) * Ty;
        }

        struct Shade {
            uint8_t R, G, B, A;
        };

        Shade Paint(const float Value, const float Band, const Shade Fill, const Shade Edge, const Shade Outside) {
            const float Edgeness = std::clamp(1.0f - std::abs(Value - 0.5f) / Band, 0.0f, 1.0f);
            const Shade Base = Value > 0.5f ? Fill : Outside;
            const float A = Edgeness * Edge.A / 255.0f;
            const float BaseA = Base.A / 255.0f;
            const float OutA = A + BaseA * (1.0f - A);
            if (OutA <= 0.0f) return {0, 0, 0, 0};
            auto Mix = [&](const uint8_t E, const uint8_t B) {
                return static_cast<uint8_t>(std::lround((E * A + B * BaseA * (1.0f - A)) / OutA));
            };
            return {Mix(Edge.R, Base.R), Mix(Edge.G, Base.G), Mix(Edge.B, Base.B),
                    static_cast<uint8_t>(std::lround(OutA * 255.0f))};
        }

        void Put(Picture& Image, const int X, const int Y, const Shade Color) {
            uint8_t* Pixel = &Image.Rgba[(static_cast<size_t>(Y) * Image.Width + X) * 4];
            Pixel[0] = Color.R;
            Pixel[1] = Color.G;
            Pixel[2] = Color.B;
            Pixel[3] = Color.A;
        }

        bool Inside(const float (*Points)[2], const int Count, const float X, const float Y) {
            bool In = false;
            for (int i = 0, j = Count - 1; i < Count; j = i++) {
                const float Xi = Points[i][0], Yi = Points[i][1], Xj = Points[j][0], Yj = Points[j][1];
                if ((Yi > Y) != (Yj > Y) && X < (Xj - Xi) * (Y - Yi) / (Yj - Yi) + Xi) In = !In;
            }
            return In;
        }

        template <typename InFn>
        void Stamp(Picture& Image, const int Left, const int Top, const int Size, InFn&& InDark, InFn&& InLight,
                   const Shade Dark, const Shade Light) {
            for (int Y = 0; Y < Size; Y++) {
                for (int X = 0; X < Size; X++) {
                    int DarkHits = 0, LightHits = 0;
                    for (int Sy = 0; Sy < 4; Sy++) {
                        for (int Sx = 0; Sx < 4; Sx++) {
                            const float Px = static_cast<float>(X) + (static_cast<float>(Sx) + 0.5f) / 4.0f - Size / 2.0f;
                            const float Py = static_cast<float>(Y) + (static_cast<float>(Sy) + 0.5f) / 4.0f - Size / 2.0f;
                            if (InLight(Px, Py)) LightHits++;
                            else if (InDark(Px, Py)) DarkHits++;
                        }
                    }
                    const int Total = DarkHits + LightHits;
                    if (!Total) continue;
                    const float Share = static_cast<float>(LightHits) / static_cast<float>(Total);
                    auto Mix = [&](const uint8_t D, const uint8_t L) {
                        return static_cast<uint8_t>(std::lround(D + (L - D) * Share));
                    };
                    Put(Image, Left + X, Top + Y, {Mix(Dark.R, Light.R), Mix(Dark.G, Light.G), Mix(Dark.B, Light.B),
                                                   static_cast<uint8_t>(Total * 255 / 16)});
                }
            }
        }
    }

    std::vector<uint8_t> EncodePng(const Picture& Image) {
        const int Width = Image.Width, Height = Image.Height;
        std::vector<uint8_t> Raw;
        Raw.reserve(static_cast<size_t>(Width * 4 + 1) * Height);
        for (int Y = 0; Y < Height; Y++) {
            Raw.push_back(0);
            Raw.insert(Raw.end(), Image.Rgba.begin() + static_cast<std::ptrdiff_t>(Y) * Width * 4,
                       Image.Rgba.begin() + static_cast<std::ptrdiff_t>(Y + 1) * Width * 4);
        }
        std::vector<uint8_t> Z{0x78, 0x01};
        uint32_t A = 1, B = 0;
        for (const uint8_t Byte : Raw) {
            A = (A + Byte) % 65521;
            B = (B + A) % 65521;
        }
        for (size_t At = 0; At < Raw.size(); At += 65535) {
            const auto Len = static_cast<uint16_t>(std::min<size_t>(65535, Raw.size() - At));
            Z.push_back(At + Len >= Raw.size() ? 1 : 0);
            Z.push_back(static_cast<uint8_t>(Len));
            Z.push_back(static_cast<uint8_t>(Len >> 8));
            Z.push_back(static_cast<uint8_t>(~Len));
            Z.push_back(static_cast<uint8_t>(~Len >> 8));
            Z.insert(Z.end(), Raw.begin() + static_cast<std::ptrdiff_t>(At), Raw.begin() + static_cast<std::ptrdiff_t>(At + Len));
        }
        PushBig(Z, (B << 16) | A);

        std::vector<uint8_t> Png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        auto Chunk = [&](const char* Type, const std::vector<uint8_t>& Body) {
            PushBig(Png, static_cast<uint32_t>(Body.size()));
            const size_t Start = Png.size();
            Png.insert(Png.end(), Type, Type + 4);
            Png.insert(Png.end(), Body.begin(), Body.end());
            PushBig(Png, Crc32(Png.data() + Start, Png.size() - Start));
        };
        std::vector<uint8_t> Header;
        PushBig(Header, static_cast<uint32_t>(Width));
        PushBig(Header, static_cast<uint32_t>(Height));
        Header.insert(Header.end(), {8, 6, 0, 0, 0});
        Chunk("IHDR", Header);
        Chunk("IDAT", Z);
        Chunk("IEND", {});
        return Png;
    }

    std::vector<float> Field(const Grid& Map) {
        std::vector<float> Values(static_cast<size_t>(Map.Width) * Map.Height, 0.0f);
        auto Cell = [&](const int X, const int Y) {
            return X < 0 || Y < 0 || X >= Map.Width || Y >= Map.Height ? 0.0f
                                                                      : static_cast<float>(Map.Walkable[Y * Map.Width + X]);
        };
        constexpr float Weights[3] = {1.0f, 2.0f, 1.0f};
        for (int Y = 0; Y < Map.Height; Y++) {
            for (int X = 0; X < Map.Width; X++) {
                float Sum = 0.0f;
                for (int Dy = -1; Dy <= 1; Dy++) {
                    for (int Dx = -1; Dx <= 1; Dx++) Sum += Weights[Dx + 1] * Weights[Dy + 1] * Cell(X + Dx, Y + Dy);
                }
                Values[Y * Map.Width + X] = Sum / 16.0f;
            }
        }
        return Values;
    }

    Picture Overlay(const Grid& Map, const std::vector<float>& Values, const float Scale) {
        Picture Image;
        Image.Width = static_cast<int>(std::ceil(Map.Width * Scale));
        Image.Height = static_cast<int>(std::ceil(Map.Height * Scale));
        Image.Rgba.assign(static_cast<size_t>(Image.Width) * Image.Height * 4, 0);
        const float Band = EdgeHalfWidth * EdgeSlope / Scale;
        constexpr Shade Fill{110, 160, 220, 60};
        constexpr Shade Edge{225, 240, 255, 255};
        constexpr Shade Outside{0, 0, 0, 0};
        for (int V = 0; V < Image.Height; V++) {
            for (int U = 0; U < Image.Width; U++) {
                const float Gx = (static_cast<float>(U) + 0.5f) / Scale;
                const float Gy = (static_cast<float>(V) + 0.5f) / Scale;
                const Shade Color = Paint(Sample(Map, Values, Gx, Gy), Band, Fill, Edge, Outside);
                if (Color.A) Put(Image, U, V, Color);
            }
        }
        return Image;
    }

    Picture Minimap(const Grid& Map, const std::vector<float>& Values, const int Size) {
        Picture Image;
        Image.Width = Image.Height = Size;
        Image.Rgba.assign(static_cast<size_t>(Size) * Size * 4, 0);
        const float PerPixel = static_cast<float>(std::max(Map.Width, Map.Height)) / static_cast<float>(Size);
        const float Band = std::max(0.7f * PerPixel, 0.35f) * EdgeSlope;
        constexpr Shade Fill{60, 84, 118, 255};
        constexpr Shade Edge{215, 232, 255, 255};
        constexpr Shade Outside{16, 20, 28, 255};
        for (int V = 0; V < Size; V++) {
            for (int U = 0; U < Size; U++) {
                const float Gx = (static_cast<float>(U) + 0.5f) * static_cast<float>(Map.Width) / static_cast<float>(Size);
                const float Gy = (static_cast<float>(V) + 0.5f) * static_cast<float>(Map.Height) / static_cast<float>(Size);
                Put(Image, U, V, Paint(Sample(Map, Values, Gx, Gy), Band, Fill, Edge, Outside));
            }
        }
        return Image;
    }

    Picture MarkerSheet() {
        Picture Image;
        Image.Width = DiamondLeft + DotSize;
        Image.Height = ArrowSize;
        Image.Rgba.assign(static_cast<size_t>(Image.Width) * Image.Height * 4, 0);
        constexpr Shade Dark{12, 20, 30, 255};
        constexpr Shade ArrowColor{120, 220, 255, 255};
        constexpr Shade White{255, 255, 255, 255};
        constexpr float Arrow[4][2] = {{0.0f, -13.0f}, {8.0f, 9.0f}, {0.0f, 4.0f}, {-8.0f, 9.0f}};
        constexpr float Outline = 1.6f;
        for (int Frame = 0; Frame < ArrowFrames; Frame++) {
            const float Turn = static_cast<float>(Frame) * 3.14159265f / 8.0f;
            const float Cos = std::cos(Turn), Sin = std::sin(Turn);
            float Inner[4][2], Outer[4][2];
            for (int i = 0; i < 4; i++) {
                const float X = Arrow[i][0], Y = Arrow[i][1] - 1.0f;
                Inner[i][0] = X * Cos - Y * Sin;
                Inner[i][1] = X * Sin + Y * Cos;
                const float Grow = (std::hypot(X, Y) + Outline * 1.8f) / std::max(std::hypot(X, Y), 0.01f);
                Outer[i][0] = Inner[i][0] * Grow;
                Outer[i][1] = Inner[i][1] * Grow;
            }
            auto InOuter = [&](const float X, const float Y) { return Inside(Outer, 4, X, Y); };
            auto InInner = [&](const float X, const float Y) { return Inside(Inner, 4, X, Y); };
            Stamp(Image, Frame * ArrowSize, 0, ArrowSize, std::function<bool(float, float)>(InOuter),
                  std::function<bool(float, float)>(InInner), Dark, ArrowColor);
        }
        auto InRing = [](const float X, const float Y) { return X * X + Y * Y <= 7.5f * 7.5f; };
        auto InDot = [](const float X, const float Y) { return X * X + Y * Y <= 5.6f * 5.6f; };
        Stamp(Image, DotLeft, 0, DotSize, std::function<bool(float, float)>(InRing),
              std::function<bool(float, float)>(InDot), Dark, White);
        auto InOuterDiamond = [](const float X, const float Y) { return std::abs(X) + std::abs(Y) <= 7.8f; };
        auto InDiamond = [](const float X, const float Y) { return std::abs(X) + std::abs(Y) <= 5.6f; };
        Stamp(Image, DiamondLeft, 0, DotSize, std::function<bool(float, float)>(InOuterDiamond),
              std::function<bool(float, float)>(InDiamond), Dark, White);
        return Image;
    }

    void StartPlaceholderScan() {
        if (Scanner.joinable()) return;
        StopScan = false;
        ScanDone = false;
        Scanner = std::thread(&Scan);
    }

    void StopPlaceholderScan() {
        StopScan = true;
        if (Scanner.joinable()) Scanner.join();
    }

    bool PlaceholdersReady() {
        return ScanDone;
    }

    bool HasPlaceholder(const int MapId) {
        if (!ScanDone) return false;
        std::lock_guard Lock(ResultLock);
        return Placeholders.contains(MapId);
    }
}
