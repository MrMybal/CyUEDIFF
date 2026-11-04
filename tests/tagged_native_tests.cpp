#include <cyuediff/schema.hpp>
#include "schema_collection_fixture.hpp"
#include "native_property_fixture.hpp"
#include <chrono>
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool b, const char *m) {
    ++checks;
    if (!b)
        throw std::runtime_error(m);
}
} // namespace
int main() {
    auto root =
        fs::current_path() / (".test-tmp-tagged-native-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const auto schemas = UnversionedSchemas::parse(collection_schema_fixture(collection_struct()).data);
        for (int version : {1000, 1009, 1013})
            for (const char *token : {"Box", "Box3f", "Box3d", "Sphere", "Sphere3f", "Sphere3d",
                                      "BoxSphereBounds", "BoxSphereBounds3f", "BoxSphereBounds3d"}) {
                const std::string name = token;
                const bool wide = version >= 1004 && name.back() != 'f';
                auto payload = geometry_value(name, wide);
                auto package =
                    Package::open(save(root, "before", native_property_fixture(payload, name, version)));
                const auto &prop = package.exports[0].properties["Native"];
                check(prop.contains("value") && package.exports[0].properties["AfterNative"]["value"] == 987,
                      "Tagged native bounds preserve following fields");
                const std::string layout = name.find("BoxSphereBounds") == 0 ? "box-sphere"
                                           : name.find("Sphere") == 0        ? "sphere"
                                                                             : "box3";
                UnversionedReadOptions opt;
                opt.native_structs["Record"] = parse_native_struct_layout(layout + (wide ? "-f64" : "-f32"));
                const auto bytes = collection_single(payload);
                const auto mapped =
                    decode_unversioned(bytes.data, 0, bytes.data.size(), schemas, "Collections", opt);
                check(mapped.properties["/Script/Test.Collections.Lookup"]["value"] == prop["value"],
                      "Classic archive versions and explicit native binding agree exactly");
                auto after = Package::open(save(
                    root, "after", native_property_fixture(geometry_value(name, wide, true), name, version)));
                const auto diff = compare(package, after);
                check(!diff.changes.empty(), "Tagged native geometry differences are semantic");
                for (std::size_t n = 0; n < payload.data.size(); ++n) {
                    auto cut = payload;
                    cut.data.resize(n);
                    auto bad = Package::open(save(root, "cut", native_property_fixture(cut, name, version)));
                    check(bad.exports[0].properties["Native"].contains("opaque") &&
                              bad.exports[0].properties["AfterNative"]["value"] == 987,
                          "Truncated native struct remains opaque with next tagged field intact");
                }
                auto bad = payload;
                if (wide) {
                    const auto nan = std::numeric_limits<double>::quiet_NaN();
                    std::memcpy(bad.data.data(), &nan, 8);
                } else {
                    const auto inf = std::numeric_limits<float>::infinity();
                    std::memcpy(bad.data.data(), &inf, 4);
                }
                check(Package::open(save(root, "invalid", native_property_fixture(bad, name, version)))
                          .exports[0]
                          .properties["Native"]
                          .contains("opaque"),
                      "Non-finite native geometry stays opaque");
            }
        fs::remove_all(root);
        std::cout << checks << " tagged native geometry checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
