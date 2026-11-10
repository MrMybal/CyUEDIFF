#include <cyuediff/schema.hpp>
#include "string_value.hpp"
#include "value_identity.hpp"
#include <cyuediff/string_encoding.hpp>
#include "archive.hpp"
#include "text_value.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
namespace cyuediff {
namespace {
enum class NativeShape {
    array,
    transform,
    guid,
    ticks,
    box3,
    sphere,
    box_sphere,
    matrix4,
    plane,
    per_platform
};
struct NativeLayoutInfo {
    NativeStructLayout layout;
    const char *name;
    unsigned kind, components;
    NativeShape shape = NativeShape::array;
};
constexpr NativeLayoutInfo native_layouts[] = {
    {NativeStructLayout::vector2_f32, "vector2-f32", 3, 2},
    {NativeStructLayout::vector2_f64, "vector2-f64", 7, 2},
    {NativeStructLayout::vector3_f32, "vector3-f32", 3, 3},
    {NativeStructLayout::vector3_f64, "vector3-f64", 7, 3},
    {NativeStructLayout::vector4_f32, "vector4-f32", 3, 4},
    {NativeStructLayout::vector4_f64, "vector4-f64", 7, 4},
    {NativeStructLayout::quat_f32, "quat-f32", 3, 4},
    {NativeStructLayout::quat_f64, "quat-f64", 7, 4},
    {NativeStructLayout::rotator_f32, "rotator-f32", 3, 3},
    {NativeStructLayout::rotator_f64, "rotator-f64", 7, 3},
    {NativeStructLayout::transform_f32, "transform-f32", 3, 10, NativeShape::transform},
    {NativeStructLayout::transform_f64, "transform-f64", 7, 10, NativeShape::transform},
    {NativeStructLayout::guid, "guid", 19, 4, NativeShape::guid},
    {NativeStructLayout::linear_color_f32, "linear-color-f32", 3, 4},
    {NativeStructLayout::color_bgra8, "color-bgra8", 0, 4},
    {NativeStructLayout::int_point, "int-point", 2, 2},
    {NativeStructLayout::int_vector, "int-vector", 2, 3},
    {NativeStructLayout::date_time_i64, "date-time-i64", 21, 1, NativeShape::ticks},
    {NativeStructLayout::timespan_i64, "timespan-i64", 21, 1, NativeShape::ticks},
    {NativeStructLayout::box3_f32, "box3-f32", 3, 6, NativeShape::box3},
    {NativeStructLayout::box3_f64, "box3-f64", 7, 6, NativeShape::box3},
    {NativeStructLayout::sphere_f32, "sphere-f32", 3, 4, NativeShape::sphere},
    {NativeStructLayout::sphere_f64, "sphere-f64", 7, 4, NativeShape::sphere},
    {NativeStructLayout::box_sphere_f32, "box-sphere-f32", 3, 7, NativeShape::box_sphere},
    {NativeStructLayout::box_sphere_f64, "box-sphere-f64", 7, 7, NativeShape::box_sphere},
    {NativeStructLayout::matrix4_f32, "matrix4-f32", 3, 16, NativeShape::matrix4},
    {NativeStructLayout::matrix4_f64, "matrix4-f64", 7, 16, NativeShape::matrix4},
    {NativeStructLayout::plane_f32, "plane-f32", 3, 4, NativeShape::plane},
    {NativeStructLayout::plane_f64, "plane-f64", 7, 4, NativeShape::plane},
    {NativeStructLayout::per_platform_float_cooked, "per-platform-float-cooked", 3, 1,
     NativeShape::per_platform},
    {NativeStructLayout::per_platform_int_cooked, "per-platform-int-cooked", 2, 1, NativeShape::per_platform},
    {NativeStructLayout::per_platform_bool_cooked, "per-platform-bool-cooked", 1, 1,
     NativeShape::per_platform}};
const NativeLayoutInfo &native_layout_info(NativeStructLayout layout) {
    for (const auto &info : native_layouts)
        if (info.layout == layout)
            return info;
    throw Error("schema-context", "Unsupported native struct layout");
}
void require(bool condition, const char *message) {
    if (!condition)
        throw Error("schema-format", message);
}
struct Reader {
    const std::vector<std::uint8_t> &data;
    const OperationControl &control;
    std::uint64_t pos, end;
    void range(std::uint64_t n) const {
        control.check();
        if (pos > end || n > end - pos)
            throw Error("bounds", "Schema/property read exceeds its window", pos);
    }
    std::uint64_t number(unsigned bytes) {
        range(bytes);
        std::uint64_t n = 0;
        for (unsigned j = 0; j < bytes; ++j)
            n |= std::uint64_t(data[static_cast<std::size_t>(pos++)]) << (8 * j);
        return n;
    }
    std::int32_t i32() {
        return static_cast<std::int32_t>(number(4));
    }
    std::uint32_t count(std::uint32_t limit) {
        auto n = number(4);
        if (n > limit)
            throw Error("schema-limit", "Schema/property count exceeds configured limit", pos - 4);
        return static_cast<std::uint32_t>(n);
    }
    void skip(std::uint64_t n) {
        range(n);
        pos += n;
    }
};
const char *type_name(unsigned kind) {
    static const char *names[] = {"ByteProperty",        "BoolProperty",
                                  "IntProperty",         "FloatProperty",
                                  "ObjectProperty",      "NameProperty",
                                  "DelegateProperty",    "DoubleProperty",
                                  "ArrayProperty",       "StructProperty",
                                  "StrProperty",         "TextProperty",
                                  "InterfaceProperty",   "MulticastDelegateProperty",
                                  "WeakObjectProperty",  "LazyObjectProperty",
                                  "AssetObjectProperty", "SoftObjectProperty",
                                  "UInt64Property",      "UInt32Property",
                                  "UInt16Property",      "Int64Property",
                                  "Int16Property",       "Int8Property",
                                  "MapProperty",         "SetProperty",
                                  "EnumProperty",        "FieldPathProperty",
                                  "OptionalProperty",    "Utf8StrProperty",
                                  "AnsiStrProperty"};
    return kind < 31 ? names[kind] : "Unknown";
}
void append_utf8(std::string &s, std::uint32_t cp) {
    if (cp < 128)
        s += static_cast<char>(cp);
    else if (cp < 2048) {
        s += static_cast<char>(192 | (cp >> 6));
        s += static_cast<char>(128 | (cp & 63));
    } else if (cp < 65536) {
        s += static_cast<char>(224 | (cp >> 12));
        s += static_cast<char>(128 | ((cp >> 6) & 63));
        s += static_cast<char>(128 | (cp & 63));
    } else {
        s += static_cast<char>(240 | (cp >> 18));
        s += static_cast<char>(128 | ((cp >> 12) & 63));
        s += static_cast<char>(128 | ((cp >> 6) & 63));
        s += static_cast<char>(128 | (cp & 63));
    }
}
} // namespace
const char *native_struct_layout_name(NativeStructLayout layout) {
    return native_layout_info(layout).name;
}
NativeStructLayout parse_native_struct_layout(const std::string &name) {
    for (const auto &info : native_layouts)
        if (name == info.name)
            return info.layout;
    throw Error("schema-context", "Unsupported native struct layout: " + name);
}
struct UnversionedSchemas::Impl {
    std::string hash, source_format = "cyuediff.usmap/1";
    unsigned version = 0;
    Json versioning = nullptr, extensions = Json::array();
    std::map<std::string, SchemaDefinition> schemas;
    std::map<std::string, SchemaEnum> enums;
    std::map<std::string, std::vector<std::string>> schema_aliases, enum_aliases;
};
UnversionedSchemas::UnversionedSchemas(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
UnversionedSchemas UnversionedSchemas::read(const fs::path &path, const SchemaReadOptions &options) {
    options.control.check();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw Error("file-open", "Cannot open USMAP mappings: " + path.u8string());
    auto size = file.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > options.max_bytes)
        throw Error("schema-limit", "USMAP file exceeds configured limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    for (std::size_t at = 0; at < bytes.size();) {
        options.control.check();
        auto n = std::min<std::size_t>(65536, bytes.size() - at);
        if (!file.read(reinterpret_cast<char *>(bytes.data() + at), n))
            throw Error("file-read", "Cannot read USMAP mappings");
        at += n;
    }
    return parse(bytes, options);
}
UnversionedSchemas UnversionedSchemas::parse(const std::vector<std::uint8_t> &bytes,
                                             const SchemaReadOptions &options) {
    if (bytes.size() > options.max_bytes)
        throw Error("schema-limit", "USMAP file exceeds configured limit");
    Reader r{bytes, options.control, 0, bytes.size()};
    require(r.number(2) == 0x30c4, "Invalid USMAP magic");
    auto p = std::make_shared<Impl>();
    p->version = static_cast<unsigned>(r.number(1));
    require(p->version <= 4, "Unsupported USMAP version");
    if (p->version >= 1) {
        auto has = r.number(4);
        require(has <= 1, "Invalid USMAP versioning flag");
        if (has) {
            auto ue4 = r.i32(), ue5 = r.i32();
            auto count = r.count(options.max_entries);
            Json custom = Json::array();
            r.range(std::uint64_t(count) * 20);
            for (unsigned j = 0; j < count; ++j) {
                std::string guid;
                static const char digits[] = "0123456789abcdef";
                for (unsigned k = 0; k < 16; ++k) {
                    auto b = r.number(1);
                    guid += digits[b >> 4];
                    guid += digits[b & 15];
                }
                custom.push_back({{"guidBytes", guid}, {"version", r.i32()}});
            }
            p->versioning = {{"ue4", ue4}, {"ue5", ue5}, {"customVersions", custom}, {"netCL", r.number(4)}};
        }
    }
    auto compression = r.number(1), compressed = r.number(4), expanded = r.number(4);
    require(compressed == r.end - r.pos, "USMAP compressed size mismatch");
    if (expanded > options.max_bytes)
        throw Error("schema-limit", "Expanded USMAP exceeds configured limit");
    std::vector<std::uint8_t> payload;
    if (!compression) {
        require(compressed == expanded, "USMAP uncompressed size mismatch");
        payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(r.pos), bytes.end());
    } else {
        require(compression <= 3, "Unsupported USMAP compression method");
        if (!options.decoder)
            throw Error("schema-codec", "Compressed USMAP requires an explicit decoder");
        payload.resize(static_cast<std::size_t>(expanded));
        const char *method = compression == 1 ? "Oodle" : (compression == 2 ? "Brotli" : "Zstd");
        options.control.check();
        auto written = options.decoder(method, bytes.data() + r.pos, static_cast<std::size_t>(compressed),
                                       payload.data(), payload.size());
        options.control.check();
        require(written == payload.size(), "USMAP decoder output size mismatch");
    }
    Reader in{payload, options.control, 0, payload.size()};
    std::uint64_t text_bytes = 0;
    auto text = [&](std::uint64_t n) {
        options.control.check();
        if (n > options.max_text_bytes - text_bytes)
            throw Error("schema-limit", "Resolved USMAP text exceeds configured limit");
        text_bytes += n;
    };
    std::vector<std::string> names;
    auto count = in.count(options.max_entries);
    for (unsigned j = 0; j < count; ++j) {
        auto n = in.number(p->version >= 2 ? 2 : 1);
        in.range(n);
        text(n);
        std::string name;
        for (unsigned k = 0; k < n; ++k) {
            auto c = in.number(1);
            require(c > 0 && c < 128, "USMAP names must contain non-NUL ASCII bytes");
            name += static_cast<char>(c);
        }
        names.push_back(std::move(name));
    }
    auto read_name = [&](Reader &source, bool optional = false) {
        auto index = source.i32();
        if (optional && index == -1)
            return std::string{};
        require(index >= 0 && static_cast<std::size_t>(index) < names.size(), "Invalid USMAP name index");
        const auto &value = names[static_cast<std::size_t>(index)];
        require(!value.empty(), "Empty USMAP identity");
        text(value.size());
        return value;
    };
    auto name = [&](bool optional = false) { return read_name(in, optional); };
    std::vector<SchemaEnum> enum_records;
    std::vector<SchemaDefinition> schema_records;
    count = in.count(options.max_entries);
    std::uint64_t entries = 0;
    auto charge = [&](std::uint64_t n) {
        if (n > options.max_entries - entries)
            throw Error("schema-limit", "USMAP total descriptor count exceeds limit");
        entries += n;
    };
    for (unsigned j = 0; j < count; ++j) {
        SchemaEnum record;
        record.name = name();
        auto n = in.number(p->version >= 3 ? 2 : 1);
        charge(n);
        Json values = Json::array();
        for (unsigned k = 0; k < n; ++k) {
            auto value =
                p->version >= 4 ? static_cast<std::int64_t>(in.number(8)) : static_cast<std::int64_t>(k);
            values.push_back({{"value", value}, {"name", name()}});
        }
        record.values = std::move(values);
        enum_records.push_back(std::move(record));
    }
    std::function<SchemaType(unsigned)> type = [&](unsigned depth) {
        if (depth >= options.max_depth)
            throw Error("schema-limit", "USMAP type nesting exceeds limit");
        charge(1);
        SchemaType t;
        t.kind = static_cast<std::uint8_t>(in.number(1));
        require(t.kind <= 30 || t.kind == 255, "Unsupported USMAP type descriptor");
        if (t.kind == 26) {
            t.arguments.push_back(type(depth + 1));
            t.name = name();
        } else if (t.kind == 9)
            t.name = name();
        else if (t.kind == 8 || t.kind == 25 || t.kind == 28)
            t.arguments.push_back(type(depth + 1));
        else if (t.kind == 24) {
            t.arguments.push_back(type(depth + 1));
            t.arguments.push_back(type(depth + 1));
        }
        return t;
    };
    count = in.count(options.max_entries);
    for (unsigned j = 0; j < count; ++j) {
        SchemaDefinition schema;
        schema.name = name();
        schema.super = name(true);
        schema.slots = static_cast<std::uint16_t>(in.number(2));
        charge(schema.slots);
        auto descriptors = in.number(2);
        require(descriptors <= schema.slots, "Too many USMAP properties for schema slots");
        std::set<std::string> property_names;
        for (unsigned k = 0; k < descriptors; ++k) {
            SchemaProperty property;
            property.index = static_cast<std::uint16_t>(in.number(2));
            property.array_size = static_cast<std::uint8_t>(in.number(1));
            property.name = name();
            property.type = type(0);
            require(property.array_size && unsigned(property.index) + property.array_size <= schema.slots,
                    "Invalid USMAP static array range");
            require(property_names.insert(property.name).second, "Duplicate USMAP property name");
            std::uint64_t type_nodes = 0, type_text = 0;
            std::function<void(const SchemaType &)> measure_type = [&](const SchemaType &t) {
                ++type_nodes;
                type_text += t.name.size();
                for (const auto &child : t.arguments)
                    measure_type(child);
            };
            measure_type(property.type);
            charge(std::uint64_t(property.array_size - 1) * type_nodes);
            text(std::uint64_t(property.array_size - 1) * (property.name.size() + type_text));
            for (unsigned n = 0; n < property.array_size; ++n)
                require(schema.properties.emplace(static_cast<std::uint16_t>(property.index + n), property)
                            .second,
                        "Overlapping USMAP property slots");
        }
        schema_records.push_back(std::move(schema));
        options.control.checkpoint("schema-load", j + 1, count);
    }
    std::set<std::string> extension_ids;
    bool has_module_paths = false;
    auto module_path = [&](std::string value) {
        require(value.empty() || (value.front() == '/' && value.find('.') == std::string::npos &&
                                  value.find('\\') == std::string::npos),
                "Invalid USMAP module path");
        return value;
    };
    auto exact_count = [&](Reader &source, std::size_t expected) {
        auto n = source.count(options.max_entries);
        require(n == expected, "USMAP extension table count mismatch");
    };
    auto extension = [&](const std::string &id, std::uint64_t length) {
        if (!extension_ids.insert(id).second)
            throw Error("schema-extension", "Duplicate USMAP extension: " + id);
        in.range(length);
        Reader ext{payload, options.control, in.pos, in.pos + length};
        if (id == "PPTH" || id == "MODL") {
            if (has_module_paths)
                throw Error("schema-extension", "Conflicting USMAP module-path extensions");
            has_module_paths = true;
        }
        if (id == "PPTH") {
            if (ext.number(1) != 0)
                throw Error("schema-extension", "Unsupported PPTH version");
            exact_count(ext, enum_records.size());
            for (auto &record : enum_records)
                record.module = module_path(read_name(ext, true));
            exact_count(ext, schema_records.size());
            for (auto &record : schema_records)
                record.module = module_path(read_name(ext, true));
        } else if (id == "MODL") {
            auto n = ext.number(2);
            charge(n);
            std::vector<std::string> modules;
            std::set<std::string> unique;
            for (unsigned j = 0; j < n; ++j) {
                auto bytes = ext.number(1);
                ext.range(bytes);
                text(bytes);
                std::string value;
                for (unsigned k = 0; k < bytes; ++k) {
                    auto c = ext.number(1);
                    require(c > 0 && c < 128, "Invalid MODL module string");
                    value += static_cast<char>(c);
                }
                value = module_path(std::move(value));
                require(unique.insert(value).second, "Duplicate MODL module path");
                modules.push_back(std::move(value));
            }
            for (auto &record : schema_records) {
                auto index = ext.number(n > 255 ? 2 : 1);
                require(index < modules.size(), "Invalid MODL module index");
                text(modules[static_cast<std::size_t>(index)].size());
                record.module = modules[static_cast<std::size_t>(index)];
            }
        } else if (id == "EATR") {
            if (ext.number(1) != 0)
                throw Error("schema-extension", "Unsupported EATR version");
            exact_count(ext, enum_records.size());
            for (auto &record : enum_records) {
                record.attributes_present = true;
                record.flags = static_cast<std::uint32_t>(ext.number(4));
            }
            exact_count(ext, schema_records.size());
            for (auto &record : schema_records) {
                record.attributes_present = true;
                record.struct_kind = static_cast<std::uint8_t>(ext.number(1));
                require(record.struct_kind <= 2, "Invalid EATR struct kind");
                record.flags = static_cast<std::uint32_t>(ext.number(4));
                auto n = ext.count(options.max_entries);
                require(n <= record.slots, "EATR property flags exceed schema slots");
                charge(n);
                ext.range(std::uint64_t(n) * 8);
                for (unsigned j = 0; j < n; ++j)
                    record.serialized_property_flags.push_back(ext.number(8));
            }
        } else if (id == "ENVP") {
            if (ext.number(1) != 0)
                throw Error("schema-extension", "Unsupported ENVP version");
            exact_count(ext, enum_records.size());
            for (auto &record : enum_records) {
                auto n = ext.count(options.max_entries);
                charge(n);
                Json values = Json::array();
                for (unsigned j = 0; j < n; ++j) {
                    auto label = read_name(ext);
                    auto value = static_cast<std::int64_t>(ext.number(8));
                    values.push_back({{"name", std::move(label)}, {"value", value}});
                }
                record.values = std::move(values);
            }
        } else
            throw Error("schema-extension", "Unsupported USMAP extension: " + id);
        require(ext.pos == ext.end, "USMAP extension payload size mismatch");
        in.pos = ext.end;
        p->extensions.push_back(id);
        options.control.checkpoint("schema-extension", extension_ids.size(), 0);
    };
    if (in.pos != in.end) {
        if (in.end - in.pos < 4)
            throw Error("schema-extension", "Truncated USMAP extension signature");
        auto magic = in.number(4);
        if (magic == 0x54584543) {
            if (in.number(1) != 0)
                throw Error("schema-extension", "Unsupported CEXT layout version");
            auto n = in.count(options.max_entries);
            charge(n);
            for (unsigned j = 0; j < n; ++j) {
                std::string id;
                for (unsigned k = 0; k < 4; ++k) {
                    auto c = in.number(1);
                    require(c >= 32 && c < 127, "Invalid USMAP extension tag");
                    id += static_cast<char>(c);
                }
                auto length = in.number(4);
                extension(id, length);
            }
        } else if (magic == 1)
            extension("MODL", in.end - in.pos);
        else
            throw Error("schema-extension", "Invalid USMAP extension signature");
        require(in.pos == in.end, "Trailing bytes after USMAP extensions");
    }
    auto identity = [&](const std::string &module, const std::string &name) {
        auto size = module.empty() ? name.size() : module.size() + 1 + name.size();
        text(size * 3); // Record identity, map key and alias target.
        return module.empty() ? name : module + "." + name;
    };
    for (auto &record : schema_records) {
        record.path = identity(record.module, record.name);
        p->schema_aliases[record.name].push_back(record.path);
        auto key = record.path;
        require(p->schemas.emplace(std::move(key), std::move(record)).second,
                "Duplicate USMAP schema identity");
    }
    for (auto &record : enum_records) {
        record.path = identity(record.module, record.name);
        p->enum_aliases[record.name].push_back(record.path);
        auto key = record.path;
        require(p->enums.emplace(std::move(key), std::move(record)).second, "Duplicate USMAP enum identity");
    }
    UnversionedSchemas registry(p);
    for (auto &pair : p->schemas) {
        if (!pair.second.super.empty()) {
            const auto &parent = registry.schema(pair.second.super);
            text(parent.path.size());
            pair.second.super = parent.path;
        }
    }
    for (const auto &pair : p->schemas) {
        const auto *schema = &pair.second;
        std::set<std::string> seen;
        while (schema) {
            options.control.check();
            require(seen.insert(schema->path).second, "Cyclic USMAP inheritance");
            if (seen.size() > options.max_depth)
                throw Error("schema-limit", "USMAP inheritance depth exceeds limit");
            if (schema->super.empty())
                break;
            auto it = p->schemas.find(schema->super);
            require(it != p->schemas.end(), "Missing USMAP base schema");
            schema = &it->second;
        }
    }
    p->hash = sha256_bytes(bytes, options.control);
    return UnversionedSchemas(p);
}
namespace {
template <class Records>
const typename Records::mapped_type &
resolve_identity(const Records &records, const std::map<std::string, std::vector<std::string>> &aliases,
                 const std::string &name) {
    auto candidates = aliases.find(name);
    // A short name shared with a qualified spelling must still be unambiguous.
    if (candidates != aliases.end() && candidates->second.size() > 1)
        throw Error("schema-ambiguous", "USMAP name requires a qualified identity: " + name);
    auto found = records.find(name);
    if (found != records.end())
        return found->second;
    if (candidates != aliases.end())
        return records.at(candidates->second.front());
    throw Error("schema-missing", "No USMAP definition for: " + name);
}
} // namespace
const SchemaDefinition &UnversionedSchemas::schema(const std::string &name) const {
    return resolve_identity(impl_->schemas, impl_->schema_aliases, name);
}
const SchemaEnum &UnversionedSchemas::enumeration(const std::string &name) const {
    return resolve_identity(impl_->enums, impl_->enum_aliases, name);
}
UnversionedSchemas UnversionedSchemas::from_runtime_capture(const Json &capture,
                                                            const SchemaReadOptions &options) {
    options.control.check();
    try {
        if (!capture.is_object() || capture.at("format") != "cyuediff.engine-default-capture/1" ||
            capture.at("completeCapture") != true || !capture.at("declarations").is_object() ||
            !capture.at("enumerations").is_object())
            throw Error("schema-runtime", "Expected complete runtime declarations");
        auto p = std::make_shared<Impl>();
        p->source_format = "cyuediff.runtime-schemas/1";
        p->version = 1;
        if (!capture.at("filterEditorOnly").is_boolean())
            throw Error("schema-runtime", "Invalid runtime editor-filter state");
        std::uint64_t entries = 0, text_bytes = 0, encoded_bytes = 0;
        auto encoded = [&](std::uint64_t bytes) {
            if (bytes > options.max_bytes - encoded_bytes)
                throw Error("schema-limit", "Runtime declaration bytes exceed limit");
            encoded_bytes += bytes;
        };
        auto encoded_string = [&](const std::string &value) {
            encoded(2);
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (!(i & 4095))
                    options.control.check();
                const auto c = static_cast<unsigned char>(value[i]);
                encoded(c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t'
                            ? 2
                        : c < 32 ? 6
                                 : 1);
            }
        };
        auto charge = [&](std::uint64_t work, std::uint64_t bytes) {
            options.control.check();
            if (work > options.max_entries - entries || bytes > options.max_text_bytes - text_bytes)
                throw Error("schema-limit", "Runtime declaration budget exceeded");
            entries += work;
            text_bytes += bytes;
        };
        std::function<void(const Json &, unsigned)> measure = [&](const Json &value, unsigned depth) {
            if (depth >= options.max_depth)
                throw Error("schema-limit", "Runtime declaration depth exceeds limit");
            charge(1, 0);
            if (value.is_string()) {
                const auto &text = value.get_ref<const std::string &>();
                charge(0, text.size());
                encoded_string(text);
            } else if (value.is_object()) {
                encoded(2 + (value.empty() ? 0 : value.size() - 1));
                for (auto it = value.begin(); it != value.end(); ++it) {
                    charge(0, it.key().size());
                    encoded_string(it.key());
                    encoded(1);
                    measure(it.value(), depth + 1);
                }
            } else if (value.is_array()) {
                encoded(2 + (value.empty() ? 0 : value.size() - 1));
                for (const auto &child : value)
                    measure(child, depth + 1);
            } else {
                if (value.is_binary() || value.is_discarded() ||
                    (value.is_number_float() && !std::isfinite(value.get<double>())))
                    throw Error("schema-runtime", "Invalid declaration JSON value");
                encoded(value.dump().size());
            }
        };
        const auto &declarations = capture.at("declarations"), &enums = capture.at("enumerations");
        measure(declarations, 0);
        measure(enums, 0);
        encoded(Json{{"format", p->source_format}, {"filterEditorOnly", capture.at("filterEditorOnly")}}
                    .dump()
                    .size() +
                std::string(",\"declarations\":,\"enumerations\":").size());
        Json document = {{"format", p->source_format},
                         {"declarations", declarations},
                         {"enumerations", enums},
                         {"filterEditorOnly", capture.at("filterEditorOnly")}};
        const auto canonical = document.dump();
        if (canonical.size() > options.max_bytes)
            throw Error("schema-limit", "Runtime declaration bytes exceed limit");
        auto word = [&](const Json &j) {
            auto value = j.get<std::string>();
            if (value.empty() || value.size() > 65536 || value.find('\0') != std::string::npos)
                throw Error("schema-runtime", "Invalid runtime declaration name");
            charge(0, value.size());
            return value;
        };
        auto unsigned_number = [](const Json &j, std::uint64_t maximum) {
            if (!j.is_number_integer() || (!j.is_number_unsigned() && j.get<std::int64_t>() < 0) ||
                j.get<std::uint64_t>() > maximum)
                throw Error("schema-runtime", "Invalid runtime unsigned integer");
            return j.get<std::uint64_t>();
        };
        auto qualified = [&](const std::string &path, std::string &module, std::string &name) {
            const auto dot = path.find_last_of('.');
            if (path.empty() || path.front() != '/' || dot == std::string::npos || dot + 1 == path.size() ||
                path.find('\\') != std::string::npos)
                throw Error("schema-runtime", "Expected a qualified runtime identity");
            module = path.substr(0, dot);
            name = path.substr(dot + 1);
        };
        std::function<SchemaType(const Json &, unsigned)> type = [&](const Json &j, unsigned depth) {
            if (depth >= options.max_depth)
                throw Error("schema-limit", "Runtime type depth exceeds limit");
            charge(1, 0);
            auto name = word(j.at("name"));
            if (name == "ObjectPtrProperty" || name == "ClassProperty" || name == "ClassPtrProperty")
                name = "ObjectProperty";
            if (name == "SoftClassProperty")
                name = "SoftObjectProperty";
            if (name == "MulticastInlineDelegateProperty" || name == "MulticastSparseDelegateProperty")
                name = "MulticastDelegateProperty";
            SchemaType result;
            for (unsigned i = 0; i <= 30; ++i)
                if (name == type_name(i))
                    result.kind = static_cast<std::uint8_t>(i);
            if (result.kind == 9 || result.kind == 26)
                result.name = word(j.at("schema"));
            const auto args = j.find("arguments");
            const auto count =
                result.kind == 24                                                                 ? 2u
                : result.kind == 8 || result.kind == 25 || result.kind == 26 || result.kind == 28 ? 1u
                                                                                                  : 0u;
            if ((args != j.end() && (!args->is_array() || args->size() != count)) ||
                (count && args == j.end()))
                throw Error("schema-runtime", "Invalid runtime type arity");
            if (args != j.end())
                for (const auto &arg : *args)
                    result.arguments.push_back(type(arg, depth + 1));
            return result;
        };
        for (auto it = enums.begin(); it != enums.end(); ++it) {
            charge(1, 0);
            SchemaEnum e;
            e.path = word(it.key());
            qualified(e.path, e.module, e.name);
            e.attributes_present = true;
            if (!it->is_array())
                throw Error("schema-runtime", "Expected runtime enum entries");
            std::set<std::string> names;
            for (const auto &value : *it) {
                charge(1, 0);
                auto name = word(value.at("name"));
                const auto decimal = word(value.at("value"));
                std::int64_t number = 0;
                const auto parsed = std::from_chars(decimal.data(), decimal.data() + decimal.size(), number);
                if (!names.insert(name).second || parsed.ec != std::errc{} ||
                    parsed.ptr != decimal.data() + decimal.size())
                    throw Error("schema-runtime", "Invalid runtime enum entry");
                e.values.push_back({{"name", std::move(name)}, {"value", number}});
            }
            p->enum_aliases[e.name].push_back(e.path);
            p->enums.emplace(e.path, std::move(e));
        }
        for (auto it = declarations.begin(); it != declarations.end(); ++it) {
            charge(1, 0);
            SchemaDefinition definition;
            definition.path = word(it.key());
            if (it->at("path") != definition.path)
                throw Error("schema-runtime", "Runtime identity/key disagree");
            qualified(definition.path, definition.module, definition.name);
            definition.super = it->at("super").get<std::string>();
            definition.slots = static_cast<std::uint16_t>(unsigned_number(it->at("slots"), UINT16_MAX));
            definition.attributes_present = true;
            const auto kind = it->at("kind").get<std::string>();
            if (kind != "class" && kind != "struct" && kind != "native-struct")
                throw Error("schema-runtime", "Invalid runtime declaration kind");
            definition.struct_kind = kind == "class" ? 0 : kind == "struct" ? 1 : 2;
            if (!it->at("properties").is_array())
                throw Error("schema-runtime", "Expected runtime properties");
            std::set<std::string> names;
            for (const auto &field : it->at("properties")) {
                charge(1, 0);
                SchemaProperty property;
                property.name = word(field.at("name"));
                if (!names.insert(property.name).second)
                    throw Error("schema-runtime", "Duplicate runtime field name");
                const auto index = unsigned_number(field.at("index"), UINT16_MAX),
                           dim = unsigned_number(field.at("arrayDim"), UINT8_MAX);
                if (!dim || dim > UINT8_MAX || index >= definition.slots || dim > definition.slots - index)
                    throw Error("schema-runtime", "Invalid runtime static array range");
                property.index = static_cast<std::uint16_t>(index);
                property.array_size = static_cast<std::uint8_t>(dim);
                property.type = type(field.at("type"), 0);
                std::uint64_t type_nodes = 0, type_text = 0;
                std::function<void(const SchemaType &)> measure_type = [&](const SchemaType &t) {
                    ++type_nodes;
                    type_text += t.name.size();
                    for (const auto &child : t.arguments)
                        measure_type(child);
                };
                measure_type(property.type);
                charge(dim + (dim - 1) * type_nodes, (dim - 1) * type_text);
                for (std::uint64_t i = 0; i < dim; ++i) {
                    charge(0, property.name.size());
                    if (!definition.properties.emplace(static_cast<std::uint16_t>(index + i), property)
                             .second)
                        throw Error("schema-runtime", "Overlapping runtime property slots");
                }
            }
            p->schema_aliases[definition.name].push_back(definition.path);
            p->schemas.emplace(definition.path, std::move(definition));
        }
        UnversionedSchemas registry(p);
        std::function<void(const SchemaType &)> references = [&](const SchemaType &t) {
            options.control.check();
            if (t.kind == 9) {
                const auto &target = registry.schema(t.name);
                if (target.struct_kind == 0)
                    throw Error("schema-runtime", "Struct descriptor targets a class");
            }
            if (t.kind == 26)
                registry.enumeration(t.name);
            for (const auto &a : t.arguments)
                references(a);
        };
        for (const auto &pair : p->schemas) {
            const auto *definition = &pair.second;
            std::set<std::string> seen;
            while (definition) {
                charge(1, 0);
                if (!seen.insert(definition->path).second)
                    throw Error("schema-runtime", "Cyclic runtime inheritance");
                if (seen.size() > options.max_depth)
                    throw Error("schema-limit", "Runtime inheritance exceeds limit");
                if (definition->super.empty())
                    break;
                const auto parent = p->schemas.find(definition->super);
                if (parent == p->schemas.end())
                    throw Error("schema-runtime", "Missing qualified runtime base");
                definition = &parent->second;
            }
            for (const auto &field : pair.second.properties)
                references(field.second.type);
        }
        p->hash =
            sha256_bytes(std::vector<std::uint8_t>(canonical.begin(), canonical.end()), options.control);
        return registry;
    } catch (const Json::exception &) {
        throw Error("schema-runtime", "Invalid runtime declaration JSON");
    }
}
const std::string &UnversionedSchemas::sha256() const {
    return impl_->hash;
}
const Json &UnversionedSchemas::versioning() const {
    return impl_->versioning;
}
Json UnversionedSchemas::to_json() const {
    Json schemas = Json::array(), enums = Json::object(), enum_metadata = Json::array();
    for (const auto &pair : impl_->schemas) {
        const auto &s = pair.second;
        Json record = {{"name", s.name},
                       {"path", s.path},
                       {"module", s.module},
                       {"super", s.super},
                       {"slots", s.slots},
                       {"mappedSlots", s.properties.size()},
                       {"attributesPresent", s.attributes_present}};
        if (s.attributes_present) {
            Json flags = Json::array();
            static const char digits[] = "0123456789abcdef";
            for (auto value : s.serialized_property_flags) {
                std::string hex(16, '0');
                for (unsigned j = 0; j < 16; ++j) {
                    hex[15 - j] = digits[value & 15];
                    value >>= 4;
                }
                flags.push_back(hex);
            }
            record["structKind"] = s.struct_kind;
            record["flags"] = s.flags;
            record["serializedPropertyFlagsHex"] = std::move(flags);
        }
        schemas.push_back(std::move(record));
    }
    for (const auto &pair : impl_->enums) {
        const auto &e = pair.second;
        enums[e.path] = e.values;
        enum_metadata.push_back({{"name", e.name},
                                 {"path", e.path},
                                 {"module", e.module},
                                 {"attributesPresent", e.attributes_present},
                                 {"flags", e.flags}});
    }
    return {{"schema", impl_->source_format},
            {"version", impl_->version},
            {"sha256", impl_->hash},
            {"versioning", impl_->versioning},
            {"schemas", std::move(schemas)},
            {"enums", std::move(enums)},
            {"enumMetadata", std::move(enum_metadata)},
            {"extensions", impl_->extensions}};
}
namespace {
struct PropertyReader {
    Reader r;
    const UnversionedSchemas &schemas;
    const UnversionedReadOptions &options;
    detail::ValueIdentityJournal identities;
    UnversionedProperties result;
    std::map<std::string, NativeStructLayout> native_structs;
    std::map<std::string, std::string> struct_layouts;
    std::map<std::string, std::string> struct_tails;
    std::map<std::string, BinaryLayoutDefinition> layout_definitions;
    bool definitions_ready = false;
    Json type_context(const SchemaType &type, unsigned nesting) {
        depth(nesting);
        charge();
        Json context = {{"kind", type.kind}};
        if (!type.name.empty()) {
            const auto &name =
                type.kind == 26 ? schemas.enumeration(type.name).path : schemas.schema(type.name).path;
            text(name.size());
            context["name"] = name;
            if (type.kind == 9) {
                const auto native = native_structs.find(name);
                const auto whole = struct_layouts.find(name);
                const auto tail = struct_tails.find(name);
                if (native != native_structs.end()) {
                    context["route"] = "builtin";
                    context["layout"] = native_struct_layout_name(native->second);
                } else if (whole != struct_layouts.end() || tail != struct_tails.end()) {
                    context["route"] = whole != struct_layouts.end() ? "layout" : "hybrid";
                    if (!definitions_ready) {
                        BinaryLayoutReadOptions read;
                        read.narrow_string_encoding = options.narrow_string_encoding;
                        read.reference = options.reference;
                        read.reference_identity = options.reference_identity;
                        if (!read.reference) {
                            read.reference = [](std::int32_t) -> Json {
                                throw Error("schema-context", "Missing package reference resolver");
                            };
                            read.reference_identity = "cyuediff.null-package-indices/1";
                        }
                        read.control = options.control;
                        layout_definitions = options.binary_layouts->definitions(read);
                        definitions_ready = true;
                    }
                    const auto &definition =
                        layout_definitions.at(whole != struct_layouts.end() ? whole->second : tail->second);
                    text(definition.sha256.size());
                    context["definitionSha256"] = definition.sha256;
                    context["referenceDependent"] = definition.reference_dependent;
                } else {
                    const auto &definition = schemas.schema(name);
                    const bool reflected = options.reflected_structs.count(name) ||
                                           (options.reflected_structs.count(definition.name) &&
                                            schemas.schema(definition.name).path == name);
                    context["route"] = reflected ? "reflected" : "unsupported";
                }
            }
        }
        if (!type.arguments.empty()) {
            context["arguments"] = Json::array();
            for (const auto &argument : type.arguments)
                context["arguments"].push_back(type_context(argument, nesting + 1));
        }
        if (type.kind == 10 || type.kind == 11 || type.kind == 17 || type.kind == 30) {
            const auto encoding = options.narrow_string_encoding == NarrowStringEncoding::unspecified
                                      ? NarrowStringEncoding::ascii
                                      : options.narrow_string_encoding;
            context["encoding"] = narrow_string_encoding_name(encoding);
        }
        if (type.kind == 4 || type.kind == 6 || type.kind == 13 || type.kind == 14 || type.kind == 12 ||
            type.kind == 27) {
            text(options.reference_identity.size());
            context["referenceIdentity"] =
                options.reference ? options.reference_identity : "cyuediff.null-package-indices/1";
        }
        if (type.kind == 6 || type.kind == 13 || type.kind == 14)
            context["referenceLayout"] = static_cast<unsigned>(options.script_reference_layout);
        if (type.kind == 11) {
            context["textLayout"] = static_cast<unsigned>(options.text_layout);
            context["argumentLayout"] = static_cast<unsigned>(options.text_argument_data_layout);
        }
        if (type.kind == 17)
            context["softPathLayout"] = static_cast<unsigned>(options.soft_path_layout);
        if (type.kind == 27)
            context["fieldPathLayout"] = static_cast<unsigned>(options.field_path_layout);
        if (type.kind == 28)
            context["optionalLayout"] = static_cast<unsigned>(options.optional_layout);
        if (type.kind == 29 || type.kind == 30)
            context["byteStringLayout"] = static_cast<unsigned>(options.byte_string_layout);
        return context;
    }
    void inventory_type(const SchemaType &type, unsigned nesting) {
        depth(nesting);
        if (type.kind == 9) {
            const auto &definition = schemas.schema(type.name);
            const bool reflected = options.reflected_structs.count(definition.path) ||
                                   (options.reflected_structs.count(definition.name) &&
                                    schemas.schema(definition.name).path == definition.path);
            if (struct_tails.count(definition.path) || reflected)
                inventory_schema(definition, nesting + 1);
        }
        for (const auto &argument : type.arguments)
            inventory_type(argument, nesting + 1);
    }
    void inventory_schema(const SchemaDefinition &schema, unsigned nesting) {
        auto &inventory = result.interpretation["schemas"];
        if (inventory.contains(schema.path))
            return;
        depth(nesting);
        charge();
        text(schema.path.size());
        inventory[schema.path] = {{"fields", Json::object()}, {"slots", schema.slots}}; // Before cycles.
        auto &fields = inventory[schema.path]["fields"];
        for (const auto &pair : schema.properties) {
            charge();
            const auto &property = pair.second;
            auto key = schema.path + "." + property.name;
            if (property.array_size > 1)
                key += "[" + std::to_string(pair.first - property.index) + "]";
            text(key.size());
            fields[key] = {{"index", pair.first}, {"type", type_context(property.type, 0)}};
            inventory_type(property.type, nesting + 1);
        }
        if (!schema.super.empty()) {
            const auto &super = schemas.schema(schema.super);
            text(super.path.size());
            inventory[schema.path]["super"] = super.path;
            inventory_schema(super, nesting + 1);
        }
    }
    void charge() {
        options.control.check();
        if (result.values_read >= options.max_values)
            throw Error("schema-limit", "Property work exceeds configured limit", r.pos);
        if (options.charge)
            options.charge(1, 0);
        ++result.values_read;
    }
    void text(std::uint64_t n) {
        if (n > options.max_text_bytes - result.text_bytes)
            throw Error("schema-limit", "Decoded property text exceeds limit", r.pos);
        if (options.charge)
            options.charge(0, n);
        result.text_bytes += n;
    }
    void depth(unsigned n) {
        if (n >= options.max_depth)
            throw Error("schema-limit", "Property nesting exceeds limit", r.pos);
    }
    std::uint32_t slots(const SchemaDefinition &schema) {
        auto *s = &schema;
        std::uint32_t count = 0;
        for (unsigned d = 0;; ++d) {
            depth(d);
            count += s->slots;
            if (s->super.empty())
                return count;
            s = &schemas.schema(s->super);
        }
    }
    void field(const SchemaDefinition &root, std::uint32_t index, bool zero, unsigned nesting, Json &object) {
        auto *schema = &root;
        auto local = index;
        for (unsigned d = 0; local >= schema->slots; ++d) {
            depth(d);
            local -= schema->slots;
            if (schema->super.empty())
                throw Error("schema-mismatch", "Property index exceeds supplied schema", r.pos);
            schema = &schemas.schema(schema->super);
        }
        auto it = schema->properties.find(static_cast<std::uint16_t>(local));
        if (it == schema->properties.end())
            throw Error("schema-missing", "Serialized property slot has no type mapping", r.pos);
        const auto &p = it->second;
        auto key = schema->path + "." + p.name;
        if (p.array_size > 1)
            key += "[" + std::to_string(local - p.index) + "]";
        text(key.size());
        detail::ValueIdentityJournal::Scope field_identity(identities, key),
            value_identity(identities, "value");
        Json decoded;
        try {
            decoded = read_value(p.type, zero, nesting + 1);
        } catch (const Error &error) {
            // Keep the specific cause and nearest fields first. Bound diagnostic
            // expansion across nested supplied mappings without splitting UTF-8.
            if (error.code == "cancelled" || error.code == "schema-limit")
                throw;
            auto message = std::string(error.what()) + " [field " + key + "]";
            if (message.size() > 4096) {
                std::size_t end = 4096;
                while (end && (static_cast<unsigned char>(message[end]) & 0xc0) == 0x80)
                    --end;
                message.resize(end);
            }
            throw Error(error.code, std::move(message), error.offset);
        }
        Json value = {{"type", type_name(p.type.kind)},
                      {"serializedZero", zero},
                      {"schemaIndex", index},
                      {"value", std::move(decoded)}};
        if (!p.type.name.empty()) {
            auto name =
                p.type.kind == 26 ? schemas.enumeration(p.type.name).path : schemas.schema(p.type.name).path;
            text(name.size());
            value["typeName"] = name;
            if (p.type.kind == 9) {
                auto native = native_structs.find(name);
                if (native != native_structs.end())
                    value["nativeLayout"] = native_struct_layout_name(native->second);
                auto layout = struct_layouts.find(name);
                if (layout != struct_layouts.end()) {
                    text(layout->second.size() + options.binary_layouts->sha256().size());
                    value["binaryLayout"] = layout->second;
                    value["binaryLayoutsSha256"] = options.binary_layouts->sha256();
                }
                auto tail = struct_tails.find(name);
                if (tail != struct_tails.end()) {
                    text(tail->second.size() + options.binary_layouts->sha256().size());
                    value["structTailLayout"] = tail->second;
                    value["binaryLayoutsSha256"] = options.binary_layouts->sha256();
                }
            }
        }
        object[key] = std::move(value);
    }
    struct LayoutInput final : DecoderInput {
        PropertyReader &reader;
        std::uint64_t begin;
        explicit LayoutInput(PropertyReader &owner) : reader(owner), begin(owner.r.pos) {}
        std::uint64_t position() const override {
            return reader.r.pos - begin;
        }
        std::uint64_t remaining() const override {
            return reader.r.end - reader.r.pos;
        }
        void read(void *out, std::size_t bytes) override {
            reader.r.range(bytes);
            if (bytes && !out)
                throw Error("schema-context", "Null layout read destination");
            if (bytes)
                std::memcpy(out, reader.r.data.data() + static_cast<std::size_t>(reader.r.pos), bytes);
            reader.r.pos += bytes;
        }
        void skip(std::uint64_t bytes) override {
            reader.r.skip(bytes);
        }
        std::uint32_t count(std::uint32_t maximum) override {
            return reader.r.count(maximum ? std::min(maximum, reader.options.max_values)
                                          : reader.options.max_values);
        }
        std::string string() override {
            throw Error("schema-context", "Use the bounded layout FString reader");
        }
        std::string name() override {
            const auto index = reader.r.i32(), suffix = reader.r.i32();
            if (!reader.options.names || index < 0 || suffix < 0 ||
                static_cast<std::size_t>(index) >= reader.options.names->size())
                throw Error("schema-context", "Invalid layout package name index", reader.r.pos - 8);
            const auto &base = (*reader.options.names)[static_cast<std::size_t>(index)];
            if (base.size() > reader.options.max_text_bytes - reader.result.text_bytes)
                throw Error("schema-limit", "Layout package name exceeds text budget", reader.r.pos);
            return suffix ? base + "_" + std::to_string(suffix - 1) : base;
        }
        std::string reference() override {
            throw Error("schema-context", "Use the explicit layout reference resolver");
        }
        std::string guid() override {
            constexpr char hex[] = "0123456789abcdef";
            std::string value;
            for (unsigned i = 0; i < 4; ++i) {
                const auto word = reader.r.number(4);
                for (int shift = 28; shift >= 0; shift -= 4)
                    value += hex[(word >> shift) & 15];
            }
            return value;
        }
    };
    Json layout_value(const std::string &layout, bool zero, unsigned nesting) {
        if (zero)
            throw Error("schema-type", "Declarative native zero defaults require an explicit value model",
                        r.pos);
        depth(nesting);
        BinaryLayoutReadOptions read;
        read.max_values = options.max_values - result.values_read;
        read.max_depth = options.max_depth - nesting;
        read.max_text_bytes = options.max_text_bytes - result.text_bytes;
        read.max_string_bytes =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(UINT32_MAX, read.max_text_bytes));
        read.max_read_bytes = r.end - r.pos;
        read.narrow_string_encoding = options.narrow_string_encoding;
        read.reference = options.reference;
        // Even a null-only reference must consume and interpret its signed package index.
        if (!read.reference)
            read.reference = [](std::int32_t) -> Json {
                throw Error("schema-context", "Layout object needs a package reference resolver");
            };
        read.control = options.control;
        read.charge = [&](std::uint32_t work, std::uint64_t bytes) {
            for (std::uint32_t i = 0; i < work; ++i)
                charge();
            text(bytes);
        };
        LayoutInput input(*this);
        try {
            return options.binary_layouts->decode(input, layout, read);
        } catch (const Error &error) {
            if (error.code == "layout-limit")
                throw Error("schema-limit", error.what(), r.pos);
            throw;
        }
    }
    Json structure(const std::string &name, bool zero, unsigned nesting) {
        depth(nesting);
        const auto &schema = schemas.schema(name);
        auto total_slots = slots(schema);
        Json object = Json::object();
        if (zero) {
            for (unsigned j = 0; j < total_slots; ++j)
                field(schema, j, true, nesting, object);
            return object;
        }
        struct Fragment {
            unsigned skip, count;
            bool mask, last;
        };
        std::vector<Fragment> fragments;
        std::uint64_t masked = 0, index = 0;
        for (;;) {
            charge();
            auto packed = static_cast<unsigned>(r.number(2));
            Fragment f{packed & 127, packed >> 9, (packed & 128) != 0, (packed & 256) != 0};
            index += f.skip + f.count;
            if (index > total_slots)
                throw Error("schema-mismatch", "Unversioned fragment exceeds schema slots", r.pos - 2);
            if (f.mask)
                masked += f.count;
            fragments.push_back(f);
            if (f.last)
                break;
        }
        auto mask_bytes =
            masked <= 8 ? (masked ? 1ull : 0ull) : (masked <= 16 ? 2ull : ((masked + 31) / 32) * 4);
        r.range(mask_bytes);
        auto mask_at = r.pos;
        r.skip(mask_bytes);
        index = 0;
        std::uint64_t bit = 0;
        for (const auto &f : fragments) {
            index += f.skip;
            for (unsigned j = 0; j < f.count; ++j) {
                bool is_zero = false;
                if (f.mask) {
                    is_zero = (r.data[static_cast<std::size_t>(mask_at + bit / 8)] & (1u << (bit % 8))) != 0;
                    ++bit;
                }
                field(schema, static_cast<std::uint32_t>(index++), is_zero, nesting, object);
            }
        }
        return object;
    }
    // Serialize canonical JSON incrementally so a large key cannot allocate an
    // unbounded temporary string before the enclosing text budget is charged.
    enum class KeyShape { raw, typed, fields, field, hybrid, collection, entries, entry, keys };
    struct KeyMode {
        KeyShape shape = KeyShape::raw;
        const SchemaType *type = nullptr;
        const SchemaDefinition *schema = nullptr;
    };
    KeyMode key_mode(KeyMode mode) {
        if (mode.shape != KeyShape::typed)
            return mode;
        const auto &type = *mode.type;
        if (type.kind == 9) {
            const auto &definition = schemas.schema(type.name);
            if (native_structs.count(definition.path) || struct_layouts.count(definition.path))
                return {};
            return {struct_tails.count(definition.path) ? KeyShape::hybrid : KeyShape::fields, mode.type,
                    &definition};
        }
        if (type.kind == 24 || type.kind == 25)
            return {KeyShape::collection, mode.type};
        if (type.kind == 8 || type.kind == 28)
            return mode;
        return {};
    }
    KeyMode key_child(KeyMode mode, const Json &value, const std::string &key) {
        if (mode.shape == KeyShape::field)
            return key == "value" ? KeyMode{KeyShape::typed, mode.type} : KeyMode{};
        if (mode.shape == KeyShape::fields) {
            const auto &field = value.at(key);
            auto index = field.at("schemaIndex").get<std::uint32_t>();
            auto *schema = mode.schema;
            while (index >= schema->slots) {
                options.control.check();
                index -= schema->slots;
                schema = &schemas.schema(schema->super);
            }
            const auto &type = schema->properties.at(static_cast<std::uint16_t>(index)).type;
            return {KeyShape::field, &type};
        }
        if (mode.shape == KeyShape::hybrid)
            return key == "properties" ? KeyMode{KeyShape::fields, mode.type, mode.schema} : KeyMode{};
        if (mode.shape == KeyShape::typed && mode.type->kind == 28)
            return key == "value" ? KeyMode{KeyShape::typed, &mode.type->arguments.at(0)} : KeyMode{};
        if (mode.shape == KeyShape::collection) {
            if (key == "removed")
                return {KeyShape::keys, &mode.type->arguments.at(0)};
            if (key == "entries")
                return mode.type->kind == 24 ? KeyMode{KeyShape::entries, mode.type}
                                             : KeyMode{KeyShape::keys, &mode.type->arguments.at(0)};
        }
        if (mode.shape == KeyShape::keys)
            return {KeyShape::typed, mode.type};
        if (mode.shape == KeyShape::entries)
            return {KeyShape::entry, mode.type};
        if (mode.shape == KeyShape::entry)
            return {KeyShape::typed, &mode.type->arguments.at(key == "key" ? 0 : 1)};
        return {};
    }
    std::string identity(const Json &key, const SchemaType &type) {
        options.control.checkpoint("unversioned-key-identity");
        std::string output;
        auto append = [&](const char *value, std::size_t size) {
            text(size);
            output.append(value, size);
        };
        auto string = [&](const std::string &value) {
            std::uint64_t bytes = 2;
            for (std::size_t j = 0; j < value.size(); ++j) {
                if ((j & 4095) == 0)
                    options.control.check();
                auto c = static_cast<unsigned char>(value[j]);
                bytes +=
                    c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t'
                        ? 2
                        : (c < 32 ? 6 : 1);
            }
            text(bytes);
            output += Json(value).dump();
        };
        std::function<void(const Json &, unsigned, KeyMode)> write = [&](const Json &value, unsigned nesting,
                                                                         KeyMode mode) {
            depth(nesting);
            charge();
            mode = key_mode(mode);
            if (value.is_string())
                string(value.get_ref<const std::string &>());
            else if (value.is_array()) {
                append("[", 1);
                for (std::size_t j = 0; j < value.size(); ++j) {
                    if (j)
                        append(",", 1);
                    const auto child = mode.shape == KeyShape::typed && mode.type->kind == 8
                                           ? KeyMode{KeyShape::typed, &mode.type->arguments.at(0)}
                                           : KeyMode{};
                    write(value[j], nesting + 1, child);
                }
                append("]", 1);
            } else if (value.is_object()) {
                append("{", 1);
                bool first = true;
                for (auto it = value.begin(); it != value.end(); ++it) {
                    // Only field wrappers produced by this reader contain annotations.
                    // Declarative/builtin native data never enters this mode.
                    if (mode.shape == KeyShape::field && it.key() != "value")
                        continue;
                    if (!first)
                        append(",", 1);
                    first = false;
                    string(it.key());
                    append(":", 1);
                    write(it.value(), nesting + 1, key_child(mode, value, it.key()));
                }
                append("}", 1);
            } else {
                if (value.is_binary() || value.is_discarded())
                    throw Error("schema-type", "Unsupported map/set identity", r.pos);
                if (value.is_number_float() && !std::isfinite(value.get<double>()))
                    throw Error("schema-value", "Non-finite map/set identity", r.pos);
                auto token = value.dump();
                append(token.data(), token.size());
            }
        };
        write(key, 0, {KeyShape::typed, &type});
        return output;
    }
    Json collection(const SchemaType &type, bool zero, unsigned nesting) {
        const bool map = type.kind == 24;
        Json removed = Json::object(), entries = Json::object();
        bool replace = zero;
        if (!zero) {
            auto removed_count = r.i32();
            if (map && removed_count == -1) {
                if (!options.allow_map_replacement)
                    throw Error("schema-type", "Map replacement marker requires an explicit supported layout",
                                r.pos - 4);
                replace = true;
                removed_count = 0;
            } else if (removed_count < 0)
                throw Error("schema-value", "Invalid map/set removal count", r.pos - 4);
            if (static_cast<std::uint32_t>(removed_count) > options.max_values - result.values_read)
                throw Error("schema-limit", "Map/set removal count exceeds remaining work", r.pos - 4);
            auto read_item = [&](Json &target, bool pair, const std::string &part) {
                detail::ValueIdentityJournal::Buffer key_names(identities);
                auto key = read_value(type.arguments.at(0), false, nesting + 1);
                auto id = identity(key, type.arguments.at(0));
                if (target.contains(id))
                    throw Error("schema-identity", "Duplicate serialized map/set identity", r.pos);
                if (pair) {
                    key_names.commit_at({part, id, "key"});
                    detail::ValueIdentityJournal::Scope list(identities, part), item(identities, id),
                        mapped(identities, "value");
                    auto value = read_value(type.arguments.at(1), false, nesting + 1);
                    target[id] = {{"key", std::move(key)}, {"value", std::move(value)}};
                } else {
                    key_names.commit_at({part, id});
                    target[id] = std::move(key);
                }
            };
            for (std::int32_t j = 0; j < removed_count; ++j) {
                read_item(removed, false, "removed");
                options.control.checkpoint("unversioned-container-removals", j + 1, removed_count);
            }
            auto count = r.count(options.max_values - result.values_read);
            for (std::uint32_t j = 0; j < count; ++j) {
                read_item(entries, map, "entries");
                options.control.checkpoint("unversioned-container-entries", j + 1, count);
            }
        }
        return {{"kind", map ? "stored-map-delta" : "stored-set-delta"},
                {"replace", replace},
                {"removed", std::move(removed)},
                {"entries", std::move(entries)}};
    }
    Json native_value(NativeStructLayout layout, bool zero, unsigned nesting) {
        const auto &info = native_layout_info(layout);
        auto values = [&](unsigned count) {
            Json result = Json::array();
            for (unsigned j = 0; j < count; ++j)
                result.push_back(
                    read_value(SchemaType{static_cast<std::uint8_t>(info.kind), {}, {}}, zero, nesting + 1));
            return result;
        };
        if (info.shape == NativeShape::per_platform) {
            charge();
            if (!zero && r.number(4) != 1)
                throw Error("schema-value", "Cooked per-platform values require a true bool32 marker",
                            r.pos - 4);
            if (layout == NativeStructLayout::per_platform_bool_cooked) {
                depth(nesting + 1);
                charge();
                const auto value = zero ? 0 : r.number(4);
                if (value > 1)
                    throw Error("schema-value", "Invalid per-platform bool32 value", r.pos - 4);
                return {{"platformValue", value != 0}};
            }
            return {{"platformValue", read_value(SchemaType{static_cast<std::uint8_t>(info.kind), {}, {}},
                                                 zero, nesting + 1)}};
        }
        if (info.shape == NativeShape::matrix4) {
            Json rows = Json::array();
            for (unsigned row = 0; row < 4; ++row)
                rows.push_back(values(4));
            return rows;
        }
        if (info.shape == NativeShape::plane) {
            auto normal = values(3), distance = values(1);
            return {{"normal", std::move(normal)}, {"distance", std::move(distance[0])}};
        }
        if (info.shape == NativeShape::box3) {
            auto minimum = values(3), maximum = values(3);
            auto validity = read_value(SchemaType{0, {}, {}}, zero, nesting + 1);
            return {
                {"min", std::move(minimum)}, {"max", std::move(maximum)}, {"isValid", std::move(validity)}};
        }
        if (info.shape == NativeShape::sphere) {
            auto center = values(3), radius = values(1);
            return {{"center", std::move(center)}, {"radius", std::move(radius[0])}};
        }
        if (info.shape == NativeShape::box_sphere) {
            auto origin = values(3), extent = values(3), radius = values(1);
            return {{"origin", std::move(origin)},
                    {"boxExtent", std::move(extent)},
                    {"sphereRadius", std::move(radius[0])}};
        }
        if (info.shape == NativeShape::ticks)
            return {{"ticks", read_value(SchemaType{21, {}, {}}, zero, nesting + 1)}};
        if (info.shape == NativeShape::transform) {
            // Serialized quaternion, translation, scale; no in-memory SIMD padding.
            auto rotation = values(4), translation = values(3), scale = values(3);
            return {{"rotation", std::move(rotation)},
                    {"translation", std::move(translation)},
                    {"scale", std::move(scale)}};
        }
        if (info.shape == NativeShape::guid) {
            text(32);
            std::string guid;
            static const char digits[] = "0123456789abcdef";
            for (unsigned j = 0; j < 4; ++j) {
                auto word = read_value(SchemaType{19, {}, {}}, zero, nesting + 1).get<std::uint32_t>();
                std::string part(8, '0');
                for (unsigned k = 0; k < 8; ++k) {
                    part[7 - k] = digits[word & 15];
                    word >>= 4;
                }
                guid += part;
            }
            return guid;
        }
        return values(info.components);
    }
    Json read_value(const SchemaType &type, bool zero, unsigned nesting) {
        depth(nesting);
        charge();
        auto integer = [&](unsigned bytes, bool sign) -> Json {
            auto n = zero ? 0ull : r.number(bytes);
            if (!sign)
                return n;
            if (bytes < 8 && (n & (1ull << (bytes * 8 - 1))))
                n |= (~0ull) << (bytes * 8);
            return static_cast<std::int64_t>(n);
        };
        switch (type.kind) {
        case 0:
            return integer(1, false);
        case 1: {
            auto v = zero ? 0 : r.number(1);
            if (v > 1)
                throw Error("schema-value", "Invalid boolean property", r.pos);
            return v != 0;
        }
        case 2:
            return integer(4, true);
        case 18:
            return integer(8, false);
        case 19:
            return integer(4, false);
        case 20:
            return integer(2, false);
        case 21:
            return integer(8, true);
        case 22:
            return integer(2, true);
        case 23:
            return integer(1, true);
        case 3:
        case 7: {
            double value = 0;
            if (!zero) {
                if (type.kind == 3) {
                    auto bits = static_cast<std::uint32_t>(r.number(4));
                    float f;
                    std::memcpy(&f, &bits, 4);
                    value = f;
                } else {
                    auto bits = r.number(8);
                    std::memcpy(&value, &bits, 8);
                }
            }
            if (!std::isfinite(value))
                throw Error("schema-value", "Non-finite property value", r.pos);
            return value;
        }
        case 4: {
            auto index = zero ? 0 : r.i32();
            if (!index)
                return nullptr;
            if (!options.reference)
                throw Error("schema-context", "Object property requires a package reference resolver", r.pos);
            auto value = options.reference(index);
            // Host-resolved identities share the reader's depth/work/text/cancellation bounds.
            auto string = [&](const std::string &s) {
                text(s.size());
                try {
                    static_cast<void>(Json(s).dump());
                } catch (const Json::exception &) {
                    throw Error("schema-value", "Invalid UTF-8 in resolved reference", r.pos);
                }
            };
            std::function<void(const Json &, unsigned)> measure = [&](const Json &v, unsigned level) {
                depth(level);
                charge();
                if (v.is_string())
                    string(v.get_ref<const std::string &>());
                else if (v.is_object())
                    for (auto it = v.begin(); it != v.end(); ++it) {
                        string(it.key());
                        measure(it.value(), level + 1);
                    }
                else if (v.is_array())
                    for (const auto &child : v)
                        measure(child, level + 1);
                else if (v.is_binary() || v.is_discarded() ||
                         (v.is_number_float() && !std::isfinite(v.get<double>())))
                    throw Error("schema-value", "Invalid resolved reference value", r.pos);
            };
            measure(value, nesting + 1);
            return value;
        }
        case 11: {
            if (!zero && options.text_layout != TextLayout::ue5_history)
                throw Error("schema-context", "Text property requires an explicit supported history layout",
                            r.pos);
            if (zero)
                return {{"flags", 0}, {"history", -1}, {"kind", "none"}, {"source", ""}};
            detail::TextWireReader reader;
            reader.argument_data_enabled =
                options.text_argument_data_layout == TextArgumentDataLayout::variant_int32 ||
                options.text_argument_data_layout == TextArgumentDataLayout::variant_int64;
            reader.argument_data_int64 =
                options.text_argument_data_layout == TextArgumentDataLayout::variant_int64;
            reader.type_error = "schema-type";
            reader.value_error = "schema-value";
            reader.limit_error = "schema-limit";
            reader.identity_error = "schema-identity";
            reader.remaining = [&] { return options.max_values - result.values_read; };
            reader.position = [&] { return r.pos; };
            reader.check = [&](unsigned level) {
                depth(level);
                charge();
            };
            reader.payload = [&](std::uint32_t bytes, unsigned level) -> Json {
                depth(level);
                charge();
                text(bytes);
                text(64);
                r.range(bytes);
                auto hash = detail::hash_range(r.data, r.pos, bytes, options.control);
                r.skip(bytes);
                return {{"bytes", bytes}, {"sha256", std::move(hash)}};
            };
            reader.scalar = [&](detail::TextScalar kind, unsigned level) -> Json {
                detail::ValueIdentityJournal::Pause unlocated_text_name(identities);
                unsigned type = 0;
                switch (kind) {
                case detail::TextScalar::uint8:
                    type = 0;
                    break;
                case detail::TextScalar::int8:
                    type = 23;
                    break;
                case detail::TextScalar::int32:
                    type = 2;
                    break;
                case detail::TextScalar::uint32:
                    type = 19;
                    break;
                case detail::TextScalar::int64:
                    type = 21;
                    break;
                case detail::TextScalar::uint64:
                    type = 18;
                    break;
                case detail::TextScalar::float32:
                    type = 3;
                    break;
                case detail::TextScalar::float64:
                    type = 7;
                    break;
                case detail::TextScalar::string:
                    type = 10;
                    break;
                case detail::TextScalar::name:
                    type = 5;
                    break;
                }
                return read_value(SchemaType{static_cast<std::uint8_t>(type), {}, {}}, false, level);
            };
            return reader.text(nesting, false);
        }
        case 6: {
            if (!zero && options.script_reference_layout != ScriptReferenceLayout::package_indices)
                throw Error("schema-context", "Delegate requires an explicit persistent package layout",
                            r.pos);
            auto object = read_value(SchemaType{4, {}, {}}, zero, nesting + 1);
            detail::ValueIdentityJournal::Scope function_identity(identities, "function");
            auto function = read_value(SchemaType{5, {}, {}}, zero, nesting + 1);
            return {{"kind", "script-delegate"},
                    {"object", std::move(object)},
                    {"function", std::move(function)}};
        }
        case 13: {
            if (!zero && options.script_reference_layout != ScriptReferenceLayout::package_indices)
                throw Error("schema-context",
                            "Multicast delegate requires an explicit persistent package layout", r.pos);
            const auto count = zero ? 0 : r.count(options.max_values - result.values_read);
            Json bindings = Json::array();
            detail::ValueIdentityJournal::Scope bindings_identity(identities, "bindings");
            for (unsigned i = 0; i < count; ++i) {
                detail::ValueIdentityJournal::Scope binding_identity(identities, static_cast<std::size_t>(i));
                bindings.push_back(read_value(SchemaType{6, {}, {}}, false, nesting + 1));
                options.control.checkpoint("unversioned-delegate-bindings", i + 1, count);
            }
            return {{"kind", "multicast-script-delegate"}, {"bindings", std::move(bindings)}};
        }
        case 14:
            if (!zero && options.script_reference_layout != ScriptReferenceLayout::package_indices)
                throw Error("schema-context", "Weak reference requires an explicit persistent package layout",
                            r.pos);
            return {{"kind", "weak-object-reference"},
                    {"object", read_value(SchemaType{4, {}, {}}, zero, nesting + 1)}};
        case 12:
            return {{"kind", "interface-reference"},
                    {"object", read_value(SchemaType{4, {}, {}}, zero, nesting + 1)}};
        case 15: {
            text(32);
            std::string guid;
            static const char digits[] = "0123456789abcdef";
            for (unsigned word = 0; word < 4; ++word) {
                auto value = zero ? 0ull : r.number(4);
                std::string part(8, '0');
                for (unsigned j = 0; j < 8; ++j) {
                    part[7 - j] = digits[value & 15];
                    value >>= 4;
                }
                guid += part;
            }
            return {{"kind", "lazy-object-guid"}, {"guid", std::move(guid)}};
        }
        case 17: {
            if (!zero && options.soft_path_layout != SoftPathLayout::ue5_top_level_names)
                throw Error("schema-context", "Soft object paths require an explicit supported layout",
                            r.pos);
            auto package = [&] {
                detail::ValueIdentityJournal::Scope part(identities, "packageName");
                return read_value(SchemaType{5, {}, {}}, zero, nesting + 1);
            }();
            auto asset = [&] {
                detail::ValueIdentityJournal::Scope part(identities, "assetName");
                return read_value(SchemaType{5, {}, {}}, zero, nesting + 1);
            }();
            auto subpath = read_value(SchemaType{10, {}, {}}, zero, nesting + 1);
            return {{"kind", "soft-object-path"},
                    {"packageName", std::move(package)},
                    {"assetName", std::move(asset)},
                    {"subPath", std::move(subpath)}};
        }
        case 5: {
            if (zero) {
                text(4);
                identities.name("None", 0, true);
                return "None";
            }
            auto index = r.i32(), number = r.i32();
            if (!options.names || index < 0 || number < 0 ||
                static_cast<std::size_t>(index) >= options.names->size())
                throw Error("schema-context", "Invalid package name index or number", r.pos - 8);
            const auto &base = (*options.names)[static_cast<std::size_t>(index)];
            text(base.size() + 12);
            identities.name(base, static_cast<std::uint32_t>(number));
            return number ? base + "_" + std::to_string(number - 1) : base;
        }
        case 29:
        case 30: {
            if (zero)
                return "";
            if (options.byte_string_layout != ByteStringLayout::counted_utf8_ascii)
                throw Error("schema-context", "Byte string requires an explicit counted layout", r.pos);
            const auto count = r.i32();
            if (count < 0)
                throw Error("schema-value", "Negative byte string length", r.pos - 4);
            text(static_cast<std::uint32_t>(count));
            r.range(static_cast<std::uint32_t>(count));
            const auto offset = r.pos;
            std::string value;
            for (std::uint32_t at = 0; at < static_cast<std::uint32_t>(count);) {
                auto size = std::min<std::uint32_t>(4096, static_cast<std::uint32_t>(count) - at);
                options.control.check();
                value.append(reinterpret_cast<const char *>(r.data.data() + r.pos), size);
                r.pos += size;
                at += size;
            }
            if (type.kind == 30 && options.narrow_string_encoding != NarrowStringEncoding::unspecified) {
                try {
                    auto decoded = decode_narrow_string(
                        value, options.narrow_string_encoding,
                        options.max_text_bytes - result.text_bytes + value.size(), options.control);
                    if (decoded.size() > value.size())
                        text(decoded.size() - value.size());
                    return decoded;
                } catch (const Error &error) {
                    if (error.code == "string-limit")
                        throw Error("schema-limit", error.what(), r.pos);
                    throw;
                }
            }
            detail::validate_byte_string(value, type.kind == 30, options.control,
                                         type.kind == 30 ? "schema-type" : "schema-value", offset);
            return value;
        }
        case 10: {
            if (zero)
                return "";
            auto length = r.i32();
            if (!length)
                return "";
            bool wide = length < 0;
            auto n = wide ? -static_cast<std::int64_t>(length) : static_cast<std::int64_t>(length);
            if (std::uint64_t(n) > options.max_text_bytes - result.text_bytes)
                throw Error("schema-limit", "String property exceeds limit", r.pos - 4);
            r.range(std::uint64_t(n) * (wide ? 2 : 1));
            if (!wide && options.narrow_string_encoding != NarrowStringEncoding::unspecified) {
                const auto at = r.pos;
                std::string raw(reinterpret_cast<const char *>(r.data.data() + at),
                                static_cast<std::size_t>(n - 1));
                r.skip(static_cast<std::uint64_t>(n));
                if (r.data[static_cast<std::size_t>(r.pos - 1)] || raw.find('\0') != std::string::npos)
                    throw Error("schema-value", "Invalid narrow FString terminator", at);
                try {
                    auto decoded =
                        decode_narrow_string(raw, options.narrow_string_encoding,
                                             options.max_text_bytes - result.text_bytes, options.control);
                    text(decoded.size());
                    return decoded;
                } catch (const Error &error) {
                    if (error.code == "string-limit")
                        throw Error("schema-limit", error.what(), at);
                    throw;
                }
            }
            std::string value;
            for (std::int64_t j = 0; j < n; ++j) {
                auto cp = static_cast<std::uint32_t>(r.number(wide ? 2 : 1));
                if (j == n - 1) {
                    if (cp)
                        throw Error("schema-value", "Unterminated string property", r.pos);
                    break;
                }
                if (!cp)
                    throw Error("schema-value", "Embedded NUL in string property", r.pos);
                if (!wide && cp >= 128)
                    throw Error("schema-type",
                                "Non-ASCII narrow FString requires a host character-set reader", r.pos);
                if (wide && cp >= 0xd800 && cp <= 0xdbff) {
                    if (j + 1 >= n - 1)
                        throw Error("schema-value", "Truncated UTF-16 surrogate", r.pos);
                    auto low = r.number(2);
                    ++j;
                    if (low < 0xdc00 || low > 0xdfff)
                        throw Error("schema-value", "Invalid UTF-16 surrogate", r.pos);
                    cp = 0x10000 + ((cp - 0xd800) << 10) + static_cast<unsigned>(low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    throw Error("schema-value", "Unpaired UTF-16 surrogate", r.pos);
                auto old = value.size();
                append_utf8(value, cp);
                text(value.size() - old);
            }
            return value;
        }
        case 27: {
            if (!zero && options.field_path_layout != FieldPathLayout::owner_and_names)
                throw Error("schema-context", "Field path requires an explicit owner/name layout", r.pos);
            const auto count = zero ? 0 : r.count(options.max_values - result.values_read);
            Json path = Json::array();
            {
                detail::ValueIdentityJournal::Scope path_identity(identities, "path");
                for (unsigned j = 0; j < count; ++j) {
                    detail::ValueIdentityJournal::Scope component(identities, static_cast<std::size_t>(j));
                    path.push_back(read_value(SchemaType{5, {}, {}}, false, nesting + 1));
                    options.control.checkpoint("unversioned-field-path", j + 1, count);
                }
            }
            auto owner = read_value(SchemaType{4, {}, {}}, zero, nesting + 1);
            return {{"kind", "field-path"}, {"path", std::move(path)}, {"owner", std::move(owner)}};
        }
        case 28: {
            if (!zero && options.optional_layout != OptionalLayout::bool32_value)
                throw Error("schema-context", "Optional requires an explicit unversioned binary layout",
                            r.pos);
            const auto present = zero ? 0ull : r.number(4);
            if (present > 1)
                throw Error("schema-value", "Invalid optional presence marker", r.pos - 4);
            Json value = {{"kind", "optional"}, {"isSet", present != 0}};
            if (present) {
                detail::ValueIdentityJournal::Scope optional_value(identities, "value");
                value["value"] = read_value(type.arguments.at(0), false, nesting + 1);
            }
            return value;
        }
        case 8: {
            auto count = zero ? 0 : r.count(options.max_values - result.values_read);
            Json values = Json::array();
            for (unsigned j = 0; j < count; ++j) {
                detail::ValueIdentityJournal::Scope item_identity(identities, static_cast<std::size_t>(j));
                values.push_back(read_value(type.arguments.at(0), false, nesting + 1));
            }
            return values;
        }
        case 24:
        case 25:
            return collection(type, zero, nesting);
        case 9: {
            const auto &definition = schemas.schema(type.name);
            auto native = native_structs.find(definition.path);
            if (native != native_structs.end())
                return native_value(native->second, zero, nesting);
            auto described = struct_layouts.find(definition.path);
            if (described != struct_layouts.end())
                return layout_value(described->second, zero, nesting);
            auto tail = struct_tails.find(definition.path);
            if (tail != struct_tails.end()) {
                if (zero)
                    throw Error("schema-type", "Hybrid native zero defaults require an explicit value model",
                                r.pos);
                text(16); // The two fixed wrapper keys: properties and native.
                auto properties = [&] {
                    detail::ValueIdentityJournal::Scope prefix(identities, "properties");
                    return structure(definition.path, false, nesting + 1);
                }();
                auto native_tail = layout_value(tail->second, false, nesting + 1);
                return {{"properties", std::move(properties)}, {"native", std::move(native_tail)}};
            }
            bool allowed = options.reflected_structs.count(definition.path) != 0;
            if (!allowed && options.reflected_structs.count(definition.name))
                allowed = schemas.schema(definition.name).path == definition.path;
            if (!allowed || (definition.attributes_present && definition.struct_kind == 2))
                throw Error("schema-type", "Struct needs an explicit reflected layout: " + definition.path,
                            r.pos);
            return structure(definition.path, zero, nesting + 1);
        }
        case 26:
            schemas.enumeration(type.name);
            if (type.arguments.empty() || !(type.arguments[0].kind == 0 || type.arguments[0].kind == 2 ||
                                            (type.arguments[0].kind >= 18 && type.arguments[0].kind <= 23)))
                throw Error("schema-type", "Enum requires an integer underlying type", r.pos);
            return read_value(type.arguments[0], zero, nesting + 1);
        default:
            throw Error("schema-type",
                        std::string("Unsupported unversioned value type: ") + type_name(type.kind), r.pos);
        }
    }
};
} // namespace
namespace {
PropertyReader prepare_property_reader(const std::vector<std::uint8_t> &bytes, std::uint64_t offset,
                                       std::uint64_t size, const UnversionedSchemas &schemas,
                                       const UnversionedReadOptions &options) {
    if (offset > bytes.size() || size > bytes.size() - offset)
        throw Error("bounds", "Invalid unversioned property window", offset);
    options.control.check();
    if ((!options.reference && !options.reference_identity.empty()) ||
        options.reference_identity.size() > 256 ||
        std::any_of(options.reference_identity.begin(), options.reference_identity.end(),
                    [](unsigned char c) { return c < 0x21 || c > 0x7e; }))
        throw Error("schema-context", "Invalid property reference resolver identity", offset);
    PropertyReader reader{{bytes, options.control, offset, offset + size},
                          schemas,
                          options,
                          detail::ValueIdentityJournal(options.value_identities, options.control),
                          {},
                          {},
                          {},
                          {}};
    for (const auto &binding : options.native_structs) {
        reader.charge();
        native_struct_layout_name(binding.second);
        const auto &definition = schemas.schema(binding.first);
        if (!reader.native_structs.emplace(definition.path, binding.second).second ||
            options.reflected_structs.count(definition.path) ||
            (options.reflected_structs.count(definition.name) &&
             schemas.schema(definition.name).path == definition.path))
            throw Error("schema-context", "Conflicting native/reflected struct binding: " + definition.path);
    }
    for (const auto &binding : options.struct_layouts) {
        reader.charge();
        if (!options.binary_layouts || !options.binary_layouts->contains(binding.second))
            throw Error("schema-context", "Missing declarative binary layout: " + binding.second);
        const auto &definition = schemas.schema(binding.first);
        if (!reader.struct_layouts.emplace(definition.path, binding.second).second ||
            reader.native_structs.count(definition.path) ||
            options.reflected_structs.count(definition.path) ||
            (options.reflected_structs.count(definition.name) &&
             schemas.schema(definition.name).path == definition.path))
            throw Error("schema-context", "Conflicting declarative struct binding: " + definition.path);
    }
    for (const auto &binding : options.struct_tails) {
        reader.charge();
        if (!options.binary_layouts || !options.binary_layouts->contains(binding.second))
            throw Error("schema-context", "Missing declarative struct tail: " + binding.second);
        const auto &definition = schemas.schema(binding.first);
        if (!reader.struct_tails.emplace(definition.path, binding.second).second ||
            reader.native_structs.count(definition.path) || reader.struct_layouts.count(definition.path) ||
            options.reflected_structs.count(definition.path) ||
            (options.reflected_structs.count(definition.name) &&
             schemas.schema(definition.name).path == definition.path))
            throw Error("schema-context", "Conflicting hybrid struct binding: " + definition.path);
    }
    return reader;
}
} // namespace
UnversionedProperties decode_unversioned(const std::vector<std::uint8_t> &bytes, std::uint64_t offset,
                                         std::uint64_t size, const UnversionedSchemas &schemas,
                                         const std::string &schema, const UnversionedReadOptions &options) {
    auto reader = prepare_property_reader(bytes, offset, size, schemas, options);
    const auto &root = schemas.schema(schema);
    reader.result.interpretation = {
        {"schema", root.path}, {"schemas", Json::object()}, {"format", "cyuediff.cooked-interpretation/1"}};
    reader.inventory_schema(root, 0);
    reader.result.properties = reader.structure(schema, false, 0);
    reader.result.consumed_bytes = reader.r.pos - offset;
    reader.result.value_identities = reader.identities.finish();
    if (!reader.result.value_identities.is_null())
        reader.result.value_identities["root"] = "properties";
    options.control.check();
    return std::move(reader.result);
}
UnversionedValue decode_unversioned_value(const std::vector<std::uint8_t> &bytes, std::uint64_t offset,
                                          std::uint64_t size, const UnversionedSchemas &schemas,
                                          const SchemaType &type, const UnversionedReadOptions &options) {
    auto reader = prepare_property_reader(bytes, offset, size, schemas, options);
    reader.result.interpretation = {{"format", "cyuediff.cooked-value-interpretation/1"},
                                    {"schemas", Json::object()}};
    std::function<void(const SchemaType &, unsigned)> validate = [&](const SchemaType &t, unsigned nesting) {
        reader.depth(nesting);
        reader.charge();
        const auto count = t.kind == 24                                                  ? 2u
                           : t.kind == 8 || t.kind == 25 || t.kind == 26 || t.kind == 28 ? 1u
                                                                                         : 0u;
        if ((t.kind > 30 && t.kind != 255) || t.arguments.size() != count ||
            ((t.kind == 9 || t.kind == 26) != !t.name.empty()))
            throw Error("schema-type", "Malformed standalone value descriptor", offset);
        for (const auto &argument : t.arguments)
            validate(argument, nesting + 1);
    };
    validate(type, 0);
    reader.result.interpretation["type"] = reader.type_context(type, 0);
    reader.inventory_type(type, 0);
    auto value = reader.read_value(type, false, 0);
    options.control.check();
    auto identities = reader.identities.finish();
    if (!identities.is_null())
        identities["root"] = "value";
    return {std::move(value),          std::move(reader.result.interpretation),
            reader.r.pos - offset,     reader.result.text_bytes,
            reader.result.values_read, std::move(identities)};
}

} // namespace cyuediff
