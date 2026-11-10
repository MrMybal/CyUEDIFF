#pragma once
#include <cyuediff/defaults.hpp>
namespace cyuediff::test::default_snapshot {
inline Json archive(std::string hex = "") {
    return {{"format", "cyuediff.engine-field-wire/1"},
            {"available", true},
            {"unversioned", true},
            {"filterEditorOnly", false},
            {"isCooking", false},
            {"forceUnicode", true},
            {"comparisonDefaults", "none"},
            {"ue4", 522},
            {"ue5", 1009},
            {"hex", hex},
            {"names", Json::array()},
            {"objects", Json::array()},
            {"customVersions", Json::array({{{"guid", std::string(32, 'a')}, {"version", -1}}})}};
}
inline Json property(const char *name, unsigned index, Json type) {
    return {{"name", name},
            {"index", index},
            {"arrayDim", 1},
            {"type", std::move(type)},
            {"flags", "0000000000000001"},
            {"serialize", true}};
}
inline Json declaration(const std::string &path, Json properties, const char *kind = "class",
                        const char *super = "") {
    return {{"path", path},
            {"super", super},
            {"kind", kind},
            {"slots", properties.size()},
            {"properties", std::move(properties)},
            {"nativeSerialization", std::string(kind) == "native-struct"},
            {"binarySerialization", std::string(kind) == "native-struct"},
            {"structGuid", std::string(32, '0')}};
}
inline Json node(const std::string &owner, const Json &property, const std::string &hex) {
    return {{"owner", owner},
            {"name", property.at("name")},
            {"arrayIndex", 0},
            {"arrayDim", 1},
            {"type", property.at("type")},
            {"flags", property.at("flags")},
            {"available", false},
            {"reason", "Reflection display is deliberately not used"},
            {"wire", archive(hex)}};
}
inline Json capture() {
    const std::string root = "/Script/Test.Child", base = "/Script/Test.Base", record = "/Script/Test.Record",
                      native = "/Script/Test.NativePoint";
    auto child_fields = Json::array({property("Count", 0, {{"name", "IntProperty"}}),
                                     property("Large", 1, {{"name", "Int64Property"}}),
                                     property("Record", 2,
                                              {{"name", "StructProperty"},
                                               {"schema", record},
                                               {"structGuid", std::string(32, '0')},
                                               {"nativeSerialization", false}}),
                                     property("Point", 3,
                                              {{"name", "StructProperty"},
                                               {"schema", native},
                                               {"structGuid", std::string(32, '0')},
                                               {"nativeSerialization", true}})});
    auto base_fields = Json::array({property("Inherited", 0, {{"name", "IntProperty"}})});
    auto record_fields = Json::array({property("Count", 0, {{"name", "IntProperty"}})});
    Json declarations = {{root, declaration(root, child_fields, "class", base.c_str())},
                         {base, declaration(base, base_fields)},
                         {record, declaration(record, record_fields, "struct")},
                         {native, declaration(native, Json::array(), "native-struct")}};
    Json fields = {{root + ".Count", node(root, child_fields[0], "29000000")},
                   {root + ".Large", node(root, child_fields[1], "0100000000002000")},
                   {root + ".Record", node(root, child_fields[2], "000313000000")},
                   {root + ".Point", node(root, child_fields[3], "09000000fcffffff")},
                   {base + ".Inherited", node(base, base_fields[0], "1b000000")}};
    return {
        {"format", "cyuediff.engine-default-capture/1"},
        {"engineVersion", "5.3-test"},
        {"completeCapture", true},
        {"classDefault", true},
        {"blueprintClass", false},
        {"classGeneratedBy", nullptr},
        {"filterEditorOnly", false},
        {"archetype", nullptr},
        {"object", {{"path", "/Script/Test.Default__Child"}, {"class", root}, {"flags", "0000000000000010"}}},
        {"archive", archive()},
        {"declarations", declarations},
        {"enumerations", Json::object()},
        {"fields", fields},
        {"structures",
         {{record,
           {{"schema", record},
            {"structGuid", std::string(32, '0')},
            {"available", true},
            {"fields", {{record + ".Count", node(record, record_fields[0], "07000000")}}}}}}}};
}
inline DefaultSnapshotReadOptions options() {
    DefaultSnapshotReadOptions result;
    result.expected_engine_version = "5.3-test";
    result.expected_ue4 = 522;
    result.expected_ue5 = 1009;
    return result;
}
inline DefaultSnapshot parse(const Json &j, const DefaultSnapshotReadOptions &o) {
    const auto text = j.dump();
    return DefaultSnapshot::parse({text.begin(), text.end()}, o);
}
} // namespace cyuediff::test::default_snapshot
