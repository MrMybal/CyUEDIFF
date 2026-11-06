#include <cyuediff/content_hash.hpp>
#include "archive.hpp"
#include "blake3_prefix.h"
#include "../third_party/blake3/blake3.h"
#include <algorithm>
namespace cyuediff {
namespace {
std::string finish(blake3_hasher &state) {
    std::array<std::uint8_t, 32> bytes{};
    blake3_hasher_finalize(&state, bytes.data(), bytes.size());
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (auto byte : bytes) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}
} // namespace
std::string blake3_bytes(const std::vector<std::uint8_t> &bytes, const OperationControl &control) {
    control.checkpoint("Hashing BLAKE3", 0, bytes.size());
    blake3_hasher state;
    blake3_hasher_init(&state);
    for (std::size_t at = 0; at < bytes.size();) {
        const auto count = std::min<std::size_t>(bytes.size() - at, 1024 * 1024);
        blake3_hasher_update(&state, bytes.data() + at, count);
        at += count;
        control.checkpoint("Hashing BLAKE3", at, bytes.size());
    }
    control.check();
    return finish(state);
}
std::string blake3_file(const fs::path &file, const OperationControl &control) {
    ReadOptions options;
    options.control = control;
    detail::Archive input(file, options);
    control.checkpoint("Hashing BLAKE3", 0, input.size());
    blake3_hasher state;
    blake3_hasher_init(&state);
    std::vector<std::uint8_t> buffer(
        static_cast<std::size_t>(std::min<std::uint64_t>(input.size(), 1024 * 1024)));
    for (std::uint64_t at = 0; at < input.size();) {
        const auto count =
            static_cast<std::size_t>(std::min<std::uint64_t>(input.size() - at, buffer.size()));
        input.read(buffer.data(), count);
        blake3_hasher_update(&state, buffer.data(), count);
        at += count;
        control.checkpoint("Hashing BLAKE3", at, input.size());
    }
    control.check();
    return finish(state);
}
} // namespace cyuediff
