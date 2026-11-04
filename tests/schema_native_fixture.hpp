#pragma once
#include "schema_collection_fixture.hpp"
#include <fstream>
#include <limits>
namespace cyuediff::test {
inline Bytes native_transform_values(bool changed = false, bool invalid = false) {
    Bytes payload;
    for (double value : {0., 0., 0., 1., 1000000000000.125, changed ? 99.5 : 2.5,
                         invalid ? std::numeric_limits<double>::infinity() : 3.5, 1., 1., 1.})
        payload.put<double>(value);
    return collection_single(payload);
}
inline void write_native_fixtures(const fs::path &root) {
    fs::create_directories(root);
    auto mapping = collection_schema_fixture(collection_struct());
    mapping.save(root / "native.usmap");
    for (const auto &name : {"native", "changed", "truncated", "invalid", "native55", "changed55"}) {
        const std::string stem = name;
        auto values = native_transform_values(stem.find("changed") == 0, stem == "invalid");
        if (stem == "truncated")
            values.data.pop_back();
        auto package = collection_zen_fixture(values, stem.find("55") != std::string::npos);
        package.save(root / (stem + ".ucas"));
        zen_toc(package).save(root / (stem + ".utoc"));
    }
    auto global = script_fixture();
    global.save(root / "global.ucas");
    zen_toc(global, 5).save(root / "global.utoc");
    Json manifest = {{"format", "cyuediff.zen-schema/2"},
                     {"profile", "ue5.3"},
                     {"usmap", "native.usmap"},
                     {"sha256", sha256_bytes(mapping.data)},
                     {"bindings", {{"/Script/Test.DemoAsset", {{"schema", "Collections"}, {"offset", 0}}}}},
                     {"reflectedStructs", Json::array()},
                     {"nativeStructs", {{"/Script/Test.Record", "transform-f64"}}}};
    auto save = [&](const char *name) {
        std::ofstream file(root / name, std::ios::binary);
        file << manifest.dump(2) << '\n';
        if (!file)
            throw std::runtime_error("Cannot write native schema fixture");
    };
    save("native.json");
    manifest["profile"] = "ue5.5";
    save("native55.json");
    manifest["profile"] = "ue5.3";
    manifest["format"] = "cyuediff.zen-schema/1";
    manifest.erase("nativeStructs");
    save("unbound.json");
}
} // namespace cyuediff::test
