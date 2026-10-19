#define NOMINMAX
#include <windows.h>

#include "MapOverlay.h"
#include "MapImage.h"
#include "ItemCatalog.h"
#include "Memory.h"
#include "TLBSWidget.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace MapOverlay {
    namespace {
        constexpr float ScreenShare = 0.7f;
        constexpr float MaxScale = 8.0f;
        constexpr int MaxTextureSide = 2048;
        constexpr int16_t ArrowShown = 28;
        constexpr int16_t PlayerShown = 12;
        constexpr int16_t MonsterShown = 9;
        constexpr int16_t ItemShown = 13;
        constexpr uint32_t MinimapImageBase = 0x5551D000;
        constexpr uint32_t MinimapCaches[] = {0, 2};
        constexpr int MinimapSize = 128;
        constexpr float Pi = 3.14159265f;
        const Color MonsterTint(0, 235, 80, 70);
        const Color ItemTint(0, 255, 190, 0);
        const Color PlayerTint(0, 110, 230, 130);

        constexpr uint8_t ScenePattern[] = {0xA1, 0, 0, 0, 0, 0, 0, 0, 0, 0x58, 0x00, 0x0F, 0x84, 0xBF};
        constexpr char SceneMask[] = "x????????xxxxx";
        constexpr uint32_t SceneGrid = 0x20;
        constexpr uint32_t GridCells = 4;
        constexpr uint32_t ScenePlayers = 0x0C;
        constexpr uint32_t SceneMonsters = 0x10;
        constexpr uint32_t SceneItems = 0x18;
        constexpr uint32_t SceneOwnPlayer = 0x30;
        constexpr uint32_t ListItems = 0x04;
        constexpr uint32_t ListCount = 0x08;
        constexpr int32_t MaxListCount = 5000;
        constexpr uint32_t ObjectId = 0x08;
        constexpr uint32_t ObjectWorldX = 0x58;
        constexpr uint32_t ObjectWorldY = 0x60;
        constexpr uint32_t ItemObjectVnum = 0xC8;
        constexpr int GoldVnum = 1046;

        struct Spot {
            long Id;
            float X;
            float Y;
            int Vnum = 0;
        };

        bool CatalogLoaded = false;
        bool CatalogTried = false;
        std::unordered_set<int> MapObjects;

        void LoadCatalog() {
            if (CatalogLoaded || CatalogTried) return;
            CatalogTried = true;
            if (!ItemCatalog::Load()) return;
            CatalogLoaded = true;
            for (const auto& Item : ItemCatalog::All()) {
                if (Item.MapObject && Item.Vnum != GoldVnum) MapObjects.insert(Item.Vnum);
            }
        }

        uintptr_t SceneHolder = 0;
        bool SceneSearched = false;

        struct Pool {
            TLBSWidget* Layer = nullptr;
            std::vector<TEWControlWidget*> Widgets;
            size_t Used = 0;
        };

        Memory::RootLookup NaviLookup("TNaviWidget");
        TLBSWidget* AttachedTo = nullptr;
        TLBSWidget* Container = nullptr;
        TEWControlWidget* MapWidget = nullptr;
        Pool MonsterPool, ItemPool, PlayerPool, LeverPool, BossPool;
        Pool* const Layers[] = {&MonsterPool, &ItemPool, &PlayerPool, &LeverPool, &BossPool};
        TEWControlWidget* Self = nullptr;

        int32_t SheetId = 0;
        int16_t SheetWidth = 0;
        int16_t SheetHeight = 0;

        struct MapState {
            int MapId = -1;
            MapImage::Grid Grid;
            std::vector<float> Values;
            float Scale = 0.0f;
            float TextureScale = 0.0f;
            int ScreenWidth = 0;
            int ScreenHeight = 0;
            int32_t ImageId = 0;
            uint16_t ImageWidth = 0;
            uint16_t ImageHeight = 0;
        };
        MapState Current;
        std::mutex DeadLock;
        std::unordered_set<long> Dead;
        bool GridDirty = true;

        std::unordered_set<int32_t> Replaced;
        int MinimapChecked = -1;

        float LastSelfX = -1.0f;
        float LastSelfY = -1.0f;
        float FacingX = 0.0f;
        float FacingY = -1.0f;

        const uint8_t* FindPattern(const uint8_t* Pattern, const char* Mask) {
            const auto Base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
            const auto* Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Base);
            const auto* Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(Base + Dos->e_lfanew);
            const size_t Size = Nt->OptionalHeader.SizeOfImage;
            const size_t Length = std::strlen(Mask);
            for (size_t At = 0; At + Length <= Size; At++) {
                MEMORY_BASIC_INFORMATION Info{};
                if (At % 0x1000 == 0 && (!VirtualQuery(Base + At, &Info, sizeof(Info)) || Info.State != MEM_COMMIT
                                         || (Info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))) {
                    At += 0xFFF;
                    continue;
                }
                size_t i = 0;
                while (i < Length && (Mask[i] == '?' || Base[At + i] == Pattern[i])) i++;
                if (i == Length) return Base + At;
            }
            return nullptr;
        }

        uintptr_t Scene() {
            if (!SceneSearched) {
                SceneSearched = true;
                if (const uint8_t* Match = FindPattern(ScenePattern, SceneMask)) {
                    Memory::Read(reinterpret_cast<uintptr_t>(Match) + 1, SceneHolder);
                }
            }
            uintptr_t Holder = 0, Manager = 0;
            if (!SceneHolder || !Memory::Read(SceneHolder, Holder) || !Memory::Read(Holder, Manager)) return 0;
            return Manager;
        }

        bool ReadGrid(MapImage::Grid& Out) {
            const uintptr_t Manager = Scene();
            uintptr_t Grid = 0;
            int16_t Size[2]{};
            if (!Manager || !Memory::Read(Manager + SceneGrid, Grid) || !Memory::Read(Grid, Size)) return false;
            if (Size[0] <= 0 || Size[1] <= 0 || Size[0] > 2048 || Size[1] > 2048) return false;
            std::vector<uint8_t> Cells(static_cast<size_t>(Size[0]) * Size[1]);
            if (!Memory::Copy(Cells.data(), Grid + GridCells, Cells.size())) return false;
            Out.Width = Size[0];
            Out.Height = Size[1];
            Out.Walkable.resize(Cells.size());
            for (size_t i = 0; i < Cells.size(); i++) Out.Walkable[i] = Cells[i] == 0 ? 1 : 0;
            return true;
        }

        bool ReadObject(const uintptr_t Object, Spot& Out) {
            int32_t Id = 0;
            float World[2]{};
            if (!Memory::Read(Object + ObjectId, Id) || !Memory::Read(Object + ObjectWorldX, World[0])
                || !Memory::Read(Object + ObjectWorldY, World[1])) {
                return false;
            }
            Out = {Id, (World[0] - 0.25f) * 2.0f, (World[1] - 0.25f) * 2.0f};
            return true;
        }

        bool IsLoot(const Spot& Item) {
            return !MapObjects.contains(Item.Vnum);
        }

        std::vector<Spot> ReadList(const uintptr_t Manager, const uint32_t Offset, const bool WithVnum = false) {
            std::vector<Spot> Found;
            uintptr_t List = 0, Items = 0;
            int32_t Count = 0;
            if (!Memory::Read(Manager + Offset, List) || !Memory::Read(List + ListItems, Items)
                || !Memory::Read(List + ListCount, Count) || Count <= 0 || Count > MaxListCount) {
                return Found;
            }
            std::vector<uintptr_t> Objects(static_cast<size_t>(Count));
            if (!Memory::Copy(Objects.data(), Items, Objects.size() * sizeof(uintptr_t))) return Found;
            Found.reserve(Objects.size());
            for (const uintptr_t Object : Objects) {
                Spot Entry{};
                if (!Object || !ReadObject(Object, Entry)) continue;
                uint16_t Vnum = 0;
                if (WithVnum && Memory::Read(Object + ItemObjectVnum, Vnum)) Entry.Vnum = Vnum;
                Found.push_back(Entry);
            }
            return Found;
        }

        void Show(TLBSWidget* Widget, const bool Visible) {
            if (Widget && Widget->isVisible != Visible) Widget->isVisible = Visible;
        }

        void Place(TLBSWidget* Widget, const int X, const int Y, const int Width, const int Height) {
            if (!Widget) return;
            auto Clamp = [](const int Value) { return static_cast<int16_t>(std::clamp(Value, -30000, 30000)); };
            const Rect Wanted{Clamp(X), Clamp(Y), Clamp(X + Width), Clamp(Y + Height)};
            if (std::memcmp(&Widget->rect, &Wanted, sizeof(Rect)) != 0) Widget->rect = Wanted;
        }

        void Tint(TEWControlWidget* Widget, const Color& Wanted) {
            if (Widget && std::memcmp(&Widget->color, &Wanted, sizeof(Color)) != 0) Widget->color = Wanted;
        }

        void SetImage(TEWControlWidget* Widget, const int32_t Image, const int16_t ImageWidth, const int16_t ImageHeight,
                      const AtlasFrame& Frame) {
            if (!Widget) return;
            auto& Data = Widget->imageData;
            if (Data.imageName != Image) Data.imageName = Image;
            if (Data.imageWidth != ImageWidth) Data.imageWidth = ImageWidth;
            if (Data.imageHeight != ImageHeight) Data.imageHeight = ImageHeight;
            if (std::memcmp(&Data.atlasFrames[0], &Frame, sizeof(AtlasFrame)) != 0) Data.atlasFrames[0] = Frame;
            if (Widget->currentImage != 0) Widget->currentImage = 0;
        }

        void Attach(TLBSWidget* Parent, TLBSWidget* Child) {
            Child->parent = Parent;
            Parent->childrenList->push_back(Child);
        }

        TLBSWidget* CreateLayer(const ModHost* Host) {
            TLBSWidget* Layer = Widget::Create<TLBSWidget>(Host);
            if (!Layer) return nullptr;
            Layer->isInteractable = false;
            Layer->rect = {0, 0, 0, 0};
            Attach(Container, Layer);
            return Layer;
        }

        TEWControlWidget* CreatePicture(const ModHost* Host, TLBSWidget* Parent) {
            TEWControlWidget* Widget = Widget::Create<TEWControlWidget>(Host);
            if (!Widget) return nullptr;
            Widget->isInteractable = false;
            delete[] Widget->imageData.atlasFrames;
            Widget->imageData.atlasFrames = new AtlasFrame[1]{{0, 0, 1, 1}};
            Widget->imageData.frameCount = 1;
            Widget->rect = {0, 0, 0, 0};
            Widget->isVisible = false;
            Attach(Parent, Widget);
            return Widget;
        }

        bool EnsureWidgets(const ModHost* Host, TLBSWidget* Root) {
            TLBSWidget* Navi = NaviLookup.First(Root);
            if (!Navi) return false;
            if (!Container) {
                Container = Widget::Create<TLBSWidget>(Host);
                if (!Container) return false;
                Container->isInteractable = false;
                MapWidget = CreatePicture(Host, Container);
                for (Pool* Layer : Layers) Layer->Layer = CreateLayer(Host);
                Self = CreatePicture(Host, Container);
            }
            TLBSWidgetList* Children = Root->childrenList;
            if (AttachedTo != Root || Children->index_of(Container) < 0) {
                Attach(Root, Container);
                AttachedTo = Root;
            }
            const int32_t NaviIndex = Children->index_of(Navi);
            const int32_t Index = Children->index_of(Container);
            const int32_t Wanted = Index > NaviIndex ? NaviIndex + 1 : NaviIndex;
            if (NaviIndex >= 0 && Index != Wanted) Children->move_to_index(Container, static_cast<uint32_t>(Wanted));
            const int Width = Root->rect.right - Root->rect.left, Height = Root->rect.bottom - Root->rect.top;
            Place(Container, 0, 0, Width, Height);
            for (Pool* Layer : Layers) Place(Layer->Layer, 0, 0, Width, Height);
            if (!SheetId) {
                const std::vector<uint8_t> Png = MapImage::EncodePng(MapImage::MarkerSheet());
                uint16_t SheetW = 0, SheetH = 0;
                SheetId = Host->LoadUiImage(Png.data(), static_cast<uint32_t>(Png.size()), &SheetW, &SheetH);
                SheetWidth = static_cast<int16_t>(SheetW);
                SheetHeight = static_cast<int16_t>(SheetH);
            }
            if (!MapWidget || !Self || !SheetId) return false;
            for (Pool* Layer : Layers) {
                if (!Layer->Layer) return false;
            }
            return true;
        }

        void LoadMap(const ModHost* Host, const TickContext& Context, const int ScreenWidth, const int ScreenHeight) {
            const bool NewMap = Context.mapId != Current.MapId || GridDirty;
            if (NewMap) {
                MapImage::Grid Grid;
                if (!ReadGrid(Grid) || Grid.Width != Context.mapWidth || Grid.Height != Context.mapHeight) return;
                Current = MapState{};
                Current.MapId = Context.mapId;
                Current.Grid = std::move(Grid);
                Current.Values = MapImage::Field(Current.Grid);
                GridDirty = false;
            }
            if (!NewMap && Current.ScreenWidth == ScreenWidth && Current.ScreenHeight == ScreenHeight) return;
            Current.ScreenWidth = ScreenWidth;
            Current.ScreenHeight = ScreenHeight;
            Current.Scale = std::min({ScreenShare * ScreenWidth / Current.Grid.Width,
                                      ScreenShare * ScreenHeight / Current.Grid.Height, MaxScale});
            Current.TextureScale = std::min(Current.Scale, static_cast<float>(MaxTextureSide)
                                                               / static_cast<float>(std::max(Current.Grid.Width, Current.Grid.Height)));
            const std::vector<uint8_t> Png = MapImage::EncodePng(MapImage::Overlay(Current.Grid, Current.Values, Current.TextureScale));
            Current.ImageId = Host->LoadUiImage(Png.data(), static_cast<uint32_t>(Png.size()), &Current.ImageWidth,
                                                &Current.ImageHeight);
        }

        void RestoreMinimaps(const ModHost* Host) {
            if (Host && Host->RestoreGameTexture) {
                for (const int32_t Id : Replaced) {
                    for (const uint32_t Cache : MinimapCaches) Host->RestoreGameTexture(Cache, Id);
                }
            }
            Replaced.clear();
            MinimapChecked = -1;
        }

        void SyncMinimap(const ModHost* Host, const bool Wanted) {
            if (!Wanted) {
                if (!Replaced.empty()) RestoreMinimaps(Host);
                return;
            }
            if (MinimapChecked == Current.MapId || !MapImage::PlaceholdersReady() || !Host->ReplaceGameTexture) return;
            MinimapChecked = Current.MapId;
            if (!MapImage::HasPlaceholder(Current.MapId)) return;
            const auto Id = static_cast<int32_t>(MinimapImageBase + static_cast<uint32_t>(Current.MapId));
            const std::vector<uint8_t> Png = MapImage::EncodePng(MapImage::Minimap(Current.Grid, Current.Values, MinimapSize));
            for (const uint32_t Cache : MinimapCaches) {
                Host->ReplaceGameTexture(Cache, Id, Png.data(), static_cast<uint32_t>(Png.size()));
            }
            Replaced.insert(Id);
        }

        struct Projection {
            float Left;
            float Top;
            float Scale;

            float X(const float Cell) const { return Left + (Cell + 0.5f) * Scale; }
            float Y(const float Cell) const { return Top + (Cell + 0.5f) * Scale; }
        };

        void Draw(const ModHost* Host, Pool& Into, const float X, const float Y, const int32_t Image, const int16_t ImageWidth,
                  const int16_t ImageHeight, const AtlasFrame& Frame, const int16_t Width, const int16_t Height,
                  const Color& Shade) {
            if (Into.Used == Into.Widgets.size()) {
                TEWControlWidget* Fresh = CreatePicture(Host, Into.Layer);
                if (!Fresh) return;
                Into.Widgets.push_back(Fresh);
            }
            TEWControlWidget* Widget = Into.Widgets[Into.Used++];
            SetImage(Widget, Image, ImageWidth, ImageHeight, Frame);
            Place(Widget, static_cast<int>(std::lround(X)) - Width / 2, static_cast<int>(std::lround(Y)) - Height / 2, Width,
                  Height);
            Tint(Widget, Shade);
            Show(Widget, true);
        }

        void DrawDots(const ModHost* Host, Pool& Into, const std::vector<Spot>& Where, const std::unordered_set<long>& Skip,
                      const Projection& View, const AtlasFrame& Frame, const int16_t Size, const Color& Shade) {
            for (const Spot& At : Where) {
                if (Skip.contains(At.Id)) continue;
                Draw(Host, Into, View.X(At.X), View.Y(At.Y), SheetId, SheetWidth, SheetHeight, Frame, Size, Size, Shade);
            }
        }

        Color WithAlpha(const Color& Base, const uint8_t Alpha) {
            Color Result = Base;
            Result.alpha = Alpha;
            return Result;
        }
    }

    void OnMapChange() {
        GridDirty = true;
        CatalogTried = false;
        std::lock_guard Lock(DeadLock);
        Dead.clear();
    }

    void OnMonsterDied(const long Id) {
        std::lock_guard Lock(DeadLock);
        Dead.insert(Id);
    }

    void OnMonsterIn(const long Id) {
        std::lock_guard Lock(DeadLock);
        Dead.erase(Id);
    }

    void Tick(const ModHost* Host, TLBSWidget* Root, const TickContext& Context, const bool Visible,
              const Options& Settings, const std::vector<Mark>& Marks) {
        if (!Host || !Root || !Root->childrenList || !Host->LoadUiImage) return;
        if (!EnsureWidgets(Host, Root)) return;
        const int ScreenWidth = Root->rect.right - Root->rect.left;
        const int ScreenHeight = Root->rect.bottom - Root->rect.top;
        if (Context.mapId >= 0 && Context.mapWidth > 0 && ScreenWidth > 0 && ScreenHeight > 0) {
            LoadMap(Host, Context, ScreenWidth, ScreenHeight);
        }
        const bool MapReady = Current.MapId == Context.mapId && Current.ImageId != 0;
        if (MapReady) SyncMinimap(Host, Settings.Minimap);
        if (!Visible || !MapReady) {
            Show(Container, false);
            return;
        }

        const uintptr_t Manager = Scene();
        uintptr_t OwnObject = 0;
        Spot Me{0, -1.0f, -1.0f};
        const bool HaveMe = Manager && Memory::Read(Manager + SceneOwnPlayer, OwnObject) && OwnObject
                            && ReadObject(OwnObject, Me);
        if (HaveMe && LastSelfX >= 0.0f) {
            const float Dx = Me.X - LastSelfX, Dy = Me.Y - LastSelfY;
            const float Length = std::hypot(Dx, Dy);
            if (Length > 0.05f) {
                FacingX = Dx / Length;
                FacingY = Dy / Length;
            }
        }
        if (HaveMe) {
            LastSelfX = Me.X;
            LastSelfY = Me.Y;
        }

        const float Width = Current.Grid.Width * Current.Scale, Height = Current.Grid.Height * Current.Scale;
        Projection View{(ScreenWidth - Width) / 2.0f, (ScreenHeight - Height) / 2.0f, Current.Scale};
        if (Settings.Center && HaveMe) {
            View.Left += ScreenWidth / 2.0f - View.X(Me.X);
            View.Top += ScreenHeight / 2.0f - View.Y(Me.Y);
        }

        const int Opacity = std::clamp(Settings.Opacity, 5, 100);
        const auto MapAlpha = static_cast<uint8_t>(Opacity * 255 / 100);
        const auto MarkAlpha = static_cast<uint8_t>((Opacity + (100 - Opacity) * 6 / 10) * 255 / 100);
        const Color MarkShade(MarkAlpha, 255, 255, 255);

        SetImage(MapWidget, Current.ImageId, static_cast<int16_t>(Current.ImageWidth), static_cast<int16_t>(Current.ImageHeight),
                 {0, 0, static_cast<int16_t>(Current.ImageWidth), static_cast<int16_t>(Current.ImageHeight)});
        Place(MapWidget, static_cast<int>(std::lround(View.Left)), static_cast<int>(std::lround(View.Top)),
              static_cast<int>(std::lround(Width)), static_cast<int>(std::lround(Height)));
        Tint(MapWidget, Color(MapAlpha, 255, 255, 255));
        Show(MapWidget, true);

        std::unordered_set<long> NotMobs;
        for (const Mark& Target : Marks) {
            if (Target.Boss) NotMobs.insert(Target.Id);
        }
        {
            std::lock_guard Lock(DeadLock);
            NotMobs.insert(Dead.begin(), Dead.end());
        }
        const std::vector<Spot> MonsterSpots = Manager ? ReadList(Manager, SceneMonsters) : std::vector<Spot>{};
        LoadCatalog();
        std::vector<Spot> ItemSpots = Manager ? ReadList(Manager, SceneItems, true) : std::vector<Spot>{};
        std::erase_if(ItemSpots, [](const Spot& Item) { return !IsLoot(Item); });
        const std::vector<Spot> PlayerSpots = Manager ? ReadList(Manager, ScenePlayers) : std::vector<Spot>{};
        std::unordered_map<long, Spot> MonsterById;
        for (const Spot& Entry : MonsterSpots) MonsterById[Entry.Id] = Entry;
        const std::unordered_set<long> OnlyMe{Me.Id};

        for (Pool* Layer : Layers) Layer->Used = 0;
        const AtlasFrame DotFrame{MapImage::DotLeft, 0, MapImage::DotSize, MapImage::DotSize};
        const AtlasFrame DiamondFrame{MapImage::DiamondLeft, 0, MapImage::DotSize, MapImage::DotSize};
        DrawDots(Host, MonsterPool, MonsterSpots, NotMobs, View, DotFrame, MonsterShown, WithAlpha(MonsterTint, MarkAlpha));
        DrawDots(Host, ItemPool, ItemSpots, {}, View, DiamondFrame, ItemShown, WithAlpha(ItemTint, MarkAlpha));
        DrawDots(Host, PlayerPool, PlayerSpots, OnlyMe, View, DotFrame, PlayerShown, WithAlpha(PlayerTint, MarkAlpha));
        for (const Mark& Target : Marks) {
            float X = static_cast<float>(Target.X), Y = static_cast<float>(Target.Y);
            if (const auto Moving = MonsterById.find(Target.Id); Moving != MonsterById.end()) {
                X = Moving->second.X;
                Y = Moving->second.Y;
            }
            Draw(Host, Target.Boss ? BossPool : LeverPool, View.X(X), View.Y(Y), Target.Image, 512, 512,
                 Target.Frame, Target.Width, Target.Height, MarkShade);
        }
        for (Pool* Layer : Layers) {
            for (size_t i = Layer->Used; i < Layer->Widgets.size(); i++) Show(Layer->Widgets[i], false);
        }

        if (HaveMe) {
            float Heading = std::atan2(FacingX, -FacingY);
            if (Heading < 0.0f) Heading += 2.0f * Pi;
            const int Frame = static_cast<int>(std::lround(Heading / (Pi / 8.0f))) % MapImage::ArrowFrames;
            SetImage(Self, SheetId, SheetWidth, SheetHeight,
                     {static_cast<int16_t>(Frame * MapImage::ArrowSize), 0, MapImage::ArrowSize, MapImage::ArrowSize});
            Place(Self, static_cast<int>(std::lround(View.X(Me.X))) - ArrowShown / 2,
                  static_cast<int>(std::lround(View.Y(Me.Y))) - ArrowShown / 2, ArrowShown, ArrowShown);
            Tint(Self, MarkShade);
            Show(Self, true);
        } else {
            Show(Self, false);
        }
        Show(Container, true);
    }

    void Forget() {
        AttachedTo = nullptr;
        Container = nullptr;
        MapWidget = nullptr;
        Self = nullptr;
        for (Pool* Layer : Layers) *Layer = Pool{};
    }

    void Remove(const ModHost* Host) {
        if (Container && AttachedTo && AttachedTo->childrenList && AttachedTo->childrenList->index_of(Container) >= 0) {
            AttachedTo->childrenList->remove(Container);
            Container->parent = nullptr;
        }
        RestoreMinimaps(Host);
        Forget();
        Current = MapState{};
        GridDirty = true;
    }
}
