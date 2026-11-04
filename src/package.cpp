#include "material_io.hpp"
#include "mesh.hpp"
#include "texture.hpp"
#include "archive.hpp"
#include "native.hpp"
#include "decoders.hpp"
#include <cmath>
#include <memory>
#include <set>
#include <sstream>
namespace cyuediff {
using detail::Archive;
namespace {
constexpr std::uint32_t filter_editor = 0x80000000u, unversioned_properties = 0x00002000u;
std::uint64_t positive64(Archive &a) {
    auto v = a.get<std::int64_t>();
    if (v < 0)
        throw Error("format", "Negative export range", a.tell() - 8);
    return static_cast<std::uint64_t>(v);
}
std::string fname(Archive &a, const Package &p) {
    auto index = a.get<std::int32_t>(), number = a.get<std::int32_t>();
    if (index < 0 || static_cast<std::size_t>(index) >= p.names.size() || number < 0)
        throw Error("name-index", "Invalid FName index", a.tell() - 8);
    return p.names[index] + (number ? "_" + std::to_string(number - 1) : "");
}
std::string engine_version(Archive &a) {
    auto major = a.get<std::uint16_t>(), minor = a.get<std::uint16_t>(), patch = a.get<std::uint16_t>();
    auto changelist = a.get<std::uint32_t>();
    auto branch = a.string();
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch) + "-" +
           std::to_string(changelist) + branch;
}
void table_range(Archive &a, std::int32_t offset, std::int32_t count, std::uint32_t minimum,
                 std::int32_t header) {
    if (offset < 0 || count < 0 || static_cast<std::uint64_t>(offset) > a.size() ||
        std::uint64_t(count) * minimum > a.size() - static_cast<std::uint64_t>(offset) ||
        (count && (!offset || offset >= header)))
        throw Error("table", "Invalid package table range");
}
void read_summary(Archive &a, PackageSummary &s) {
    if (a.size() >= 40) {
        char prefix[40]{};
        a.read(prefix, 40);
        a.seek(0);
        if (std::string(prefix, 40).find("version https://git-lfs.github.com/spec/") == 0)
            throw Error("lfs-pointer",
                        "This is a Git LFS pointer. Materialize the binary asset before reading it.");
    }
    auto magic = a.get<std::uint32_t>();
    if (magic == 0xc1832a9e)
        throw Error("unsupported-endian", "Big-endian packages are not supported");
    if (magic != 0x9e2a83c1)
        throw Error(
            "magic",
            "Expected a loose Unreal package (.uasset/.umap); pak and IoStore need a container backend");
    s.legacy_version = a.get<std::int32_t>();
    if (s.legacy_version > -7 || s.legacy_version < -9)
        throw Error("version", "Unsupported legacy package format");
    a.skip(4);
    s.ue4_version = a.get<std::int32_t>();
    if (s.legacy_version <= -8)
        s.ue5_version = a.get<std::int32_t>();
    s.licensee_version = a.get<std::int32_t>();
    // The -9 contract requires this check before reading any further fields.
    if (s.ue4_version < 518 || s.ue4_version > 522 || s.ue5_version < 0 ||
        (s.ue5_version > 0 && s.ue5_version < 1000) || s.ue5_version > 1018)
        throw Error("version", "Unsupported or unversioned package; an explicit backend/schema is required");
    if (s.licensee_version)
        throw Error("licensee-version", "Custom licensee serialization requires a dedicated backend");
    if (s.ue5_version >= 1016) {
        a.skip(20);
        s.header_size = a.get<std::int32_t>();
    }
    auto versions = a.count();
    for (std::uint32_t i = 0; i < versions; ++i) {
        auto key = a.guid();
        s.custom_versions[key] = a.get<std::int32_t>();
    }
    if (s.ue5_version < 1016)
        s.header_size = a.get<std::int32_t>();
    if (s.header_size <= 0 || static_cast<std::uint64_t>(s.header_size) > a.size())
        throw Error("header", "Invalid package header size");
    s.package_name = a.string();
    s.flags = a.get<std::uint32_t>();
    s.name_count = static_cast<int>(a.count());
    s.name_offset = a.get<std::int32_t>();
    if (s.ue5_version >= 1008)
        a.skip(8);
    if (!(s.flags & filter_editor))
        a.string();
    a.skip(8); // gatherable text table
    s.export_count = static_cast<int>(a.count());
    s.export_offset = a.get<std::int32_t>();
    s.import_count = static_cast<int>(a.count());
    s.import_offset = a.get<std::int32_t>();
    if (s.ue5_version >= 1015)
        a.skip(16); // Verse cell tables
    if (s.ue5_version >= 1014)
        a.skip(4);
    a.skip(4 + 8 + 4 + 4); // depends, soft packages, searchable names, thumbnails
    if (s.ue5_version >= 1018)
        a.skip(8);
    if (s.ue5_version < 1016)
        a.skip(16);
    if (!(s.flags & filter_editor)) {
        a.skip(16);
        if (s.ue4_version < 520)
            a.skip(16);
    }
    auto generations = a.count();
    a.skip(std::uint64_t(generations) * 8);
    s.saved_engine = engine_version(a);
    engine_version(a);
    auto compression = a.get<std::uint32_t>();
    auto chunks = a.count();
    if (compression || chunks)
        throw Error("compression", "Package-level compression requires a decompression backend");
    table_range(a, s.name_offset, s.name_count, 8, s.header_size);
    table_range(a, s.import_offset, s.import_count, 28, s.header_size);
    table_range(a, s.export_offset, s.export_count, 80, s.header_size);
}
struct Type {
    std::string name;
    std::vector<Type> parameters;
};
Json type_json(const Type &t) {
    Json j = {{"name", t.name}};
    if (!t.parameters.empty()) {
        j["parameters"] = Json::array();
        for (const auto &child : t.parameters)
            j["parameters"].push_back(type_json(child));
    }
    return j;
}
struct Tag {
    std::string name, guid;
    Type type;
    std::uint32_t bytes = 0, index = 0;
    std::uint8_t flags = 0;
    bool boolean = false;
};
class PropertyReader {
    Archive &a;
    const Package &p;
    const ObjectExport &object;
    const ReadOptions &options;
    std::uint32_t count_ = 0, type_nodes_ = 0;
    void depth_check(unsigned depth) const {
        if (depth > options.max_depth)
            throw Error("limit", "Property nesting exceeds limit", a.tell());
    }
    Type full_type(unsigned depth) {
        depth_check(depth);
        if (++type_nodes_ > options.max_properties)
            throw Error("limit", "Type nodes exceed limit", a.tell());
        Type t;
        t.name = fname(a, p);
        auto n = a.count(options.max_properties);
        for (std::uint32_t i = 0; i < n; ++i)
            t.parameters.push_back(full_type(depth + 1));
        return t;
    }
    void extensions() {
        auto flags = a.get<std::uint8_t>();
        if (flags & ~2u)
            throw Error("property-extension", "Unknown property extension", a.tell() - 1);
        if (flags & 2u)
            a.skip(5); // operation byte + archive boolean
    }
    Tag tag() {
        Tag t;
        t.name = fname(a, p);
        if (t.name == "None")
            return t;
        if (++count_ > options.max_properties)
            throw Error("limit", "Property count exceeds limit", a.tell());
        if (p.summary.ue5_version >= 1012) {
            t.type = full_type(0);
            t.bytes = a.count(
                static_cast<std::uint32_t>(std::min<std::uint64_t>(options.max_property_bytes, UINT32_MAX)));
            t.flags = a.get<std::uint8_t>();
            if (t.flags & 0xc0)
                throw Error("property-flags", "Unknown property tag flags", a.tell() - 1);
            if (t.flags & 1)
                t.index = a.count();
            if (t.flags & 2)
                t.guid = a.guid();
            if (t.flags & 4)
                extensions();
            t.boolean = (t.flags & 0x10) != 0;
        } else {
            t.type.name = fname(a, p);
            t.bytes = a.count(
                static_cast<std::uint32_t>(std::min<std::uint64_t>(options.max_property_bytes, UINT32_MAX)));
            t.index = a.count();
            auto add = [&]() { t.type.parameters.push_back({fname(a, p), {}}); };
            if (t.type.name == "StructProperty") {
                add();
                a.skip(16);
            } else if (t.type.name == "BoolProperty")
                t.boolean = a.get<std::uint8_t>() != 0;
            else if (t.type.name == "ByteProperty" || t.type.name == "EnumProperty" ||
                     t.type.name == "ArrayProperty" || t.type.name == "SetProperty" ||
                     t.type.name == "OptionalProperty")
                add();
            else if (t.type.name == "MapProperty") {
                add();
                add();
            }
            if (a.get<std::uint8_t>())
                t.guid = a.guid();
            if (p.summary.ue5_version >= 1011)
                extensions();
        }
        return t;
    }
    Json number(double value) {
        if (std::isfinite(value))
            return value;
        return {{"nonFinite", std::isnan(value) ? "NaN" : (value > 0 ? "Infinity" : "-Infinity")}};
    }
    Json value(const Type &t, std::uint64_t end, unsigned depth, bool exact_size = true) {
        depth_check(depth);
        const auto &n = t.name;
        if (n == "IntProperty")
            return a.get<std::int32_t>();
        if (n == "Int8Property")
            return a.get<std::int8_t>();
        if (n == "Int16Property")
            return a.get<std::int16_t>();
        if (n == "Int64Property")
            return a.get<std::int64_t>();
        if (n == "UInt16Property")
            return a.get<std::uint16_t>();
        if (n == "UInt32Property")
            return a.get<std::uint32_t>();
        if (n == "UInt64Property")
            return a.get<std::uint64_t>();
        if (n == "FloatProperty")
            return number(a.get<float>());
        if (n == "DoubleProperty")
            return number(a.get<double>());
        if (n == "BoolProperty")
            return a.get<std::uint8_t>() != 0;
        if (n == "NameProperty" || n == "EnumProperty")
            return fname(a, p);
        if (n == "ByteProperty") {
            if (t.parameters.empty() || t.parameters[0].name == "None")
                return a.get<std::uint8_t>();
            return fname(a, p);
        }
        if (n == "StrProperty")
            return a.string();
        if (n == "ObjectProperty" || n == "ClassProperty" || n == "WeakObjectProperty")
            return p.reference(a.get<std::int32_t>());
        if (n == "SoftObjectProperty" || n == "SoftClassProperty") {
            auto path = fname(a, p);
            if (p.summary.ue5_version >= 1007) {
                auto asset = fname(a, p);
                if (asset != "None")
                    path += "." + asset;
            }
            auto sub = a.string();
            return path + (sub.empty() ? "" : ":" + sub);
        }
        if (n == "StructProperty" && !t.parameters.empty()) {
            const auto &s = t.parameters[0].name;
            if (options.decoders)
                if (const auto *decoder = options.decoders->find(DecoderKind::struct_value, s, p.summary))
                    return detail::decode_extension(a, p, object, options, *decoder, end, exact_size);
            if (s == "Guid")
                return a.guid();
            if (detail::is_material_input(s)) {
                if (!options.native_materials)
                    throw Error("material-disabled", "Native material inputs disabled", a.tell());
                if (detail::native_material_input(p.summary))
                    return detail::decode_material_input(a, p, s, end);
            }
            unsigned components = 0;
            bool doubles = false, integers = false;
            if (s == "Vector" || s == "Rotator") {
                components = 3;
                doubles = p.summary.ue5_version >= 1004;
            } else if (s == "Vector2D") {
                components = 2;
                doubles = p.summary.ue5_version >= 1004;
            } else if (s == "Quat" || s == "Vector4") {
                components = 4;
                doubles = p.summary.ue5_version >= 1004;
            } else if (s == "LinearColor")
                components = 4;
            else if (s == "IntPoint") {
                components = 2;
                integers = true;
            } else if (s == "IntVector") {
                components = 3;
                integers = true;
            } else if (s == "Color") {
                Json c = Json::array();
                for (int i = 0; i < 4; ++i)
                    c.push_back(a.get<std::uint8_t>());
                return c;
            }
            if (components) {
                Json v = Json::array();
                for (unsigned i = 0; i < components; ++i)
                    v.push_back(integers ? Json(a.get<std::int32_t>())
                                         : number(doubles ? a.get<double>() : a.get<float>()));
                return v;
            }
            return properties(end, depth + 1, false);
        }
        if (n == "ArrayProperty" && !t.parameters.empty()) {
            auto count = a.count(options.max_properties);
            Json values = Json::array();
            auto inner = t.parameters[0];
            if (inner.name == "StructProperty" && p.summary.ue5_version < 1012) {
                auto element = tag();
                if (element.type.name != "StructProperty" || element.type.parameters.empty())
                    throw Error("array-tag", "Invalid struct array element tag", a.tell());
                inner = std::move(element.type);
            }
            // Legacy array tags omit the enum type. Exact size distinguishes FNames from raw bytes.
            bool enum_names = inner.name == "ByteProperty" && inner.parameters.empty() && count &&
                              p.summary.ue5_version < 1012 && end - a.tell() == std::uint64_t(count) * 8;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (a.tell() >= end)
                    throw Error("bounds", "Array exceeds property payload", a.tell());
                values.push_back(enum_names ? Json(fname(a, p)) : value(inner, end, depth + 1, false));
            }
            return values;
        }
        throw Error("opaque", "No decoder for " + n, a.tell());
    }

  public:
    PropertyReader(Archive &ar, const Package &package, const ObjectExport &entry, const ReadOptions &o)
        : a(ar), p(package), object(entry), options(o) {}
    bool partial = false;
    Json properties(std::uint64_t end, unsigned depth = 0, bool root = true) {
        depth_check(depth);
        Archive::ReadLimit script_limit(a, end);
        Json result = Json::object();
        if (root && p.summary.ue5_version >= 1011) {
            auto flags = a.get<std::uint8_t>();
            if (flags)
                throw Error("class-extension", "Overridable root serialization requires an adapter",
                            a.tell() - 1);
        }
        while (a.tell() < end) {
            auto t = tag();
            if (t.name == "None")
                return result;
            if (a.tell() > end || t.bytes > end - a.tell())
                throw Error("bounds", "Property range exceeds script stream", a.tell());
            auto start = a.tell(), next = start + t.bytes;
            Json entry = {{"type", type_json(t.type)}};
            if (!t.guid.empty())
                entry["guid"] = t.guid;
            if (t.flags & 0x28)
                entry["serializationFlags"] = t.flags;
            try {
                Archive::ReadLimit property_limit(a, next);
                if (t.type.name == "BoolProperty")
                    entry["value"] = t.boolean;
                else if (t.flags & 0x20)
                    entry["value"] = nullptr;
                else
                    entry["value"] = value(t.type, next, depth + 1);
                if (a.tell() != next)
                    throw Error("opaque", "Property decoder did not consume exact payload", a.tell());
            } catch (const Error &e) {
                partial = true;
                entry.erase("value");
                entry["opaque"] = {{"bytes", t.bytes}, {"reason", e.code}};
                if (options.fingerprints)
                    entry["opaque"]["sha256"] = a.hash(start, t.bytes);
            }
            a.seek(next);
            auto key = t.name + (t.index ? "[" + std::to_string(t.index) + "]" : "");
            if (result.contains(key))
                throw Error("property-key", "Duplicate property identity", start);
            result[key] = std::move(entry);
        }
        throw Error("property-terminator", "Missing None terminator", a.tell());
    }
};
Json opaque_range(Archive &a, std::uint64_t start, std::uint64_t bytes, const ReadOptions &options) {
    Json j = {{"bytes", bytes}};
    if (options.fingerprints)
        j["sha256"] = a.hash(start, bytes);
    return j;
}
} // namespace
Package Package::open(const fs::path &file, const ReadOptions &options) {
    Package p;
    p.source = fs::weakly_canonical(fs::absolute(file));
    Archive header(file, options);
    read_summary(header, p.summary);
    const auto &s = p.summary;
    header.seek(s.name_offset);
    p.names.reserve(s.name_count);
    for (int i = 0; i < s.name_count; ++i) {
        p.names.push_back(header.string());
        header.skip(4);
        if (header.tell() > static_cast<std::uint64_t>(s.header_size))
            throw Error("table", "Name table escapes header");
    }
    header.seek(s.import_offset);
    p.imports.reserve(s.import_count);
    for (int i = 0; i < s.import_count; ++i) {
        ObjectImport entry;
        entry.class_package = fname(header, p);
        entry.class_name = fname(header, p);
        entry.outer_index = header.get<std::int32_t>();
        entry.name = fname(header, p);
        if (s.ue4_version >= 520 && !(s.flags & filter_editor))
            entry.package_name = fname(header, p);
        if (s.ue5_version >= 1003)
            header.skip(4);
        p.imports.push_back(std::move(entry));
    }
    if (header.tell() > static_cast<std::uint64_t>(s.header_size))
        throw Error("table", "Import table escapes header");
    header.seek(s.export_offset);
    p.exports.reserve(s.export_count);
    for (int i = 0; i < s.export_count; ++i) {
        ObjectExport entry;
        entry.class_index = header.get<std::int32_t>();
        entry.super_index = header.get<std::int32_t>();
        entry.template_index = header.get<std::int32_t>();
        entry.outer_index = header.get<std::int32_t>();
        entry.name = fname(header, p);
        entry.flags = header.get<std::uint32_t>();
        entry.size = positive64(header);
        entry.offset = positive64(header);
        header.skip(12);
        if (s.ue5_version < 1005)
            header.skip(16);
        if (s.ue5_version >= 1006)
            header.skip(4);
        header.skip(12);
        if (s.ue5_version >= 1003)
            header.skip(4);
        header.skip(20);
        if (s.ue5_version >= 1010 && !(s.flags & unversioned_properties)) {
            entry.script_start = positive64(header);
            entry.script_end = positive64(header);
        }
        if (entry.script_start > entry.script_end || entry.script_end > entry.size)
            throw Error("export-script", "Invalid script serialization range");
        p.exports.push_back(std::move(entry));
    }
    if (header.tell() > static_cast<std::uint64_t>(s.header_size))
        throw Error("table", "Export table escapes header");
    std::set<std::string> object_paths;
    for (std::size_t i = 0; i < p.exports.size(); ++i) {
        auto &entry = p.exports[i];
        entry.path = p.reference(static_cast<int>(i + 1));
        entry.class_name = p.reference(entry.class_index);
        entry.category = detail::category(entry.class_name);
        if (!object_paths.insert(entry.path).second)
            throw Error("object-path", "Duplicate export identity");
        p.reference(entry.super_index);
        p.reference(entry.template_index);
    }
    for (const auto &entry : p.imports)
        p.reference(entry.outer_index);
    std::unique_ptr<Archive> export_data;
    auto sidecar = file;
    sidecar.replace_extension(".uexp");
    if (fs::is_regular_file(sidecar))
        export_data = std::make_unique<Archive>(sidecar, options);
    for (auto &entry : p.exports) {
        if (entry.size == 0) {
            entry.coverage = "empty";
            continue;
        }
        if (entry.offset < static_cast<std::uint64_t>(s.header_size))
            throw Error("export-range", "Export payload overlaps package header");
        Archive *data = &header;
        auto base = entry.offset;
        if (base > header.size() || entry.size > header.size() - base) {
            if (!export_data || base < static_cast<std::uint64_t>(s.header_size)) {
                p.complete_input = false;
                entry.coverage = "missing";
                p.diagnostics.push_back(entry.path + ": missing export payload (.uexp)");
                continue;
            }
            data = export_data.get();
            base -= s.header_size;
        }
        if (base > data->size() || entry.size > data->size() - base) {
            p.complete_input = false;
            entry.coverage = "missing";
            p.diagnostics.push_back(entry.path + ": truncated export payload");
            continue;
        }
        Archive::ReadLimit export_limit(*data, base + entry.size);
        if (!options.properties || (s.flags & unversioned_properties)) {
            entry.opaque["export"] = opaque_range(*data, base, entry.size, options);
            entry.coverage = "opaque";
            continue;
        }
        auto start = base + entry.script_start;
        auto end = entry.script_end ? base + entry.script_end : base + entry.size;
        if (end - start > options.max_property_bytes) {
            entry.coverage = "opaque";
            entry.opaque["export"] = opaque_range(*data, base, entry.size, options);
            p.diagnostics.push_back(entry.path + ": script stream exceeds property byte limit");
            continue;
        }
        try {
            data->seek(start);
            PropertyReader reader(*data, p, entry, options);
            entry.properties = reader.properties(end);
            if (entry.script_end && data->tell() != end)
                throw Error("script-range", "Property terminator does not match script range", data->tell());
            entry.coverage = reader.partial ? "partial-properties" : "properties";
            if (start > base)
                entry.opaque["prefix"] = opaque_range(*data, base, start - base, options);
            auto consumed = data->tell();
            bool extension_native = false;
            if (options.decoders) {
                try {
                    if (const auto *decoder =
                            options.decoders->find(DecoderKind::native_export, entry.class_name, s)) {
                        extension_native = true;
                        entry.native_start = consumed - base;
                        entry.native_data = detail::decode_extension(*data, p, entry, options, *decoder,
                                                                     base + entry.size, true);
                        entry.native_coverage = "extension";
                        consumed = data->tell();
                        entry.native_end = consumed - base;
                    }
                } catch (const Error &error) {
                    extension_native = true;
                    entry.native_coverage = "opaque";
                    p.diagnostics.push_back(entry.path + ": native " + error.code + ": " + error.what());
                    data->seek(consumed);
                }
            }
            if (!extension_native && options.native_graphs && detail::is_edgraph_node(entry)) {
                try {
                    if (base + entry.size - consumed > options.max_property_bytes)
                        throw Error("limit", "Native node payload exceeds configured limit", consumed);
                    entry.native_start = consumed - base;
                    auto decoded = detail::decode_edgraph_node(
                        *data, p, static_cast<std::size_t>(&entry - p.exports.data()), base + entry.size,
                        options);
                    entry.native_data = std::move(decoded.data);
                    entry.native_coverage = "pins";
                    entry.native_end = decoded.end - base;
                    consumed = decoded.end;
                } catch (const Error &error) {
                    entry.native_coverage = "opaque";
                    p.diagnostics.push_back(entry.path + ": native " + error.code + " at " +
                                            std::to_string(error.offset) + ": " + error.what());
                    data->seek(consumed);
                }
            }
            if (!extension_native && options.native_textures && detail::is_texture2d(entry)) {
                try {
                    entry.native_start = consumed - base;
                    auto decoded = detail::decode_texture_source(
                        *data, p, static_cast<std::size_t>(&entry - p.exports.data()), base + entry.size,
                        options);
                    entry.native_data = std::move(decoded.native);
                    entry.native_coverage = "texture-source";
                    entry.native_end = decoded.end - base;
                    consumed = decoded.end;
                    p.textures.push_back(std::move(decoded.source));
                } catch (const Error &error) {
                    entry.native_coverage = "opaque";
                    std::string diagnostic = error.code + ": " + error.what();
                    TextureSource texture;
                    try {
                        texture = detail::texture_metadata(entry);
                    } catch (const Error &) {
                    }
                    texture.object_path = entry.path;
                    texture.diagnostics.push_back(diagnostic);
                    p.textures.push_back(std::move(texture));
                    p.diagnostics.push_back(entry.path + ": " + diagnostic);
                    data->seek(consumed);
                }
            }
            if (!extension_native && options.native_meshes && detail::is_mesh_description(entry)) {
                try {
                    entry.native_start = consumed - base;
                    auto decoded = detail::decode_mesh_source(
                        *data, p, static_cast<std::size_t>(&entry - p.exports.data()), base + entry.size,
                        options);
                    entry.native_data = std::move(decoded.native);
                    entry.native_coverage = "mesh-description-source";
                    entry.native_end = decoded.end - base;
                    consumed = decoded.end;
                    p.mesh_sources.push_back(std::move(decoded.source));
                } catch (const Error &error) {
                    entry.native_coverage = "opaque";
                    MeshSource source;
                    source.object_path = entry.path;
                    source.ue5_version = p.summary.ue5_version;
                    source.custom_versions = p.summary.custom_versions;
                    source.diagnostics.push_back(error.code + ": " + error.what());
                    p.mesh_sources.push_back(std::move(source));
                    p.diagnostics.push_back(entry.path + ": " + error.code + ": " + error.what());
                    data->seek(consumed);
                }
            }
            if (consumed < base + entry.size)
                entry.opaque["suffix"] = opaque_range(*data, consumed, base + entry.size - consumed, options);
        } catch (const Error &e) {
            entry.properties = Json::object();
            entry.coverage = "opaque";
            entry.opaque["export"] = opaque_range(*data, base, entry.size, options);
            p.diagnostics.push_back(entry.path + ": " + e.code + " at " + std::to_string(e.offset) + ": " +
                                    e.what());
        }
    }
    if (options.fingerprints) {
        p.fingerprints["package"] = {{"bytes", header.size()}, {"sha256", header.hash(0, header.size())}};
        for (const auto *extension : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
            auto companion = file;
            companion.replace_extension(extension);
            if (fs::is_regular_file(companion))
                p.fingerprints[extension] = {{"bytes", fs::file_size(companion)},
                                             {"sha256", sha256_file(companion)}};
        }
    }
    if (s.flags & unversioned_properties)
        p.diagnostics.push_back(
            "Unversioned properties require a schema backend; export bytes are retained as opaque hashes.");
    return p;
}
std::string Package::reference(std::int32_t index) const {
    if (index == 0)
        return "None";
    std::set<std::int32_t> visited;
    std::vector<std::string> parts;
    while (index) {
        if (visited.size() >= 256 || !visited.insert(index).second)
            throw Error("reference-cycle", "Cyclic or excessive outer chain");
        if (index < 0) {
            auto n = std::uint64_t(-std::int64_t(index) - 1);
            if (n >= imports.size())
                throw Error("reference-index", "Invalid import reference");
            parts.push_back(imports[n].name);
            index = imports[n].outer_index;
            if (!index && !imports[n].package_name.empty() && imports[n].package_name != "None" &&
                imports[n].name.rfind("/", 0) != 0)
                parts.push_back(imports[n].package_name);
        } else {
            auto n = static_cast<std::uint64_t>(index - 1);
            if (n >= exports.size())
                throw Error("reference-index", "Invalid export reference");
            parts.push_back(exports[n].name);
            index = exports[n].outer_index;
        }
    }
    std::string result;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!result.empty())
            result += '.';
        result += *it;
    }
    return result;
}
Json PackageSummary::to_json() const {
    return {{"legacyVersion", legacy_version},
            {"ue4Version", ue4_version},
            {"ue5Version", ue5_version},
            {"licenseeVersion", licensee_version},
            {"flags", flags},
            {"headerSize", header_size},
            {"nameCount", name_count},
            {"nameOffset", name_offset},
            {"importCount", import_count},
            {"importOffset", import_offset},
            {"exportCount", export_count},
            {"exportOffset", export_offset},
            {"packageName", package_name},
            {"savedEngine", saved_engine},
            {"customVersions", custom_versions}};
}
Json Package::semantic_snapshot() const {
    Json objects = Json::object();
    for (const auto &entry : exports) {
        objects[entry.path] = {{"class", entry.class_name},
                               {"category", entry.category},
                               {"flags", entry.flags},
                               {"super", reference(entry.super_index)},
                               {"template", reference(entry.template_index)},
                               {"properties", entry.properties},
                               {"opaque", entry.opaque}};
        if (!entry.native_data.empty())
            objects[entry.path]["native"] = entry.native_data;
    }
    Json dependencies = Json::object();
    for (std::size_t i = 0; i < imports.size(); ++i) {
        auto key = reference(-static_cast<int>(i) - 1);
        if (!dependencies.contains(key))
            dependencies[key] = Json::array();
        dependencies[key].push_back({{"class", imports[i].class_name},
                                     {"classPackage", imports[i].class_package},
                                     {"package", imports[i].package_name}});
    }
    for (auto &entries : dependencies)
        std::sort(entries.begin(), entries.end());
    return {{"schema", "cyuediff.snapshot/1"}, {"objects", objects},
            {"dependencies", dependencies},    {"textures", texture_snapshot().at("textures")},
            {"meshes", mesh_snapshot()},       {"materials", material_snapshot()}};
}
Json Package::to_json() const {
    Json coverage = Json::object();
    for (const auto &e : exports)
        coverage[e.path] = {{"coverage", e.coverage},
                            {"exportIndex", static_cast<std::size_t>(&e - exports.data()) + 1},
                            {"offset", e.offset},
                            {"bytes", e.size},
                            {"scriptStart", e.script_start},
                            {"scriptEnd", e.script_end},
                            {"nativeCoverage", e.native_coverage},
                            {"nativeStart", e.native_start},
                            {"nativeEnd", e.native_end}};
    return {{"schema", "cyuediff.package/1"},
            {"source", source.u8string()},
            {"summary", summary.to_json()},
            {"snapshot", semantic_snapshot()},
            {"blueprint", blueprint_snapshot()},
            {"textures", texture_snapshot()},
            {"meshes", mesh_snapshot()},
            {"materials", material_snapshot()},
            {"coverage", coverage},
            {"fingerprints", fingerprints},
            {"completeInput", complete_input},
            {"diagnostics", diagnostics}};
}
} // namespace cyuediff
