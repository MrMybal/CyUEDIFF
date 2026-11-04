#pragma once
#include "package_fixture.hpp"
#include <algorithm>
namespace cyuediff::test {
constexpr std::uint64_t script_root = 0x4000000000000001ull, script_class = 0x4000000000000002ull,
                        script_cdo = 0x4000000000000003ull;
inline void name_batch(Bytes &b, const std::vector<std::string> &names) {
    b.put<std::uint32_t>(static_cast<std::uint32_t>(names.size()));
    if (names.empty())
        return;
    std::size_t size = 0;
    for (const auto &name : names)
        size += name.size();
    b.put<std::uint32_t>(static_cast<std::uint32_t>(size));
    b.put<std::uint64_t>(0);
    for (std::size_t j = 0; j < names.size(); ++j)
        b.put<std::uint64_t>(0);
    for (const auto &name : names) {
        b.put<std::uint8_t>(static_cast<std::uint8_t>(name.size() >> 8));
        b.put<std::uint8_t>(static_cast<std::uint8_t>(name.size()));
    }
    for (const auto &name : names)
        b.data.insert(b.data.end(), name.begin(), name.end());
}
inline Bytes script_fixture(const std::string &class_name = "DemoAsset") {
    Bytes b;
    name_batch(b, {"/Script/Test", class_name, "Default__DemoAsset"});
    b.i(3);
    for (unsigned j = 0; j < 3; ++j) {
        b.put<std::uint32_t>(0x80000000u | j);
        b.i(0);
        b.put(script_root + j);
        b.put(j ? script_root : ~0ull);
        b.put<std::uint64_t>(~0ull);
    }
    return b;
}
inline Bytes zen_fixture(bool modern = false, bool versioned = false, bool reorder = false) {
    Bytes b;
    b.zeros(52);
    if (versioned) {
        b.i(3);
        b.i(522);
        b.i(modern ? 1013 : 1009);
        b.i(0);
        b.i(1);
        for (int i = 1; i <= 4; ++i)
            b.i(i);
        b.i(42);
    }
    name_batch(b, {"None", "/Game/Example", "Example", "Nested"});
    if (modern) {
        auto pad = (8 - (b.data.size() + 8) % 8) % 8;
        b.put<std::uint64_t>(pad);
        b.zeros(static_cast<int>(pad));
    }
    b.put<std::uint64_t>(32);
    b.put<std::int64_t>(0);
    b.put<std::int64_t>(-1);
    b.put<std::int64_t>(4);
    b.i(0);
    b.i(0);
    if (modern)
        b.zeros(static_cast<int>((8 - b.data.size() % 8) % 8));
    auto offset = [&](int index) { b.patch(24 + 4 * index, static_cast<int>(b.data.size())); };
    offset(0);
    b.put<std::uint64_t>(0x1234);
    offset(1);
    b.put(script_class);
    b.put<std::uint64_t>(0x8000000000000000ull);
    b.put<std::uint64_t>(reorder ? 0 : 1);
    b.put<std::uint64_t>(~0ull);
    offset(2);
    for (unsigned k = 0; k < 2; ++k) {
        unsigned j = reorder ? 1 - k : k;
        b.put<std::uint64_t>(j ? 4 : 0);
        b.put<std::uint64_t>(2);
        b.i(j ? 3 : 2);
        b.i(0);
        b.put<std::uint64_t>(j ? (reorder ? 1 : 0) : ~0ull);
        b.put(script_class);
        b.put<std::uint64_t>(~0ull);
        b.put(script_cdo);
        b.put<std::uint64_t>(0x100 + j);
        b.i(3);
        b.i(0);
    }
    offset(3);
    for (unsigned j = 0; j < 2; ++j) {
        b.i(j);
        b.i(0);
    }
    for (unsigned j = 0; j < 2; ++j) {
        b.i(j);
        b.i(1);
    }
    offset(4);
    for (unsigned k = 0; k < 2; ++k) {
        unsigned j = reorder ? 1 - k : k;
        b.i(0);
        b.i(j ? 1 : 0);
        b.i(0);
        b.i(0);
        b.i(j ? 1 : 0);
    }
    offset(5);
    b.i(-2);
    b.i(reorder ? 2 : 1);
    offset(6);
    name_batch(b, {"/Game/Referenced"});
    b.i(0);
    auto header = b.data.size();
    b.patch(0, versioned ? 1 : 0);
    b.patch(4, static_cast<int>(header));
    b.patch(8, 1);
    b.patch(12, 0);
    b.patch(16, 0x2000);
    b.patch(20, 2048);
    for (char c : std::string("ab..cdTAIL"))
        b.put(c);
    return b;
}
inline Bytes zen_toc(const Bytes &payload, std::uint8_t type = 1) {
    Bytes b;
    b.zeros(144);
    std::string magic = "-==--==--==--==-";
    std::copy(magic.begin(), magic.end(), b.data.begin());
    b.data[16] = 5;
    b.patch(20, 144);
    b.patch(24, 1);
    b.patch(28, 1);
    b.patch(32, 12);
    b.patch(44, 65536);
    b.patch(52, 1);
    std::uint64_t partition = ~0ull;
    std::memcpy(b.data.data() + 88, &partition, 8);
    b.zeros(11);
    b.put<std::uint8_t>(type);
    b.zeros(5);
    for (int j = 4; j >= 0; --j)
        b.put<std::uint8_t>(static_cast<std::uint8_t>(std::uint64_t(payload.data.size()) >> (8 * j)));
    b.zeros(5);
    for (unsigned repeat = 0; repeat < 2; ++repeat)
        for (unsigned j = 0; j < 3; ++j)
            b.put<std::uint8_t>(static_cast<std::uint8_t>(payload.data.size() >> (8 * j)));
    b.put<std::uint8_t>(0);
    b.zeros(33);
    return b;
}
} // namespace cyuediff::test
