#include <cyuediff/reflection.hpp>
#include "reflection_fixture.hpp"
#include "../src/native.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-reflection-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        fs::create_directories(root);
        auto check = [&](bool valid, const char *message) {
            ++checks;
            if (!valid)
                throw std::runtime_error(message);
        };
        auto read = [&](Fixture f, const ReadOptions &options = ReadOptions{}) {
            return Package::open(save(root, "input", std::move(f)), options);
        };
        auto before = read(reflection_fixture());
        const auto &entry = before.exports.at(0);
        check(entry.native_coverage == "struct-declaration-prefix", "Known declaration profile is decoded");
        check(entry.native_data["fields"]["Value"]["propertyFlags"] == 0x1000000000000001ull,
              "Property flags preserve all 64 bits");
        check(entry.native_data["fields"]["Value"]["metadata"]["DisplayName"] == "Visible value",
              "Editor metadata uses stored FName/FString entries");
        check(entry.native_data["super"] == "/Script/Engine.BlueprintGeneratedClass" &&
                  entry.native_data["children"] == Json::array({"TestAsset"}),
              "References resolve exact identities");
        check(entry.native_data["fields"]["Value"]["replicationIndex"] == 7 &&
                  entry.native_data["fields"]["Value"]["replicationNotify"] == "Notify" &&
                  entry.native_data["fields"]["Value"]["replicationCondition"] == 3,
              "Replication declaration is retained without interpreting defaults");
        check(entry.native_end + 4 == entry.size && !entry.opaque.empty(),
              "Unread bytecode tail stays opaque");
        auto projection = reflection_definitions(before);
        check(projection["complete"] &&
                  projection["definitions"]["TestAsset"]["declaration"] == entry.native_data,
              "Public Core projection exposes declared fields");
        const auto reordered = read(reflection_fixture({{}}, false, 1013, 4, 37, false, true));
        check(reflection_definitions(reordered) == projection,
              "Name-table order does not alter declarations");
        auto edited_field = ReflectionField{};
        edited_field.metadata = "Edited value";
        auto after = read(reflection_fixture({edited_field}));
        const auto changes = compare(before, after).to_json().at("changes").dump();
        check(changes.find("/native/fields/Value/metadata/DisplayName") != std::string::npos,
              "General semantic diff names the changed declaration field");
        for (const bool cooked : {false, true}) {
            for (const std::string type : {"Int8Property",
                                           "Int16Property",
                                           "IntProperty",
                                           "Int64Property",
                                           "UInt16Property",
                                           "UInt32Property",
                                           "UInt64Property",
                                           "FloatProperty",
                                           "DoubleProperty",
                                           "NameProperty",
                                           "StrProperty",
                                           "TextProperty",
                                           "Utf8StrProperty",
                                           "AnsiStrProperty",
                                           "ObjectProperty",
                                           "WeakObjectProperty",
                                           "LazyObjectProperty",
                                           "SoftObjectProperty",
                                           "ClassProperty",
                                           "SoftClassProperty",
                                           "StructProperty",
                                           "ByteProperty",
                                           "InterfaceProperty",
                                           "DelegateProperty",
                                           "MulticastDelegateProperty",
                                           "MulticastInlineDelegateProperty",
                                           "MulticastSparseDelegateProperty",
                                           "BoolProperty",
                                           "FieldPathProperty",
                                           "ArrayProperty",
                                           "SetProperty",
                                           "OptionalProperty",
                                           "MapProperty",
                                           "EnumProperty"}) {
                ReflectionField field;
                field.type = type;
                field.nested = {{"IntProperty", "Key"}, {"StrProperty", "MappedValue"}};
                const auto package = read(reflection_fixture({field}, cooked));
                check(package.exports[0].native_coverage == "struct-declaration-prefix",
                      "Supported field class decodes");
                const auto &decoded = package.exports[0].native_data["fields"]["Value"];
                check(decoded["type"] == type && decoded["arrayDimension"] == 1 &&
                          decoded["elementSize"] == 4,
                      "Serialized field layout and class are preserved");
                check(cooked ? decoded["metadata"].is_null() : decoded["metadata"].is_object(),
                      "Cooked profile omits editor metadata bytes explicitly");
                if (type == "BoolProperty")
                    check(decoded["booleanLayout"]["nativeBool"] &&
                              decoded["booleanLayout"]["fieldMask"] == 255,
                          "All six bool layout bytes are read");
                if (type == "MapProperty")
                    check(decoded["key"]["name"] == "Key" && decoded["value"]["name"] == "MappedValue",
                          "Map keeps named key and value declarations");
                if (type == "ClassProperty" || type == "SoftClassProperty")
                    check(decoded["metaClass"] == "TestAsset", "Class meta-class reference is retained");
            }
        }
        check(read(reflection_fixture({}, false)).exports[0].native_data["fields"].empty(),
              "Empty declared field list is valid");
        check(read(reflection_fixture({{}}, false, 1013, 4, 37, true))
                  .exports[0]
                  .native_data.contains("objectGuid"),
              "Optional UObject GUID is bounded and retained");
        for (const auto profile : {std::array<int, 3>{1000, 4, 29}, {1009, 4, 37}, {1013, 4, 37}})
            check(read(reflection_fixture({{}}, false, profile[0], profile[1], profile[2]))
                          .exports[0]
                          .native_coverage == "struct-declaration-prefix",
                  "Supported profile boundaries decode");
        for (const auto profile :
             {std::array<int, 3>{1014, 4, 37}, {1013, 3, 37}, {1013, 4, 28}, {1013, 4, 38}}) {
            auto unavailable = read(reflection_fixture({{}}, false, profile[0], profile[1], profile[2]));
            check(unavailable.exports[0].native_coverage == "opaque" &&
                      !reflection_definitions(unavailable)["complete"].get<bool>(),
                  "Unknown profile stays unavailable instead of guessing");
        }
        for (const auto invalid : {"UnknownPluginProperty", "None"}) {
            ReflectionField field;
            field.type = invalid;
            auto bad = read(reflection_fixture({field}));
            check(bad.exports[0].native_coverage == "opaque" && bad.exports[0].native_data.empty(),
                  "Unknown or null top-level class cannot publish a partial declaration");
        }
        for (int mode = 0; mode < 6; ++mode) {
            ReflectionField field;
            if (mode == 0)
                field.dimension = 0;
            if (mode == 1)
                field.size = -1;
            if (mode == 2)
                field.name = "None";
            if (mode == 3) {
                field.type = "StructProperty";
                field.reference = 12345;
            }
            if (mode == 4)
                field.type = "ArrayProperty";
            if (mode == 5) {
                field.type = "MapProperty";
                field.nested = {{"IntProperty", "Key"}};
            }
            check(read(reflection_fixture({field})).exports[0].native_coverage == "opaque",
                  "Invalid dimension, name, reference or nested declaration is rejected");
        }
        check(read(reflection_fixture({{}, {}})).exports[0].native_coverage == "opaque",
              "Duplicate field identities are rejected");
        ReflectionField deep{
            "ArrayProperty", "Value", {{"ArrayProperty", "Inner", {{"IntProperty", "Leaf"}}}}};
        ReadOptions limited;
        limited.max_depth = 1;
        auto native_rejected = [&](Fixture fixture, const ReadOptions &options, const char *code = "limit") {
            const auto path = save(root, "native-limit", fixture);
            const auto package = Package::open(path);
            detail::Archive archive(path, options);
            archive.seek(package.exports[0].offset + package.exports[0].native_start);
            try {
                detail::decode_struct_definition(
                    archive, package, package.exports[0].offset + package.exports[0].size, options);
            } catch (const Error &error) {
                return error.code == code;
            }
            return false;
        };
        check(native_rejected(reflection_fixture({deep}), limited), "Nested field depth is bounded");
        limited = ReadOptions{};
        limited.max_properties = 2;
        check(native_rejected(reflection_fixture(), limited), "Field metadata shares a finite value budget");
        limited = ReadOptions{};
        limited.max_property_bytes = 16;
        check(native_rejected(reflection_fixture(), limited, "bounds"),
              "Native prefix has a bounded byte window");
        auto original = reflection_fixture();
        const auto native_start = static_cast<std::size_t>(entry.native_start);
        for (std::size_t cut = native_start; cut < entry.native_end; ++cut) {
            auto broken = original;
            broken.script.data.resize(cut);
            const auto size = std::int64_t(cut);
            std::memcpy(broken.header.data.data() + broken.export_offset + 28, &size, 8);
            const auto decoded = read(std::move(broken));
            check(decoded.exports[0].native_coverage == "opaque" && decoded.exports[0].native_data.empty(),
                  "Every truncated prefix is bounded and cannot publish decoded fields");
        }
        ReadOptions disabled;
        disabled.native_reflection = false;
        check(!reflection_definitions(read(reflection_fixture(), disabled))["complete"].get<bool>(),
              "Disabled native declarations are explicitly unavailable");
        auto incomplete = before;
        incomplete.complete_input = false;
        check(!reflection_definitions(incomplete)["complete"].get<bool>(),
              "Incomplete package cannot claim complete declarations");
        OperationControl control;
        control.cancelled = [] { return true; };
        bool cancelled = false;
        try {
            reflection_definitions(before, control);
        } catch (const Error &error) {
            cancelled = error.code == "cancelled";
        }
        check(cancelled, "Public projection propagates cancellation");
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto output = fs::u8path(argv[2]);
            fs::create_directories(output);
            save(output, "before", reflection_fixture());
            save(output, "after", reflection_fixture({edited_field}));
            save(output, "split", reflection_fixture({deep}), true);
        }
        fs::remove_all(root);
        std::cout << checks << " reflection declaration checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
