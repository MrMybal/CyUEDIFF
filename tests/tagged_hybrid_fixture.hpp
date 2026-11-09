#pragma once
#include "package_fixture.hpp"
#include <cyuediff/binary_layout.hpp>
namespace cyuediff::test {
inline Json hybrid_type(const char *name, Json parameters = Json::array()) {
    return {{"name", name}, {"parameters", std::move(parameters)}};
}
inline void hybrid_tag(Bytes &b, const NameWriter &name, int version, const std::string &field,
                       const Json &type, std::size_t size) {
    name(b, field);
    std::function<void(const Json &)> full = [&](const Json &t) {
        name(b, t.at("name").get<std::string>());
        b.i(static_cast<int>(t.at("parameters").size()));
        for (const auto &v : t.at("parameters"))
            full(v);
    };
    if (version >= 1012)
        full(type);
    else
        name(b, type.at("name").get<std::string>());
    b.i(static_cast<int>(size));
    if (version >= 1012)
        b.put<std::uint8_t>(0);
    else {
        b.i(0);
        const auto kind = type.at("name").get<std::string>();
        if (kind == "StructProperty") {
            name(b, type["parameters"][0]["name"]);
            b.zeros(16);
        } else if (kind == "ArrayProperty" || kind == "SetProperty")
            name(b, type["parameters"][0]["name"]);
        else if (kind == "MapProperty") {
            name(b, type["parameters"][0]["name"]);
            name(b, type["parameters"][1]["name"]);
        }
        b.put<std::uint8_t>(0);
        if (version >= 1011)
            b.put<std::uint8_t>(0);
    }
}
inline Json hybrid_catalogue() {
    const auto field = [](const char *name, Json t) { return Json{{"name", name}, {"value", std::move(t)}}; };
    Json d = {
        {"format", "cyuediff.binary-layouts/1"},
        {"layouts",
         {{"InnerTail",
           {{"type", "record"},
            {"fields",
             Json::array({field("stamp", {{"type", "i64"}}), field("label", {{"type", "string"}})})}}},
          {"OuterTail",
           {{"type", "record"},
            {"fields",
             Json::array(
                 {field("samples", {{"type", "array"}, {"count", "i32"}, {"element", {{"type", "f32"}}}}),
                  field("name", {{"type", "name"}}), field("enabled", {{"type", "bool32"}})})}}}}},
        {"decoders", Json::array()}};
    for (const auto &binding :
         {std::make_pair("HybridInner", "InnerTail"), std::make_pair("HybridOuter", "OuterTail")})
        d["decoders"].push_back({{"id", std::string("original.") + binding.first + "/1"},
                                 {"kind", "struct"},
                                 {"target", binding.first},
                                 {"profile", {{"ue4", {522, 522}}, {"ue5", {0, 1013}}}},
                                 {"layout", binding.second},
                                 {"prefix", "tagged-properties"}});
    return d;
}
inline Bytes hybrid_inner(const NameWriter &name, int version, int marker, bool changed = false,
                          bool empty_tail = false) {
    Bytes b;
    if (!empty_tail) {
        hybrid_tag(b, name, version, "Marker", hybrid_type("IntProperty"), 4);
        b.i(marker);
    }
    name(b, "None");
    if (!empty_tail) {
        b.put<std::int64_t>(9007199254740993ll + marker + (changed ? 3 : 0));
        b.string(changed ? "native after" : "native before");
    }
    return b;
}
inline Fixture tagged_hybrid_fixture(int version = 1013, bool changed = false,
                                     std::size_t truncate = SIZE_MAX, bool unknown_prefix = false,
                                     bool empty_tail = false, bool collections = false) {
    return fixture(
        version, 1, false, 0,
        {"Native", "AfterNative", "HybridOuter", "HybridInner", "Mode", "Label", "Marker", "Records",
         "Nested", "TailName", "MapProperty", "SetProperty", "Lookup", "Selected", "NameProperty",
         "UnknownProperty"},
        [&](Bytes &out, const NameWriter &name) {
            const auto inner = hybrid_type("StructProperty", Json::array({hybrid_type("HybridInner")}));
            Bytes body, payload;
            hybrid_tag(body, name, version, "Mode",
                       hybrid_type(unknown_prefix ? "UnknownProperty" : "IntProperty"), 4);
            body.i(changed ? 8 : 7);
            Bytes label;
            label.string(changed ? "prefix after" : "prefix before");
            hybrid_tag(body, name, version, "Label", hybrid_type("StrProperty"), label.data.size());
            body.append(label);
            auto nested = hybrid_inner(name, version, 11, changed, empty_tail);
            hybrid_tag(body, name, version, "Nested", inner, nested.data.size());
            body.append(nested);
            payload.i(2);
            if (version < 1012)
                hybrid_tag(payload, name, version, "Records", inner, 0);
            payload.append(hybrid_inner(name, version, 21, false, empty_tail));
            payload.append(hybrid_inner(name, version, 22, changed, empty_tail));
            hybrid_tag(body, name, version, "Records", hybrid_type("ArrayProperty", Json::array({inner})),
                       payload.data.size());
            body.append(payload);
            if (collections) {
                Bytes map;
                map.i(0);
                map.i(1);
                name(map, "TailName");
                map.append(hybrid_inner(name, version, 31, changed, empty_tail));
                hybrid_tag(body, name, version, "Lookup",
                           hybrid_type("MapProperty", Json::array({hybrid_type("NameProperty"), inner})),
                           map.data.size());
                body.append(map);
                Bytes set;
                set.i(0);
                set.i(1);
                set.append(hybrid_inner(name, version, 41, changed, empty_tail));
                hybrid_tag(body, name, version, "Selected", hybrid_type("SetProperty", Json::array({inner})),
                           set.data.size());
                body.append(set);
            }
            name(body, "None");
            body.i(3);
            body.put<float>(.25f);
            body.put<float>(changed ? 1.5f : .5f);
            body.put<float>(.75f);
            name(body, "TailName");
            body.i(1);
            if (truncate < body.data.size())
                body.data.resize(truncate);
            hybrid_tag(out, name, version, "Native",
                       hybrid_type("StructProperty", Json::array({hybrid_type("HybridOuter")})),
                       body.data.size());
            out.append(body);
            hybrid_tag(out, name, version, "AfterNative", hybrid_type("IntProperty"), 4);
            out.i(987);
        });
}
inline void save_tagged_hybrid(const fs::path &root, const std::string &name, Fixture f) {
    f.header.save(root / (name + ".uasset"));
    f.script.save(root / (name + ".uexp"));
}
} // namespace cyuediff::test
