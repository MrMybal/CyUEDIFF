#include <cyuediff/reflection.hpp>
#include "native.hpp"
#include <algorithm>
#include <set>
namespace cyuediff {
namespace detail {
namespace {
constexpr const char *core_id = "375ec13c06e448fbb50084f0262a717e";
constexpr const char *framework_id = "cffc743f43b04480939114df171d2073";
bool struct_class(const std::string &name) {
    static const std::set<std::string> classes = {"/Script/CoreUObject.Class",
                                                  "/Script/CoreUObject.ScriptStruct",
                                                  "/Script/CoreUObject.Function",
                                                  "/Script/Engine.BlueprintGeneratedClass",
                                                  "/Script/Engine.AnimBlueprintGeneratedClass",
                                                  "/Script/Engine.UserDefinedStruct",
                                                  "/Script/UMG.WidgetBlueprintGeneratedClass"};
    return classes.count(name) != 0;
}
class FieldReader {
    Archive &a;
    const Package &p;
    const ReadOptions &options;
    bool editor_metadata;
    std::uint32_t values = 0;
    void charge() {
        options.control.check();
        if (values >= options.max_properties)
            throw Error("limit", "Serialized field metadata exceeds its value budget", a.tell());
        ++values;
    }

  public:
    FieldReader(Archive &archive, const Package &package, const ReadOptions &read)
        : a(archive), p(package), options(read), editor_metadata(!(package.summary.flags & 0x200u)) {}
    std::string name() {
        const auto index = a.get<std::int32_t>();
        const auto number = a.get<std::int32_t>();
        if (index < 0 || static_cast<std::size_t>(index) >= p.names.size() || number < 0)
            throw Error("reflection-name", "Invalid serialized field FName", a.tell() - 8);
        return p.names[static_cast<std::size_t>(index)] + (number ? "_" + std::to_string(number - 1) : "");
    }
    bool boolean() {
        const auto value = a.get<std::uint32_t>();
        if (value > 1)
            throw Error("reflection-boolean", "Invalid serialized field boolean", a.tell() - 4);
        return value != 0;
    }
    std::string reference() {
        return p.reference(a.get<std::int32_t>());
    }
    Json field(unsigned depth, bool nullable = false) {
        charge();
        if (depth > options.max_depth)
            throw Error("limit", "Serialized field nesting exceeds configured depth", a.tell());
        const auto type = name();
        if (nullable && type == "None")
            return nullptr;
        static const std::set<std::string> plain = {
            "Int8Property",   "Int16Property",  "IntProperty",     "Int64Property",  "UInt16Property",
            "UInt32Property", "UInt64Property", "FloatProperty",   "DoubleProperty", "NameProperty",
            "StrProperty",    "TextProperty",   "Utf8StrProperty", "AnsiStrProperty"};
        static const std::set<std::string> objects = {"ObjectProperty",     "WeakObjectProperty",
                                                      "LazyObjectProperty", "SoftObjectProperty",
                                                      "ClassProperty",      "SoftClassProperty"};
        static const std::set<std::string> composite = {"ArrayProperty",
                                                        "SetProperty",
                                                        "MapProperty",
                                                        "OptionalProperty",
                                                        "EnumProperty",
                                                        "StructProperty",
                                                        "ByteProperty",
                                                        "InterfaceProperty",
                                                        "DelegateProperty",
                                                        "MulticastDelegateProperty",
                                                        "MulticastInlineDelegateProperty",
                                                        "MulticastSparseDelegateProperty",
                                                        "BoolProperty",
                                                        "FieldPathProperty"};
        if (!plain.count(type) && !objects.count(type) && !composite.count(type))
            throw Error("reflection-type", "Unsupported serialized field class: " + type, a.tell() - 8);
        const auto field_name = name();
        if (field_name.empty() || field_name == "None")
            throw Error("reflection-name", "Serialized property has no name", a.tell() - 8);
        Json out = {{"name", field_name}, {"type", type}, {"objectFlags", a.get<std::uint32_t>()}};
        Json metadata = nullptr;
        if (editor_metadata) {
            metadata = Json::object();
            if (boolean()) {
                const auto count = a.count(options.max_properties);
                for (std::uint32_t i = 0; i < count; ++i) {
                    charge();
                    const auto key = name();
                    if (key == "None" || metadata.contains(key))
                        throw Error("reflection-metadata", "Invalid or duplicate field metadata key",
                                    a.tell());
                    metadata[key] = a.string();
                }
            }
        }
        out["metadata"] = std::move(metadata);
        const auto dimension = a.get<std::int32_t>();
        const auto size = a.get<std::int32_t>();
        if (dimension < 1 || static_cast<std::uint32_t>(dimension) > options.max_table_entries || size < 1 ||
            static_cast<std::uint64_t>(size) > options.max_property_bytes)
            throw Error("reflection-layout", "Invalid serialized property dimension or element size",
                        a.tell() - 8);
        out["arrayDimension"] = dimension;
        out["elementSize"] = size;
        out["propertyFlags"] = a.get<std::uint64_t>();
        out["replicationIndex"] = a.get<std::uint16_t>();
        out["replicationNotify"] = name();
        out["replicationCondition"] = a.get<std::uint8_t>();
        if (objects.count(type)) {
            out["propertyClass"] = reference();
            if (type == "ClassProperty" || type == "SoftClassProperty")
                out["metaClass"] = reference();
        } else if (type == "StructProperty")
            out["structure"] = reference();
        else if (type == "ByteProperty" || type == "EnumProperty") {
            out["enumeration"] = reference();
            if (type == "EnumProperty")
                out["underlying"] = field(depth + 1, true);
        } else if (type == "InterfaceProperty")
            out["interfaceClass"] = reference();
        else if (type.find("DelegateProperty") != std::string::npos)
            out["signatureFunction"] = reference();
        else if (type == "ArrayProperty" || type == "SetProperty" || type == "OptionalProperty") {
            auto inner = field(depth + 1, true);
            if (inner.is_null())
                throw Error("reflection-layout", "Container property has no inner declaration", a.tell());
            out["inner"] = std::move(inner);
        } else if (type == "MapProperty") {
            out["key"] = field(depth + 1, true);
            out["value"] = field(depth + 1, true);
            if (out["key"].is_null() || out["value"].is_null())
                throw Error("reflection-layout", "Map property has a missing declaration", a.tell());
        } else if (type == "BoolProperty") {
            Json layout;
            for (const char *key : {"fieldSize", "byteOffset", "byteMask", "fieldMask", "boolSize"})
                layout[key] = a.get<std::uint8_t>();
            const auto native = a.get<std::uint8_t>();
            if (native > 1)
                throw Error("reflection-boolean", "Invalid native-bool declaration flag", a.tell() - 1);
            layout["nativeBool"] = native != 0;
            out["booleanLayout"] = std::move(layout);
        } else if (type == "FieldPathProperty")
            out["fieldClass"] = name();
        return out;
    }
    Json structure() {
        Json out = {{"kind", "struct-definition"},
                    {"coverage", "declared-fields"},
                    {"inheritedFieldsResolved", false},
                    {"editorMetadataPresent", editor_metadata}};
        if (boolean())
            out["objectGuid"] = a.guid();
        out["super"] = reference();
        const auto child_count = a.count(options.max_properties);
        out["children"] = Json::array();
        for (std::uint32_t i = 0; i < child_count; ++i) {
            charge();
            out["children"].push_back(reference());
        }
        const auto count = a.count(options.max_properties);
        out["fields"] = Json::object();
        out["fieldOrder"] = Json::array();
        for (std::uint32_t i = 0; i < count; ++i) {
            auto definition = field(0);
            const auto key = definition.at("name").get<std::string>();
            if (out["fields"].contains(key))
                throw Error("reflection-name", "Duplicate declared property name", a.tell());
            out["fieldOrder"].push_back(key);
            out["fields"][key] = std::move(definition);
        }
        return out;
    }
};
} // namespace
bool is_reflected_struct(const ObjectExport &entry) {
    return struct_class(entry.class_name);
}
NativeDecodeResult decode_struct_definition(Archive &archive, const Package &package, std::uint64_t end,
                                            const ReadOptions &options) {
    const auto framework = package.summary.custom_versions.value(framework_id, -1);
    if (package.summary.ue5_version < 1000 || package.summary.ue5_version > 1013 ||
        package.summary.custom_versions.value(core_id, -1) != 4 || framework < 29 || framework > 37)
        throw Error("reflection-profile", "Unsupported serialized field-definition profile", archive.tell());
    const auto start = archive.tell();
    if (end < start)
        throw Error("bounds", "Invalid serialized field-definition range", start);
    Archive::ReadLimit window(archive, start + std::min(end - start, options.max_property_bytes));
    FieldReader reader(archive, package, options);
    auto result = reader.structure();
    return {std::move(result), archive.tell()};
}
} // namespace detail
Json reflection_definitions(const Package &package, const OperationControl &control) {
    control.check();
    Json definitions = Json::object(), unavailable = Json::array();
    for (const auto &entry : package.exports) {
        control.check();
        if (!detail::is_reflected_struct(entry))
            continue;
        if (entry.native_data.value("kind", std::string{}) == "struct-definition")
            definitions[entry.path] = {{"class", entry.class_name}, {"declaration", entry.native_data}};
        else
            unavailable.push_back({{"path", entry.path},
                                   {"class", entry.class_name},
                                   {"nativeCoverage", entry.native_coverage}});
    }
    return {{"schema", "cyuediff.reflection/1"},
            {"coverage", "declared-fields"},
            {"complete", package.complete_input && unavailable.empty()},
            {"definitions", definitions},
            {"unavailable", unavailable}};
}
} // namespace cyuediff
