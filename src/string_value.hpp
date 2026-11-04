#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff::detail {
// Validate stored bytes without conversion, replacement or a temporary JSON dump.
inline void validate_byte_string(const std::string &value, bool ascii, const OperationControl &control,
                                 const char *code, std::uint64_t offset) {
    for (std::size_t i = 0; i < value.size();) {
        if ((i & 4095) == 0)
            control.check();
        auto byte = [&](std::size_t at) { return static_cast<unsigned char>(value[at]); };
        const auto first = byte(i);
        if (first < 128) {
            ++i;
            continue;
        }
        if (ascii)
            throw Error(code, "Non-ASCII ANSI string requires an explicit character-set decoder", offset + i);
        unsigned length = first >= 0xc2 && first <= 0xdf   ? 2
                          : first >= 0xe0 && first <= 0xef ? 3
                          : first >= 0xf0 && first <= 0xf4 ? 4
                                                           : 0;
        if (!length || length > value.size() - i)
            throw Error(code, "Invalid UTF-8 byte string", offset + i);
        std::uint32_t cp = first & (0x7f >> length);
        for (unsigned j = 1; j < length; ++j) {
            const auto next = byte(i + j);
            if ((next & 0xc0) != 0x80)
                throw Error(code, "Invalid UTF-8 continuation", offset + i + j);
            cp = (cp << 6) | (next & 63);
        }
        if (cp < (length == 2   ? 0x80u
                  : length == 3 ? 0x800u
                                : 0x10000u) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            throw Error(code, "Invalid UTF-8 code point", offset + i);
        i += length;
    }
    control.check();
}
} // namespace cyuediff::detail
