#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

const unsigned char* desktopThornBank()
{
    static const auto* bytes = []() -> const unsigned char* {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&desktopThornBank),&module))
            return nullptr;
        const auto resource=FindResourceW(module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10)); // RT_RCDATA
        if (!resource || SizeofResource(module,resource)!=SLINK_THORN_BANK_SIZE) return nullptr;
        return static_cast<const unsigned char*>(LockResource(LoadResource(module,resource)));
    }();
    return bytes;
}
