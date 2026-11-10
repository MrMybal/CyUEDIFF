#include <cyuediff/binary_layout.hpp>
#include <cyuediff/tagged_types.hpp>
#include "tagged_hybrid_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
const std::string guid = "0123456789abcdef0011223344556677", other_guid = "1123456789abcdef0011223344556677";
void check(bool ok, const char *why) {
    ++checks;
    if (!ok)
        throw std::runtime_error(why);
}
void put_guid(Bytes &b, const std::string &text) {
    for (unsigned i = 0; i < 4; ++i)
        b.put<std::uint32_t>(static_cast<std::uint32_t>(std::stoul(text.substr(i * 8, 8), nullptr, 16)));
}
void full(Bytes &b, const NameWriter &name, const Json &t) {
    name(b, t.at("name"));
    const auto p = t.value("parameters", Json::array());
    b.i(static_cast<int>(p.size()));
    for (const auto &v : p)
        full(b, name, v);
}
Json stype(const std::string &owner = {}, const std::string &id = {}, bool bad = false) {
    Json symbol = {{"name", "IntPoint"}};
    if (!owner.empty())
        symbol["parameters"] = Json::array({{{"name", owner}}});
    Json t = {{"name", "StructProperty"}, {"parameters", Json::array({symbol})}};
    if (!id.empty())
        t["parameters"].push_back({{"name", bad ? "bad-guid" : id}});
    return t;
}
void tag(Bytes &b, const NameWriter &name, int version, const char *field, const Json &type,
         std::size_t bytes, const std::string &id, const std::string &member = {}) {
    name(b, field);
    if (version >= 1012) {
        full(b, name, type);
        b.i(static_cast<int>(bytes));
        b.put<std::uint8_t>(member.empty() ? 0 : 2);
        if (!member.empty())
            put_guid(b, member);
    } else {
        name(b, type.at("name"));
        b.i(static_cast<int>(bytes));
        b.i(0);
        if (type.at("name") == "StructProperty") {
            name(b, "IntPoint");
            put_guid(b, id);
        } else if (type.at("name") == "ArrayProperty")
            name(b, "StructProperty");
        b.put<std::uint8_t>(member.empty() ? 0 : 1);
        if (!member.empty())
            put_guid(b, member);
        if (version >= 1011)
            b.put<std::uint8_t>(0);
    }
}
Fixture make(int version, const std::string &id, bool nonempty = false, const std::string &owner = {},
             bool array = false, bool bad = false, const std::string &member = {}) {
    return fixture(version, 1, false, 0,
                   {"Identity", "AfterIdentity", "IntPoint", "X", guid, other_guid, std::string(32, '0'),
                    "bad-guid", owner.empty() ? "/Script/Foreign" : owner},
                   [&](Bytes &out, const NameWriter &name) {
                       Bytes payload;
                       if (nonempty) {
                           hybrid_tag(payload, name, version, "X", hybrid_type("IntProperty"), 4);
                           payload.i(123);
                       }
                       name(payload, "None");
                       auto type = stype(owner, version >= 1012 ? id : std::string{}, bad);
                       if (array) {
                           Bytes items;
                           items.i(1);
                           if (version < 1012)
                               tag(items, name, version, "Identity", stype(), payload.data.size(), id);
                           items.append(payload);
                           payload = std::move(items);
                           type = {{"name", "ArrayProperty"}, {"parameters", Json::array({type})}};
                       }
                       tag(out, name, version, "Identity", type, payload.data.size(), id, member);
                       out.append(payload);
                       hybrid_tag(out, name, version, "AfterIdentity", hybrid_type("IntProperty"), 4);
                       out.i(987);
                   });
}
Json certificate(int version, const std::string &id = guid, bool native_hint = false) {
    Json type = {{"name", "StructProperty"},
                 {"parameters", Json::array({{{"name", "IntPoint"}}})},
                 {"schema", "/Script/Foreign.IntPoint"},
                 {"structGuid", id}};
    Json schemas = Json::object();
    schemas["/Script/CoreUObject.DataAsset"]["properties"]["Identity"]["type"] = type;
    schemas["/Script/Foreign.IntPoint"] = {{"native", native_hint},
                                           {"serialization", "tagged"},
                                           {"structGuid", id},
                                           {"properties", Json::object()}};
    Json context = {{"format", "cyuediff.tagged-types/1"},
                    {"profile", {{"ue4", {522, 522}}, {"ue5", {version, version}}}},
                    {"schemas", std::move(schemas)}};
    return {{"format", "cyuediff.binary-layouts/1"},
            {"layouts", Json::object()},
            {"taggedTypes", std::move(context)}};
}
Package read(const fs::path &file, const Json &c) {
    ReadOptions o;
    o.decoders = BinaryLayouts::parse(c).decoder_registry();
    return Package::open(file, o);
}
Json native_catalogue(const std::string &selector = {}) {
    Json fields = Json::array(
        {{{"name", "a"}, {"value", {{"type", "i32"}}}}, {{"name", "b"}, {"value", {{"type", "i32"}}}}});
    Json entry = {{"id", "identity.native/1"},
                  {"kind", "struct"},
                  {"target", "IntPoint"},
                  {"profile", {{"ue4", {522, 522}}, {"ue5", {0, 1018}}}},
                  {"layout", "Pair"}};
    if (!selector.empty())
        entry["structGuid"] = selector;
    return {{"format", "cyuediff.binary-layouts/1"},
            {"layouts", {{"Pair", {{"type", "record"}, {"fields", std::move(fields)}}}}},
            {"decoders", Json::array({entry})}};
}
} // namespace
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-struct-identity-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        for (int version : {0, 1004, 1009, 1011, 1012, 1013}) {
            const auto file = save(root, "empty" + std::to_string(version),
                                   make(version, guid, false, {}, false, false, other_guid));
            auto p = Package::open(file);
            const auto &e = p.exports[0].properties.at("Identity");
            check(e.at("type").at("structGuid") == guid && e.at("guid") == other_guid,
                  "Struct and member GUIDs remain separate");
            check(!e.contains("value") && e.at("opaque").at("reason") == "struct-serialization-ambiguous",
                  "An uncertified empty custom struct never becomes a builtin native pair");
            check(p.exports[0].properties.at("AfterIdentity").at("value") == 987,
                  "A following property survives ambiguous struct recovery");
            auto known = read(file, certificate(version));
            check(known.exports[0].properties.at("Identity").at("value") == Json::object(),
                  "Explicit matching tagged context reads an empty stored override map");
            check(read(file, certificate(version, guid, true))
                          .exports[0]
                          .properties.at("Identity")
                          .at("value") == Json::object(),
                  "Explicit wire certificate takes precedence over the legacy native hint");
            check(!read(file, certificate(version, other_guid))
                       .exports[0]
                       .properties.at("Identity")
                       .contains("value"),
                  "Wrong struct GUID metadata cannot select a value reader");
            const auto populated = save(root, "tagged" + std::to_string(version), make(version, guid, true));
            check(Package::open(populated)
                          .exports[0]
                          .properties.at("Identity")
                          .at("value")
                          .at("X")
                          .at("value") == 123,
                  "A nonempty bounded tagged stream with a builtin-like name stays reflected");
            const auto arrayfile =
                save(root, "array" + std::to_string(version), make(version, guid, false, {}, true));
            auto catalogue = native_catalogue(guid);
            auto array = read(arrayfile, catalogue);
            check(array.exports[0].properties.at("Identity").at("value")[0].at("data").at("a") == 0,
                  "GUID-selected explicit native array reader is used");
            if (version < 1012) {
                const auto &prop = array.exports[0].properties.at("Identity");
                check(prop.at("wireTypes").at("/*").at("structGuid") == guid,
                      "Legacy inner tags retain their actual stored GUID");
                check(prop.at("resolvedType").at("parameters")[0].at("structGuid") == guid,
                      "Observed inner identity participates in effective type correspondence");
            }
            bool called = false;
            AssetDecoder d;
            d.id = "host.guid/1";
            d.target = "IntPoint";
            d.struct_guid = guid;
            d.decode = [&](DecoderInput &i, const DecoderContext &ctx) -> Json {
                called = true;
                check(ctx.effective_type && ctx.effective_type->struct_guid == guid,
                      "Callback receives borrowed effective struct identity");
                return Json::array({i.get<std::int32_t>(), i.get<std::int32_t>()});
            };
            ReadOptions o;
            o.decoders = std::make_shared<DecoderRegistry>(std::vector<AssetDecoder>{d});
            check(Package::open(file, o).exports[0].properties.at("Identity").contains("value") && called,
                  "A matching explicit GUID registration can certify a native format");
            called = false;
            d.struct_guid = other_guid;
            o.decoders = std::make_shared<DecoderRegistry>(std::vector<AssetDecoder>{d});
            check(!Package::open(file, o).exports[0].properties.at("Identity").contains("value") && !called,
                  "A different GUID cannot invoke the registered decoder");
            auto another = d;
            another.id = "host.wildcard/1";
            another.struct_guid.clear();
            d.struct_guid = guid;
            o.decoders = std::make_shared<DecoderRegistry>(std::vector<AssetDecoder>{d, another});
            check(Package::open(file, o).exports[0].properties.at("Identity").at("opaque").at("reason") ==
                      "decoder-ambiguous",
                  "Overlapping wildcard and GUID registrations fail without order dependence");
        }
        for (int version : {1012, 1013}) {
            const auto foreign = save(root, "foreign" + std::to_string(version),
                                      make(version, std::string(32, '0'), false, "/Script/Foreign"));
            check(!Package::open(foreign).exports[0].properties.at("Identity").contains("value"),
                  "Stored foreign owner blocks builtin dispatch even with a zero GUID");
            const auto reflected = save(root, "foreign-populated" + std::to_string(version),
                                        make(version, std::string(32, '0'), true, "/Script/Foreign"));
            check(Package::open(reflected)
                          .exports[0]
                          .properties.at("Identity")
                          .at("value")
                          .at("X")
                          .at("value") == 123,
                  "Qualified foreign reflected types remain readable");
            const auto malformed =
                save(root, "bad-guid" + std::to_string(version), make(version, guid, false, {}, false, true));
            const auto p = Package::open(malformed);
            check(!p.exports[0].properties.at("Identity").contains("value") &&
                      p.exports[0].properties.at("AfterIdentity").at("value") == 987,
                  "Malformed full-type GUID is bounded to its value window");
        }
        PackageSummary summary;
        summary.ue4_version = 522;
        summary.ue5_version = 1009;
        auto c = native_catalogue();
        const auto first = BinaryLayouts::parse(c).decoder_registry()->definitions(summary);
        c["decoders"][0]["profile"]["ue5"] = {1000, 1018};
        c["decoders"][0]["structGuid"] = guid;
        check(first == BinaryLayouts::parse(c).decoder_registry()->definitions(summary),
              "Matched profile/GUID selector changes do not alter the immutable native program");
        const auto first_file = save(root, "guid-before", make(1009, guid));
        const auto second_file = save(root, "guid-after", make(1009, other_guid));
        const auto wildcard = native_catalogue();
        const auto compared = compare(read(first_file, wildcard), read(second_file, wildcard));
        check(compared.status() == "unverified",
              "Equal decoded numbers under different stored GUIDs cannot establish verified equality");
        bool identity_scope = false;
        for (const auto &scope : compared.unavailable_scopes)
            identity_scope = identity_scope || scope.at("reason") == "source-type-format-incompatible";
        check(identity_scope, "GUID changes explicitly guard source value correspondence");
        const auto first_array = save(root, "array-guid-before", make(1009, guid, false, {}, true));
        const auto second_array = save(root, "array-guid-after", make(1009, other_guid, false, {}, true));
        const auto array_compared = compare(read(first_array, wildcard), read(second_array, wildcard));
        identity_scope = false;
        for (const auto &scope : array_compared.unavailable_scopes)
            identity_scope = identity_scope || scope.at("reason") == "source-type-format-incompatible";
        check(identity_scope, "Stored legacy array element GUIDs guard value correspondence");
        const auto tinyfile = save(root, "budget", make(1013, guid, true, "/Script/Foreign"));
        ReadOptions limited;
        limited.max_property_bytes = 32;
        check(Package::open(tinyfile, limited).source_read_limit_failed,
              "Repeated stored type text is charged to the source identity budget");
        fs::remove_all(root);
        std::cout << checks << " source struct identity checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
