#pragma once
#include "schema_collection_fixture.hpp"
#include <fstream>
namespace cyuediff::test {
inline Bytes optional_value(const Bytes &inner = {}, bool present = true) {
    Bytes value;
    value.i(present ? 1 : 0);
    if (present)
        value.append(inner);
    return value;
}
inline Bytes field_path_value(const std::vector<int> &names = {3, 2}, int owner = 1) {
    Bytes value;
    value.i(static_cast<int>(names.size()));
    for (int name : names) {
        value.i(name);
        value.i(0);
    }
    value.i(owner);
    return value;
}
inline Bytes mapped_path_values(const std::string &mode = "mapped") {
    Bytes bytes;
    if (mode == "zero") {
        bytes.put<std::uint16_t>(0x0780);
        bytes.put<std::uint8_t>(7);
        return bytes;
    }
    bytes.put<std::uint16_t>(0x0700);
    const bool changed = mode == "changed", unset = mode == "unset";
    bytes.append(optional_value(
        field_path_value(changed ? std::vector<int>{2, 2} : std::vector<int>{3, 2}, mode == "set-null" ? 0
                                                                                    : changed          ? 2
                                                                                                       : 1),
        !unset));
    bytes.append(
        field_path_value(unset ? std::vector<int>{} : std::vector<int>{changed ? 3 : 2}, unset ? 0 : 1));
    Bytes number;
    number.i(mode == "set-null" ? 0 : changed ? 18 : 17);
    bytes.append(optional_value(number, !unset));
    if (mode == "bad-marker")
        bytes.patch(2, 2);
    if (mode == "bad-name")
        bytes.patch(10, 999);
    if (mode == "bad-owner")
        bytes.patch(26, 999);
    if (mode == "truncated")
        bytes.data.pop_back();
    return bytes;
}
inline void write_path_fixtures(const fs::path &root) {
    fs::create_directories(root);
    auto mappings = collection_schema_fixture(collection_type(28, collection_type(27)), collection_type(27),
                                              collection_type(28, collection_type(2)));
    mappings.save(root / "paths.usmap");
    auto fields = collection_schema_fixture(collection_type(27));
    fields.save(root / "field.usmap");
    for (const char *name :
         {"mapped", "changed", "unset", "set-null", "zero", "bad-marker", "bad-name", "bad-owner",
          "truncated", "old-release", "old-fortnite", "optional53", "paths53"}) {
        const std::string mode = name;
        const bool legacy = mode == "old-release" || mode == "old-fortnite";
        const bool modern = mode != "optional53" && mode != "paths53";
        auto values = mode == "paths53" ? collection_single(field_path_value()) : mapped_path_values(mode);
        auto package = collection_zen_fixture(values, modern, legacy);
        if (legacy) {
            const std::uint32_t release[] = {0x9c54d522, 0xa8264fbe, 0x94210746, 0x61b482d0};
            const std::uint32_t fortnite[] = {0x601d1886, 0xac644f84, 0xaa16d3de, 0x0deac7d6};
            const auto *words = mode == "old-release" ? release : fortnite;
            for (unsigned i = 0; i < 4; ++i)
                std::memcpy(package.data.data() + 72 + i * 4, &words[i], 4);
            package.patch(88, mode == "old-release" ? 29 : 32);
        }
        package.save(root / (mode + ".ucas"));
        zen_toc(package).save(root / (mode + ".utoc"));
    }
    auto global = script_fixture();
    global.save(root / "global.ucas");
    zen_toc(global, 5).save(root / "global.utoc");
    Json manifest = {{"format", "cyuediff.zen-schema/2"},
                     {"profile", "ue5.5"},
                     {"usmap", "paths.usmap"},
                     {"sha256", sha256_bytes(mappings.data)},
                     {"bindings", {{"/Script/Test.DemoAsset", {{"schema", "Collections"}, {"offset", 0}}}}},
                     {"nativeStructs", Json::object()}};
    auto save = [&](const char *name) {
        std::ofstream file(root / name, std::ios::binary);
        file << manifest.dump(2) << '\n';
        if (!file)
            throw std::runtime_error("Cannot write mapped field path fixture");
    };
    save("paths.json");
    manifest["profile"] = "ue5.3";
    save("optional53.json");
    manifest["usmap"] = "field.usmap";
    manifest["sha256"] = sha256_bytes(fields.data);
    save("field53.json");
}
} // namespace cyuediff::test
