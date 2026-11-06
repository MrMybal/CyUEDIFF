#include <cyuediff/string_encoding.hpp>
#include "string_value.hpp"
namespace cyuediff {
const char *narrow_string_encoding_name(NarrowStringEncoding encoding) {
    switch (encoding) {
    case NarrowStringEncoding::unspecified:
        return "unspecified";
    case NarrowStringEncoding::ascii:
        return "ascii";
    case NarrowStringEncoding::latin1:
        return "latin1";
    case NarrowStringEncoding::windows_1252:
        return "windows-1252";
    case NarrowStringEncoding::utf8:
        return "utf8";
    }
    throw Error("string-encoding", "Unsupported narrow string encoding");
}
NarrowStringEncoding parse_narrow_string_encoding(const std::string &value) {
    for (auto encoding : {NarrowStringEncoding::ascii, NarrowStringEncoding::latin1,
                          NarrowStringEncoding::windows_1252, NarrowStringEncoding::utf8})
        if (value == narrow_string_encoding_name(encoding))
            return encoding;
    throw Error("string-encoding", "Expected ascii, latin1, windows-1252 or utf8");
}
std::string decode_narrow_string(const std::string &value, NarrowStringEncoding encoding,
                                 std::uint64_t maximum, const OperationControl &control) {
    control.check();
    if (encoding == NarrowStringEncoding::unspecified)
        throw Error("string-encoding", "A narrow string encoding must be selected explicitly");
    narrow_string_encoding_name(encoding);
    if (value.size() > maximum)
        throw Error("string-limit", "Decoded narrow string exceeds limit");
    if (encoding == NarrowStringEncoding::ascii || encoding == NarrowStringEncoding::utf8) {
        detail::validate_byte_string(value, encoding == NarrowStringEncoding::ascii, control,
                                     "string-encoding", 0);
        return value;
    }
    constexpr std::uint16_t cp1252[] = {0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                                        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017d, 0,
                                        0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178};
    auto codepoint = [&](unsigned char byte) -> std::uint32_t {
        if (encoding == NarrowStringEncoding::windows_1252 && byte >= 0x80 && byte <= 0x9f) {
            auto cp = cp1252[byte - 0x80];
            if (!cp)
                throw Error("string-encoding", "Undefined Windows-1252 code point");
            return cp;
        }
        return byte;
    };
    std::uint64_t size = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if ((i & 4095) == 0)
            control.check();
        auto cp = codepoint(static_cast<unsigned char>(value[i]));
        auto bytes = cp < 0x80 ? 1u : cp < 0x800 ? 2u : 3u;
        if (bytes > maximum - size)
            throw Error("string-limit", "Decoded narrow string exceeds limit");
        size += bytes;
    }
    if (size > std::string{}.max_size())
        throw Error("string-limit", "Decoded string cannot be represented");
    std::string result;
    result.reserve(static_cast<std::size_t>(size));
    for (std::size_t i = 0; i < value.size(); ++i) {
        if ((i & 4095) == 0)
            control.check();
        auto cp = codepoint(static_cast<unsigned char>(value[i]));
        if (cp < 0x80)
            result += char(cp);
        else if (cp < 0x800) {
            result += char(0xc0 | (cp >> 6));
            result += char(0x80 | (cp & 63));
        } else {
            result += char(0xe0 | (cp >> 12));
            result += char(0x80 | ((cp >> 6) & 63));
            result += char(0x80 | (cp & 63));
        }
    }
    control.check();
    return result;
}
} // namespace cyuediff
