#define NOMINMAX
#include <windows.h>

#include "ModContract.h"
#include "WidgetKit.h"
#include "Memory.h"
#include "TLBSWidget.h"
#include "TEWLabel.h"
#include "TEWEditWidget.h"
#include "TEWButtonWidget.h"
#include "TEWControlWidget.h"
#include "TEWCustomPanelWidget.h"
#include "TEWGraphicButtonWidget.h"
#include "TNTIconWidget.h"
#include "InPacket.h"
#include "MvPacket.h"
#include "SuPacket.h"
#include "OutPacket.h"
#include "CMapPacket.h"
#include "RaidMbfPacket.h"
#include "GroundEffects.h"
#include "EffectCatalog.h"
#include "MapImage.h"
#include "MapOverlay.h"
#include "GameHint.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <mutex>
#include <tuple>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
    constexpr ModClassRequirement Requirements[] = {
        {TLBSWidget::ClassName, TLBSWidget::Version, TLBSWidget::ExpectedSize},
        {TEWControlWidget::ClassName, TEWControlWidget::Version, TEWControlWidget::ExpectedSize},
        {TEWCustomPanelWidget::ClassName, TEWCustomPanelWidget::Version, TEWCustomPanelWidget::ExpectedSize},
        {TEWGraphicButtonWidget::ClassName, TEWGraphicButtonWidget::Version, TEWGraphicButtonWidget::ExpectedSize},
        {TEWButtonWidget::ClassName, TEWButtonWidget::Version, TEWButtonWidget::ExpectedSize},
        {TEWEditWidget::ClassName, TEWEditWidget::Version, TEWEditWidget::ExpectedSize},
        {TEWLabel::ClassName, TEWLabel::Version, TEWLabel::ExpectedSize},
        {TNTIconWidget::ClassName, TNTIconWidget::Version, TNTIconWidget::ExpectedSize},
    };

    constexpr int LeverVnum = 1135;
    constexpr int PulledLeverVnum = 1136;
    constexpr ULONGLONG CreditWindowMs = 3000;
    constexpr ULONGLONG LateFlipWindowMs = 500;
    constexpr int32_t MiniMapAtlas = 1593835568;
    constexpr int32_t RaidAtlas = 1593835784;

    struct MarkIcon {
        const wchar_t* Name;
        int32_t Image;
        AtlasFrame Frame;
        int16_t Width;
        int16_t Height;
    };

    constexpr MarkIcon Icons[] = {
        {L"Orange marker", MiniMapAtlas, {272, 107, 12, 12}, 12, 12},
        {L"Blue marker", MiniMapAtlas, {272, 120, 12, 12}, 12, 12},
        {L"Red gem", RaidAtlas, {232, 300, 19, 19}, 10, 10},
        {L"Purple gem", RaidAtlas, {213, 300, 19, 19}, 10, 10},
        {L"Red dot", MiniMapAtlas, {292, 87, 7, 7}, 7, 7},
        {L"Blue gem", MiniMapAtlas, {301, 87, 8, 9}, 8, 9},
        {L"Purple diamond", MiniMapAtlas, {309, 87, 9, 9}, 9, 9},
        {L"Green gem", MiniMapAtlas, {301, 97, 8, 9}, 8, 9},
    };
    constexpr int IconCount = static_cast<int>(std::size(Icons));
    constexpr int DefaultLeverIcon = 0;
    constexpr int DefaultBossIcon = 2;

    constexpr uint16_t WindowWidth = 350;
    constexpr uint16_t WindowHeight = 450;
    constexpr int16_t Pad = 14;
    constexpr int16_t TextNudge = -1;
    constexpr int16_t DropdownArrow = 17;
    constexpr int16_t LeverTop = 42;
    constexpr int16_t BossTop = 72;
    constexpr int16_t CountTop = 102;
    constexpr int16_t EffectsTop = 128;
    constexpr int16_t CorrectEffectTop = 154;
    constexpr int16_t TrapEffectTop = 182;
    constexpr int16_t StatusTop = 218;
    constexpr int16_t LearnedTop = 236;
    constexpr int16_t ForgetTop = 258;
    constexpr int16_t OverlayTop = 296;
    constexpr int16_t EverywhereTop = 322;
    constexpr int16_t CenterTop = 348;
    constexpr int16_t OpacityTop = 374;
    constexpr int16_t MinimapTop = 404;
    constexpr int OpacityStep = 10;
    constexpr int16_t EffectLabelWidth = 96;
    constexpr int16_t LookPrevX = Pad + EffectLabelWidth;
    constexpr int16_t LookTextWidth = 96;
    constexpr int PreviewLeverRange = 25;
    constexpr int DefaultCorrectEffect = 4732;
    constexpr int DefaultTrapEffect = 1054;
    constexpr int Looks[] = {
        2018, 3, 6, 9, 15, 18, 35, 194, 196, 198, 203, 212, 303, 634,
        824, 826, 828, 829, 830, 831, 853, 854, 857, 863, 864, 866, 868, 869,
        870, 871, 872, 881, 1054, 3007, 3019, 3020, 3021, 3022, 3619, 3952, 4013, 4294,
        4295, 4296, 4407, 4415, 4465, 4467, 4536, 4543, 4544, 4545, 4546, 4600, 4620, 4732,
        4799, 4829, 4914, 5000, 5001, 5003, 5004, 5005, 5073, 5074, 5075, 5077, 5082, 5083,
        5095, 5099, 5100, 5101, 6000, 6003, 6004, 6007, 7427, 7606, 7739,
    };
    constexpr uint32_t PreviewHandle = 0x7E52FFFF;
    constexpr uint32_t FirstLeverHandle = 0x7E520000;
    constexpr ULONGLONG PreviewMs = 6000;
    constexpr ULONGLONG MaxPreviewMs = 15000;
    constexpr int16_t PickerX = 104;
    constexpr int16_t PickerWidth = 110;
    constexpr int16_t PreviewX = PickerX + PickerWidth + DropdownArrow + 8;

    const Color HintColor(255, 190, 190, 190);
    const Color WarnColor(255, 255, 110, 90);
    const Color CountColor(255, 255, 255, 255);
    const Color CountShadow(255, 0, 0, 0);

    enum class Kind { Lever, Boss };

    struct Target {
        Kind TargetKind;
        int X;
        int Y;
    };

    struct Settings {
        bool ShowLevers = true;
        bool ShowBosses = true;
        bool ShowCount = true;
        int LeverIcon = DefaultLeverIcon;
        int BossIcon = DefaultBossIcon;
        std::wstring ExtraBossVnums;
        bool ShowEffects = true;
        int CorrectEffect = DefaultCorrectEffect;
        int TrapEffect = DefaultTrapEffect;
        bool Overlay = true;
        bool OverlayEverywhere = false;
        MapOverlay::Options View;
    };

    struct Mark {
        TEWControlWidget* Widget = nullptr;
        int Icon = -1;
    };

    const ModHost* CachedHost = nullptr;
    TLBSWidget* AttachedRoot = nullptr;
    Settings Config;

    std::mutex TargetLock;
    std::unordered_map<long, Target> Targets;
    std::unordered_set<int> ExtraBosses;
    std::atomic<bool> Dirty{true};

    using LeverKey = std::tuple<int, int, int>;
    using SizeKey = std::tuple<int, int, int, int>;
    struct LeverRecord {
        int Good = 0;
        int Bad = 0;
    };
    struct Position {
        int X;
        int Y;
    };
    std::map<LeverKey, LeverRecord> Learned;
    std::map<SizeKey, LeverRecord> BySize;
    std::set<int> Took;
    std::unordered_map<long, int> LeverStates;
    std::unordered_set<long> RemovedObjects;
    bool HasLateFlip = false;
    LeverKey LateFlip{};
    ULONGLONG LateFlipTime = 0;
    int RoomWidth = -1;
    int RoomHeight = -1;
    int RoomMap = -1;
    std::tuple<int, int, int> SeenRoom{-1, -1, -1};
    ULONGLONG SeenRoomAt = 0;
    constexpr ULONGLONG RoomSettleMs = 1000;
    int LastLeversLeft = -1;
    int PendingCredit = 0;
    ULONGLONG CreditTime = 0;
    std::atomic<bool> LearnedDirty{false};
    std::atomic<bool> InRaid{false};

    struct LeverEffect {
        uint32_t Handle = 0;
        int Effect = 0;
        int X = 0;
        int Y = 0;
        ULONGLONG Spawned = 0;
    };
    std::unordered_map<long, LeverEffect> LeverEffects;
    uint32_t NextLeverHandle = FirstLeverHandle;
    bool EffectsSearched = false;
    int PendingPreview = 0;
    bool PreviewActive = false;
    ULONGLONG PreviewSpawned = 0;
    ULONGLONG PreviewLength = PreviewMs;
    int LastPlayerX = -1;
    int LastPlayerY = -1;

    struct EffectEditor {
        int* Value;
        WidgetKit::EditBox Box{};
        std::wstring Shown;
    };
    bool EditorsChanged = false;
    EffectEditor CorrectEditor{&Config.CorrectEffect};
    EffectEditor TrapEditor{&Config.TrapEffect};

    Memory::RootLookup MiniMapLookup("TNTMiniMapWidget");
    TLBSWidget* MiniMap = nullptr;
    TLBSWidget* Container = nullptr;
    Rect ContainerRect{};
    int32_t KnownMapWidth = -1;
    int32_t KnownMapHeight = -1;
    std::unordered_map<long, Mark> Active;
    std::vector<Mark> Spare;
    TEWLabel* CountLabel = nullptr;
    std::wstring CountText;

    TEWCustomPanelWidget* Window = nullptr;
    TLBSWidget* LeverBox = nullptr;
    TLBSWidget* BossBox = nullptr;
    TLBSWidget* CountBox = nullptr;
    TLBSWidget* EffectsBox = nullptr;
    WidgetKit::Dropdown* LeverPicker = nullptr;
    WidgetKit::Dropdown* BossPicker = nullptr;
    TEWControlWidget* LeverPreview = nullptr;
    TEWControlWidget* BossPreview = nullptr;
    TEWLabel* StatusLabel = nullptr;
    std::wstring StatusText;
    TEWLabel* LearnedLabel = nullptr;
    std::wstring LearnedText;
    bool PendingToggle = false;
    TLBSWidget* OverlayBox = nullptr;
    TLBSWidget* EverywhereBox = nullptr;
    TLBSWidget* CenterBox = nullptr;
    TLBSWidget* MinimapBox = nullptr;
    TEWLabel* OpacityLabel = nullptr;
    std::wstring OpacityText;

    constexpr int16_t HelpWidth = 22;
    const Color HelpColor(255, 150, 200, 255);
    struct Help {
        TEWGraphicButtonWidget* Hit;
        std::vector<std::wstring> Lines;
    };
    std::vector<Help> Helps;
    bool HintShown = false;

    std::filesystem::path IniPath() {
        wchar_t Exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, Exe, MAX_PATH);
        return std::filesystem::path(Exe).parent_path() / L"mods" / L"RaidHelper.ini";
    }

    int ReadInt(const wchar_t* Key, const int Default) {
        return static_cast<int>(GetPrivateProfileIntW(L"RaidHelper", Key, Default, IniPath().c_str()));
    }

    void WriteValue(const wchar_t* Key, const std::wstring& Value) {
        WritePrivateProfileStringW(L"RaidHelper", Key, Value.c_str(), IniPath().c_str());
    }

    int ClampIcon(const int Index, const int Default) {
        return Index >= 0 && Index < IconCount ? Index : Default;
    }

    void LoadSettings() {
        Config.ShowLevers = ReadInt(L"ShowLevers", 1) != 0;
        Config.ShowBosses = ReadInt(L"ShowBosses", 1) != 0;
        Config.ShowCount = ReadInt(L"ShowCount", 1) != 0;
        Config.LeverIcon = ClampIcon(ReadInt(L"LeverIcon", DefaultLeverIcon), DefaultLeverIcon);
        Config.BossIcon = ClampIcon(ReadInt(L"BossIcon", DefaultBossIcon), DefaultBossIcon);
        Config.ShowEffects = ReadInt(L"ShowEffects", 1) != 0;
        Config.CorrectEffect = std::max(0, ReadInt(L"CorrectEffect", DefaultCorrectEffect));
        Config.TrapEffect = std::max(0, ReadInt(L"TrapEffect", DefaultTrapEffect));
        Config.Overlay = ReadInt(L"Overlay", 1) != 0;
        Config.OverlayEverywhere = ReadInt(L"OverlayEverywhere", 0) != 0;
        Config.View.Center = ReadInt(L"OverlayCenter", 0) != 0;
        Config.View.Opacity = std::clamp(ReadInt(L"OverlayOpacity", 60), OpacityStep, 100);
        Config.View.Minimap = ReadInt(L"DrawnMinimap", 1) != 0;
        wchar_t Extra[512]{};
        GetPrivateProfileStringW(L"RaidHelper", L"ExtraBossVnums", L"", Extra, 512, IniPath().c_str());
        Config.ExtraBossVnums = Extra;

        std::unordered_set<int> Parsed;
        std::wstringstream Stream(Config.ExtraBossVnums);
        std::wstring Token;
        while (std::getline(Stream, Token, L',')) {
            try {
                if (!Token.empty()) Parsed.insert(std::stoi(Token));
            } catch (const std::exception&) {}
        }
        std::lock_guard Lock(TargetLock);
        ExtraBosses = std::move(Parsed);
    }

    void SaveSettings() {
        WriteValue(L"ShowLevers", Config.ShowLevers ? L"1" : L"0");
        WriteValue(L"ShowBosses", Config.ShowBosses ? L"1" : L"0");
        WriteValue(L"ShowCount", Config.ShowCount ? L"1" : L"0");
        WriteValue(L"LeverIcon", std::to_wstring(Config.LeverIcon));
        WriteValue(L"BossIcon", std::to_wstring(Config.BossIcon));
        WriteValue(L"ExtraBossVnums", Config.ExtraBossVnums);
        WriteValue(L"ShowEffects", Config.ShowEffects ? L"1" : L"0");
        WriteValue(L"CorrectEffect", std::to_wstring(Config.CorrectEffect));
        WriteValue(L"TrapEffect", std::to_wstring(Config.TrapEffect));
        WriteValue(L"Overlay", Config.Overlay ? L"1" : L"0");
        WriteValue(L"OverlayEverywhere", Config.OverlayEverywhere ? L"1" : L"0");
        WriteValue(L"OverlayCenter", Config.View.Center ? L"1" : L"0");
        WriteValue(L"OverlayOpacity", std::to_wstring(Config.View.Opacity));
        WriteValue(L"DrawnMinimap", Config.View.Minimap ? L"1" : L"0");
    }

    std::filesystem::path LearnedPath() {
        return IniPath().parent_path() / L"RaidHelper" / L"levers.txt";
    }

    void LoadLearned() {
        std::map<LeverKey, LeverRecord> Loaded;
        std::map<SizeKey, LeverRecord> LoadedSizes;
        std::set<int> LoadedTook;
        std::ifstream File(LearnedPath());
        std::string Line;
        while (std::getline(File, Line)) {
            std::istringstream Words(Line);
            std::string First;
            if (!(Words >> First)) continue;
            int A, B, C, D, E;
            if (First == "map") {
                if (Words >> A >> B >> C >> D >> E) Loaded[{A, B, C}] = {D, E};
            } else if (First == "took") {
                if (Words >> A) LoadedTook.insert(A);
            } else if (First == "size") {
                int F;
                if (Words >> A >> B >> C >> D >> E >> F) LoadedSizes[{A, B, C, D}] = {E, F};
            } else {
                try {
                    A = std::stoi(First);
                } catch (const std::exception&) {
                    continue;
                }
                if (Words >> B >> C >> D >> E) {
                    int F;
                    if (Words >> F) LoadedSizes[{A, B, C, D}] = {E, F};
                }
            }
        }
        std::lock_guard Lock(TargetLock);
        Learned = std::move(Loaded);
        BySize = std::move(LoadedSizes);
        Took = std::move(LoadedTook);
    }

    void SaveLearned() {
        std::map<LeverKey, LeverRecord> Copy;
        std::map<SizeKey, LeverRecord> Sizes;
        std::set<int> Taken;
        {
            std::lock_guard Lock(TargetLock);
            Copy = Learned;
            Sizes = BySize;
            Taken = Took;
        }
        std::error_code Error;
        std::filesystem::create_directories(LearnedPath().parent_path(), Error);
        const std::filesystem::path Temp = LearnedPath().wstring() + L".tmp";
        {
            std::ofstream File(Temp, std::ios::trunc);
            if (!File) return;
            for (const auto& [Key, Record] : Copy) {
                File << "map " << std::get<0>(Key) << ' ' << std::get<1>(Key) << ' ' << std::get<2>(Key) << ' '
                     << Record.Good << ' ' << Record.Bad << '\n';
            }
            for (const int Map : Taken) File << "took " << Map << '\n';
            for (const auto& [Key, Record] : Sizes) {
                File << "size " << std::get<0>(Key) << ' ' << std::get<1>(Key) << ' ' << std::get<2>(Key) << ' '
                     << std::get<3>(Key) << ' ' << Record.Good << ' ' << Record.Bad << '\n';
            }
            if (!File) return;
        }
        std::filesystem::rename(Temp, LearnedPath(), Error);
    }

    void AdoptSizeLevers(const TickContext& Context) {
        const std::tuple<int, int, int> Room{Context.mapId, Context.mapWidth, Context.mapHeight};
        const ULONGLONG Now = GetTickCount64();
        if (Room != SeenRoom) {
            SeenRoom = Room;
            SeenRoomAt = Now;
            return;
        }
        if (Context.mapId < 0 || Context.mapWidth <= 0 || Now - SeenRoomAt < RoomSettleMs) return;
        if (Took.contains(Context.mapId) || BySize.empty() || LeverStates.empty()) return;
        Took.insert(Context.mapId);
        for (auto It = BySize.begin(); It != BySize.end();) {
            const auto& [Key, Record] = *It;
            if (std::get<0>(Key) != Context.mapWidth || std::get<1>(Key) != Context.mapHeight) {
                ++It;
                continue;
            }
            LeverRecord& Into = Learned[{Context.mapId, std::get<2>(Key), std::get<3>(Key)}];
            Into.Good += Record.Good;
            Into.Bad += Record.Bad;
            It = BySize.erase(It);
        }
        LearnedDirty = true;
        Dirty = true;
    }

    bool IsKnownCorrect(const int X, const int Y) {
        if (RoomMap < 0) return false;
        const auto Found = Learned.find({RoomMap, X, Y});
        return Found != Learned.end() && Found->second.Good > 0 && Found->second.Good >= Found->second.Bad;
    }

    bool IsKnownTrap(const int X, const int Y) {
        if (RoomMap < 0) return false;
        const auto Found = Learned.find({RoomMap, X, Y});
        return Found != Learned.end() && Found->second.Bad > Found->second.Good;
    }

    void LearnPull(const Position& Lever, const bool AllowTrap) {
        if (RoomMap < 0) return;
        const LeverKey Key{RoomMap, Lever.X, Lever.Y};
        LeverRecord& Record = Learned[Key];
        if (PendingCredit > 0 && GetTickCount64() - CreditTime <= CreditWindowMs) {
            PendingCredit--;
            Record.Good++;
        } else if (AllowTrap) {
            Record.Bad++;
            HasLateFlip = true;
            LateFlip = Key;
            LateFlipTime = GetTickCount64();
        }
        LearnedDirty = true;
    }

    bool OnIn(const Packet::InPacket& Packet) {
        if (Packet.entityType == EntityType::Monster) {
            MapOverlay::OnMonsterIn(Packet.entityID);
            std::lock_guard Lock(TargetLock);
            const bool Marked = (!Packet.fields.empty() && Packet.fields.back() == "1")
                                || ExtraBosses.contains(Packet.entityVNum);
            if (Marked) {
                Targets[Packet.entityID] = {Kind::Boss, Packet.xPos, Packet.yPos};
                Dirty = true;
            } else if (Targets.erase(Packet.entityID)) {
                Dirty = true;
            }
        } else if (Packet.entityType == EntityType::Object && Packet.entityVNum == LeverVnum) {
            std::lock_guard Lock(TargetLock);
            LeverStates[Packet.entityID] = LeverVnum;
            Targets[Packet.entityID] = {Kind::Lever, Packet.xPos, Packet.yPos};
            Dirty = true;
        } else if (Packet.entityType == EntityType::Object && Packet.entityVNum == PulledLeverVnum) {
            std::lock_guard Lock(TargetLock);
            const auto Previous = LeverStates.find(Packet.entityID);
            const bool SeenUnpulled = Previous != LeverStates.end() && Previous->second == LeverVnum;
            const bool NeverSeen = Previous == LeverStates.end();
            const bool WasRemoved = RemovedObjects.erase(Packet.entityID) > 0;
            LeverStates[Packet.entityID] = PulledLeverVnum;
            if (SeenUnpulled) LearnPull({Packet.xPos, Packet.yPos}, true);
            else if (NeverSeen && WasRemoved) LearnPull({Packet.xPos, Packet.yPos}, false);
            if (Targets.erase(Packet.entityID)) Dirty = true;
        }
        return true;
    }

    bool OnRaidCounters(const Packet::RaidMbfPacket& Packet) {
        InRaid = true;
        std::lock_guard Lock(TargetLock);
        if (LastLeversLeft >= 0 && Packet.leversLeft < LastLeversLeft) {
            PendingCredit += LastLeversLeft - Packet.leversLeft;
            CreditTime = GetTickCount64();
            if (HasLateFlip && CreditTime - LateFlipTime <= LateFlipWindowMs) {
                LeverRecord& Record = Learned[LateFlip];
                if (Record.Bad > 0) Record.Bad--;
                Record.Good++;
                PendingCredit--;
                LearnedDirty = true;
            }
            HasLateFlip = false;
        }
        LastLeversLeft = Packet.leversLeft;
        return true;
    }

    bool OnMove(const Packet::MvPacket& Packet) {
        if (Packet.entityType != EntityType::Monster) return true;
        std::lock_guard Lock(TargetLock);
        const auto Found = Targets.find(Packet.entityID);
        if (Found != Targets.end()) {
            Found->second.X = Packet.xPos;
            Found->second.Y = Packet.yPos;
            Dirty = true;
        }
        return true;
    }

    bool OnSkillUse(const Packet::SuPacket& Packet) {
        if (Packet.targetEntityType != EntityType::Monster || Packet.targetAlive) return true;
        MapOverlay::OnMonsterDied(Packet.targetEntityID);
        std::lock_guard Lock(TargetLock);
        if (Targets.erase(Packet.targetEntityID)) Dirty = true;
        return true;
    }

    bool OnOut(const Packet::OutPacket& Packet) {
        if (Packet.entityType != EntityType::Monster && Packet.entityType != EntityType::Object) return true;
        std::lock_guard Lock(TargetLock);
        if (Packet.entityType == EntityType::Object) RemovedObjects.insert(Packet.entityID);
        if (Targets.erase(Packet.entityID)) Dirty = true;
        return true;
    }

    bool OnMapChange(const Packet::CMapPacket&) {
        InRaid = false;
        MapOverlay::OnMapChange();
        std::lock_guard Lock(TargetLock);
        Targets.clear();
        LeverStates.clear();
        RemovedObjects.clear();
        HasLateFlip = false;
        LastLeversLeft = -1;
        PendingCredit = 0;
        Dirty = true;
        return true;
    }

    void Attach(TLBSWidget* Parent, TLBSWidget* Child) {
        if (!Parent || !Child || !Parent->childrenList) return;
        Child->parent = Parent;
        Parent->childrenList->push_back(Child);
    }

    void Detach(TLBSWidget* Widget) {
        if (!Widget || !Widget->parent || !Widget->parent->childrenList) return;
        Widget->parent->childrenList->remove(Widget);
        Widget->parent = nullptr;
    }

    void Show(TLBSWidget* Widget, const bool Visible) {
        if (Widget && Widget->isVisible != Visible) Widget->isVisible = Visible;
    }

    void Place(TLBSWidget* Widget, const int16_t X, const int16_t Y, const int16_t Width, const int16_t Height) {
        if (!Widget) return;
        const Rect Wanted{X, Y, static_cast<int16_t>(X + Width), static_cast<int16_t>(Y + Height)};
        if (std::memcmp(&Widget->rect, &Wanted, sizeof(Rect)) != 0) Widget->rect = Wanted;
    }

    void SetLabel(TEWLabel* Label, std::wstring& Shown, const std::wstring& Text) {
        if (!Label || Shown == Text) return;
        Shown = Text;
        Label->SetText(Text.c_str());
    }

    void ApplyIcon(TEWControlWidget* Sprite, const int Index) {
        const MarkIcon& Icon = Icons[Index];
        Sprite->imageData.imageName = Icon.Image;
        Sprite->imageData.imageWidth = 512;
        Sprite->imageData.imageHeight = 512;
        Sprite->imageData.frameCount = 1;
        Sprite->imageData.atlasFrames[0] = Icon.Frame;
        Sprite->currentImage = 0;
        Place(Sprite, Sprite->rect.left, Sprite->rect.top, Icon.Width, Icon.Height);
    }

    TEWControlWidget* CreateSprite(const int Index) {
        TEWControlWidget* Sprite = Widget::Create<TEWControlWidget>(CachedHost);
        if (!Sprite) return nullptr;
        Sprite->isInteractable = false;
        delete[] Sprite->imageData.atlasFrames;
        Sprite->imageData.atlasFrames = new AtlasFrame[1]{Icons[Index].Frame};
        Sprite->rect = {0, 0, 0, 0};
        ApplyIcon(Sprite, Index);
        return Sprite;
    }

    void ForgetMarks() {
        Container = nullptr;
        MiniMap = nullptr;
        ContainerRect = {};
        CountLabel = nullptr;
        CountText.clear();
        Active.clear();
        Spare.clear();
        Dirty = true;
    }

    bool IsRootChild(const TLBSWidget* Root, TLBSWidget* Widget) {
        return Root && Root->childrenList && Root->childrenList->index_of(Widget) >= 0;
    }

    TLBSWidget* MapArea(TLBSWidget* Whole) {
        if (!Whole || !Whole->childrenList || Whole->childrenList->count < 2) return nullptr;
        return Whole->childrenList->list[1];
    }

    bool EnsureContainer(const TLBSWidget* Root) {
        TLBSWidget* Found = MiniMapLookup.First(Root);
        TLBSWidget* Area = MapArea(Found);
        if (!Found || !Area) return false;

        if (!Container) {
            Container = Widget::Create<TLBSWidget>(CachedHost);
            if (!Container) return false;
            Container->isInteractable = false;
            CountLabel = Widget::Create<TEWLabel>(CachedHost);
            if (CountLabel) {
                CountLabel->isInteractable = false;
                CountLabel->textAlignment = 3;
                CountLabel->textColor = CountColor;
                CountLabel->shadowColor = CountShadow;
                Show(CountLabel, false);
            }
        }

        if (Found != MiniMap || Found->childrenList->index_of(Container) < 0) {
            if (MiniMap && MiniMap != Found && IsRootChild(Root, MiniMap)) {
                Detach(Container);
                Detach(CountLabel);
            }
            Container->parent = nullptr;
            if (CountLabel) CountLabel->parent = nullptr;
            MiniMap = Found;
            Attach(MiniMap, Container);
            if (CountLabel && MiniMap->childrenList->index_of(CountLabel) < 0) Attach(MiniMap, CountLabel);
            const int32_t AreaIndex = MiniMap->childrenList->index_of(Area);
            if (AreaIndex >= 0) MiniMap->childrenList->move_to_index(Container, AreaIndex + 1);
            Dirty = true;
        }

        if (std::memcmp(&ContainerRect, &Area->rect, sizeof(Rect)) != 0) {
            ContainerRect = Area->rect;
            Container->rect = ContainerRect;
            Dirty = true;
        }
        return true;
    }

    void PlaceCount() {
        if (!CountLabel || !MiniMap || !MiniMap->childrenList) return;
        const Rect Area = ContainerRect;
        const TLBSWidget* Slot = nullptr;
        const TLBSWidgetList* Children = MiniMap->childrenList;
        for (uint32_t i = 0; i < Children->count; i++) {
            const TLBSWidget* Child = Children->list[i];
            if (!Child || Child == Container || Child == CountLabel || Child->isVisible) continue;
            const int Height = Child->rect.bottom - Child->rect.top;
            const int Width = Child->rect.right - Child->rect.left;
            if (Child->rect.bottom > Area.top + 2 || Height < 10 || Height > 30 || Width < 40) continue;
            if (!Slot || Child->rect.bottom > Slot->rect.bottom) Slot = Child;
        }
        const int Top = Slot ? Slot->rect.top + (Slot->rect.bottom - Slot->rect.top - 12) / 2 : Area.top - 16;
        const auto Width = static_cast<int16_t>(std::max(Area.right - Area.left, 10));
        Place(CountLabel, Area.left, static_cast<int16_t>(Top + TextNudge), Width, 30);
        if (CountLabel->pxPerLine != Width) CountLabel->pxPerLine = Width;
    }

    bool Wanted(const Kind TargetKind) {
        return TargetKind == Kind::Lever ? Config.ShowLevers : Config.ShowBosses;
    }

    int IconFor(const Kind TargetKind) {
        return TargetKind == Kind::Lever ? Config.LeverIcon : Config.BossIcon;
    }

    void Retire(Mark& Retired) {
        Show(Retired.Widget, false);
        Spare.push_back(Retired);
    }

    Mark* Acquire(const long Id) {
        const auto Found = Active.find(Id);
        if (Found != Active.end()) return &Found->second;
        Mark Fresh;
        if (!Spare.empty()) {
            Fresh = Spare.back();
            Spare.pop_back();
        } else {
            Fresh.Widget = CreateSprite(0);
            if (!Fresh.Widget) return nullptr;
            Fresh.Icon = 0;
            Show(Fresh.Widget, false);
            Attach(Container, Fresh.Widget);
        }
        return &Active.emplace(Id, Fresh).first->second;
    }

    void Sync(const TickContext& Context) {
        std::unordered_map<long, Target> Snapshot;
        {
            std::lock_guard Lock(TargetLock);
            for (const auto& [Id, Entry] : Targets) {
                if (Entry.TargetKind == Kind::Lever && !IsKnownCorrect(Entry.X, Entry.Y)) continue;
                Snapshot.emplace(Id, Entry);
            }
        }

        for (auto It = Active.begin(); It != Active.end();) {
            const auto Found = Snapshot.find(It->first);
            if (Found == Snapshot.end() || !Wanted(Found->second.TargetKind)) {
                Retire(It->second);
                It = Active.erase(It);
            } else {
                ++It;
            }
        }

        const bool MapKnown = Context.mapWidth > 0 && Context.mapHeight > 0;
        const int AreaWidth = ContainerRect.right - ContainerRect.left;
        const int AreaHeight = ContainerRect.bottom - ContainerRect.top;
        int Levers = 0, Bosses = 0;
        for (const auto& [Id, Entry] : Snapshot) {
            (Entry.TargetKind == Kind::Lever ? Levers : Bosses)++;
            if (!Wanted(Entry.TargetKind)) continue;
            Mark* Current = Acquire(Id);
            if (!Current) continue;
            const int Icon = IconFor(Entry.TargetKind);
            if (Current->Icon != Icon) {
                ApplyIcon(Current->Widget, Icon);
                Current->Icon = Icon;
            }
            if (!MapKnown || AreaWidth <= 0 || AreaHeight <= 0) {
                Show(Current->Widget, false);
                continue;
            }
            const MarkIcon& Shape = Icons[Icon];
            const int CenterX = static_cast<int>(std::lround(static_cast<double>(Entry.X) * AreaWidth / Context.mapWidth));
            const int CenterY = static_cast<int>(std::lround(static_cast<double>(Entry.Y) * AreaHeight / Context.mapHeight));
            Place(Current->Widget, static_cast<int16_t>(CenterX - Shape.Width / 2),
                  static_cast<int16_t>(CenterY - Shape.Height / 2), Shape.Width, Shape.Height);
            Show(Current->Widget, true);
        }

        std::wstring Text;
        if (Levers) Text += L"Levers " + std::to_wstring(Levers);
        if (Bosses) Text += std::wstring(Text.empty() ? L"" : L"   ") + L"Bosses " + std::to_wstring(Bosses);
        if (CountLabel) {
            PlaceCount();
            SetLabel(CountLabel, CountText, Text);
            Show(CountLabel, Config.ShowCount && !Text.empty());
            TLBSWidgetList* Children = MiniMap ? MiniMap->childrenList : nullptr;
            if (Children && Children->index_of(CountLabel) != static_cast<int32_t>(Children->count) - 1) {
                Children->bubble_up(CountLabel);
            }
        }
    }

    TEWLabel* AddLabel(TLBSWidget* Parent, const int16_t X, const int16_t TextY, const int16_t Width,
                       const wchar_t* Text) {
        TEWLabel* Label = Widget::Create<TEWLabel>(CachedHost);
        if (!Label) return nullptr;
        Place(Label, X, static_cast<int16_t>(TextY + TextNudge), Width, 30);
        Label->textAlignment = 1;
        Label->pxPerLine = Width;
        Label->SetText(Text);
        Attach(Parent, Label);
        return Label;
    }

    void RefreshPreview(TEWControlWidget* Preview, const int Index, const int16_t RowTop) {
        if (!Preview) return;
        const MarkIcon& Icon = Icons[Index];
        ApplyIcon(Preview, Index);
        Place(Preview, static_cast<int16_t>(PreviewX + (14 - Icon.Width) / 2),
              static_cast<int16_t>(RowTop + (18 - Icon.Height) / 2), Icon.Width, Icon.Height);
    }

    void __cdecl OnOptionChanged(void*) {
        SaveSettings();
        Dirty = true;
    }

    void __cdecl OnLeverIcon(void*, const int Index) {
        Config.LeverIcon = ClampIcon(Index, DefaultLeverIcon);
        RefreshPreview(LeverPreview, Config.LeverIcon, LeverTop);
        OnOptionChanged(nullptr);
    }

    void __cdecl OnBossIcon(void*, const int Index) {
        Config.BossIcon = ClampIcon(Index, DefaultBossIcon);
        RefreshPreview(BossPreview, Config.BossIcon, BossTop);
        OnOptionChanged(nullptr);
    }

    WidgetKit::Dropdown* CreatePicker(const int16_t Y, const int Selected, const WidgetKit::Dropdown::PickFn OnPick) {
        WidgetKit::Dropdown::Desc Desc;
        Desc.X = PickerX;
        Desc.Y = static_cast<int16_t>(Y - 1);
        Desc.Width = PickerWidth;
        Desc.VisibleRows = IconCount;
        Desc.Filterable = false;
        Desc.OnPick = OnPick;
        WidgetKit::Dropdown* Picker = WidgetKit::Dropdown::Create(CachedHost, Desc);
        if (!Picker) return nullptr;
        std::vector<std::wstring> Names;
        for (const MarkIcon& Icon : Icons) Names.emplace_back(Icon.Name);
        Picker->SetItems(std::move(Names));
        Picker->SetSelected(Selected);
        Attach(Window, Picker->Widget());
        return Picker;
    }

    void ClearEffects() {
        for (const auto& [Id, Effect] : LeverEffects) GroundEffects::Remove(Effect.Handle);
        LeverEffects.clear();
        if (PreviewActive) GroundEffects::Remove(PreviewHandle);
        PreviewActive = false;
    }

    void SyncEffects() {
        if (!GroundEffects::Ready()) return;
        std::unordered_map<long, LeverEffect> Wanted;
        if (Config.ShowEffects) {
            std::lock_guard Lock(TargetLock);
            for (const auto& [Id, Entry] : Targets) {
                if (Entry.TargetKind != Kind::Lever) continue;
                const int Effect = IsKnownCorrect(Entry.X, Entry.Y) ? Config.CorrectEffect
                                   : IsKnownTrap(Entry.X, Entry.Y)  ? Config.TrapEffect
                                                                    : 0;
                if (Effect > 0) Wanted[Id] = {0, Effect, Entry.X, Entry.Y, 0};
            }
        }

        const ULONGLONG Now = GetTickCount64();
        for (auto It = LeverEffects.begin(); It != LeverEffects.end();) {
            const auto Found = Wanted.find(It->first);
            const bool Keep = Found != Wanted.end() && Found->second.Effect == It->second.Effect
                              && Found->second.X == It->second.X && Found->second.Y == It->second.Y;
            if (Keep) {
                ++It;
                continue;
            }
            GroundEffects::Remove(It->second.Handle);
            It = LeverEffects.erase(It);
        }
        for (const auto& [Id, Want] : Wanted) {
            const auto Found = LeverEffects.find(Id);
            if (Found == LeverEffects.end()) {
                LeverEffect Fresh = Want;
                Fresh.Handle = NextLeverHandle++;
                if (NextLeverHandle >= PreviewHandle) NextLeverHandle = FirstLeverHandle;
                Fresh.Spawned = Now;
                GroundEffects::Spawn(Fresh.Handle, Fresh.Effect, Fresh.X, Fresh.Y);
                LeverEffects.emplace(Id, Fresh);
            } else if (const uint32_t Length = EffectCatalog::DurationMs(Found->second.Effect);
                       Length > 0 && Now - Found->second.Spawned >= Length) {
                Found->second.Spawned = Now;
                GroundEffects::Spawn(Found->second.Handle, Found->second.Effect, Found->second.X, Found->second.Y);
            }
        }

        if (PreviewActive && PendingPreview != 0) {
            GroundEffects::Remove(PreviewHandle);
            PreviewActive = false;
        }
        if (PendingPreview > 0 && LastPlayerX >= 0 && LastPlayerY >= 0) {
            int AtX = LastPlayerX, AtY = LastPlayerY;
            int Best = PreviewLeverRange * PreviewLeverRange + 1;
            {
                std::lock_guard Lock(TargetLock);
                for (const auto& [Id, Entry] : Targets) {
                    if (Entry.TargetKind != Kind::Lever) continue;
                    const int DX = Entry.X - LastPlayerX, DY = Entry.Y - LastPlayerY;
                    if (DX * DX + DY * DY < Best) {
                        Best = DX * DX + DY * DY;
                        AtX = Entry.X;
                        AtY = Entry.Y;
                    }
                }
            }
            PreviewActive = GroundEffects::Spawn(PreviewHandle, PendingPreview, AtX, AtY);
            PreviewSpawned = Now;
            PreviewLength = std::clamp<ULONGLONG>(EffectCatalog::DurationMs(PendingPreview), PreviewMs, MaxPreviewMs);
        }
        PendingPreview = 0;
        if (PreviewActive && Now - PreviewSpawned >= PreviewLength) {
            GroundEffects::Remove(PreviewHandle);
            PreviewActive = false;
        }
    }

    TEWGraphicButtonWidget* AddSpriteButton(TLBSWidget* Parent, std::initializer_list<AtlasFrame> Frames,
                                            const WidgetKit::ClickFn OnClick, void* Argument) {
        TEWGraphicButtonWidget* Button = Widget::Create<TEWGraphicButtonWidget>(CachedHost);
        if (!Button) return nullptr;
        delete[] Button->imageData.atlasFrames;
        Button->imageData.imageName = 1593835585;
        Button->imageData.imageWidth = 512;
        Button->imageData.imageHeight = 512;
        Button->imageData.frameCount = static_cast<int16_t>(Frames.size());
        Button->imageData.atlasFrames = new AtlasFrame[Frames.size()];
        std::copy(Frames.begin(), Frames.end(), Button->imageData.atlasFrames);
        Button->drawMode = 0;
        WidgetKit::SetOnClick(Button, OnClick, Argument);
        Attach(Parent, Button);
        return Button;
    }

    TEWGraphicButtonWidget* AddBlueButton(TLBSWidget* Parent, const int16_t X, const int16_t Y, const wchar_t* Text,
                                          const WidgetKit::ClickFn OnClick, void* Argument) {
        TEWGraphicButtonWidget* Button = AddSpriteButton(Parent, {
            {419, 308, 5, 24}, {424, 308, 10, 24}, {409, 308, 10, 24},
            {445, 308, 5, 24}, {450, 308, 10, 24}, {435, 308, 10, 24},
            {471, 308, 5, 24}, {476, 308, 10, 24}, {461, 308, 10, 24},
        }, OnClick, Argument);
        if (!Button) return nullptr;
        constexpr int16_t Cap = 10;
        constexpr int16_t Air = 8;
        const int Measured = WidgetKit::MeasureText(1, Text);
        const auto Width = static_cast<int16_t>(std::max(40, (Measured > 0 ? Measured : 30) + 2 * (Cap + Air)));
        Button->nineSliceInfo = {static_cast<uint16_t>(Width - 2 * Cap), 25, static_cast<uint16_t>(Width - Cap), 25,
                                 Cap, 0, Cap, 0};
        Button->sliceCount = 3;
        Button->drawMode = 4;
        Button->rect = {X, Y, static_cast<int16_t>(X + Width), static_cast<int16_t>(Y + 25)};
        TEWLabel* Caption = AddLabel(Button, 0, 6, Width, Text);
        if (Caption) {
            Caption->textAlignment = 3;
            Caption->textColor = Color(255, 201, 227, 252);
            Caption->shadowColor = Color(255, 1, 61, 255);
        }
        return Button;
    }

    const std::vector<int>& LookList() {
        static const std::vector<int> Fallback = [] {
            std::vector<int> Sorted(std::begin(Looks), std::end(Looks));
            std::sort(Sorted.begin(), Sorted.end());
            return Sorted;
        }();
        return EffectCatalog::Ids().empty() ? Fallback : EffectCatalog::Ids();
    }

    void StepLook(EffectEditor& Editor, const int Step) {
        const std::vector<int>& List = LookList();
        if (List.empty()) return;
        const int Current = *Editor.Value;
        int Next = 0;
        if (Step > 0) {
            const auto After = std::upper_bound(List.begin(), List.end(), Current);
            Next = After == List.end() ? 0 : *After;
        } else if (Current <= 0) {
            Next = List.back();
        } else {
            const auto Before = std::lower_bound(List.begin(), List.end(), Current);
            Next = Before == List.begin() ? 0 : *(Before - 1);
        }
        *Editor.Value = Next;
        SaveSettings();
        EditorsChanged = true;
        PendingPreview = Next;
    }
    void __cdecl OnLookPreview(void* Argument) {
        PendingPreview = *static_cast<EffectEditor*>(Argument)->Value;
    }

    void __cdecl OnLookPrev(void* Argument) {
        StepLook(*static_cast<EffectEditor*>(Argument), -1);
    }

    void __cdecl OnLookNext(void* Argument) {
        StepLook(*static_cast<EffectEditor*>(Argument), 1);
    }
    void __cdecl OnForgetRoom(void*) {
        {
            std::lock_guard Lock(TargetLock);
            if (RoomMap < 0) return;
            for (auto It = Learned.begin(); It != Learned.end();) {
                if (std::get<0>(It->first) == RoomMap) {
                    It = Learned.erase(It);
                } else {
                    ++It;
                }
            }
        }
        LearnedDirty = true;
        Dirty = true;
    }

    std::vector<MapOverlay::Mark> OverlayMarks() {
        std::vector<MapOverlay::Mark> Marks;
        std::lock_guard Lock(TargetLock);
        for (const auto& [Id, Entry] : Targets) {
            if (!Wanted(Entry.TargetKind)) continue;
            if (Entry.TargetKind == Kind::Lever && !IsKnownCorrect(Entry.X, Entry.Y)) continue;
            const MarkIcon& Icon = Icons[IconFor(Entry.TargetKind)];
            Marks.push_back({Id, Entry.X, Entry.Y, Icon.Image, Icon.Frame, Icon.Width, Icon.Height,
                             Entry.TargetKind == Kind::Boss});
        }
        return Marks;
    }

    std::wstring OpacityLine() {
        return L"Opacity " + std::to_wstring(Config.View.Opacity) + L"%";
    }

    void StepOpacity(const int Step) {
        Config.View.Opacity = std::clamp(Config.View.Opacity + Step, OpacityStep, 100);
        SetLabel(OpacityLabel, OpacityText, OpacityLine());
        SaveSettings();
    }

    void __cdecl OnOpacityDown(void*) {
        StepOpacity(-OpacityStep);
    }

    void __cdecl OnOpacityUp(void*) {
        StepOpacity(OpacityStep);
    }

    TEWGraphicButtonWidget* AddHitArea(TLBSWidget* Parent, const int16_t X, const int16_t Y, const int16_t Width,
                                       const int16_t Height) {
        auto* Area = Widget::Create<TEWGraphicButtonWidget>(CachedHost);
        if (!Area) return nullptr;
        delete[] Area->imageData.atlasFrames;
        Area->imageData.imageName = 1593835585;
        Area->imageData.frameCount = 3;
        Area->imageData.atlasFrames = new AtlasFrame[3]{{0, 0, 1, 1}, {0, 0, 1, 1}, {0, 0, 1, 1}};
        Area->drawMode = 0;
        Area->color = Color(0, 255, 255, 255);
        Area->rect = {X, Y, static_cast<int16_t>(X + Width), static_cast<int16_t>(Y + Height)};
        Attach(Parent, Area);
        return Area;
    }

    bool Hovered(const TEWGraphicButtonWidget* Area) {
        if (!Area || !Area->hovered) return false;
        for (const TLBSWidget* At = Area; At; At = At->parent) {
            if (!At->isVisible) return false;
        }
        return true;
    }

    void AddHelp(const int16_t X, const int16_t Y, std::vector<std::wstring> Lines) {
        TEWLabel* Label = AddLabel(Window, X, static_cast<int16_t>(Y - TextNudge), HelpWidth, L"(?)");
        if (Label) Label->textColor = HelpColor;
        Helps.push_back({AddHitArea(Window, X, Y, HelpWidth, 15), std::move(Lines)});
    }

    int16_t AfterCheckbox(const wchar_t* Text) {
        return static_cast<int16_t>(Pad + 15 + 4 + WidgetKit::MeasureText(1, Text) + 4);
    }

    void AddEffectRow(EffectEditor& Editor, const int16_t Top, const wchar_t* Title, std::vector<std::wstring> HelpLines) {
        AddLabel(Window, Pad, static_cast<int16_t>(Top + 4), EffectLabelWidth, Title);
        AddHelp(static_cast<int16_t>(Pad + WidgetKit::MeasureText(1, Title) + 3), static_cast<int16_t>(Top + 4),
                std::move(HelpLines));
        TEWGraphicButtonWidget* Prev = AddSpriteButton(Window, {{476, 71, 12, 19}, {476, 49, 12, 19}, {476, 71, 12, 19}},
                                                       &OnLookPrev, &Editor);
        Place(Prev, LookPrevX, static_cast<int16_t>(Top + 1), 12, 19);
        Editor.Box = WidgetKit::CreateEditBox(CachedHost, static_cast<int16_t>(LookPrevX + 14), static_cast<int16_t>(Top - 1),
                                              LookTextWidth, 22);
        Attach(Window, Editor.Box.Frame);
        Editor.Shown = std::to_wstring(*Editor.Value);
        if (Editor.Box.Edit) Editor.Box.Edit->SetText(Editor.Shown.c_str());
        TEWGraphicButtonWidget* Next = AddSpriteButton(Window, {{495, 71, 12, 19}, {495, 49, 12, 19}, {495, 71, 12, 19}},
                                                       &OnLookNext, &Editor);
        Place(Next, static_cast<int16_t>(LookPrevX + 16 + LookTextWidth), static_cast<int16_t>(Top + 1), 12, 19);
        AddBlueButton(Window, static_cast<int16_t>(LookPrevX + 34 + LookTextWidth), static_cast<int16_t>(Top - 1),
                      L"Preview", &OnLookPreview, &Editor);
    }
    void CreateSettingsWindow(TLBSWidget* Root) {
        const WidgetKit::WindowDesc Desc{WidgetKit::WindowStyle::Plain, 240, 140, WindowWidth, WindowHeight,
                                         L"Raid Helper", true};
        Window = WidgetKit::CreateGameWindow(CachedHost, Desc);
        if (!Window) return;
        Window->isVisible = false;
        Window->isRestrictedToScreen = true;

        LeverBox = WidgetKit::CreateLabeledCheckbox(CachedHost, Pad, LeverTop, L"Levers", 70, &Config.ShowLevers,
                                                    &OnOptionChanged);
        Attach(Window, LeverBox);
        AddHelp(AfterCheckbox(L"Levers"), LeverTop, {
            L"Marks levers on the minimap, only ones learned as correct.",
            L"A lever counts as correct when pulling it lowers the",
            L"raid's lever counter; traps spawn monsters instead.",
        });
        BossBox = WidgetKit::CreateLabeledCheckbox(CachedHost, Pad, BossTop, L"Bosses", 70, &Config.ShowBosses,
                                                   &OnOptionChanged);
        Attach(Window, BossBox);
        AddHelp(AfterCheckbox(L"Bosses"), BossTop, {
            L"Marks bosses and mini-bosses on the minimap",
            L"until they die.",
        });
        CountBox = WidgetKit::CreateLabeledCheckbox(CachedHost, Pad, CountTop, L"Count on the minimap", 200,
                                                    &Config.ShowCount, &OnOptionChanged);
        Attach(Window, CountBox);
        AddHelp(AfterCheckbox(L"Count on the minimap"), CountTop, {
            L"Shows above the minimap how many correct levers",
            L"and bosses are left in this room.",
        });
        EffectsBox = WidgetKit::CreateLabeledCheckbox(CachedHost, Pad, EffectsTop, L"Ground effect on known levers", 230,
                                                      &Config.ShowEffects, &OnOptionChanged);
        Attach(Window, EffectsBox);
        AddHelp(AfterCheckbox(L"Ground effect on known levers"), EffectsTop, {
            L"Plays an effect on the ground at levers already learned,",
            L"so you can tell correct ones from traps in the world.",
            L"Pick the two effects below.",
        });
        AddEffectRow(CorrectEditor, CorrectEffectTop, L"Correct levers", {
            L"Effect the game plays on levers learned as correct.",
            L"Type its id, or step through them with the arrows.",
            L"Preview plays it on the nearest lever. 0 turns it off.",
        });
        AddEffectRow(TrapEditor, TrapEffectTop, L"Trap levers", {
            L"Effect the game plays on levers learned as traps.",
            L"Type its id, or step through them with the arrows.",
            L"Preview plays it on the nearest lever. 0 turns it off.",
        });
        TEWGraphicButtonWidget* Forget = AddBlueButton(Window, Pad, ForgetTop, L"Forget this room", &OnForgetRoom, nullptr);
        if (Forget) {
            AddHelp(static_cast<int16_t>(Forget->rect.right + 4), static_cast<int16_t>(ForgetTop + 5), {
                L"Forgets every lever learned in this room,",
                L"correct and trap alike. Use it if it was learned wrong.",
            });
        }
        OverlayBox = WidgetKit::CreateLabeledCheckbox(CachedHost, Pad, OverlayTop, L"Map overlay (shows in raids)", 230,
                                                      &Config.Overlay, &OnOptionChanged);
        Attach(Window, OverlayBox);
        AddHelp(AfterCheckbox(L"Map overlay (shows in raids)"), OverlayTop, {
            L"A see-through map in the middle of the screen that",
            L"turns on by itself when you enter a raid room.",
            L"Clicks go through it. Red: monsters, gold: items,",
            L"green: players, the arrow is you.",
        });
        EverywhereBox = WidgetKit::CreateLabeledCheckbox(CachedHost, Pad, EverywhereTop, L"Also outside raids", 230,
                                                         &Config.OverlayEverywhere, &OnOptionChanged);
        Attach(Window, EverywhereBox);
        AddHelp(AfterCheckbox(L"Also outside raids"), EverywhereTop, {
            L"Shows the map overlay on every map, not only in raids.",
            L"Needs \"Map overlay\" on.",
        });
        CenterBox = WidgetKit::CreateLabeledCheckbox(CachedHost, Pad, CenterTop, L"Keep me in the middle", 230,
                                                     &Config.View.Center, &OnOptionChanged);
        Attach(Window, CenterBox);
        AddHelp(AfterCheckbox(L"Keep me in the middle"), CenterTop, {
            L"On: the map moves with you, so you stay in the middle",
            L"of the screen. Off: the whole map stays centered.",
        });
        TEWGraphicButtonWidget* Down = AddSpriteButton(Window, {{476, 71, 12, 19}, {476, 49, 12, 19}, {476, 71, 12, 19}},
                                                       &OnOpacityDown, nullptr);
        Place(Down, Pad, static_cast<int16_t>(OpacityTop + 1), 12, 19);
        OpacityLabel = AddLabel(Window, Pad + 14, static_cast<int16_t>(OpacityTop + 4), LookTextWidth, OpacityLine().c_str());
        if (OpacityLabel) OpacityLabel->textAlignment = 3;
        OpacityText = OpacityLine();
        TEWGraphicButtonWidget* Up = AddSpriteButton(Window, {{495, 71, 12, 19}, {495, 49, 12, 19}, {495, 71, 12, 19}},
                                                     &OnOpacityUp, nullptr);
        Place(Up, static_cast<int16_t>(Pad + 16 + LookTextWidth), static_cast<int16_t>(OpacityTop + 1), 12, 19);
        AddHelp(static_cast<int16_t>(Pad + 32 + LookTextWidth), static_cast<int16_t>(OpacityTop + 4), {
            L"How solid the map overlay is. Markers always stay",
            L"a bit more solid than the map so they stand out.",
        });
        MinimapBox = WidgetKit::CreateLabeledCheckbox(CachedHost, Pad, MinimapTop, L"Draw minimaps that have no picture", 260,
                                                      &Config.View.Minimap, &OnOptionChanged);
        Attach(Window, MinimapBox);
        AddHelp(AfterCheckbox(L"Draw minimaps that have no picture"), MinimapTop, {
            L"Some maps, many raid rooms among them, only have a",
            L"placeholder picture on the top-right minimap.",
            L"This draws their real walkable area there instead.",
        });
        StatusLabel = AddLabel(Window, Pad, StatusTop, WindowWidth - 2 * Pad, L"");
        if (StatusLabel) StatusLabel->textColor = HintColor;
        StatusText.clear();
        LearnedLabel = AddLabel(Window, Pad, LearnedTop, WindowWidth - 2 * Pad, L"");
        if (LearnedLabel) LearnedLabel->textColor = HintColor;
        LearnedText.clear();

        LeverPreview = CreateSprite(Config.LeverIcon);
        Attach(Window, LeverPreview);
        RefreshPreview(LeverPreview, Config.LeverIcon, LeverTop);
        BossPreview = CreateSprite(Config.BossIcon);
        Attach(Window, BossPreview);
        RefreshPreview(BossPreview, Config.BossIcon, BossTop);

        BossPicker = CreatePicker(BossTop, Config.BossIcon, &OnBossIcon);
        LeverPicker = CreatePicker(LeverTop, Config.LeverIcon, &OnLeverIcon);
        Attach(Root, Window);
    }

    void ForgetWindow() {
        Window = nullptr;
        LeverBox = BossBox = CountBox = EffectsBox = nullptr;
        OverlayBox = EverywhereBox = CenterBox = MinimapBox = nullptr;
        OpacityLabel = nullptr;
        OpacityText.clear();
        Helps.clear();
        HintShown = false;
        CorrectEditor.Box = TrapEditor.Box = WidgetKit::EditBox{};
        CorrectEditor.Shown.clear();
        TrapEditor.Shown.clear();
        LeverPicker = BossPicker = nullptr;
        LeverPreview = BossPreview = nullptr;
        StatusLabel = nullptr;
        StatusText.clear();
        LearnedLabel = nullptr;
        LearnedText.clear();
    }

    void CloseDropdowns() {
        for (WidgetKit::Dropdown* Picker : {LeverPicker, BossPicker}) {
            if (Picker) Picker->SetOpen(false);
        }
    }
}

extern "C" {
    __declspec(dllexport) const ModClassRequirement* ModGetRequirements(size_t* OutCount) {
        *OutCount = std::size(Requirements);
        return Requirements;
    }

    __declspec(dllexport) void ModStartup(ImGuiContext* Context, const ImGuiMemAllocFunc AllocFunc,
                                          const ImGuiMemFreeFunc FreeFunc, void* AllocUserData,
                                          const ModHost* Host) {
        ImGui::SetCurrentContext(Context);
        ImGui::SetAllocatorFunctions(AllocFunc, FreeFunc, AllocUserData);
        CachedHost = Host;
        LoadSettings();
        LoadLearned();
        EffectCatalog::Load();
        MapImage::StartPlaceholderScan();
        Packet::SubscribePacket(Host, &OnIn);
        Packet::SubscribePacket(Host, &OnMove);
        Packet::SubscribePacket(Host, &OnSkillUse);
        Packet::SubscribePacket(Host, &OnOut);
        Packet::SubscribePacket(Host, &OnMapChange);
        Packet::SubscribePacket(Host, &OnRaidCounters);
    }

    __declspec(dllexport) void ModShutdown() {
        if (CachedHost) {
            Packet::UnsubscribePacket(CachedHost, &OnIn);
            Packet::UnsubscribePacket(CachedHost, &OnMove);
            Packet::UnsubscribePacket(CachedHost, &OnSkillUse);
            Packet::UnsubscribePacket(CachedHost, &OnOut);
            Packet::UnsubscribePacket(CachedHost, &OnMapChange);
            Packet::UnsubscribePacket(CachedHost, &OnRaidCounters);
        }
        if (LearnedDirty.exchange(false)) SaveLearned();
        ClearEffects();
        MapOverlay::Remove(CachedHost);
        MapImage::StopPlaceholderScan();
        GameHint::Destroy();
        if (MiniMap && IsRootChild(AttachedRoot, MiniMap)) {
            Detach(Container);
            Detach(CountLabel);
        }
        Detach(Window);
        ForgetMarks();
        ForgetWindow();
        AttachedRoot = nullptr;
        CachedHost = nullptr;
    }

    __declspec(dllexport) void ModEarlyTick(const TLBSWidget* RootWidget, const TickContext tickContext) {
        TLBSWidget* Root = const_cast<TLBSWidget*>(RootWidget);
        if (!CachedHost || !Root || !Root->childrenList) return;
        static WidgetKit::WindowRaiser Raiser;
        Raiser.Tick(Window, tickContext);
        if (!tickContext.isPlayerLoaded) {
            Show(Window, false);
            return;
        }

        if (Root != AttachedRoot) {
            ForgetMarks();
            ForgetWindow();
            MapOverlay::Forget();
            GameHint::Destroy();
            AttachedRoot = Root;
            CreateSettingsWindow(Root);
        }

        if (LearnedDirty.exchange(false)) SaveLearned();
        {
            std::lock_guard Lock(TargetLock);
            if (RoomWidth != tickContext.mapWidth || RoomHeight != tickContext.mapHeight || RoomMap != tickContext.mapId) {
                RoomWidth = tickContext.mapWidth;
                RoomHeight = tickContext.mapHeight;
                RoomMap = tickContext.mapId;
                Dirty = true;
            }
            AdoptSizeLevers(tickContext);
        }

        const bool HaveMiniMap = EnsureContainer(Root);
        if (HaveMiniMap) {
            if (tickContext.mapWidth != KnownMapWidth || tickContext.mapHeight != KnownMapHeight) {
                KnownMapWidth = tickContext.mapWidth;
                KnownMapHeight = tickContext.mapHeight;
                Dirty = true;
            }
            if (Dirty.exchange(false)) Sync(tickContext);
        }

        if (!EffectsSearched) {
            EffectsSearched = true;
            GroundEffects::Init();
        }
        LastPlayerX = tickContext.playerX;
        LastPlayerY = tickContext.playerY;
        SyncEffects();
        const bool OverlayOn = Config.Overlay && (InRaid || Config.OverlayEverywhere);
        MapOverlay::Tick(CachedHost, Root, tickContext, OverlayOn, Config.View,
                         OverlayOn ? OverlayMarks() : std::vector<MapOverlay::Mark>{});

        if (!Window) {
            CachedHost->ReportStatus(ModHealthLevel::Broken, "Couldn't create the settings window.");
        } else if (!HaveMiniMap) {
            CachedHost->ReportStatus(ModHealthLevel::Warning, "Minimap not found.");
        } else if (!GroundEffects::Ready()) {
            CachedHost->ReportStatus(ModHealthLevel::Warning, "Ground effects not found; levers only show on the minimap.");
        } else if (tickContext.mapWidth <= 0) {
            CachedHost->ReportStatus(ModHealthLevel::Warning, "Map size unknown; update NosBoost.");
        } else {
            CachedHost->ReportStatus(ModHealthLevel::Ok, "");
        }
        if (!Window) return;

        if (PendingToggle) {
            PendingToggle = false;
            const bool Open = !Window->isVisible;
            if (!Open) CloseDropdowns();
            Show(Window, Open);
            if (Open) Window->BubbleUp();
        }
        const std::vector<std::wstring>* HelpLines = nullptr;
        if (Window->isVisible) {
            for (const Help& Entry : Helps) {
                if (Hovered(Entry.Hit)) HelpLines = &Entry.Lines;
            }
        }
        if (HelpLines) {
            if (GameHint::Create(CachedHost, Root)) GameHint::Show(*HelpLines, tickContext.mouseX, tickContext.mouseY);
            HintShown = true;
        } else if (HintShown) {
            GameHint::Hide();
            HintShown = false;
        }
        if (!Window->isVisible) return;

        for (EffectEditor* Editor : {&CorrectEditor, &TrapEditor}) {
            if (!Editor->Box.Edit) continue;
            if (EditorsChanged) {
                Editor->Shown = std::to_wstring(*Editor->Value);
                Editor->Box.Edit->SetText(Editor->Shown.c_str());
                continue;
            }
            const std::wstring Typed = Editor->Box.Edit->GetText();
            if (Typed == Editor->Shown) continue;
            Editor->Shown = Typed;
            try {
                size_t Used = 0;
                const int Effect = Typed.empty() ? -1 : std::stoi(Typed, &Used);
                const std::vector<int>& Known = LookList();
                const bool Real = Effect == 0 || std::binary_search(Known.begin(), Known.end(), Effect);
                if (Used == Typed.size() && Real && Effect != *Editor->Value) {
                    *Editor->Value = Effect;
                    SaveSettings();
                }
            } catch (const std::exception&) {}
        }
        EditorsChanged = false;

        std::wstring Status;
        if (!HaveMiniMap) {
            Status = L"Minimap not found.";
        } else {
            int Levers = 0, Bosses = 0;
            {
                std::lock_guard Lock(TargetLock);
                for (const auto& [Id, Entry] : Targets) {
                    if (Entry.TargetKind == Kind::Boss) Bosses++;
                    else if (IsKnownCorrect(Entry.X, Entry.Y)) Levers++;
                }
            }
            Status = L"This map: " + std::to_wstring(Levers) + (Levers == 1 ? L" correct lever, " : L" correct levers, ")
                     + std::to_wstring(Bosses) + (Bosses == 1 ? L" boss" : L" bosses");
        }
        if (StatusLabel && Status != StatusText) {
            StatusLabel->textColor = HaveMiniMap ? HintColor : WarnColor;
        }
        SetLabel(StatusLabel, StatusText, Status);

        int KnownLevers = 0;
        std::unordered_set<int> KnownRooms;
        {
            std::lock_guard Lock(TargetLock);
            for (const auto& [Key, Record] : Learned) {
                if (Record.Good <= Record.Bad) continue;
                KnownLevers++;
                KnownRooms.insert(std::get<0>(Key));
            }
            std::set<std::pair<int, int>> WaitingRooms;
            for (const auto& [Key, Record] : BySize) {
                if (Record.Good <= Record.Bad) continue;
                KnownLevers++;
                WaitingRooms.insert({std::get<0>(Key), std::get<1>(Key)});
            }
            for (const auto& Room : WaitingRooms) KnownRooms.insert(-1 - Room.first * 10000 - Room.second);
        }
        SetLabel(LearnedLabel, LearnedText, L"Learned: " + std::to_wstring(KnownLevers) + L" correct levers in "
                                             + std::to_wstring(KnownRooms.size())
                                             + (KnownRooms.size() == 1 ? L" room" : L" rooms"));
        for (WidgetKit::Dropdown* Picker : {LeverPicker, BossPicker}) {
            if (Picker) Picker->Tick(tickContext);
        }
    }

    __declspec(dllexport) void ModTick(const TLBSWidget*, TickContext) {}

    __declspec(dllexport) void ModToggleMainWindow() {
        PendingToggle = true;
    }
}
