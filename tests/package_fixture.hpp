#pragma once
#include <cyuediff/core.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
namespace cyuediff::test {
struct Bytes {
    std::vector<unsigned char> data;
    template <class T> void put(T value) {
        auto p = reinterpret_cast<unsigned char *>(&value);
        data.insert(data.end(), p, p + sizeof(T));
    }
    void i(int value) { put<std::int32_t>(value); }
    void zeros(int n) { data.insert(data.end(), n, 0); }
    void string(const std::string &s) {
        i(static_cast<int>(s.size() + 1));
        data.insert(data.end(), s.begin(), s.end());
        put<char>(0);
    }
    void patch(std::size_t pos, int value) { std::memcpy(data.data() + pos, &value, 4); }
    void append(const Bytes &b) { data.insert(data.end(), b.data.begin(), b.data.end()); }
    void save(const fs::path &path) {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }
};
struct Fixture {
    Bytes header, script;
    std::size_t export_offset = 0, serial_offset_field = 0;
    std::size_t header_size_field=0,export_count_field=0,export_offset_field=0,import_count_field=0;
};
using NameWriter = std::function<void(Bytes &, const std::string &)>;
using ExtraScript = std::function<void(Bytes &, const NameWriter &)>;
Fixture fixture(int version, int value = 1, bool reorder = false, int suffix = 0,
                const std::vector<std::string> &extra_names = {}, const ExtraScript &extra = {},
                int editor_version = -1, int release_version = -1, int core_version = -1,
                int rendering_version = -1, int anim_phys_version = -1, int framework_version = -1,
                int anim_version = -1) {
    std::vector<std::string> names = {
        "None",        "TestAsset",   "DataAsset",     "/Script/CoreUObject", "Class",
        "Value",       "IntProperty", "Enabled",       "BoolProperty",        "Text",
        "StrProperty", "Items",       "ArrayProperty", "StructProperty",      "ExampleStruct"};
    for (const auto &name : extra_names)
        if (std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
    if (reorder)
        std::reverse(names.begin(), names.end());
    std::map<std::string, int> ids;
    for (std::size_t i = 0; i < names.size(); ++i)
        ids[names[i]] = static_cast<int>(i);
    auto name = [&](Bytes &b, const std::string &n) {
        b.i(ids.at(n));
        b.i(0);
    };
    Fixture f;
    auto &b = f.header;
    auto &s = f.script;
    if (version >= 1011)
        s.put<std::uint8_t>(0);
    auto write_tag = [&](Bytes &target, const std::string &n, const std::string &type, int size,
                         bool boolean = false) {
        name(target, n);
        name(target, type);
        if (version >= 1012) {
            target.i(type == "ArrayProperty" ? 1 : 0);
            if (type == "ArrayProperty") {
                name(target, "StructProperty");
                target.i(1);
                name(target, "ExampleStruct");
                target.i(0);
            }
        }
        target.i(size);
        if (version >= 1012)
            target.put<std::uint8_t>(boolean ? 0x10 : 0);
        else {
            target.i(0);
            if (type == "BoolProperty")
                target.put<std::uint8_t>(boolean ? 1 : 0);
            if (type == "ArrayProperty")
                name(target, "StructProperty");
            if (type == "StructProperty") {
                name(target, "ExampleStruct");
                target.zeros(16);
            }
            target.put<std::uint8_t>(0);
            if (version >= 1011)
                target.put<std::uint8_t>(0);
        }
    };
    auto tag = [&](const std::string &n, const std::string &type, int size, bool boolean = false) {
        write_tag(s, n, type, size, boolean);
    };
    tag("Value", "IntProperty", 4);
    s.i(value);
    tag("Enabled", "BoolProperty", 0, true);
    Bytes text;
    text.string("hello");
    tag("Text", "StrProperty", static_cast<int>(text.data.size()));
    s.append(text);
    Bytes array;
    array.i(2);
    if (version < 1012)
        write_tag(array, "Items", "StructProperty", 0);
    for (int i = 0; i < 2; ++i) {
        write_tag(array, "Value", "IntProperty", 4);
        array.i(i + 7);
        name(array, "None");
    }
    tag("Items", "ArrayProperty", static_cast<int>(array.data.size()));
    s.append(array);
    if (extra)
        extra(s, name);
    name(s, "None");
    b.put<std::uint32_t>(0x9e2a83c1);
    b.i(version >= 1016 ? -9 : (version ? -8 : -7));
    b.i(864);
    b.i(522);
    if (version)
        b.i(version);
    b.i(0);
    auto custom_versions = [&] {
        b.i((editor_version < 0 ? 0 : 1) + (release_version < 0 ? 0 : 1) + (core_version < 0 ? 0 : 1) +
            (rendering_version < 0 ? 0 : 1) + (anim_phys_version < 0 ? 0 : 1) +
            (framework_version < 0 ? 0 : 1) + (anim_version < 0 ? 0 : 1));
        for (auto v : {std::make_pair(framework_version, false), std::make_pair(anim_version, true)})
            if (v.first >= 0) {
                const auto words =
                    v.second ? std::array<unsigned, 4>{0xaf43a65du, 0x7fd34947u, 0x98733e8eu, 0xd9c1bb05u}
                             : std::array<unsigned, 4>{0xcffc743fu, 0x43b04480u, 0x939114dfu, 0x171d2073u};
                for (auto word : words)
                    b.put<std::uint32_t>(word);
                b.i(v.first);
            }
        if (anim_phys_version >= 0) {
            for (auto word : {0x29e575ddu, 0xe0a34627u, 0x9d10d276u, 0x232cdceau})
                b.put<std::uint32_t>(word);
            b.i(anim_phys_version);
        }
        for (auto version : {std::make_pair(core_version, 0), std::make_pair(rendering_version, 1)})
            if (version.first >= 0) {
                const auto words =
                    version.second
                        ? std::array<unsigned, 4>{0x12f88b9fu, 0x88754afcu, 0xa67cd90cu, 0x383abd29u}
                        : std::array<unsigned, 4>{0x375ec13cu, 0x06e448fbu, 0xb50084f0u, 0x262a717eu};
                for (auto word : words)
                    b.put<std::uint32_t>(word);
                b.i(version.first);
            }
        if (editor_version >= 0) {
            for (auto word : {0xe4b068edu, 0xf49442e9u, 0xa231da0bu, 0x2e46bb41u})
                b.put<std::uint32_t>(word);
            b.i(editor_version);
        }
        if (release_version >= 0) {
            for (auto word : {0xd89b5e42u, 0x24bd4d46u, 0x8412aca8u, 0xdf641779u})
                b.put<std::uint32_t>(word);
            b.i(release_version);
        }
    };
    std::size_t size_pos;
    if (version >= 1016) {
        b.zeros(20);
        size_pos = b.data.size();
        b.i(0);
        custom_versions();
    } else {
        custom_versions();
        size_pos = b.data.size();
        b.i(0);
    }
    f.header_size_field=size_pos;
    b.string("/Game/TestAsset");
    b.i(0);
    b.i(static_cast<int>(names.size()));
    auto name_pos = b.data.size();
    b.i(0);
    if (version >= 1008)
        b.zeros(8);
    b.string("");
    b.zeros(8);
    f.export_count_field=b.data.size();
    b.i(1);
    auto export_pos = b.data.size();f.export_offset_field=export_pos;
    b.i(0);
    f.import_count_field=b.data.size();
    b.i(1);
    auto import_pos = b.data.size();
    b.i(0);
    if (version >= 1015)
        b.zeros(16);
    if (version >= 1014)
        b.zeros(4);
    b.zeros(20);
    if (version >= 1018)
        b.zeros(8);
    if (version < 1016)
        b.zeros(16);
    b.zeros(16);
    b.i(0);
    for (int i = 0; i < 2; ++i) {
        b.put<std::uint16_t>(5);
        b.put<std::uint16_t>(4);
        b.put<std::uint16_t>(0);
        b.i(0);
        b.string("");
    }
    b.zeros(8); // compression and compressed chunk count
    b.patch(name_pos, static_cast<int>(b.data.size()));
    for (const auto &n : names) {
        b.string(n);
        b.zeros(4);
    }
    b.patch(import_pos, static_cast<int>(b.data.size()));
    name(b, "/Script/CoreUObject");
    name(b, "Class");
    b.i(0);
    name(b, "DataAsset");
    name(b, "/Script/CoreUObject");
    if (version >= 1003)
        b.zeros(4);
    f.export_offset = b.data.size();
    b.patch(export_pos, static_cast<int>(f.export_offset));
    b.i(-1);
    b.i(0);
    b.i(0);
    b.i(0);
    name(b, "TestAsset");
    b.i(1);
    b.put<std::int64_t>(static_cast<std::int64_t>(s.data.size() + 4));
    f.serial_offset_field = b.data.size();
    b.put<std::int64_t>(0);
    b.zeros(12);
    if (version < 1005)
        b.zeros(16);
    if (version >= 1006)
        b.zeros(4);
    b.zeros(12);
    if (version >= 1003)
        b.zeros(4);
    b.zeros(20);
    if (version >= 1010) {
        b.put<std::int64_t>(0);
        b.put<std::int64_t>(static_cast<std::int64_t>(s.data.size()));
    }
    int header_size = static_cast<int>(b.data.size());
    b.patch(size_pos, header_size);
    b.patch(f.serial_offset_field, header_size);
    s.i(suffix);
    return f;
}
fs::path save(const fs::path &root, const std::string &name, Fixture f, bool split = false) {
    auto path = root / fs::u8path(name + ".uasset");
    if (split) {
        f.header.save(path);
        auto companion = path;
        companion.replace_extension(".uexp");
        f.script.save(companion);
    } else {
        f.header.append(f.script);
        f.header.save(path);
    }
    return path;
}
} // namespace cyuediff::test
