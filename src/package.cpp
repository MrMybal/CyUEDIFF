#include <cyuediff/reflection.hpp>
#include <cyuediff/tagged_types.hpp>
#include "material_io.hpp"
#include "mesh.hpp"
#include "texture.hpp"
#include "archive.hpp"
#include "native.hpp"
#include "skeleton_native.hpp"
#include "decoders.hpp"
#include "archive_text.hpp"
#include "struct_identity.hpp"
#include "key_identity.hpp"
#include "value_identity.hpp"
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
std::string fname(Archive &a, const Package &p, detail::ValueIdentityJournal *identities = nullptr) {
    auto index = a.get<std::int32_t>(), number = a.get<std::int32_t>();
    if (index < 0 || static_cast<std::size_t>(index) >= p.names.size() || number < 0)
        throw Error("name-index", "Invalid FName index", a.tell() - 8);
    if (identities)
        identities->name(p.names[index], static_cast<std::uint32_t>(number));
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
using Type = TaggedType;
Json type_json(const Type &t) { return tagged_type_json(t, false); }
bool read_limit_failed(const Error &error) {
    return error.code == "limit" || error.code == "decoder-limit" || error.code == "layout-limit" ||
           error.code == "text-limit";
}
struct Tag {
    std::string name, guid;
    Type type;
    std::uint32_t bytes = 0, index = 0;
    std::uint8_t flags = 0;
    bool boolean = false;
    Json extensions;
};
class PropertyReader {
    Archive &a;
    const Package &p;
    const ObjectExport &object;
    const ReadOptions &options;
    detail::ValueIdentityJournal &identities_;
    std::uint32_t count_ = 0, type_nodes_ = 0, text_nodes_ = 0, native_nodes_ = 0;
    std::uint64_t text_blob_bytes_ = 0, extension_nodes_ = 0, extension_text_ = 0;
    std::uint64_t identity_text_ = 0, key_text_ = 0;
    static bool struct_type(const Type &type) {
        if (type.name == "StructProperty")
            return true;
        if (type.name == "ArrayProperty" || type.name == "MapProperty" || type.name == "SetProperty" ||
            type.name == "OptionalProperty")
            for (const auto &part : type.parameters)
                if (struct_type(part))
                    return true;
        return false;
    }
    void prepare_type(Type &type) const {
        if (type.name == "StructProperty" && !type.parameters.empty()) {
            const auto identity = detail::struct_identity(type);
            auto expected = detail::builtin_struct_owner(identity.name);
            if (expected.empty() && detail::is_material_input(identity.name))
                expected = "/Script/Engine." + identity.name;
            if (!expected.empty() && !identity.custom_guid() &&
                (identity.owner.empty() || identity.owner == expected)) {
                const auto mode = options.decoders && options.decoders->tagged_types()
                                      ? options.decoders->tagged_types()->serialization(
                                            identity.owner.empty() ? identity.name : identity.owner,
                                            identity.guid, p.summary)
                                      : TaggedSerialization::unspecified;
                if (mode != TaggedSerialization::tagged)
                    type.schema = expected;
            }
        } else if (type.name == "ArrayProperty" || type.name == "MapProperty" || type.name == "SetProperty" ||
                   type.name == "OptionalProperty")
            for (auto &part : type.parameters)
                prepare_type(part);
    }
    void format(Json *formats, const std::string &path, const char *kind) const {
        if (!formats)
            return;
        auto i = formats->find(path);
        if (i != formats->end() && *i != kind)
            throw Error("reader-format", "Struct readers disagree within one type path", a.tell());
        if (i == formats->end())
            (*formats)[path] = kind;
    }
    void charge_identity(std::size_t bytes) {
        if (bytes > options.max_property_bytes - identity_text_)
            throw Error("text-limit", "Stored type identity text exceeds its source budget", a.tell());
        identity_text_ += bytes;
    }
    std::string identity_name() {
        auto name = fname(a, p);
        charge_identity(name.size());
        return name;
    }
    void validate_struct_types(const Type &type) const {
        if (type.name == "StructProperty") {
            if (!type.parameters.empty() || p.summary.ue5_version >= 1012)
                (void)detail::struct_identity(type);
        } else if (type.name == "ArrayProperty" || type.name == "SetProperty" || type.name == "MapProperty" ||
                   type.name == "OptionalProperty")
            for (const auto &part : type.parameters)
                validate_struct_types(part);
    }
    std::uint32_t opaque_properties_ = 0;
    void charge_extension(std::uint32_t values, std::uint64_t bytes) {
        options.control.check();
        if (extension_nodes_ > options.max_properties || count_ > options.max_properties - extension_nodes_ ||
            values > options.max_properties - extension_nodes_ - count_ ||
            extension_text_ > options.max_property_bytes ||
            bytes > options.max_property_bytes - extension_text_)
            throw Error("decoder-limit", "Native values exceed enclosing property work/text budget",
                        a.tell());
        extension_nodes_ += values;
        extension_text_ += bytes;
    }
    void depth_check(unsigned depth) const {
        if (depth > options.max_depth)
            throw Error("limit", "Property nesting exceeds limit", a.tell());
    }
    Type full_type(unsigned depth) {
        depth_check(depth);
        if (++type_nodes_ > options.max_properties)
            throw Error("limit", "Type nodes exceed limit", a.tell());
        Type t;
        t.name = identity_name();
        auto n = a.count(options.max_properties);
        for (std::uint32_t i = 0; i < n; ++i)
            t.parameters.push_back(full_type(depth + 1));
        if (t.name == "StructProperty" && !t.parameters.empty()) {
            t.schema = detail::struct_symbol_owner(t.parameters[0], false);
            if (t.parameters.size() == 1)
                t.struct_guid = detail::zero_struct_guid();
            else if (t.parameters.size() == 2)
                t.struct_guid = detail::normalized_struct_guid(t.parameters[1].name);
        }
        return t;
    }
    Json extensions() {
        auto flags = a.get<std::uint8_t>();
        if (flags & ~2u)
            throw Error("property-extension", "Unknown property extension", a.tell() - 1);
        if (flags & 2u) {
            auto operation = a.get<std::uint8_t>();
            auto experimental = a.get<std::uint32_t>();
            if (experimental > 1)
                throw Error("property-extension", "Invalid overridable serialization flag", a.tell() - 4);
            return Json{{"operation", operation}, {"experimentalOverridable", experimental != 0}};
        }
        return nullptr;
    }
    Tag tag() {
        Tag t;
        t.name = identity_name();
        if (t.name == "None")
            return t;
        if (++count_ > options.max_properties || extension_nodes_ > options.max_properties - count_)
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
                t.extensions = extensions();
            t.boolean = (t.flags & 0x10) != 0;
        } else {
            t.type.name = identity_name();
            t.bytes = a.count(
                static_cast<std::uint32_t>(std::min<std::uint64_t>(options.max_property_bytes, UINT32_MAX)));
            t.index = a.count();
            auto add = [&]() { t.type.parameters.push_back({identity_name(), {}}); };
            if (t.type.name == "StructProperty") {
                add();
                t.type.struct_guid = a.guid();
                charge_identity(t.type.struct_guid.size());
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
                t.extensions = extensions();
        }
        return t;
    }
    Json number(double value) {
        if (std::isfinite(value))
            return value;
        return {{"nonFinite", std::isnan(value) ? "NaN" : (value > 0 ? "Infinity" : "-Infinity")}};
    }
    Json value(const Type &t, std::uint64_t end, unsigned depth, bool exact_size = true,
               Type *observed = nullptr, Json *wire_types = nullptr, const std::string &type_path = {},
               Json *read_formats = nullptr) {
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
            return fname(a, p, &identities_);
        if (n == "ByteProperty") {
            if (t.parameters.empty() || t.parameters[0].name == "None")
                return a.get<std::uint8_t>();
            return fname(a, p, &identities_);
        }
        if (n == "Utf8StrProperty" || n == "AnsiStrProperty") {
            if (p.summary.ue5_version < 1013)
                throw Error("string-layout", "Byte string requires a checked UE 5.5 or later layout",
                            a.tell());
            return a.byte_string(n == "AnsiStrProperty");
        }
        if (n == "StrProperty")
            return a.string();
        if (n == "TextProperty")
            return detail::read_archive_text(a, p.summary, options, depth, text_nodes_, text_blob_bytes_,
                                             [&] { return fname(a, p); });
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
            const auto identity = detail::struct_identity(t);
            const auto &s = identity.name;
            auto serialization = TaggedSerialization::unspecified;
            const auto owner = identity.owner.empty() ? s : identity.owner;
            if (options.decoders && options.decoders->tagged_types())
                serialization =
                    options.decoders->tagged_types()->serialization(owner, identity.guid, p.summary);
            const AssetDecoder *selected_decoder = nullptr;
            if (options.decoders) {
                selected_decoder =
                    options.decoders->find(DecoderKind::struct_value, s, p.summary, identity.guid);
                if (!identity.owner.empty() && identity.owner != s) {
                    const auto *qualified = options.decoders->find(DecoderKind::struct_value, identity.owner,
                                                                   p.summary, identity.guid);
                    if (qualified && selected_decoder && qualified != selected_decoder)
                        throw Error("decoder-ambiguous", "Both qualified and short struct decoders match",
                                    a.tell());
                    if (qualified)
                        selected_decoder = qualified;
                }
            }
            if (const auto *decoder = selected_decoder) {
                format(read_formats, type_path, "extension");
                const auto begin = a.tell();
                const auto opaque_before = opaque_properties_;
                Json prefix;
                if (decoder->prefix == DecoderPrefix::tagged_properties) {
                    detail::ValueIdentityJournal::Scope hybrid_data(identities_, "data"),
                        hybrid_properties(identities_, "properties");
                    prefix = properties(end, depth + 1, false, t.schema.empty() ? s : t.schema);
                    if (opaque_properties_ != opaque_before)
                        throw Error("hybrid-prefix", "Tagged prefix contains unavailable properties", begin);
                    detail::validate_extension_value(
                        prefix, options,
                        [&](std::uint32_t values, std::uint64_t bytes) { charge_extension(values, bytes); },
                        depth);
                }
                auto result = detail::decode_extension(
                    a, p, object, options, *decoder, end, exact_size,
                    [&](std::uint32_t values, std::uint64_t bytes) { charge_extension(values, bytes); },
                    depth, a.tell() != begin,
                    decoder->prefix == DecoderPrefix::tagged_properties ? &prefix : nullptr, &t);
                if (decoder->prefix == DecoderPrefix::tagged_properties) {
                    result["prefix"] = "tagged-properties";
                    result["data"] = {{"properties", std::move(prefix)},
                                      {"native", std::move(result["data"])}};
                }
                return result;
            }
            auto builtin_owner = detail::builtin_struct_owner(s);
            if (builtin_owner.empty() && detail::is_material_input(s))
                builtin_owner = "/Script/Engine." + s;
            const bool builtin_allowed = !identity.custom_guid() &&
                                         serialization != TaggedSerialization::tagged &&
                                         (identity.owner.empty() || identity.owner == builtin_owner);
            if (builtin_allowed) {
                const bool reflected_builtin =
                    (s == "Transform" && p.summary.ue5_version >= 1004) ||
                    (s == "RawAnimSequenceTrack" &&
                     p.summary.custom_versions.value("d89b5e4224bd4d468412aca8df641779", -1) < 39) ||
                    (detail::is_material_input(s) && !detail::native_material_input(p.summary));
                if (!builtin_owner.empty() && !reflected_builtin)
                    format(read_formats, type_path, "builtin");
                if (s == "RichCurveKey") {
                    if (p.summary.ue4_version != 522 || p.summary.licensee_version ||
                        (p.summary.ue5_version != 0 && p.summary.ue5_version != 1004 &&
                         p.summary.ue5_version != 1013))
                        throw Error("curve-layout", "Unverified rich curve key profile", a.tell());
                    if (exact_size && (a.tell() > end || end - a.tell() != 27))
                        throw Error("curve-layout", "Rich curve key must contain exactly 27 bytes", a.tell());
                    if (native_nodes_ > options.max_properties || options.max_properties - native_nodes_ < 9)
                        throw Error("limit", "Rich curve key work budget exceeded", a.tell());
                    native_nodes_ += 9;
                    Json out = {{"kind", "rich-curve-key"}};
                    for (const char *field : {"interpMode", "tangentMode", "tangentWeightMode"}) {
                        options.control.checkpoint("Reading rich curve keys", native_nodes_,
                                                   options.max_properties);
                        out[field] = a.get<std::uint8_t>();
                    }
                    for (const char *field : {"time", "value", "arriveTangent", "arriveTangentWeight",
                                              "leaveTangent", "leaveTangentWeight"}) {
                        options.control.check();
                        const auto number = a.get<float>();
                        if (!std::isfinite(number))
                            throw Error("curve-value", "Non-finite rich curve key", a.tell());
                        out[field] = number;
                    }
                    return out;
                }
                if (s == "RawAnimSequenceTrack") {
                    const auto version =
                        p.summary.custom_versions.value("d89b5e4224bd4d468412aca8df641779", -1);
                    if (version >= 39) {
                        if (version > 51 || p.summary.ue4_version != 522 ||
                            (p.summary.ue5_version != 1004 && p.summary.ue5_version != 1013) ||
                            p.summary.licensee_version)
                            throw Error("animation-layout", "Unverified raw animation key profile", a.tell());
                        Json out = {{"kind", "raw-animation-keys"}};
                        for (const auto &channel :
                             {std::make_pair("positions", 3u), std::make_pair("rotations", 4u),
                              std::make_pair("scales", 3u)}) {
                            options.control.check();
                            if (a.get<std::int32_t>() != static_cast<int>(channel.second * 4))
                                throw Error("animation-layout", "Raw animation key element size mismatch",
                                            a.tell());
                            const auto count = a.count(options.max_properties);
                            if (native_nodes_ > options.max_properties ||
                                count > (options.max_properties - native_nodes_) / channel.second)
                                throw Error("limit", "Raw animation key aggregate budget exceeded", a.tell());
                            Json keys = Json::array();
                            for (std::uint32_t key = 0; key < count; ++key) {
                                options.control.checkpoint("Reading raw animation keys", key, count);
                                Json components = Json::array();
                                for (unsigned c = 0; c < channel.second; ++c) {
                                    options.control.check();
                                    ++native_nodes_;
                                    const auto component = a.get<float>();
                                    if (!std::isfinite(component))
                                        throw Error("animation-value", "Non-finite raw animation key",
                                                    a.tell());
                                    components.push_back(component);
                                }
                                keys.push_back(std::move(components));
                            }
                            out[channel.first] = std::move(keys);
                        }
                        return out;
                    }
                    // Versions before 39 use ordinary tagged property serialization.
                }
                if (s == "Guid")
                    return a.guid();
                if (s == "DateTime" || s == "Timespan")
                    return Json{{"ticks", a.get<std::int64_t>()}};
                const bool box = s == "Box" || s == "Box3f" || s == "Box3d";
                const bool sphere = s == "Sphere" || s == "Sphere3f" || s == "Sphere3d";
                const bool matrix = s == "Matrix" || s == "Matrix44f" || s == "Matrix44d";
                const bool plane = s == "Plane" || s == "Plane4f" || s == "Plane4d";
                if (box || sphere || matrix || plane) {
                    const bool narrow = s == "Box3f" || s == "Sphere3f" || s == "Matrix44f" || s == "Plane4f";
                    const bool doubles = !narrow && p.summary.ue5_version >= 1004;
                    // Sphere is immutable binary reflection; combined bounds are ordinary reflected fields.
                    if (s == "Sphere3d" && p.summary.ue5_version < 1004)
                        throw Error("native-layout", "Pre-LWC explicit double sphere is not certified",
                                    a.tell());
                    const std::uint64_t expected = (matrix ? 16
                                                    : box  ? 6
                                                           : 4) *
                                                       (doubles ? 8ull : 4ull) +
                                                   (box ? 1 : 0);
                    if (exact_size && (a.tell() > end || end - a.tell() != expected))
                        throw Error("native-layout", "Native geometry property has an unexpected wire size",
                                    a.tell());
                    auto component = [&]() {
                        depth_check(depth + 1);
                        options.control.check();
                        if (++native_nodes_ > options.max_properties)
                            throw Error("limit", "Native struct work exceeds configured limit", a.tell());
                        const double value = doubles ? a.get<double>() : a.get<float>();
                        if (!std::isfinite(value))
                            throw Error("native-value", "Non-finite native geometry component", a.tell());
                        return Json(value);
                    };
                    auto vector = [&]() {
                        Json result = Json::array();
                        for (unsigned i = 0; i < 3; ++i)
                            result.push_back(component());
                        return result;
                    };
                    if (matrix) {
                        Json rows = Json::array();
                        for (unsigned row = 0; row < 4; ++row) {
                            Json columns = Json::array();
                            for (unsigned column = 0; column < 4; ++column)
                                columns.push_back(component());
                            rows.push_back(std::move(columns));
                        }
                        return rows;
                    }
                    if (plane) {
                        auto normal = vector(), distance = component();
                        return {{"normal", std::move(normal)}, {"distance", std::move(distance)}};
                    }
                    if (sphere) {
                        auto center = vector(), radius = component();
                        return {{"center", std::move(center)}, {"radius", std::move(radius)}};
                    }
                    if (box) {
                        auto minimum = vector(), maximum = vector();
                        if (++native_nodes_ > options.max_properties)
                            throw Error("limit", "Native struct work exceeds configured limit", a.tell());
                        const auto validity = a.get<std::uint8_t>();
                        return {
                            {"min", std::move(minimum)}, {"max", std::move(maximum)}, {"isValid", validity}};
                    }
                }
                if (s == "Transform3f" || s == "Transform3d" ||
                    (s == "Transform" && p.summary.ue5_version < 1004)) {
                    const bool wide =
                        s == "Transform3d" || (s != "Transform3f" && p.summary.ue5_version >= 1004);
                    if ((s == "Transform3f" || s == "Transform3d") && p.summary.ue5_version < 1004)
                        throw Error("native-layout", "Explicit transform variants require an LWC profile",
                                    a.tell());
                    if (exact_size && (a.tell() > end || end - a.tell() != (wide ? 80u : 40u)))
                        throw Error("native-layout", "Immutable transform byte size disagrees with profile",
                                    a.tell());
                    if (native_nodes_ > options.max_properties || options.max_properties - native_nodes_ < 10)
                        throw Error("limit", "Native transform work budget exceeded", a.tell());
                    native_nodes_ += 10;
                    auto vector = [&](unsigned count) {
                        Json values = Json::array();
                        for (unsigned i = 0; i < count; ++i)
                            values.push_back(number(wide ? a.get<double>() : a.get<float>()));
                        return values;
                    };
                    Json result;
                    result["rotation"] = vector(4);
                    result["translation"] = vector(3);
                    result["scale"] = vector(3);
                    return result;
                }
                if (detail::is_material_input(s)) {
                    if (!options.native_materials)
                        throw Error("material-disabled", "Native material inputs disabled", a.tell());
                    if (detail::native_material_input(p.summary))
                        return detail::decode_material_input(a, p, s, end);
                }
                unsigned components = 0;
                bool doubles = false, integers = false;
                if (s == "Vector" || s == "Rotator" || s == "Vector3f" || s == "Vector3d") {
                    components = 3;
                    doubles = s != "Vector3f" && p.summary.ue5_version >= 1004;
                } else if (s == "Vector2D") {
                    components = 2;
                    doubles = p.summary.ue5_version >= 1004;
                } else if (s == "Quat" || s == "Quat4f" || s == "Vector4") {
                    components = 4;
                    doubles = s != "Quat4f" && p.summary.ue5_version >= 1004;
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
            }
            if (serialization == TaggedSerialization::native)
                throw Error("tagged-types-native", "Native struct needs an explicit serializer binding",
                            a.tell());
            auto reflected = properties(end, depth + 1, false, owner);
            if (reflected.empty() && serialization != TaggedSerialization::tagged)
                throw Error("struct-serialization-ambiguous",
                            "Empty struct bytes need a tagged serialization certificate or native binding",
                            a.tell());
            format(read_formats, type_path, "tagged");
            return reflected;
        }
        if ((n == "MapProperty" && t.parameters.size() == 2) ||
            (n == "SetProperty" && t.parameters.size() == 1)) {
            const bool map = n == "MapProperty";
            // These are stored deltas; inherited/default values are deliberately not fabricated.
            Json removed = Json::object(), entries = Json::object();
            auto key_charge = [&](std::uint64_t bytes) {
                if (bytes > options.max_property_bytes - key_text_)
                    throw Error("text-limit", "Encoded source key identities exceed their text budget",
                                a.tell());
                key_text_ += bytes;
            };
            ValueIdentityOptions key_options;
            key_options.max_names = options.max_properties;
            key_options.max_text_bytes = options.max_property_bytes;
            key_options.charge = [&](std::uint32_t, std::uint64_t bytes) { key_charge(bytes); };
            auto key_identity = [&](Json &target, const Json &key,
                                    detail::ValueIdentityJournal::KeyProbe &probe) {
                if (!probe.complete())
                    throw Error("limit", "Functional key name capture exceeded its budget", a.tell());
                auto names = probe.names();
                auto identity = detail::named_key_identity(
                    key, names,
                    [&](const Json &v, bool raw) {
                        return detail::source_key_identity(v, options.control, key_charge, !raw);
                    },
                    key_charge);
                probe.publish();
                if (target.contains(identity))
                    throw Error("container-identity", "Duplicate map/set key", a.tell());
                return identity;
            };
            auto removed_count = a.count(options.max_properties);
            for (std::uint32_t i = 0; i < removed_count; ++i) {
                detail::ValueIdentityJournal::Buffer key_names(identities_);
                detail::ValueIdentityJournal::KeyProbe probe(identities_, key_options);
                auto key = value(t.parameters[0], end, depth + 1, false,
                                 observed ? &observed->parameters[0] : nullptr, wire_types,
                                 type_path + "/key", read_formats);
                auto id = key_identity(removed, key, probe);
                key_names.commit_at({"removed", id});
                removed[id] = std::move(key);
            }
            auto count = a.count(options.max_properties);
            if (count > options.max_properties - removed_count)
                throw Error("limit", "Map/set entry budget exceeded", a.tell());
            for (std::uint32_t i = 0; i < count; ++i) {
                detail::ValueIdentityJournal::Buffer key_names(identities_);
                detail::ValueIdentityJournal::KeyProbe probe(identities_, key_options);
                auto key = value(t.parameters[0], end, depth + 1, false,
                                 observed ? &observed->parameters[0] : nullptr, wire_types,
                                 type_path + "/key", read_formats);
                auto id = key_identity(entries, key, probe);
                if (map) {
                    key_names.commit_at({"entries", id, "key"});
                    detail::ValueIdentityJournal::Scope list(identities_, "entries"), item(identities_, id),
                        mapped(identities_, "value");
                    entries[id] = {{"key", std::move(key)},
                                   {"value", value(t.parameters[1], end, depth + 1, false,
                                                   observed ? &observed->parameters[1] : nullptr, wire_types,
                                                   type_path + "/value", read_formats)}};
                } else {
                    key_names.commit_at({"entries", id});
                    entries[id] = std::move(key);
                }
            }
            return Json{{"kind", map ? "stored-map-delta" : "stored-set-delta"},
                        {"removed", std::move(removed)},
                        {"entries", std::move(entries)}};
        }
        if (n == "ArrayProperty" && !t.parameters.empty()) {
            auto count = a.count(options.max_properties);
            Json values = Json::array();
            auto inner = t.parameters[0];
            if (inner.name == "StructProperty" && p.summary.ue5_version < 1012) {
                auto element = tag();
                if (element.type.name != "StructProperty" || element.type.parameters.empty())
                    throw Error("array-tag", "Invalid struct array element tag", a.tell());
                if (wire_types) {
                    const auto key = type_path + "/*";
                    const auto raw = type_json(element.type);
                    if (wire_types->contains(key) && (*wire_types)[key] != raw)
                        throw Error("array-tag-identity", "Nested array struct identities disagree",
                                    a.tell());
                    (*wire_types)[key] = raw;
                }
                inner = inner.schema.empty() ? std::move(element.type)
                                             : TaggedTypes::complete(element.type, inner);
            }
            prepare_type(inner);
            // Legacy array tags omit the enum type. Exact size distinguishes FNames from raw bytes.
            if (observed)
                observed->parameters[0] = inner;
            bool enum_names = inner.name == "ByteProperty" && inner.parameters.empty() && count &&
                              p.summary.ue5_version < 1012 && end - a.tell() == std::uint64_t(count) * 8;
            for (std::uint32_t i = 0; i < count; ++i) {
                detail::ValueIdentityJournal::Scope item_identity(identities_, static_cast<std::size_t>(i));
                if (a.tell() >= end)
                    throw Error("bounds", "Array exceeds property payload", a.tell());
                values.push_back(enum_names ? Json(fname(a, p, &identities_))
                                            : value(inner, end, depth + 1, false,
                                                    observed ? &observed->parameters[0] : nullptr, wire_types,
                                                    type_path + "/*", read_formats));
            }
            return values;
        }
        throw Error("opaque", "No decoder for " + n, a.tell());
    }

  public:
    PropertyReader(Archive &ar, const Package &package, const ObjectExport &entry, const ReadOptions &o,
                   detail::ValueIdentityJournal &identities)
        : a(ar), p(package), object(entry), options(o), identities_(identities) {}
    bool partial = false, limit_failed = false;
    Json properties(std::uint64_t end, unsigned depth = 0, bool root = true, const std::string &owner = {}) {
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
            const auto key = t.name + (t.index ? "[" + std::to_string(t.index) + "]" : "");
            detail::ValueIdentityJournal::Buffer property_names(identities_);
            detail::ValueIdentityJournal::Scope property_identity(identities_, key),
                value_identity(identities_, "value");
            Json entry = {{"type", type_json(t.type)}};
            if (!t.guid.empty())
                entry["guid"] = t.guid;
            if (t.flags & 0x28)
                entry["serializationFlags"] = t.flags;
            if (!t.extensions.is_null())
                entry["extensions"] = t.extensions;
            Json wire_types = Json::object();
            try {
                Archive::ReadLimit property_limit(a, next);
                validate_struct_types(t.type);
                auto effective = t.type;
                if (options.decoders && options.decoders->tagged_types()) {
                    const auto &hints = *options.decoders->tagged_types();
                    if (const auto *declared =
                            hints.property(root ? object.class_name : owner, t.name, t.index, p.summary)) {
                        effective = TaggedTypes::complete(t.type, *declared);
                        if (tagged_type_json(effective) != tagged_type_json(t.type))
                            entry["resolvedType"] = tagged_type_json(effective);
                    }
                }
                prepare_type(effective);
                if (tagged_type_json(effective) != tagged_type_json(t.type))
                    entry["resolvedType"] = tagged_type_json(effective);
                if (t.extensions.is_object() && t.extensions.value("experimentalOverridable", false))
                    throw Error("property-extension",
                                "Experimental overridable values require a schema decoder", a.tell());
                if (t.type.name == "BoolProperty")
                    entry["value"] = t.boolean;
                else if (t.flags & 0x20)
                    entry["value"] = nullptr;
                else {
                    auto observed = effective;
                    Json read_formats = Json::object();
                    entry["value"] =
                        value(effective, next, depth + 1, true, &observed, &wire_types, "", &read_formats);
                    if (struct_type(effective))
                        entry["readFormats"] = std::move(read_formats);
                    if (tagged_type_json(observed) != tagged_type_json(t.type))
                        entry["resolvedType"] = tagged_type_json(observed);
                }
                if (a.tell() != next)
                    throw Error("opaque", "Property decoder did not consume exact payload", a.tell());
                property_names.commit();
            } catch (const Error &e) {
                if (e.code == "cancelled")
                    throw;
                limit_failed = limit_failed || read_limit_failed(e);
                partial = true;
                ++opaque_properties_;
                entry.erase("value");
                entry.erase("readFormats");
                entry["opaque"] = {{"bytes", t.bytes}, {"reason", e.code}};
                if (options.fingerprints)
                    entry["opaque"]["sha256"] = a.hash(start, t.bytes);
            }
            if (!wire_types.empty())
                entry["wireTypes"] = std::move(wire_types);
            a.seek(next);
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
    if (options.decoders) {
        p.decoder_context_sha256 = options.decoders->context_sha256();
        p.decoder_definitions = options.decoders->definitions(p.summary, options.control);
    }
    if (options.decoders && options.decoders->tagged_types()) {
        const auto &types = *options.decoders->tagged_types();
        p.source_type_context = {{"sha256", types.sha256()},
                                 {"profileMatched", types.matches(p.summary)},
                                 {"defaultsResolved", false}};
    }
    const ReadOptions defaults;
    if (options.max_properties != defaults.max_properties ||
        options.max_property_bytes != defaults.max_property_bytes)
        p.source_read_limits = {{"maxProperties", options.max_properties},
                                {"maxPropertyBytes", options.max_property_bytes}};
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
    detail::ValueIdentityJournal value_identities(options.value_identities, options.control);
    for (auto &entry : p.exports) {
        options.control.checkpoint("Reading exports", static_cast<std::uint64_t>(&entry - p.exports.data()),
                                   p.exports.size());
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
            p.source_read_limit_failed = true;
            entry.coverage = "opaque";
            entry.opaque["export"] = opaque_range(*data, base, entry.size, options);
            p.diagnostics.push_back(entry.path + ": script stream exceeds property byte limit");
            continue;
        }
        try {
            data->seek(start);
            detail::ValueIdentityJournal::Scope identity_objects(value_identities, "objects"),
                identity_object(value_identities, entry.path),
                identity_properties(value_identities, "properties");
            detail::ValueIdentityJournal::Buffer object_names(value_identities);
            PropertyReader reader(*data, p, entry, options, value_identities);
            entry.properties = reader.properties(end);
            p.source_read_limit_failed = p.source_read_limit_failed || reader.limit_failed;
            if (entry.script_end && data->tell() != end)
                throw Error("script-range", "Property terminator does not match script range", data->tell());
            object_names.commit();
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
                    if (error.code == "cancelled")
                        throw;
                    p.source_read_limit_failed = p.source_read_limit_failed || read_limit_failed(error);
                    extension_native = true;
                    entry.native_coverage = "opaque";
                    p.diagnostics.push_back(entry.path + ": native " + error.code + ": " + error.what());
                    data->seek(consumed);
                }
            }
            if (!extension_native && options.native_reflection && detail::is_reflected_struct(entry)) {
                try {
                    entry.native_start = consumed - base;
                    auto decoded = detail::decode_struct_definition(*data, p, base + entry.size, options);
                    entry.native_data = std::move(decoded.data);
                    entry.native_coverage = "struct-declaration-prefix";
                    entry.native_end = decoded.end - base;
                    consumed = decoded.end;
                } catch (const Error &error) {
                    if (error.code == "cancelled")
                        throw;
                    p.source_read_limit_failed = p.source_read_limit_failed || read_limit_failed(error);
                    entry.native_coverage = "opaque";
                    p.diagnostics.push_back(entry.path + ": " + error.code + ": " + error.what());
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
                    if (error.code == "cancelled")
                        throw;
                    p.source_read_limit_failed = p.source_read_limit_failed || read_limit_failed(error);
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
                    if (error.code == "cancelled")
                        throw;
                    p.source_read_limit_failed = p.source_read_limit_failed || read_limit_failed(error);
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
                    if (error.code == "cancelled")
                        throw;
                    p.source_read_limit_failed = p.source_read_limit_failed || read_limit_failed(error);
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
            if (!extension_native && options.native_skeletons &&
                (entry.class_name == "/Script/Engine.Skeleton" ||
                 entry.class_name == "/Script/Engine.SkeletalMesh")) {
                try {
                    entry.native_start = consumed - base;
                    auto decoded =
                        detail::decode_reference_skeleton(*data, p, base + entry.size, options,
                                                          entry.class_name == "/Script/Engine.SkeletalMesh");
                    entry.native_data = std::move(decoded.data);
                    entry.native_coverage = "reference-skeleton";
                    entry.native_end = decoded.end - base;
                    consumed = decoded.end;
                } catch (const Error &error) {
                    if (error.code == "cancelled")
                        throw;
                    p.source_read_limit_failed = p.source_read_limit_failed || read_limit_failed(error);
                    entry.native_coverage = "opaque";
                    p.diagnostics.push_back(entry.path + ": " + error.code + ": " + error.what());
                    data->seek(consumed);
                }
            }
            if (consumed < base + entry.size)
                entry.opaque["suffix"] = opaque_range(*data, consumed, base + entry.size - consumed, options);
        } catch (const Error &e) {
            if (e.code == "cancelled")
                throw;
            p.source_read_limit_failed = p.source_read_limit_failed || read_limit_failed(e);
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
                                             {"sha256", sha256_file(companion, options.control)}};
        }
    }
    if (s.flags & unversioned_properties)
        p.diagnostics.push_back(
            "Unversioned properties require a schema backend; export bytes are retained as opaque hashes.");
    p.value_identities = value_identities.finish();
    if (!p.value_identities.is_null())
        p.value_identities["root"] = "semantic-snapshot";
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
    Json result = {{"schema", "cyuediff.package/1"},
                   {"source", source.u8string()},
                   {"summary", summary.to_json()},
                   {"snapshot", semantic_snapshot()},
                   {"blueprint", blueprint_snapshot()},
                   {"textures", texture_snapshot()},
                   {"meshes", mesh_snapshot()},
                   {"materials", material_snapshot()},
                   {"collisions", collision_snapshot()},
                   {"skeletons", skeleton_snapshot()},
                   {"reflection", reflection_definitions(*this)},
                   {"coverage", coverage},
                   {"fingerprints", fingerprints},
                   {"completeInput", complete_input},
                   {"diagnostics", diagnostics}};
    if (!decoder_context_sha256.empty())
        result["decoderContextSha256"] = decoder_context_sha256;
    if (!decoder_definitions.empty())
        result["decoderDefinitions"] = decoder_definitions;
    if (source_read_limit_failed)
        result["sourceReadLimitFailed"] = true;
    if (!source_read_limits.is_null())
        result["sourceReadLimits"] = source_read_limits;
    if (!source_type_context.is_null())
        result["sourceTypeContext"] = source_type_context;
    if (!value_identities.is_null())
        result["valueIdentities"] = value_identities;
    return result;
}
} // namespace cyuediff
