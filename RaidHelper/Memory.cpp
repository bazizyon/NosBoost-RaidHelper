#define NOMINMAX
#include <windows.h>

#include "Memory.h"
#include "TLBSWidget.h"

namespace Memory {
    namespace {
        constexpr ptrdiff_t VmtClassName = -0x2C;
    }

    bool Copy(void* Out, const uintptr_t Address, const size_t Size) {
        __try {
            std::memcpy(Out, reinterpret_cast<const void*>(Address), Size);
            return true;
        } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            return false;
        }
    }

    bool IsClass(const void* Object, const char* Name) {
        uintptr_t VTable = 0, NamePtr = 0;
        uint8_t Length = 0;
        if (!Read(reinterpret_cast<uintptr_t>(Object), VTable)) return false;
        if (!Read(VTable + VmtClassName, NamePtr) || !Read(NamePtr, Length)) return false;
        char Text[256];
        return Length == std::strlen(Name) && Copy(Text, NamePtr + 1, Length) && std::memcmp(Text, Name, Length) == 0;
    }

    void RootLookup::Update(const TLBSWidget* Root) {
        const TLBSWidgetList* List = Root ? Root->childrenList : nullptr;
        const uint32_t Count = List ? List->count : 0;
        if (Root == SeenRoot && List == SeenList && Count == SeenCount) return;
        SeenRoot = Root;
        SeenList = List;
        SeenCount = Count;
        Found.clear();
        for (uint32_t i = 0; i < Count; i++) {
            TLBSWidget* Child = List->list[i];
            if (Child && IsClass(Child, Class)) Found.push_back(Child);
        }
    }

    TLBSWidget* RootLookup::First(const TLBSWidget* Root) {
        Update(Root);
        return Found.empty() ? nullptr : Found.front();
    }

    const std::vector<TLBSWidget*>& RootLookup::All(const TLBSWidget* Root) {
        Update(Root);
        return Found;
    }
}
