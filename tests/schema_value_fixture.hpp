#pragma once
#include "schema_collection_fixture.hpp"
#include <fstream>
namespace cyuediff::test {
inline Bytes text_value(const std::string &source = "Original source") {
    Bytes bytes;
    bytes.put<std::uint32_t>(0x40000000);
    bytes.put<std::int8_t>(0);
    bytes.string("Stored Namespace");
    bytes.string("StableKey");
    bytes.string(source);
    return bytes;
}
inline Bytes invariant_text_value(bool present = true) {
    Bytes bytes;
    bytes.i(2);
    bytes.put<std::int8_t>(-1);
    bytes.i(present ? 1 : 0);
    if (present)
        bytes.string("Invariant source");
    return bytes;
}
inline Bytes table_text_value() {
    Bytes bytes;
    bytes.i(0);
    bytes.put<std::int8_t>(11);
    collection_name(bytes, 2);
    bytes.string("EntryKey");
    return bytes;
}
inline Bytes delegate_value(int object = 1, int function = 2, int suffix = 0) {
    Bytes bytes;
    bytes.i(object);
    bytes.i(function);
    bytes.i(suffix);
    return bytes;
}
inline Bytes multicast_value(bool changed = false, bool reorder = false) {
    Bytes bytes;
    bytes.i(3);
    auto first = delegate_value(1, changed ? 3 : 2), second = delegate_value(0, 0);
    bytes.append(reorder ? second : first);
    bytes.append(reorder ? first : second);
    bytes.append(first);
    return bytes;
}
inline Bytes mixed_value_bytes(const std::string &mode = "mixed") {
    Bytes bytes;
    if (mode == "zero") {
        bytes.put<std::uint16_t>(0x0780);
        bytes.put<std::uint8_t>(7);
        return bytes;
    }
    bytes.put<std::uint16_t>(0x0700);
    if (mode == "table")
        bytes.append(table_text_value());
    else if (mode == "invariant")
        bytes.append(invariant_text_value());
    else if (mode == "unsupported") {
        bytes.i(0);
        bytes.put<std::int8_t>(42);
    } else
        bytes.append(text_value(mode.find("changed") == 0 ? "Edited source" : "Original source"));
    auto multi = multicast_value(mode.find("changed") == 0);
    if (mode == "invalid-target")
        multi.patch(4, 999);
    bytes.append(multi);
    bytes.i(mode.find("changed") == 0 ? 2 : 1);
    if (mode == "truncated")
        bytes.data.pop_back();
    return bytes;
}
inline void write_value_fixtures(const fs::path &root) {
    fs::create_directories(root);
    auto mappings = collection_schema_fixture(collection_type(11), collection_type(13), collection_type(14));
    mappings.save(root / "values.usmap");
    for (const auto &name : {"mixed", "changed", "mixed55", "changed55", "table", "invariant", "unsupported",
                             "invalid-target", "truncated", "zero"}) {
        const std::string stem = name;
        auto package = collection_zen_fixture(mixed_value_bytes(stem), stem.find("55") != std::string::npos);
        package.save(root / (stem + ".ucas"));
        zen_toc(package).save(root / (stem + ".utoc"));
    }
    auto global = script_fixture();
    global.save(root / "global.ucas");
    zen_toc(global, 5).save(root / "global.utoc");
    Json manifest = {{"format", "cyuediff.zen-schema/2"},
                     {"profile", "ue5.3"},
                     {"usmap", "values.usmap"},
                     {"sha256", sha256_bytes(mappings.data)},
                     {"bindings", {{"/Script/Test.DemoAsset", {{"schema", "Collections"}, {"offset", 0}}}}},
                     {"nativeStructs", Json::object()}};
    for (const char *name : {"values.json", "values55.json"}) {
        std::ofstream file(root / name, std::ios::binary);
        file << manifest.dump(2) << '\n';
        if (!file)
            throw std::runtime_error("Cannot write property value fixture");
        manifest["profile"] = "ue5.5";
    }
}
} // namespace cyuediff::test
