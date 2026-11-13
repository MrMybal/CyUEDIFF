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
Fixture pin_type_fixture(int container, bool alternate_name = false, bool truncate = false,
                         int version = 1009, bool invalid_boolean = false) {
    return fixture(version, 1, false, 0,
                   {"PinType", "EdGraphPinType", "Type_1", "Type", "ValueType", "Member"},
                   [&](Bytes &out, const NameWriter &name) {
                       Bytes payload;
                       name(payload, alternate_name ? "Type" : "Type_1");
                       if (alternate_name)
                           payload.patch(4, 2); // Same display, different stored FName components.
                       name(payload, "None");
                       payload.i(0);
                       payload.put<std::uint8_t>(static_cast<std::uint8_t>(container));
                       if (container == 3) {
                           name(payload, "ValueType");
                           name(payload, "None");
                           payload.i(0);
                           payload.i(1);
                           payload.i(0);
                           payload.i(1);
                       }
                       payload.i(invalid_boolean ? 2 : 1);
                       payload.i(0);
                       payload.i(0);
                       name(payload, "Member");
                       payload.zeros(16);
                       payload.i(1);
                       payload.i(0);
                       payload.i(1);
                       if (truncate)
                           payload.data.pop_back();
                       name(out, "PinType");
                       name(out, "StructProperty");
                       out.i(static_cast<int>(payload.data.size()));
                       out.i(0);
                       name(out, "EdGraphPinType");
                       out.zeros(16);
                       out.put<std::uint8_t>(0);
                       out.append(payload);
                   },
                   40, 40, -1, -1, -1, 37, -1, "DataAsset", "/Script/CoreUObject",
                   {{{0xb0d832e4u, 0x1f894f0du, 0xaccf7eb7u, 0x36fd4aa2u}, 10},
                    {{0x9c54d522u, 0xa8264fbeu, 0x94210746u, 0x61b482d0u}, 44},
                    {{0x697dd581u, 0xe64f41abu, 0xaa4a51ecu, 0xbeb7b628u}, 101}});
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
            for (const char *token : {"Box", "Box3f", "Box3d", "Sphere", "Sphere3f", "Sphere3d", "Matrix",
                                      "Matrix44f", "Matrix44d", "Plane", "Plane4f", "Plane4d"}) {
                const std::string name = token;
                if (version < 1004 && name == "Sphere3d")
                    continue;
                const bool wide = version >= 1004 && name.back() != 'f';
                auto payload = geometry_value(name, wide);
                auto package =
                    Package::open(save(root, "before", native_property_fixture(payload, name, version)));
                const auto &prop = package.exports[0].properties["Native"];
                check(prop.contains("value") && package.exports[0].properties["AfterNative"]["value"] == 987,
                      "Tagged native bounds preserve following fields");
                const std::string layout = name.find("Matrix") == 0            ? "matrix4"
                                           : name.find("Plane") == 0           ? "plane"
                                           : name.find("BoxSphereBounds") == 0 ? "box-sphere"
                                           : name.find("Sphere") == 0          ? "sphere"
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
                auto padded = payload;
                padded.zeros(1);
                check(Package::open(save(root, "padded", native_property_fixture(padded, name, version)))
                          .exports[0]
                          .properties["Native"]
                          .contains("opaque"),
                      "Fixed binary native layouts reject unexpected trailing bytes");
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
        for (int version : {1000, 1009, 1013})
            for (const char *token : {"BoxSphereBounds", "BoxSphereBounds3f", "BoxSphereBounds3d"}) {
                const std::string name = token;
                const bool wide = version >= 1004 && name.back() != 'f', sphere = name.find("Sphere") == 0;
                const auto package =
                    Package::open(save(root, "reflected", reflected_geometry_fixture(name, version)));
                const auto value = package.exports[0].properties["Native"]["value"];
                check(value[sphere ? "Center" : "Origin"]["value"][0] == (wide ? 1000000000000.125 : -3.5),
                      "Spheres and combined bounds use reflected fields, including explicit vector aliases");
                check(value[sphere ? "W" : "SphereRadius"]["value"] ==
                              (wide ? 1000000000000.125 : -3.5) + (sphere ? 3 : 6) &&
                          package.exports[0].properties["AfterNative"]["value"] == 987,
                      "Reflected radius and following tag retained");
                const auto after = Package::open(
                    save(root, "reflected-after", reflected_geometry_fixture(name, version, true)));
                check(!compare(package, after).changes.empty(), "Reflected bounds have semantic field diffs");
                const auto raw = Package::open(save(
                    root, "raw-refused", native_property_fixture(geometry_value(name, wide), name, version)));
                check(raw.exports[0].properties["Native"].contains("opaque"),
                      "Archive operator alone does not certify a native StructProperty serializer");
            }
        for (int container = 0; container <= 3; ++container) {
            auto package = Package::open(save(root, "pin", pin_type_fixture(container)));
            const auto &prop = package.exports[0].properties.at("PinType");
            check(prop.contains("value") && !package.source_read_limit_failed,
                  "Native Blueprint variable types do not trigger false tagged/count limits");
            const auto &value = prop.at("value");
            check(value.at("category") == "Type_1" && value.at("container") == container &&
                      value.at("member").at("name") == "Member" && value.at("const") == true &&
                      value.at("singlePrecisionDefault") == true,
                  "Scalar, array, set and map variable types retain their serialized fields");
            check(container != 3 || (value.at("valueType").at("category") == "ValueType" &&
                                     value.at("valueType").at("objectWrapper") == true),
                  "Map terminal types preserve their native fields");
            auto alternate = Package::open(save(root, "pin-after", pin_type_fixture(container, true)));
            check(alternate.exports[0].properties.at("PinType").at("value") == value &&
                      !compare(package, alternate).changes.empty(),
                  "Native pin type names compare actual components even with identical display text");
            check(package.name_values.at("names")
                          .at("/objects/TestAsset/properties/PinType/value/member/name")
                          .at("base") == "Member",
                  "Native member FName identity is recorded at its real value path");
        }
        for (const auto &bad :
             {pin_type_fixture(4), pin_type_fixture(0, false, true), pin_type_fixture(0, false, false, 1010),
              pin_type_fixture(0, false, false, 1009, true)}) {
            const auto package = Package::open(save(root, "pin-refused", bad));
            check(package.exports[0].properties.at("PinType").contains("opaque") &&
                      !package.source_read_limit_failed &&
                      package.exports[0].properties.at("Value").at("value") == 1,
                  "Invalid native pin values and unknown profiles stay opaque without fake limit failures");
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
