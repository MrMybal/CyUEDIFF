#pragma once
#include "zen_fixture.hpp"
namespace cyuediff::test {
inline Bytes schema_fixture(unsigned version = 4, bool versioning = false) {
    std::vector<std::string> names = {"Base",     "DemoAsset", "Enabled", "Health",      "Values",
                                      "Target",   "Label",     "Title",   "Samples",     "Mode",
                                      "ModeEnum", "Off",       "On",      "Wide",        "Item",
                                      "Nested",   "Inner",     "Record",  "Unsupported", "Text"};
    Bytes body;
    body.i(static_cast<int>(names.size()));
    for (const auto &n : names) {
        if (version >= 2)
            body.put<std::uint16_t>(static_cast<std::uint16_t>(n.size()));
        else
            body.put<std::uint8_t>(static_cast<std::uint8_t>(n.size()));
        body.data.insert(body.data.end(), n.begin(), n.end());
    }
    body.i(1);
    body.i(10);
    if (version >= 3)
        body.put<std::uint16_t>(2);
    else
        body.put<std::uint8_t>(2);
    if (version >= 4)
        body.put<std::int64_t>(0);
    body.i(11);
    if (version >= 4)
        body.put<std::int64_t>(7);
    body.i(12);
    body.i(6);
    auto schema = [&](int name, int base, unsigned slots, unsigned props) {
        body.i(name);
        body.i(base);
        body.put<std::uint16_t>(static_cast<std::uint16_t>(slots));
        body.put<std::uint16_t>(static_cast<std::uint16_t>(props));
    };
    auto property = [&](unsigned index, unsigned dim, int name, unsigned type) {
        body.put<std::uint16_t>(static_cast<std::uint16_t>(index));
        body.put<std::uint8_t>(static_cast<std::uint8_t>(dim));
        body.i(name);
        body.put<std::uint8_t>(static_cast<std::uint8_t>(type));
    };
    schema(0, -1, 1, 1);
    property(0, 1, 2, 1);
    schema(1, 0, 8, 7);
    property(0, 1, 3, 2);
    property(1, 1, 4, 8);
    body.put<std::uint8_t>(22);
    property(2, 1, 5, 4);
    property(3, 1, 6, 5);
    property(4, 1, 7, 10);
    property(5, 2, 8, 3);
    property(7, 1, 9, 26);
    body.put<std::uint8_t>(0);
    body.i(10);
    schema(13, -1, 32, 1);
    property(0, 32, 14, 2);
    schema(15, -1, 1, 1);
    property(0, 1, 16, 2);
    schema(17, -1, 1, 1);
    property(0, 1, 16, 9);
    body.i(15);
    schema(18, -1, 1, 1);
    property(0, 1, 19, 11);
    Bytes file;
    file.put<std::uint16_t>(0x30c4);
    file.put<std::uint8_t>(static_cast<std::uint8_t>(version));
    if (version >= 1) {
        file.i(versioning ? 1 : 0);
        if (versioning) {
            file.i(522);
            file.i(1009);
            file.i(1);
            file.zeros(16);
            file.i(42);
            file.i(123);
        }
    }
    file.put<std::uint8_t>(0);
    file.i(static_cast<int>(body.data.size()));
    file.i(static_cast<int>(body.data.size()));
    file.append(body);
    return file;
}
inline Bytes schema_values(int health = 42) {
    Bytes b;
    b.put<std::uint16_t>((9u << 9) | 256u | 128u); // nine present values, mixed zero mask
    b.put<std::uint16_t>((1u << 5) | (1u << 7));   // Samples[0] and Mode omitted as serialized zero
    b.i(health);
    b.i(2);
    b.put<std::int16_t>(-12);
    b.put<std::int16_t>(34);
    b.i(-1);
    b.i(2);
    b.i(3); // import 0; name Example_2
    b.i(-4);
    b.put<std::uint16_t>('A');
    b.put<std::uint16_t>(0xd83d);
    b.put<std::uint16_t>(0xde00);
    b.put<std::uint16_t>(0);
    b.put<float>(2.5f);
    b.put<std::uint8_t>(1);
    return b;
}
inline Bytes schema_zen_fixture(int health = 42) {
    auto b = zen_fixture();
    auto read = [&](std::size_t at) {
        unsigned n;
        std::memcpy(&n, b.data.data() + at, 4);
        return n;
    };
    auto header = read(4), exports = read(32);
    b.data.resize(header);
    auto values = schema_values(health);
    b.append(values);
    b.put<std::uint16_t>(0x100);
    b.patch(exports + 8, static_cast<int>(values.data.size()));
    b.patch(exports + 72, static_cast<int>(values.data.size()));
    return b;
}
} // namespace cyuediff::test
