#include <cyuediff/core.hpp>
#include <memory>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace cyuediff {
BlockDecoder load_block_decoder(const fs::path &module) {
#ifdef _WIN32
    std::error_code error;
    auto path = fs::canonical(module, error);
    if (error)
        throw Error("codec-load", "Cannot resolve codec module path (filesystem error " +
                                      std::to_string(error.value()) + ")");
    auto handle = LoadLibraryExW(path.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!handle)
        throw Error("codec-load",
                    "Cannot load codec module (Windows error " + std::to_string(GetLastError()) + ")");
    auto owned = std::shared_ptr<void>(handle, [](void *p) { FreeLibrary(static_cast<HMODULE>(p)); });
    using Decode =
        std::size_t (*)(std::uint32_t, const std::uint8_t *, std::size_t, std::uint8_t *, std::size_t);
    auto decode = reinterpret_cast<Decode>(GetProcAddress(handle, "cyuediff_decode_block_v1"));
    if (!decode)
        throw Error("codec-abi", "Module does not export cyuediff_decode_block_v1");
    return [owned, decode](std::uint32_t method, const std::uint8_t *input, std::size_t size,
                           std::uint8_t *output,
                           std::size_t capacity) { return decode(method, input, size, output, capacity); };
#else
    (void)module;
    throw Error("codec-platform", "Dynamic codec loading currently requires Windows");
#endif
}
} // namespace cyuediff
