#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

struct TLBSWidget;

namespace Memory {
    bool Copy(void* Out, uintptr_t Address, size_t Size);

    template <typename T>
    bool Read(const uintptr_t Address, T& Out) {
        return Address >= 0x10000 && Copy(&Out, Address, sizeof(T));
    }

    bool IsClass(const void* Object, const char* Name);

    class RootLookup {
    public:
        explicit RootLookup(const char* ClassName) : Class(ClassName) {}
        TLBSWidget* First(const TLBSWidget* Root);
        const std::vector<TLBSWidget*>& All(const TLBSWidget* Root);

    private:
        void Update(const TLBSWidget* Root);
        const char* Class;
        const TLBSWidget* SeenRoot = nullptr;
        uint32_t SeenCount = UINT32_MAX;
        const void* SeenList = nullptr;
        std::vector<TLBSWidget*> Found;
    };
}
