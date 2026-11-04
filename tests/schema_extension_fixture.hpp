#pragma once
#include "schema_fixture.hpp"
namespace cyuediff::test {
inline Bytes append_schema_extensions(Bytes file,
                                      const std::vector<std::pair<std::string, Bytes>> &extensions,
                                      bool legacy = false) {
    Bytes extra;
    extra.i(legacy ? 1 : 0x54584543);
    if (!legacy) {
        extra.put<std::uint8_t>(0);
        extra.i(static_cast<int>(extensions.size()));
        for (const auto &ext : extensions) {
            extra.data.insert(extra.data.end(), ext.first.begin(), ext.first.end());
            extra.i(static_cast<int>(ext.second.data.size()));
            extra.append(ext.second);
        }
    } else
        extra.append(extensions.at(0).second);
    file.append(extra);
    // These fixtures use USMAP v4 without version metadata.
    file.patch(8, static_cast<int>(file.data.size() - 16));
    file.patch(12, static_cast<int>(file.data.size() - 16));
    return file;
}
inline Bytes schema_ppth() {
    Bytes ext;
    ext.put<std::uint8_t>(0);
    ext.i(1);
    ext.i(20);
    ext.i(6);
    for (unsigned j = 0; j < 6; ++j)
        ext.i(20);
    return ext;
}
inline Bytes schema_eatr() {
    Bytes ext;
    ext.put<std::uint8_t>(0);
    ext.i(1);
    ext.i(3);
    ext.i(6);
    const unsigned counts[] = {1, 8, 32, 1, 1, 1};
    for (unsigned j = 0; j < 6; ++j) {
        ext.put<std::uint8_t>(j == 3 || j == 4 ? 1 : 2);
        ext.put<std::uint32_t>(0x80000000u + j);
        ext.i(static_cast<int>(counts[j]));
        for (unsigned k = 0; k < counts[j]; ++k)
            ext.put<std::uint64_t>(0x8000000000000000ull + k);
    }
    return ext;
}
inline Bytes schema_envp() {
    Bytes ext;
    ext.put<std::uint8_t>(0);
    ext.i(1);
    ext.i(2);
    ext.i(11);
    ext.put<std::int64_t>(-5);
    ext.i(12);
    ext.put<std::int64_t>(9223372036854775807ll);
    return ext;
}
inline Bytes schema_modl(unsigned modules = 1) {
    Bytes ext;
    ext.put<std::uint16_t>(static_cast<std::uint16_t>(modules));
    for (unsigned j = 0; j < modules; ++j) {
        auto name = "/Script/Module" + std::to_string(j);
        ext.put<std::uint8_t>(static_cast<std::uint8_t>(name.size()));
        ext.data.insert(ext.data.end(), name.begin(), name.end());
    }
    for (unsigned j = 0; j < 6; ++j) {
        if (modules > 255)
            ext.put<std::uint16_t>(static_cast<std::uint16_t>(modules - 1));
        else
            ext.put<std::uint8_t>(static_cast<std::uint8_t>(modules - 1));
    }
    return ext;
}
inline Bytes extended_schema_fixture() {
    return append_schema_extensions(
        schema_fixture(4, false, true),
        {{"PPTH", schema_ppth()}, {"EATR", schema_eatr()}, {"ENVP", schema_envp()}});
}
// Three records exercise homonyms, a qualified base, and nested/enum references.
inline Bytes ambiguous_schema_fixture(bool modules = true, bool ambiguous_base = false,
                                      bool duplicate_path = false, unsigned inheritance = 0) {
    std::vector<std::string> names = {"Twin",           "Value",  "/Script/A", "/Script/B",     "Child",
                                      "/Script/A.Twin", "Nested", "Mode",      "/Script/B.Twin"};
    Bytes body;
    body.i(static_cast<int>(names.size()));
    for (const auto &name : names) {
        body.put<std::uint16_t>(static_cast<std::uint16_t>(name.size()));
        body.data.insert(body.data.end(), name.begin(), name.end());
    }
    body.i(2);
    for (unsigned j = 0; j < 2; ++j) {
        body.i(7);
        body.put<std::uint16_t>(1);
        body.put<std::int64_t>(j + 7);
        body.i(1);
    }
    body.i(3);
    for (unsigned j = 0; j < 3; ++j) {
        body.i(j == 2 ? 4 : 0);
        int parent = j == 2 ? (ambiguous_base ? 0 : 5) : -1;
        if (j == 1 && inheritance)
            parent = 5;
        if (j == 0 && inheritance >= 2)
            parent = inheritance == 2 ? 5 : 8;
        body.i(parent);
        body.put<std::uint16_t>(j == 2 ? 0 : 1);
        body.put<std::uint16_t>(j == 2 ? 0 : 1);
        if (j < 2) {
            body.put<std::uint16_t>(0);
            body.put<std::uint8_t>(1);
            body.i(1);
            body.put<std::uint8_t>(j == 0 ? 2 : 1);
        }
    }
    Bytes file;
    file.put<std::uint16_t>(0x30c4);
    file.put<std::uint8_t>(4);
    file.i(0);
    file.put<std::uint8_t>(0);
    file.i(static_cast<int>(body.data.size()));
    file.i(static_cast<int>(body.data.size()));
    file.append(body);
    if (modules) {
        Bytes ext;
        ext.put<std::uint8_t>(0);
        ext.i(2);
        ext.i(2);
        ext.i(3);
        ext.i(3);
        ext.i(2);
        ext.i(duplicate_path ? 2 : 3);
        ext.i(3);
        file = append_schema_extensions(file, {{"PPTH", ext}});
    }
    return file;
}
} // namespace cyuediff::test
