#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>

static const void* bitmapResource(int id, DWORD bytes)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&bitmapResource), &module)) return nullptr;
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!resource || SizeofResource(module, resource) != bytes) return nullptr;
    return LockResource(LoadResource(module, resource));
}

const unsigned char* desktopThornBank()
{
    static const auto* bytes = static_cast<const unsigned char*>(bitmapResource(101, SLINK_THORN_BANK_SIZE));
    return bytes;
}

const uint32_t* desktopWordmark()
{
    static const auto* pixels = static_cast<const uint32_t*>(bitmapResource(102, 680 * 108 * sizeof(uint32_t)));
    return pixels;
}

const uint8_t* desktopFooterBank()
{
    static const auto* bytes = static_cast<const uint8_t*>(bitmapResource(103, SLINK_FOOTER_BANK_SIZE));
    return bytes;
}
