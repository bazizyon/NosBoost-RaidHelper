#define NOMINMAX
#include <windows.h>
#include <cstring>

#include "GroundEffects.h"

namespace GroundEffects {
    namespace {
        bool Tried = false;
        uintptr_t ManagerGlobal = 0;
        uintptr_t SpawnFn = 0;
        uintptr_t RemoveFn = 0;
        float CellScale = 0.0f;
        float CellOffset = 0.0f;

        struct WorldPosition {
            float X;
            float Height;
            float Y;
        };

        uintptr_t Scan(const uint8_t* Pattern, const char* Mask, const uintptr_t From, const uintptr_t To) {
            const size_t Length = strlen(Mask);
            if (To < From + Length) return 0;
            for (uintptr_t At = From; At <= To - Length; At++) {
                size_t i = 0;
                for (; i < Length; i++) {
                    if (Mask[i] != '?' && *reinterpret_cast<const uint8_t*>(At + i) != Pattern[i]) break;
                }
                if (i == Length) return At;
            }
            return 0;
        }

        uint32_t Read32(const uintptr_t At) { return *reinterpret_cast<const uint32_t*>(At); }
        uintptr_t CallTarget(const uintptr_t CallAt) { return CallAt + 5 + static_cast<int32_t>(Read32(CallAt + 1)); }

        bool Locate() {
            const auto Base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Base);
            const auto* Nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(Base + Dos->e_lfanew);
            const uintptr_t End = Base + Nt->OptionalHeader.SizeOfImage;

            constexpr uint8_t NameP[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x05, 0x00, 0x00, 0x00, 'e', 'f', 'f', '_', 'g', 0x00};
            const uintptr_t Name = Scan(NameP, "xxxxxxxxxxxxxx", Base, End);
            if (!Name) return false;
            const uint32_t Text = static_cast<uint32_t>(Name + 8);

            uint8_t RegisterP[10] = {0x68, 0, 0, 0, 0, 0xBA};
            std::memcpy(RegisterP + 6, &Text, 4);
            const uintptr_t Register = Scan(RegisterP, "x????xxxxx", Base, End);
            if (!Register) return false;
            const uintptr_t Handler = Read32(Register + 1);
            if (Handler < Base || Handler >= End) return false;

            constexpr uint8_t RemoveP[] = {0xA1, 0, 0, 0, 0, 0x8B, 0x00, 0x8B, 0x55, 0xFC, 0xE8, 0, 0, 0, 0, 0xE9};
            const uintptr_t RemoveAt = Scan(RemoveP, "x????xxxxxx????x", Handler, Handler + 0x300);
            constexpr uint8_t SpawnP[] = {0x8D, 0x45, 0xF0, 0x50, 0xA1, 0, 0, 0, 0, 0x8B, 0x00, 0x8B, 0xCE, 0x8B, 0x55, 0xFC, 0xE8};
            const uintptr_t SpawnAt = Scan(SpawnP, "xxxxx????xxxxxxxx", Handler, Handler + 0x300);
            constexpr uint8_t ConvertP[] = {0x33, 0xD2, 0xE8, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x83, 0xC4, 0xFC, 0xD9, 0x1C, 0x24};
            const uintptr_t ConvertAt = Scan(ConvertP, "xxx????x????xxxxxx", Handler, Handler + 0x300);
            if (!RemoveAt || !SpawnAt || !ConvertAt) return false;
            if (Read32(RemoveAt + 1) != Read32(SpawnAt + 5)) return false;

            const uintptr_t Convert = CallTarget(ConvertAt + 7);
            constexpr uint8_t ScaleP[] = {0xDF, 0x44, 0x24, 0x04, 0xD8, 0x0D, 0, 0, 0, 0, 0xD8, 0x05};
            const uintptr_t ScaleAt = Scan(ScaleP, "xxxxxx????xx", Convert, Convert + 0x20);
            if (!ScaleAt) return false;

            ManagerGlobal = Read32(RemoveAt + 1);
            RemoveFn = CallTarget(RemoveAt + 10);
            SpawnFn = CallTarget(SpawnAt + 16);
            CellScale = *reinterpret_cast<const float*>(Read32(ScaleAt + 6));
            CellOffset = *reinterpret_cast<const float*>(Read32(ScaleAt + 12));
            return true;
        }

        uintptr_t Manager() {
            if (!ManagerGlobal) return 0;
            const uintptr_t Holder = Read32(ManagerGlobal);
            return Holder ? Read32(Holder) : 0;
        }

        __declspec(noinline) void CallSpawn(uintptr_t Function, uintptr_t Target, uint32_t Handle, int EffectId,
                                            const WorldPosition* Where) {
            _asm {
                push Where
                mov ecx, EffectId
                mov edx, Handle
                mov eax, Target
                call Function
            }
        }

        __declspec(noinline) void CallRemove(uintptr_t Function, uintptr_t Target, uint32_t Handle) {
            _asm {
                mov edx, Handle
                mov eax, Target
                call Function
            }
        }

        bool GuardedSpawn(const uintptr_t Target, const uint32_t Handle, const int EffectId, const WorldPosition* Where) {
            __try {
                CallSpawn(SpawnFn, Target, Handle, EffectId, Where);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        void GuardedRemove(const uintptr_t Target, const uint32_t Handle) {
            __try {
                CallRemove(RemoveFn, Target, Handle);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    bool Init() {
        if (!Tried) {
            Tried = true;
            __try {
                if (!Locate()) ManagerGlobal = SpawnFn = RemoveFn = 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ManagerGlobal = SpawnFn = RemoveFn = 0;
            }
        }
        return Ready();
    }

    bool Ready() {
        return ManagerGlobal && SpawnFn && RemoveFn && CellScale != 0.0f;
    }

    bool Spawn(const uint32_t Handle, const int EffectId, const int CellX, const int CellY) {
        if (!Ready() || EffectId <= 0) return false;
        const uintptr_t Target = Manager();
        if (!Target) return false;
        const WorldPosition Where{CellX * CellScale + CellOffset, 0.0f, CellY * CellScale + CellOffset};
        return GuardedSpawn(Target, Handle, EffectId, &Where);
    }

    void Remove(const uint32_t Handle) {
        if (!Ready()) return;
        const uintptr_t Target = Manager();
        if (Target) GuardedRemove(Target, Handle);
    }
}
