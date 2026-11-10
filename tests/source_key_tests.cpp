#include "tagged_hybrid_fixture.hpp"
#include <cyuediff/tagged_types.hpp>
#include "../src/key_identity.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool ok, const char *why) {
    ++checks;
    if (!ok)
        throw std::runtime_error(why);
}
Json structure(const char *name) {
    return {{"name", "StructProperty"},
            {"parameters", Json::array({{{"name", name}}})},
            {"schema", std::string("/Script/Original.") + name}};
}
Json catalogue(bool extra, bool set = false, bool native = false) {
    Json type = {{"name", set ? "SetProperty" : "MapProperty"},
                 {"parameters", Json::array({structure(native ? "NativeKey" : "RefKey")})}};
    if (!set)
        type["parameters"].push_back({{"name", "IntProperty"}});
    Json schemas = Json::object();
    schemas["/Script/CoreUObject.DataAsset"]["properties"]["Points"]["type"] = type;
    schemas[native ? "/Script/Original.NativeKey" : "/Script/Original.RefKey"] = {
        {"properties", Json::object()}, {"native", native}};
    if (extra && !native) {
        auto vector = structure("Vector");
        vector["schema"] = "/Script/CoreUObject.Vector";
        schemas["/Script/Original.RefKey"]["properties"]["Position"]["type"] = vector;
        schemas["/Script/CoreUObject.Vector"] = {{"properties", Json::object()}};
    }
    Json hints = {{"format", "cyuediff.tagged-types/1"},
                  {"profile", {{"ue4", {522, 522}}, {"ue5", {1004, 1004}}}},
                  {"schemas", schemas}};
    return {{"format", "cyuediff.binary-layouts/1"}, {"layouts", Json::object()}, {"taggedTypes", hints}};
}
Fixture data(bool set = false, bool removed = false, int mapped = 41, bool native = false,
             bool unread = false, unsigned count = 1) {
    return fixture(1004, 1, false, 0,
                   {"Points", "Following", "Position", "Vector", "RefKey", "NativeKey", "UnknownProperty",
                    "MapProperty", "SetProperty", "NameProperty"},
                   [&](Bytes &out, const NameWriter &name) {
                       auto key = [&](Bytes &body, unsigned i) {
                           if (native) {
                               body.i(17 + static_cast<int>(i));
                               return;
                           }
                           hybrid_tag(
                               body, name, 1004, "Position",
                               unread ? hybrid_type("UnknownProperty")
                                      : hybrid_type("StructProperty", Json::array({hybrid_type("Vector")})),
                               unread ? 4 : 24);
                           if (unread)
                               body.i(17);
                           else {
                               body.put<double>(1);
                               body.put<double>(2);
                               body.put<double>(3);
                           }
                           name(body, "None");
                       };
                       Bytes body;
                       body.i(removed ? static_cast<int>(count) : 0);
                       if (removed)
                           for (unsigned i = 0; i < count; ++i)
                               key(body, i);
                       body.i(removed ? 0 : static_cast<int>(count));
                       if (!removed)
                           for (unsigned i = 0; i < count; ++i) {
                               key(body, i);
                               if (!set)
                                   body.i(mapped);
                           }
                       auto type = hybrid_type(set ? "SetProperty" : "MapProperty",
                                               Json::array({hybrid_type("StructProperty")}));
                       if (!set)
                           type["parameters"].push_back(hybrid_type("IntProperty"));
                       hybrid_tag(out, name, 1004, "Points", type, body.data.size());
                       out.append(body);
                       hybrid_tag(out, name, 1004, "Following", hybrid_type("IntProperty"), 4);
                       out.i(987);
                   });
}
Package read(const fs::path &file, const Json &context, const std::string &id = {}) {
    ReadOptions o;
    if (id.empty())
        o.decoders = BinaryLayouts::parse(context).decoder_registry();
    else {
        auto hints = std::make_shared<TaggedTypes>(TaggedTypes::parse(context.at("taggedTypes")));
        AssetDecoder d;
        d.id = id;
        d.target = "NativeKey";
        d.decode = [](DecoderInput &in, const DecoderContext &) -> Json {
            const auto n = in.get<std::int32_t>();
            return {{"type", {{"name", "IntProperty"}}}, {"value", 5}, {"resolvedType", n}};
        };
        o.decoders = std::make_shared<DecoderRegistry>(std::vector<AssetDecoder>{d}, hints);
    }
    return Package::open(file, o);
}
void write(const fs::path &file, const Json &value) { std::ofstream(file) << value.dump(2) << '\n'; }
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-source-keys-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(root))
        throw std::runtime_error("Expected a new owned test directory");
    auto cleanup = [&] {
        const auto target = fs::weakly_canonical(root);
        if (target.parent_path() != fs::weakly_canonical(fs::current_path()) ||
            target.filename().u8string().rfind(".test-tmp-source-keys-", 0) != 0)
            throw std::runtime_error("Source key test cleanup escaped its owned directory");
        fs::remove_all(target);
    };
    try {
        if (argc == 3 && std::string(argv[1]) == "--emit-fixtures") {
            auto dest = fs::u8path(argv[2]);
            if (!fs::create_directory(dest))
                throw std::runtime_error("Use a new output directory");
            save(dest, "map", data());
            save(dest, "set", data(true));
            save(dest, "changed", data(false, false, 42));
            write(dest / "before-map.json", catalogue(false));
            write(dest / "after-map.json", catalogue(true));
            write(dest / "before-set.json", catalogue(false, true));
            write(dest / "after-set.json", catalogue(true, true));
            cleanup();
            return 0;
        }
        for (bool set : {false, true})
            for (bool removed : {false, true}) {
                const auto file =
                    save(root, "case" + std::to_string(set) + std::to_string(removed), data(set, removed));
                const auto a = read(file, catalogue(false, set)), b = read(file, catalogue(true, set));
                const auto &x = a.exports[0].properties.at("Points").at("value"),
                           &y = b.exports[0].properties.at("Points").at("value");
                const char *group = removed ? "removed" : "entries";
                check(x.at(group).begin().key() == y.at(group).begin().key(),
                      "Reader-only resolved type context cannot change a stored key identity");
                auto diff = compare(a, b);
                check(diff.changes.empty() && diff.unavailable_scopes.empty(),
                      "Equal stored keys do not produce false removals/additions");
                check(b.exports[0].properties.at("Following").at("value") == 987,
                      "Map/set keys stay bounded to their property");
                if (!set && !removed) {
                    const auto changed = save(root, "changed", data(false, false, 42));
                    const auto d = compare(a, read(changed, catalogue(true)));
                    check(d.changes.size() == 1 && d.changes[0].at("before") == 41 &&
                              d.changes[0].at("after") == 42,
                          "Actual mapped value change remains known through metadata normalization");
                }
            }
        for (bool set : {false, true}) {
            const auto file =
                save(root, "native" + std::to_string(set), data(set, false, 41, true, false, 2));
            const auto cat = catalogue(false, set, true);
            const auto a = read(file, cat, "host.key/1"), b = read(file, cat, "host.key/2");
            const auto &entries = a.exports[0].properties.at("Points").at("value").at("entries");
            check(entries.size() == 2 && entries.begin().key().find("resolvedType") != std::string::npos,
                  "Raw native members named resolvedType are real key data and remain distinct");
            const auto d = compare(a, b);
            check(d.changes.empty() && !d.unavailable_scopes.empty() &&
                      d.unavailable_scopes[0].at("reason") ==
                          (set ? "set-key-format-incompatible" : "map-key-format-incompatible"),
                  "Changed callback IDs cannot invent key additions/removals without explicit hashes");
            const auto unread = save(root, "unread" + std::to_string(set), data(set, false, 41, false, true));
            const auto unknown = read(unread, catalogue(false, set));
            const auto self = compare(unknown, unknown);
            check(self.changes.empty() && !self.unavailable_scopes.empty() &&
                      self.unavailable_scopes[0].at("reason") ==
                          (set ? "set-key-identity-unavailable" : "map-key-identity-unavailable"),
                  "Unread stored set keys retain the same availability guarantees as map keys");
        }
        const auto point = fixture(
            1004, 1, false, 0, {"Points", "Following", "IntPoint"}, [&](Bytes &out, const NameWriter &name) {
                hybrid_tag(out, name, 1004, "Points",
                           hybrid_type("StructProperty", Json::array({hybrid_type("IntPoint")})), 8);
                out.i(0);
                out.i(0);
                hybrid_tag(out, name, 1004, "Following", hybrid_type("IntProperty"), 4);
                out.i(987);
            });
        const auto point_file = save(root, "point", point);
        auto tagged = catalogue(false);
        tagged["taggedTypes"]["schemas"]["/Script/CoreUObject.DataAsset"]["properties"]["Points"]["type"] = {
            {"name", "StructProperty"},
            {"parameters", Json::array({{{"name", "IntPoint"}}})},
            {"schema", "/Script/CoreUObject.IntPoint"}};
        tagged["taggedTypes"]["schemas"]["/Script/CoreUObject.IntPoint"] = {{"serialization", "tagged"},
                                                                            {"properties", Json::object()}};
        const auto native_point = Package::open(point_file), tagged_point = read(point_file, tagged);
        check(native_point.exports[0].properties.at("Points").at("value") == Json::array({0, 0}) &&
                  tagged_point.exports[0].properties.at("Points").at("value") == Json::object(),
              "The controlled ambiguous bytes exercise two actual readable serialization modes");
        const auto changed_reader = compare(native_point, tagged_point);
        check(changed_reader.changes.empty() &&
                  changed_reader.unavailable_scopes[0].at("reason") == "source-reader-format-incompatible",
              "A changed actual reader under the same qualified type does not invent stored value changes");
        Json key_tag = {{"type", {{"name", "IntProperty"}}}, {"value", 5}};
        auto annotated = key_tag;
        annotated["readFormats"] = {{"", "builtin"}};
        annotated["resolvedType"] = {{"name", "IntProperty"}};
        auto identity = [](const Json &v) {
            std::uint64_t bytes = 0;
            return detail::source_key_identity(v, {}, [&](std::uint64_t n) {
                if (n > 4096 - bytes)
                    throw Error("text-limit", "test key limit");
                bytes += n;
            });
        };
        check(identity(key_tag) == identity(annotated),
              "Only known tag reader annotations are omitted from key text");
        Json raw = {{"decoder", "original.raw/1"}, {"data", key_tag}}, raw_changed = raw;
        raw_changed["data"]["resolvedType"] = 7;
        check(identity(raw) != identity(raw_changed),
              "A native raw member with the annotation name stays key data");
        const std::string escaped = {char(34), char(92), char(8), char(12),
                                     char(10), char(13), char(9), char(1)};
        check(identity(Json(escaped)) == Json(escaped).dump(),
              "Key escaping agrees exactly with canonical JSON");
        bool cancelled = false;
        OperationControl control;
        control.cancelled = [] { return true; };
        try {
            detail::source_key_identity(key_tag, control, [](std::uint64_t) {});
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Encoded key preparation obeys the shared cancellation policy");
        const auto large = fixture(
            1004, 1, false, 0, {"Points", "Following", "MapProperty", "NameProperty", std::string(1000, 'x')},
            [&](Bytes &out, const NameWriter &name) {
                Bytes body;
                body.i(0);
                body.i(1);
                name(body, std::string(1000, 'x'));
                body.i(41);
                hybrid_tag(out, name, 1004, "Points",
                           hybrid_type("MapProperty", Json::array({hybrid_type("NameProperty"),
                                                                   hybrid_type("IntProperty")})),
                           body.data.size());
                out.append(body);
                hybrid_tag(out, name, 1004, "Following", hybrid_type("IntProperty"), 4);
                out.i(987);
            });
        ReadOptions limited;
        limited.max_property_bytes = 512;
        const auto p = Package::open(save(root, "large", large), limited);
        check(p.source_read_limit_failed && !p.exports[0].properties.at("Points").contains("value") &&
                  p.exports[0].properties.at("Following").at("value") == 987,
              "Encoded key identity text is bounded before allocation and following fields survive");
        cleanup();
        std::cout << checks << " source key checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        cleanup();
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
