#include "package_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
Json type(const char *name, Json parameters = Json::array()) {
    return {{"name", name}, {"parameters", parameters}};
}
void field(Bytes &out, const NameWriter &name, int version, const char *key, const Json &t,
           const Bytes &payload) {
    name(out, key);
    std::function<void(const Json &)> full = [&](const Json &node) {
        name(out, node.at("name"));
        out.i(static_cast<int>(node.at("parameters").size()));
        for (const auto &child : node.at("parameters"))
            full(child);
    };
    if (version >= 1012)
        full(t);
    else
        name(out, t.at("name"));
    out.i(static_cast<int>(payload.data.size()));
    if (version >= 1012)
        out.put<std::uint8_t>(0);
    else {
        out.i(0);
        for (const auto &child : t.at("parameters"))
            name(out, child.at("name"));
        if (t.at("name") == "StructProperty")
            out.zeros(16);
        out.put<std::uint8_t>(0);
    }
    out.append(payload);
}
const std::vector<std::string> names = {
    "Map",       "MapProperty", "Set",         "SetProperty",      "NameProperty",
    "Alpha",     "Beta",        "Localized",   "TextProperty",     "Invariant",
    "Table",     "Empty",       "Unsupported", "AfterUnsupported", "ST_Demo",
    "Placement", "Transform",   "When",        "DateTime",         "Duplicate"};
Fixture example(int version, bool reverse = false) {
    return fixture(
        version, 1, false, 0, names,
        [&](Bytes &s, const NameWriter &name) {
            Bytes map;
            map.i(1);
            name(map, "Beta");
            map.i(2);
            for (auto key : reverse ? std::vector<std::string>{"Beta", "Alpha"}
                                    : std::vector<std::string>{"Alpha", "Beta"}) {
                name(map, key);
                map.i(key == "Alpha" ? 10 : 20);
            }
            field(s, name, version, "Map",
                  type("MapProperty", Json::array({type("NameProperty"), type("IntProperty")})), map);
            Bytes set;
            set.i(1);
            set.i(99);
            set.i(2);
            set.i(reverse ? 2 : 1);
            set.i(reverse ? 1 : 2);
            field(s, name, version, "Set", type("SetProperty", Json::array({type("IntProperty")})), set);
            Bytes localized;
            localized.i(0);
            localized.put<std::int8_t>(0);
            localized.string("UI");
            localized.string("Greeting");
            localized.string("Bonjour");
            field(s, name, version, "Localized", type("TextProperty"), localized);
            Bytes invariant;
            invariant.i(2);
            invariant.put<std::int8_t>(-1);
            invariant.i(1);
            invariant.string("Exact source");
            field(s, name, version, "Invariant", type("TextProperty"), invariant);
            Bytes table;
            table.i(0);
            table.put<std::int8_t>(11);
            name(table, "ST_Demo");
            table.string("Title");
            field(s, name, version, "Table", type("TextProperty"), table);
            Bytes empty;
            empty.i(0);
            empty.put<std::int8_t>(-1);
            empty.i(0);
            field(s, name, version, "Empty", type("TextProperty"), empty);
            Bytes unsupported;
            unsupported.i(0);
            unsupported.put<std::int8_t>(42);
            unsupported.zeros(8);
            field(s, name, version, "Unsupported", type("TextProperty"), unsupported);
            Bytes after;
            after.i(88);
            field(s, name, version, "AfterUnsupported", type("IntProperty"), after);
            Bytes placement;
            for (double number : {0., 0., 0., 1., 1., 2., 3., 1., 1., 1.})
                placement.put(number);
            field(s, name, version, "Placement", type("StructProperty", Json::array({type("Transform")})),
                  placement);
            Bytes date;
            date.put<std::int64_t>(123456789);
            field(s, name, version, "When", type("StructProperty", Json::array({type("DateTime")})), date);
        },
        40);
}
} // namespace
int main() {
    auto root =
        fs::current_path() / (".test-tmp-properties-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        for (int version : {1009, 1012}) {
            auto a = Package::open(save(root, "before", example(version)));
            auto b = Package::open(save(root, "after", example(version, true)));
            const auto &p = a.exports.at(0).properties;
            check(p.at("Map").at("value").at("entries").at("\"Alpha\"").at("value") == 10,
                  "Map keys carry stable typed values");
            check(p.at("Map").at("value").at("removed").at("\"Beta\"") == "Beta",
                  "Removed map keys remain explicit deltas");
            check(p.at("Set").at("value").at("removed").at("99") == 99, "Set removal retained");
            check(p.at("Set").at("value").at("entries").size() == 2, "Set elements decoded");
            check(compare(a, b).changes.empty() && compare(a, b).binary_changed,
                  "Map/set iteration order is not semantic");
            check(p.at("Localized").at("value").at("source") == "Bonjour" &&
                      p.at("Localized").at("value").at("key") == "Greeting",
                  "Localized text retains namespace/key/source");
            check(p.at("Invariant").at("value").at("source") == "Exact source",
                  "Culture-invariant text decoded");
            check(p.at("Table").at("value").at("table") == "ST_Demo",
                  "String-table reference retained without loading external assets");
            check(p.at("Empty").at("value").at("source") == "", "Empty text decoded");
            check(p.at("Unsupported").at("opaque").at("reason") == "text-history" &&
                      p.at("AfterUnsupported").at("value") == 88,
                  "Unknown history stays bounded and preserves following property");
            check(p.at("Placement").at("value").at("translation") == Json::array({1., 2., 3.}),
                  "Native LWC transform decoded");
            check(p.at("When").at("value").at("ticks") == 123456789, "Native date ticks preserved");
        }
        auto duplicates = fixture(1012, 1, false, 0, names, [](Bytes &s, const NameWriter &name) {
            Bytes map;
            map.i(0);
            map.i(2);
            name(map, "Alpha");
            map.i(1);
            name(map, "Alpha");
            map.i(2);
            field(s, name, 1012, "Duplicate",
                  type("MapProperty", Json::array({type("NameProperty"), type("IntProperty")})), map);
        });
        auto p = Package::open(save(root, "duplicate", duplicates));
        check(p.exports[0].properties.at("Duplicate").at("opaque").at("reason") == "container-identity",
              "Duplicate keys rejected without data loss");
        auto legacy = fixture(
            1009, 1, false, 0, names,
            [](Bytes &s, const NameWriter &name) {
                Bytes empty;
                empty.i(0);
                empty.put<std::int8_t>(-1);
                field(s, name, 1009, "Empty", type("TextProperty"), empty);
            },
            31);
        check(Package::open(save(root, "legacy-none", legacy))
                  .exports[0]
                  .properties.at("Empty")
                  .contains("value"),
              "Old text version has no invented invariant flag");
        auto overrides=fixture(1012,1,false,0,names,[](Bytes &s,const NameWriter &name) {
            name(s,"Duplicate");name(s,"IntProperty");s.i(0);s.i(4);s.put<std::uint8_t>(4);
            s.put<std::uint8_t>(2);s.put<std::uint8_t>(1);s.put<std::uint32_t>(1);s.i(777);
        });
        auto experimental=Package::open(save(root,"overrides",overrides)).exports[0].properties.at("Duplicate");
        check(experimental.at("opaque").at("reason")=="property-extension" && experimental.at("extensions").at("experimentalOverridable")==true,
              "Experimental container override stream remains opaque with its metadata");
        fs::remove_all(root);
        std::cout << checks << " extended property checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nFixtures: " << root << '\n';
        return 1;
    }
}
