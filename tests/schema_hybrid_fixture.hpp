#pragma once
#include "schema_collection_fixture.hpp"
#include <cyuediff/schema_context.hpp>
#include <fstream>
namespace cyuediff::test {
inline Json hybrid_field(const char *name, Json value) {
    return {{"name", name}, {"value", std::move(value)}};
}
inline Json hybrid_layouts() {
    Json tether = {{"type", "record"},
                   {"fields", Json::array({hybrid_field("anchor", {{"type", "i32"}}),
                                           hybrid_field("particle", {{"type", "i32"}}),
                                           hybrid_field("length", {{"type", "f32"}})})}};
    Json batch = {{"type", "array"}, {"count", "i32"}, {"element", tether}};
    return {{"format", "cyuediff.binary-layouts/1"},
            {"layouts",
             {{"Tethers",
               {{"type", "record"},
                {"fields", Json::array({hybrid_field(
                               "batches", {{"type", "array"}, {"count", "i32"}, {"element", batch}})})}}},
              {"Word", {{"type", "i32"}}}}}};
}
inline Bytes hybrid_mapping(bool array = false) {
    return collection_schema_fixture(array ? collection_type(8, collection_struct()) : collection_struct(),
                                     {}, {}, true);
}
inline Bytes hybrid_record(int code = 17, float length = 2.5f, bool empty_prefix = false) {
    Bytes b;
    b.put<std::uint16_t>(empty_prefix ? 0x0100 : 0x0300);
    if (!empty_prefix)
        b.i(code);
    b.i(2); // Two batches, including a known empty one.
    b.i(2);
    for (const auto particle : {1, 2}) {
        b.i(0);
        b.i(particle);
        b.put<float>(particle == 1 ? length : 3.5f);
    }
    b.i(0);
    return b;
}
inline Bytes hybrid_values(float length = 2.5f) {
    Bytes b;
    b.put<std::uint16_t>(0x0780); // All fields; Tags is serialized zero.
    b.put<std::uint8_t>(2);
    b.append(hybrid_record(17, length));
    b.i(123); // Proves the native tail did not consume the following property.
    return b;
}
inline Json hybrid_manifest(const std::string &hash, const char *profile = "ue5.3") {
    return {
        {"format", "cyuediff.zen-schema/4"},
        {"profile", profile},
        {"usmap", "hybrid.usmap"},
        {"sha256", hash},
        {"bindings", {{"/Script/Test.DemoAsset", {{"schema", "/Script/Test.Collections"}, {"offset", 0u}}}}},
        {"nativeStructs", Json::object()},
        {"structLayouts", Json::object()},
        {"structTails", {{"/Script/Test.Record", "Tethers"}}},
        {"binaryLayouts", hybrid_layouts()}};
}
inline void write_hybrid_fixtures(const fs::path &root) {
    fs::create_directories(root);
    auto mapping = hybrid_mapping();
    mapping.save(root / "hybrid.usmap");
    for (bool modern : {false, true}) {
        const auto profile = modern ? "ue5.5" : "ue5.3";
        auto context = hybrid_manifest(sha256_bytes(mapping.data), profile);
        std::ofstream(root / (std::string("hybrid-") + profile + ".json"), std::ios::binary)
            << context.dump(2) << '\n';
        auto wrong = context;
        wrong["structTails"] = Json::object();
        wrong["reflectedStructs"] = Json::array({"/Script/Test.Record"});
        std::ofstream(root / (std::string("unbound-") + profile + ".json"), std::ios::binary)
            << wrong.dump(2) << '\n';
        for (const auto *kind : {"before", "after", "truncated"}) {
            auto values = hybrid_values(std::string(kind) == "after" ? 4.5f : 2.5f);
            if (std::string(kind) == "truncated")
                values.data.resize(20);
            const auto name = std::string(kind) + (modern ? "55" : "");
            auto zen = collection_zen_fixture(values, modern);
            zen.save(root / (name + ".ucas"));
            zen_toc(zen).save(root / (name + ".utoc"));
        }
    }
    auto global = script_fixture();
    global.save(root / "global.ucas");
    zen_toc(global, 5).save(root / "global.utoc");
}
} // namespace cyuediff::test
