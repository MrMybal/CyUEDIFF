#pragma once
#include "zen_fixture.hpp"
namespace cyuediff::test {
inline Bytes collection_type(unsigned kind) {
    Bytes t;
    t.put<std::uint8_t>(static_cast<std::uint8_t>(kind));
    return t;
}
inline Bytes collection_type(unsigned kind, const Bytes &inner) {
    auto t = collection_type(kind);
    t.append(inner);
    return t;
}
inline Bytes map_type(const Bytes &key, const Bytes &value) {
    auto t = collection_type(24, key);
    t.append(value);
    return t;
}
inline Bytes collection_struct() {
    auto t = collection_type(9);
    t.i(11);
    return t;
}
inline Bytes collection_enum() {
    auto t = collection_type(26, collection_type(0));
    t.i(7);
    return t;
}
inline Bytes collection_schema_fixture(Bytes lookup = {}, Bytes tags = {}) {
    if (lookup.data.empty())
        lookup = map_type(collection_type(5), collection_type(2));
    if (tags.data.empty())
        tags = collection_type(25, collection_type(2));
    const std::vector<std::string> names = {"Collections", "Lookup", "Tags",         "After",
                                            "Item",        "Code",   "Native",       "Mode",
                                            "Off",         "On",     "/Script/Test", "Record"};
    Bytes payload;
    payload.i(static_cast<int>(names.size()));
    for (const auto &n : names) {
        payload.put<std::uint8_t>(static_cast<std::uint8_t>(n.size()));
        payload.data.insert(payload.data.end(), n.begin(), n.end());
    }
    payload.i(1);
    payload.i(7);
    payload.put<std::uint8_t>(2);
    payload.i(8);
    payload.i(9);
    payload.i(2);
    auto schema = [&](int name, unsigned count) {
        payload.i(name);
        payload.i(-1);
        payload.put<std::uint16_t>(static_cast<std::uint16_t>(count));
        payload.put<std::uint16_t>(static_cast<std::uint16_t>(count));
    };
    auto field = [&](unsigned index, int name, const Bytes &type) {
        payload.put<std::uint16_t>(static_cast<std::uint16_t>(index));
        payload.put<std::uint8_t>(1);
        payload.i(name);
        payload.append(type);
    };
    schema(0, 3);
    field(0, 1, lookup);
    field(1, 2, tags);
    field(2, 3, collection_type(2));
    schema(11, 1);
    field(0, 5, collection_type(2));
    Bytes paths;
    paths.put<std::uint8_t>(0);
    paths.i(1);
    paths.i(10);
    paths.i(2);
    paths.i(10);
    paths.i(10);
    payload.i(0x54584543);
    payload.put<std::uint8_t>(0);
    payload.i(1);
    for (char c : std::string("PPTH"))
        payload.put(c);
    payload.i(static_cast<int>(paths.data.size()));
    payload.append(paths);
    Bytes file;
    file.put<std::uint16_t>(0x30c4);
    file.put<std::uint8_t>(0);
    file.put<std::uint8_t>(0);
    file.i(static_cast<int>(payload.data.size()));
    file.i(static_cast<int>(payload.data.size()));
    file.append(payload);
    return file;
}
inline void collection_name(Bytes &b, unsigned index) {
    b.i(static_cast<int>(index));
    b.i(0);
}
inline Bytes collection_values(bool reorder = false, int changed = 20) {
    Bytes b;
    b.put<std::uint16_t>(0x0700);
    b.i(1);
    collection_name(b, 3);
    b.i(2);
    for (unsigned j = 0; j < 2; ++j) {
        auto k = reorder ? 1 - j : j;
        collection_name(b, k ? 0 : 2);
        b.i(k ? changed : 10);
    }
    b.i(1);
    b.i(99);
    b.i(2);
    b.i(reorder ? 5 : 3);
    b.i(reorder ? 3 : 5);
    b.i(123);
    return b;
}
inline Bytes collection_single(Bytes payload, bool set = false) {
    Bytes body;
    body.put<std::uint16_t>(static_cast<std::uint16_t>(0x0300 | (set ? 1 : 0)));
    body.append(payload);
    return body;
}
inline Bytes collection_zen_fixture(const Bytes &values, bool modern = false, bool versioned = false) {
    auto b = zen_fixture(modern, versioned);
    auto u32 = [&](std::size_t pos) {
        unsigned n;
        std::memcpy(&n, b.data.data() + pos, 4);
        return n;
    };
    auto header = u32(4), exports = u32(32);
    b.data.resize(header);
    b.append(values);
    b.put<std::uint16_t>(0x0100);
    b.patch(exports + 8, static_cast<int>(values.data.size()));
    b.patch(exports + 72, static_cast<int>(values.data.size()));
    return b;
}
} // namespace cyuediff::test
