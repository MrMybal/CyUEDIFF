#include <cyuediff/cooked_animation.hpp>
#include <cyuediff/mesh_skin.hpp>
#include <cyuediff/string_encoding.hpp>
#include "animation_transform.hpp"
#include "archive.hpp"
#include "zen_bulk.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <limits>
#include <string_view>
#include <utility>
namespace cyuediff {
namespace {
void need(bool b, const char *s, std::uint64_t pos = 0) {
    if (!b)
        throw Error("cooked-animation-layout", s, pos);
}
void context(bool b, const char *s) {
    if (!b)
        throw Error("cooked-animation-context", s);
}
void supported(bool b, const char *s) {
    if (!b)
        throw Error("cooked-animation-profile", s);
}
void limit(bool b, const char *s) {
    if (!b)
        throw Error("cooked-animation-limit", s);
}
struct Budget {
    const CookedAnimationOptions &o;
    std::uint64_t read = 0, output = 0;
    void reserve(std::uint64_t n) {
        limit(output <= o.max_output_bytes && n <= o.max_output_bytes - output,
              "Cooked animation exceeds output byte budget");
        output += n;
    }
};
void charge_properties(const Json &root, Budget &b) {
    std::vector<const Json *> pending;
    auto add = [&](const Json &value) {
        b.reserve(64);
        pending.push_back(&value);
    };
    add(root);
    while (!pending.empty()) {
        b.o.control.check();
        const auto &value = *pending.back();
        pending.pop_back();
        if (value.is_string())
            b.reserve(value.get_ref<const std::string &>().size());
        else if (value.is_object())
            for (auto it = value.begin(); it != value.end(); ++it) {
                b.reserve(160 + it.key().size());
                add(*it);
            }
        else if (value.is_array())
            for (const auto &item : value)
                add(item);
    }
}
struct Cursor {
    const std::vector<std::uint8_t> &bytes;
    const CookedAnimationOptions &o;
    std::uint64_t pos, end;
    void range(std::uint64_t n) const {
        need(pos <= end && n <= end - pos, "Animation field exceeds export window", pos);
    }
    std::uint64_t number(unsigned n) {
        range(n);
        std::uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v |= std::uint64_t(bytes[std::size_t(pos++)]) << (8 * i);
        return v;
    }
    std::uint32_t u32() {
        return static_cast<std::uint32_t>(number(4));
    }
    std::int32_t i32() {
        auto v = u32();
        std::int32_t s;
        std::memcpy(&s, &v, 4);
        return s;
    }
    bool boolean() {
        auto v = u32();
        need(v <= 1, "Invalid animation bool32", pos - 4);
        return v != 0;
    }
    std::uint32_t count(std::uint32_t max) {
        auto n = i32();
        need(n >= 0, "Negative animation array count", pos - 4);
        limit(std::uint32_t(n) <= max, "Animation array exceeds element budget");
        return std::uint32_t(n);
    }
    double f64() {
        auto v = number(8);
        double d;
        std::memcpy(&d, &v, 8);
        need(std::isfinite(d), "Non-finite Skeleton transform", pos - 8);
        return d;
    }
    void skip(std::uint64_t n) {
        range(n);
        pos += n;
    }
    std::string name(const std::vector<std::string> &names) {
        auto index = u32(), suffix = u32();
        need(index < names.size() && suffix != ~0u, "Invalid animation name index", pos - 8);
        auto s = names[index];
        if (suffix)
            s += "_" + std::to_string(suffix - 1);
        limit(s.size() <= o.max_string_bytes, "Animation name exceeds string budget");
        return s;
    }
    std::string guid() {
        const char *hex = "0123456789abcdef";
        std::string s;
        for (unsigned i = 0; i < 16; ++i) {
            auto v = number(1);
            s += hex[v >> 4];
            s += hex[v & 15];
        }
        return s;
    }
    std::string string() {
        const auto n = i32();
        if (!n)
            return {};
        const auto chars = n < 0 ? std::uint64_t(-std::int64_t(n)) : std::uint64_t(n);
        limit(chars * (n < 0 ? 2 : 1) <= o.max_string_bytes, "Animation FString exceeds byte budget");
        range(chars * (n < 0 ? 2 : 1));
        std::string s;
        if (n > 0) {
            for (std::uint64_t i = 0; i < chars; ++i) {
                auto v = number(1);
                if (i + 1 == chars)
                    need(v == 0, "Unterminated animation FString", pos - 1);
                else {
                    need(v != 0, "Embedded null in animation FString", pos - 1);
                    s += char(v);
                }
            }
            auto encoding = o.narrow_string_encoding;
            if (encoding == NarrowStringEncoding::unspecified)
                encoding = NarrowStringEncoding::ascii;
            return decode_narrow_string(s, encoding, o.max_string_bytes, o.control);
        }
        auto append = [&](std::uint32_t cp) {
            if (cp < 128)
                s += char(cp);
            else if (cp < 2048) {
                s += char(192 | (cp >> 6));
                s += char(128 | (cp & 63));
            } else if (cp < 65536) {
                s += char(224 | (cp >> 12));
                s += char(128 | ((cp >> 6) & 63));
                s += char(128 | (cp & 63));
            } else {
                s += char(240 | (cp >> 18));
                s += char(128 | ((cp >> 12) & 63));
                s += char(128 | ((cp >> 6) & 63));
                s += char(128 | (cp & 63));
            }
        };
        for (std::uint64_t i = 0; i < chars; ++i) {
            o.control.check();
            auto cp = std::uint32_t(number(2));
            if (i + 1 == chars) {
                need(cp == 0, "Unterminated animation UTF16 FString", pos - 2);
                break;
            }
            need(cp != 0, "Embedded null in animation UTF16 FString", pos - 2);
            if (cp >= 0xd800 && cp <= 0xdbff) {
                need(i + 2 < chars, "Truncated animation UTF16 surrogate", pos - 2);
                auto low = number(2);
                ++i;
                need(low >= 0xdc00 && low <= 0xdfff, "Invalid animation UTF16 surrogate", pos - 2);
                cp = 65536 + ((cp - 0xd800) << 10) + std::uint32_t(low - 0xdc00);
            } else
                need(cp < 0xdc00 || cp > 0xdfff, "Invalid animation UTF16 surrogate", pos - 2);
            append(cp);
            limit(s.size() <= o.max_string_bytes, "Decoded animation FString exceeds string budget");
        }
        return s;
    }
};
struct Loaded {
    const Container *container;
    std::size_t entry;
    std::vector<std::uint8_t> bytes;
    ZenPackage package;
};
Loaded load(const Container &c, std::size_t entry, const ZenReadOptions &zen,
            const ContainerReadOptions &read, Budget &b) {
    b.o.control.check();
    supported(zen.profile == ZenProfile::unreal_5_3,
              "Animation dependencies require matching UE 5.3 Zen contexts");
    need(entry < c.entries().size(), "Invalid animation package entry");
    const auto &m = c.entries()[entry];
    need(m.chunk_type == 1 && m.id.size() == 24 && !m.deleted,
         "Animation requires a live Zen export-bundle chunk");
    limit(b.read <= b.o.max_read_bytes && m.size <= b.o.max_read_bytes - b.read,
          "Animation packages exceed aggregate read budget");
    b.read += m.size;
    auto reads = read;
    const auto old = read.control;
    reads.control.cancelled = [a = b.o.control, old] {
        a.check();
        old.check();
        return false;
    };
    reads.control.progress = b.o.control.progress ? b.o.control.progress : old.progress;
    auto bytes = c.read(entry, reads);
    auto z = zen;
    const auto old_z = zen.control;
    z.control.cancelled = [a = b.o.control, old_z] {
        a.check();
        old_z.check();
        return false;
    };
    z.control.progress = reads.control.progress;
    auto package = ZenPackage::parse(bytes, z);
    supported((package.flags & 0x80000200u) == 0x80000200u,
              "Animation requires editor-stripped cooked packages");
    return {&c, entry, std::move(bytes), std::move(package)};
}
std::uint64_t boundary(const ZenExport &e) {
    context(!e.property_read.is_null() && e.property_read.value("status", std::string{}) == "decoded",
            "Animation/Skeleton requires a decoded explicit property binding");
    const auto offset = e.property_read.at("offset").get<std::uint64_t>(),
               consumed = e.property_read.at("consumedBytes").get<std::uint64_t>();
    need(offset <= e.size && consumed <= e.size - offset, "Invalid animation property boundary");
    return e.offset + offset + consumed;
}
std::string hex64(std::uint64_t v) {
    const char *hex = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i) {
        s[std::size_t(i)] = hex[v & 15];
        v >>= 4;
    }
    return s;
}
Json provenance(const Loaded &p, const ZenExport &e, std::size_t index) {
    return {{"package", p.package.name},
            {"packageSha256", p.package.sha256},
            {"chunkId", p.container->entries()[p.entry].id},
            {"scriptObjectsSha256", p.package.script_objects_sha256},
            {"schemaContextSha256", p.package.schema_context_sha256},
            {"object", e.path},
            {"class", e.class_name},
            {"exportIndex", index},
            {"publicExportHash", hex64(e.public_hash)}};
}
const Json *property(const Json &p, const char *name, const char *type) {
    const auto it = p.find(name);
    if (it == p.end())
        return nullptr;
    context(it->is_object() && it->value("type", std::string{}) == type && it->contains("value"),
            "Animation property has an incompatible mapped type");
    return &it->at("value");
}
Json reference(const Json &p, const char *name) {
    const auto *v = property(p, name, "ObjectProperty");
    return v ? *v : Json(nullptr);
}
struct Resolved {
    std::shared_ptr<Loaded> owned;
    const Loaded *package = nullptr;
    std::size_t index = SIZE_MAX;
    const ZenExport &object() const {
        return package->package.exports[index];
    }
};
Resolved resolve(const Loaded &main, const Json &ref, const std::optional<CookedAnimationDependency> &source,
                 const char *expected, Budget &b) {
    if (!ref.is_object())
        return {};
    Resolved result;
    if (ref.value("kind", std::string{}) == "export") {
        result.package = &main;
        for (std::size_t i = 0; i < main.package.exports.size(); ++i)
            if (main.package.exports[i].path == ref.value("path", std::string{})) {
                context(result.index == SIZE_MAX, "Ambiguous animation local dependency");
                result.index = i;
            }
        context(result.index != SIZE_MAX, "Animation local dependency is absent");
    } else if (ref.value("kind", std::string{}) == "package") {
        if (!source)
            return {};
        result.owned =
            std::make_shared<Loaded>(load(source->container, source->entry, source->zen, source->read, b));
        result.package = result.owned.get();
        context(ref.contains("package") && ref.at("package") == result.package->package.name,
                "Animation dependency package name disagrees with stored reference");
        const auto hash = ref.value("publicExportHash", std::string{});
        context(hash.size() == 16 && hash != "0000000000000000",
                "Animation dependency requires a nonzero public export hash");
        for (std::size_t i = 0; i < result.package->package.exports.size(); ++i)
            if (hex64(result.package->package.exports[i].public_hash) == hash) {
                context(result.index == SIZE_MAX, "Ambiguous animation dependency public export hash");
                result.index = i;
            }
        context(result.index != SIZE_MAX, "Animation dependency public export hash was not found");
    } else
        return {};
    context(result.object().class_name == expected && !(result.object().flags & 0x30),
            "Animation dependency is not the expected non-template class");
    return result;
}
struct CurveSelection {
    const ZenExport *object = nullptr;
    std::size_t index = SIZE_MAX;
    CompressedCurveCodec codec = CompressedCurveCodec::unspecified;
    std::string unavailable;
};
CurveSelection curve_codec(const Resolved &settings, Budget &b) {
    const auto &root = settings.object();
    const auto &p = settings.package->package;
    limit(p.exports.size() <= b.o.max_codec_exports, "Curve settings exceed export budget");
    if (root.property_read.is_null() || root.property_read.value("status", std::string{}) != "decoded")
        return {nullptr, SIZE_MAX, CompressedCurveCodec::unspecified,
                "curve-settings-properties-not-decoded"};
    const auto &properties = root.property_read.at("properties");
    charge_properties(properties, b);
    const auto *registered =
        property(properties, "/Script/Engine.AnimCurveCompressionSettings.Codec", "ObjectProperty");
    if (!registered)
        return {nullptr, SIZE_MAX, CompressedCurveCodec::unspecified, "curve-codec-not-stored"};
    context(registered->is_object(), "Registered curve codec is null or invalid");
    if (registered->value("kind", std::string{}) != "export")
        return {nullptr, SIZE_MAX, CompressedCurveCodec::unspecified, "external-curve-codec-registration"};
    const auto path = registered->value("path", std::string{});
    context(!path.empty(), "Registered curve codec has no path");
    std::size_t index = SIZE_MAX;
    for (std::size_t i = 0; i < p.exports.size(); ++i) {
        b.o.control.check();
        if (p.exports[i].path == path) {
            context(index == SIZE_MAX, "Ambiguous registered curve codec path");
            index = i;
        }
    }
    context(index != SIZE_MAX && !(p.exports[index].flags & 0x30),
            "Registered curve codec is absent or a template");
    const auto &selected = p.exports[index];
    CompressedCurveCodec type = CompressedCurveCodec::unspecified;
    if (selected.class_name == "/Script/Engine.AnimCurveCompressionCodec_CompressedRichCurve")
        type = CompressedCurveCodec::rich_curve;
    else if (selected.class_name == "/Script/Engine.AnimCurveCompressionCodec_UniformIndexable")
        type = CompressedCurveCodec::uniform_indexable;
    else if (selected.class_name == "/Script/Engine.AnimCurveCompressionCodec_UniformlySampled")
        type = CompressedCurveCodec::uniformly_sampled;
    if (type == CompressedCurveCodec::unspecified)
        return {&selected, index, type, "unsupported-curve-codec-lookup"};
    // These exact classes inherit the verified base lookup returning the registered codec.
    // The serialized path is retained; it is not a selector for this known lookup behavior.
    return {&selected, index, type, {}};
}
struct CodecSelection {
    const ZenExport *object = nullptr;
    std::size_t index = SIZE_MAX;
    std::string unavailable, canonical_handle;
};
bool same_handle(const std::string &a, const std::string &b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if (x == y)
            continue;
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y)
            return false;
    }
    return true;
}
CodecSelection codec(const Resolved &settings, const std::string &handle, Budget &b) {
    const auto &root = settings.object();
    const auto &p = settings.package->package;
    limit(p.exports.size() <= b.o.max_codec_exports, "Compression settings exceed export budget");
    if (root.property_read.is_null() || root.property_read.value("status", std::string{}) != "decoded")
        return {nullptr, SIZE_MAX, "compression-settings-properties-not-decoded"};
    const auto *registered = property(root.property_read.at("properties"),
                                      "/Script/Engine.AnimBoneCompressionSettings.Codecs", "ArrayProperty");
    if (!registered)
        return {nullptr, SIZE_MAX, "compression-codec-list-not-stored"};
    context(registered->is_array(), "Compression settings Codecs is not an array");
    limit(registered->size() <= b.o.max_codec_exports, "Registered codecs exceed export budget");
    b.reserve(std::uint64_t(p.exports.size()) * 64);
    std::map<std::string_view, std::size_t> paths;
    for (std::size_t i = 0; i < p.exports.size(); ++i) {
        b.o.control.check();
        context(paths.emplace(p.exports[i].path, i).second, "Ambiguous compression export path");
    }
    std::map<std::string, std::size_t> known_handles;
    CodecSelection result;
    for (const auto &ref : *registered) {
        b.o.control.check();
        if (ref.is_null())
            continue;
        if (!ref.is_object() || ref.value("kind", std::string{}) != "export")
            return {nullptr, SIZE_MAX, "external-compression-codec-registration"};
        const auto path = ref.value("path", std::string{});
        const auto found = paths.find(path);
        const auto index = found == paths.end() ? SIZE_MAX : found->second;
        context(index != SIZE_MAX && !(p.exports[index].flags & 0x30),
                "Registered compression codec is absent or a template");
        const auto &selected = p.exports[index];
        // Only exact built-in classes with the verified base GetCodec behavior.
        // A plugin can override lookup as well as decompression; do not emulate it.
        if (selected.class_name != "/Script/Engine.AnimCompress_PerTrackCompression" &&
            selected.class_name != "/Script/Engine.AnimCompress_BitwiseCompressOnly" &&
            selected.class_name != "/Script/Engine.AnimCompress_RemoveLinearKeys")
            return {nullptr, SIZE_MAX, "unsupported-compression-codec-lookup"};
        std::uint64_t current = index;
        std::string name;
        unsigned depth = 0;
        while (current != settings.index && (current >> 62) == 0) {
            need(current < p.exports.size(), "Invalid compression codec outer index");
            limit(depth++ < b.o.max_outer_depth, "Compression codec outer depth exceeds budget");
            const auto &e = p.exports[std::size_t(current)];
            if (e.class_name == "/Script/Engine.AnimBoneCompressionSettings")
                break;
            limit(name.size() <= b.o.max_string_bytes &&
                      e.name.size() + (name.empty() ? 0 : 1) <= b.o.max_string_bytes - name.size(),
                  "Compression codec handle exceeds string budget");
            if (!name.empty())
                name += '.';
            name += e.name;
            current = e.outer_index;
        }
        context(current == settings.index, "Registered compression codec is outside verified settings");
        const auto previous = known_handles.emplace(name, index);
        context(previous.second || previous.first->second == index,
                "Ambiguous registered compression codec handle");
        if (same_handle(name, handle)) {
            result.canonical_handle = name;
            result.object = &selected;
            result.index = index;
            // Engine lookup ends at the first matching registration.
            return result;
        }
    }
    if (handle.empty())
        return {nullptr, SIZE_MAX, "empty-bone-codec"};
    context(false, "Stored compression codec handle is absent from verified registrations");
    return result;
}

std::vector<MeshBone> skeleton(const Resolved &binding, const std::string &expected_guid, Json &out,
                               Budget &b) {
    const auto &p = *binding.package;
    const auto &e = binding.object();
    const auto begin = boundary(e);
    Cursor r{p.bytes, b.o, begin, e.offset + e.size};
    Json object_guid = nullptr;
    if (r.boolean())
        object_guid = r.guid();
    auto count = r.count(b.o.max_bones);
    b.reserve(std::uint64_t(count) * 1536);
    std::vector<MeshBone> bones;
    bones.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        b.o.control.check();
        MeshBone bone;
        bone.id = i;
        bone.name = r.name(p.package.names);
        bone.parent = r.i32();
        b.reserve(bone.name.size());
        bones.push_back(std::move(bone));
    }
    need(r.count(b.o.max_bones) == count, "Skeleton reference pose count disagrees");
    for (auto &bone : bones) {
        b.o.control.check();
        for (auto &v : bone.rotation)
            v = r.f64();
        for (auto &v : bone.translation)
            v = r.f64();
        for (auto &v : bone.scale)
            v = r.f64();
    }
    need(r.count(b.o.max_bones) == count, "Skeleton name-index map count disagrees");
    std::set<std::uint32_t> seen;
    for (unsigned i = 0; i < count; ++i) {
        b.o.control.check();
        auto name = r.name(p.package.names);
        auto index = r.i32();
        need(index >= 0 && std::uint32_t(index) < count && bones[std::size_t(index)].name == name &&
                 seen.insert(std::uint32_t(index)).second,
             "Skeleton name-index map disagrees");
    }
    MeshSkin validation;
    validation.bones = std::move(bones);
    validation.validate(b.o.control);
    bones = std::move(validation.bones);
    const auto sources = r.count(b.o.max_retarget_sources);
    Json retarget = Json::object();
    std::uint64_t total = 0;
    for (unsigned i = 0; i < sources; ++i) {
        b.o.control.check();
        auto key = r.name(p.package.names), pose = r.name(p.package.names);
        need(key != "None" && !retarget.contains(key), "Invalid retarget source identity");
        auto transforms = r.count(b.o.max_bones);
        limit(transforms <= b.o.max_bones - total, "Skeleton retarget poses exceed aggregate bone budget");
        total += transforms;
        b.reserve(std::uint64_t(transforms) * 768 + key.size() + pose.size() + 1024);
        Json values = Json::array();
        for (unsigned j = 0; j < transforms; ++j) {
            b.o.control.check();
            Json t = Json::object();
            for (const auto &field : {std::make_pair("rotation", 4u), std::make_pair("translation", 3u),
                                      std::make_pair("scale", 3u)}) {
                Json v = Json::array();
                for (unsigned k = 0; k < field.second; ++k)
                    v.push_back(r.f64());
                t[field.first] = std::move(v);
            }
            values.push_back(std::move(t));
        }
        retarget[key] = {{"poseName", pose}, {"localPoses", std::move(values)}};
    }
    const auto guid = r.guid();
    context(guid == expected_guid, "Skeleton GUID differs from animation's stored Skeleton GUID");
    out = provenance(p, e, binding.index);
    out["status"] = "verified";
    out["nativeOffset"] = begin;
    out["parsedNativeBytes"] = r.pos - begin;
    out["objectGuid"] = object_guid;
    out["guid"] = guid;
    out["retargetSources"] = std::move(retarget);
    out["tail"] = {{"offset", r.pos},
                   {"bytes", r.end - r.pos},
                   {"sha256", detail::hash_range(p.bytes, r.pos, r.end - r.pos, b.o.control)}};
    out["bones"] = Json::array();
    for (const auto &bone : bones)
        out["bones"].push_back({{"id", bone.id},
                                {"name", bone.name},
                                {"parent", bone.parent},
                                {"rotation", bone.rotation},
                                {"translation", bone.translation},
                                {"scale", bone.scale}});
    return bones;
}
} // namespace
struct CookedAnimation::Impl {
    Json metadata = Json::object();
    std::shared_ptr<const PerTrackAnimation> channels;
    std::shared_ptr<const CompressedCurves> curves;
    std::vector<MeshBone> bones;
    bool bound = false;
    std::optional<CompressedAnimationInterpolation> interpolation;
    std::optional<std::uint32_t> frames;
};
CookedAnimation::CookedAnimation(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
bool CookedAnimation::decoded() const {
    return bool(impl_->channels);
}
bool CookedAnimation::skeleton_bound() const {
    return impl_->bound;
}
std::shared_ptr<const CompressedCurves> CookedAnimation::compressed_curves() const {
    return impl_->curves;
}
Json CookedAnimation::curve_information() const {
    return impl_->metadata.at("curves");
}
Json CookedAnimation::sample_curves(double seconds, double empty_default,
                                    const OperationControl &control) const {
    control.check();
    need(std::isfinite(seconds) && std::abs(seconds) <= std::numeric_limits<float>::max(),
         "Invalid curve time");
    need(std::isfinite(empty_default) && std::abs(empty_default) <= std::numeric_limits<float>::max(),
         "Invalid empty curve default");
    const auto &v = *impl_;
    const auto &info = v.metadata.at("curves");
    const bool known = bool(v.curves) || (info.is_object() && info.value("status", std::string{}) == "empty");
    Json out = {
        {"schema", "cyuediff.cooked-animation-curves/1"},
        {"scope",
         "stored encoded-order scalar channels in seconds; no runtime sorting, filtering or blending"},
        {"source", v.metadata.at("source")},
        {"time", seconds},
        {"nativeTime", static_cast<float>(seconds)},
        {"emptyDefault", empty_default},
        {"nativeEmptyDefault", static_cast<float>(empty_default)},
        {"ready", known},
        {"storedCurvesKnown", known},
        {"declaredNames", info.is_object() ? info.at("names") : Json(nullptr)},
        {"values", Json::object()},
        {"unavailable", known              ? Json(nullptr)
                        : info.is_object() ? info.value("unavailable", Json("curve-data-unavailable"))
                                           : Json("compressed-data-unavailable")}};
    if (!v.curves)
        return out;
    for (std::size_t i = 0; i < v.curves->curve_count(); ++i) {
        control.checkpoint("sample-cooked-curves", i, v.curves->curve_count());
        const auto name = info.at("names").at(i).get<std::string>();
        Json row = {{"index", i}, {"name", name}, {"ready", false}, {"value", nullptr}};
        try {
            row["value"] = v.curves->sample_curve(i, seconds, empty_default, control);
            row["ready"] = true;
        } catch (const Error &e) {
            if (e.code == "cancelled")
                throw;
            row["unavailable"] = e.code + ": " + e.what();
            out["ready"] = false;
        }
        out["values"]["name:" + name] = std::move(row);
    }
    control.checkpoint("sample-cooked-curves", v.curves->curve_count(), v.curves->curve_count());
    return out;
}
std::optional<std::uint32_t> CookedAnimation::frame_count() const {
    return impl_->frames;
}
Json CookedAnimation::to_json(bool keys) const {
    auto j = impl_->metadata;
    j["animation"] = impl_->channels ? impl_->channels->to_json(keys) : Json(nullptr);
    if (impl_->curves)
        j["curves"]["decoded"] = impl_->curves->to_json(keys);
    return j;
}
Json CookedAnimation::catalog(const OperationControl &control) const {
    control.check();
    const auto &v = *impl_;
    Json j = {{"schema", "cyuediff.cooked-animation-catalog/1"},
              {"boneChannelsDecoded", bool(v.channels)},
              {"identitiesComplete", v.bound},
              {"source", v.metadata.at("source")},
              {"interpolation", v.metadata.at("interpolation")},
              {"unavailable", v.metadata.at("unavailable")},
              {"boneCodec", v.metadata.at("boneCodec")},
              {"curves", v.metadata.at("curves")},
              {"frameCount", v.frames ? Json(*v.frames) : Json(nullptr)},
              {"skeleton", Json::object()},
              {"animation", v.channels ? v.channels->catalog(control) : Json(nullptr)}};
    if (v.curves)
        j["curves"]["decoded"] = v.curves->catalog(control);
    for (auto it = v.metadata.at("skeleton").begin(); it != v.metadata.at("skeleton").end(); ++it)
        if (it.key() != "bones" && it.key() != "retargetSources")
            j["skeleton"][it.key()] = *it;
    j["skeleton"]["bones"] = Json::array();
    for (const auto &bone : v.bones) {
        control.check();
        j["skeleton"]["bones"].push_back({{"id", bone.id}, {"name", bone.name}, {"parent", bone.parent}});
    }
    return j;
}
Json CookedAnimation::key_preview(std::size_t track, std::uint32_t maximum,
                                  const OperationControl &control) const {
    control.check();
    if (!impl_->channels)
        throw Error("cooked-animation-unavailable", "Compressed bone channels are unavailable");
    return impl_->channels->key_preview(track, maximum, control);
}
Json CookedAnimation::stored_key(std::size_t track, CompressedAnimationChannel channel, std::uint32_t key,
                                 const OperationControl &control) const {
    control.check();
    if (!impl_->channels)
        throw Error("cooked-animation-unavailable", "Compressed bone channels are unavailable");
    return impl_->channels->stored_key(track, channel, key, control);
}
Json CookedAnimation::sample_track(std::size_t track, double frame, const OperationControl &control) const {
    control.check();
    need(std::isfinite(frame) && frame >= 0, "Invalid cooked animation sample frame");
    if (impl_->frames)
        need(frame <= double(*impl_->frames - 1), "Cooked animation frame exceeds sequence");
    Json j = {{"frame", frame}, {"track", track}, {"ready", false}, {"identitiesComplete", impl_->bound}};
    if (!impl_->channels) {
        j["unavailable"] = impl_->metadata.at("unavailable");
        return j;
    }
    if (!impl_->interpolation) {
        j["unavailable"] = "interpolation-not-specified";
        return j;
    }
    const auto p = impl_->channels->sample_track(track, frame, *impl_->interpolation, control);
    j["skeletonIndex"] = p.skeleton_index;
    j["positions"] = p.translation;
    j["rotations"] = p.rotation;
    j["scales"] = p.scale;
    j["ready"] = true;
    if (impl_->bound) {
        need(p.skeleton_index >= 0 && std::size_t(p.skeleton_index) < impl_->bones.size(),
             "Compressed track exceeds verified Skeleton");
        j["name"] = impl_->bones[std::size_t(p.skeleton_index)].name;
    }
    return j;
}
Json CookedAnimation::sample(double frame, const OperationControl &control) const {
    control.check();
    need(std::isfinite(frame) && frame >= 0, "Invalid cooked animation sample frame");
    const auto &v = *impl_;
    if (v.frames)
        need(frame <= double(*v.frames - 1), "Cooked animation frame exceeds sequence");
    Json binding = Json::object();
    for (auto it = v.metadata.at("skeleton").begin(); it != v.metadata.at("skeleton").end(); ++it)
        if (it.key() != "bones" && it.key() != "retargetSources")
            binding[it.key()] = *it;
    Json out = {{"schema", "cyuediff.cooked-animation-frame/1"},
                {"scope", "stored compressed bone-local channels; no retargeting, additive base, "
                          "missing-track fallback or skinning"},
                {"object", v.metadata.at("source").at("object")},
                {"frame", frame},
                {"ready", false},
                {"identitiesComplete", v.bound},
                {"tracksCoverage", "unavailable"},
                {"tracks", Json::object()},
                {"numericTracks", Json::array()},
                {"source", v.metadata.at("source")},
                {"skeleton", std::move(binding)},
                {"diagnostics", Json::array()}};
    if (!v.channels) {
        out["diagnostics"].push_back(v.metadata.at("unavailable"));
        return out;
    }
    need(frame <= double(v.channels->frame_count() - 1), "Cooked animation frame exceeds sequence");
    if (!v.interpolation) {
        out["diagnostics"].push_back("Omitted Interpolation requires an explicit producer setting");
        return out;
    }
    out["interpolation"] = *v.interpolation == CompressedAnimationInterpolation::linear ? "linear" : "step";
    const auto poses = v.channels->sample(frame, *v.interpolation, control);
    for (const auto &pose : poses) {
        control.check();
        out["numericTracks"].push_back(pose.to_json());
        if (v.bound) {
            need(pose.skeleton_index >= 0 && std::size_t(pose.skeleton_index) < v.bones.size(),
                 "Compressed track exceeds verified Skeleton");
            const auto &name = v.bones[std::size_t(pose.skeleton_index)].name;
            out["tracks"]["name:" + name] = {
                {"name", name},        {"skeletonIndex", pose.skeleton_index}, {"track", pose.track},
                {"ready", true},       {"positions", pose.translation},        {"rotations", pose.rotation},
                {"scales", pose.scale}};
        }
    }
    out["ready"] = true;
    out["tracksCoverage"] = "stored";
    if (!v.bound)
        out["diagnostics"].push_back("Skeleton is not bound; numeric indices are not named identities");
    return out;
}

CookedAnimation read_zen_animation(const Container &container, std::size_t entry, const ZenReadOptions &zen,
                                   const ContainerReadOptions &read, const CookedAnimationOptions &input,
                                   std::size_t selected) {
    auto o = input;
    if (o.narrow_string_encoding == NarrowStringEncoding::unspecified && zen.schema_context)
        o.narrow_string_encoding = zen.schema_context->narrow_string_encoding;
    o.control.cancelled = [a = input.control, b = zen.control, c = read.control, d = input.channels.control,
                           settings = input.bone_settings, skel = input.skeleton,
                           curve_settings = input.curve_settings,
                           curve_control = input.curve_channels.control] {
        a.check();
        b.check();
        c.check();
        d.check();
        curve_control.check();
        for (const auto *source : {&settings, &skel, &curve_settings})
            if (*source) {
                (*source)->zen.control.check();
                (*source)->read.control.check();
            }
        return false;
    };
    o.control.progress = input.control.progress            ? input.control.progress
                         : zen.control.progress            ? zen.control.progress
                         : read.control.progress           ? read.control.progress
                         : input.channels.control.progress ? input.channels.control.progress
                                                           : input.curve_channels.control.progress;
    o.control.check();
    supported(o.profile == CookedAnimationProfile::unreal_5_3_windows &&
                  zen.profile == ZenProfile::unreal_5_3,
              "Cooked animation requires an explicit UE 5.3 Windows profile");
    if (o.interpolation)
        supported(*o.interpolation == CompressedAnimationInterpolation::linear ||
                      *o.interpolation == CompressedAnimationInterpolation::step,
                  "Unsupported explicit animation interpolation");
    Budget budget{o};
    budget.reserve(8192);
    auto main = load(container, entry, zen, read, budget);
    const auto &package = main.package;
    if (selected == SIZE_MAX)
        for (std::size_t i = 0; i < package.exports.size(); ++i)
            if (package.exports[i].class_name == "/Script/Engine.AnimSequence" &&
                !(package.exports[i].flags & 0x30)) {
                need(selected == SIZE_MAX, "Select one of several cooked AnimSequence exports");
                selected = i;
            }
    need(selected < package.exports.size(), "No selected cooked AnimSequence export");
    const auto &object = package.exports[selected];
    context(object.class_name == "/Script/Engine.AnimSequence" && !(object.flags & 0x30),
            "Selected export is not a non-template AnimSequence");
    const auto start = boundary(object);
    const auto &properties = object.property_read.at("properties");
    charge_properties(properties, budget);
    auto out = std::make_shared<CookedAnimation::Impl>();
    out->interpolation = o.interpolation;
    Json interpolation = nullptr;
    if (auto it = properties.find("/Script/Engine.AnimSequence.Interpolation"); it != properties.end()) {
        context((it->value("type", std::string{}) == "ByteProperty" ||
                 it->value("type", std::string{}) == "EnumProperty") &&
                    it->contains("value") && it->at("value").is_number_integer(),
                "Invalid mapped animation interpolation");
        const auto value = it->at("value").get<std::int64_t>();
        supported(value == 0 || value == 1, "Unsupported stored animation interpolation");
        const auto mode =
            value ? CompressedAnimationInterpolation::step : CompressedAnimationInterpolation::linear;
        context(!o.interpolation || *o.interpolation == mode,
                "Explicit interpolation disagrees with stored property");
        out->interpolation = mode;
        interpolation = {{"source", "stored-property"}, {"mode", value ? "step" : "linear"}};
    } else if (o.interpolation)
        interpolation = {
            {"source", "explicit-producer-setting"},
            {"mode", *o.interpolation == CompressedAnimationInterpolation::linear ? "linear" : "step"}};
    out->metadata = {{"schema", "cyuediff.cooked-animation/1"},
                     {"profile", "ue5.3-windows"},
                     {"source", provenance(main, object, selected)},
                     {"nativeOffset", start},
                     {"properties", properties},
                     {"interpolation", interpolation},
                     {"unavailable", "compressed-data-unavailable"},
                     {"skeleton",
                      {{"status", "unavailable"},
                       {"reference", reference(properties, "/Script/Engine.AnimationAsset.Skeleton")}}},
                     {"boneCodec",
                      {{"status", "unavailable"},
                       {"settingsReference",
                        reference(properties, "/Script/Engine.AnimSequence.BoneCompressionSettings")}}},
                     {"curves", nullptr},
                     {"prefix", Json::object()}};
    Cursor r{main.bytes, o, start, object.offset + object.size};
    Json object_guid = nullptr;
    if (r.boolean())
        object_guid = r.guid();
    const auto skeleton_guid = r.guid();
    supported(r.number(1) == 1 && r.number(1) == 0,
              "Animation requires editor-stripped non-server native data");
    const bool serialized = r.boolean();
    out->metadata["prefix"] = {{"objectGuid", object_guid},
                               {"skeletonGuid", skeleton_guid},
                               {"serializedCompressedData", serialized}};
    auto finish = [&] {
        out->metadata["parsedNativeBytes"] = r.pos - start;
        out->metadata["tail"] = {{"offset", r.pos},
                                 {"bytes", r.end - r.pos},
                                 {"sha256", detail::hash_range(main.bytes, r.pos, r.end - r.pos, o.control)}};
        out->metadata["boneChannelsDecoded"] = bool(out->channels);
        out->metadata["identitiesComplete"] = out->bound;
        o.control.check();
        return CookedAnimation(out);
    };
    if (!serialized) {
        out->metadata["unavailable"] = "compressed-data-not-serialized";
        return finish();
    }
    const auto raw_size = r.i32();
    need(raw_size >= 0, "Negative animation raw-data size");
    out->metadata["prefix"]["compressedRawDataSize"] = raw_size;
    PerTrackAnimationSource source;
    const auto tracks = r.count(o.channels.max_tracks);
    r.range(std::uint64_t(tracks) * 4);
    budget.reserve(std::uint64_t(tracks) * 4);
    source.track_to_skeleton.reserve(tracks);
    std::set<std::int32_t> unique;
    for (unsigned i = 0; i < tracks; ++i) {
        o.control.check();
        const auto bone = r.i32();
        need(bone >= 0 && unique.insert(bone).second,
             "Animation compressed track map has invalid or duplicate index");
        source.track_to_skeleton.push_back(bone);
    }
    out->metadata["trackToSkeleton"] = source.track_to_skeleton;
    const auto curves = r.count(std::min(o.max_curves, o.curve_channels.max_curves));
    Json names = Json::array();
    std::set<std::string> named;
    for (unsigned i = 0; i < curves; ++i) {
        o.control.check();
        auto name = r.name(package.names);
        need(name != "None" && named.insert(name).second, "Invalid animation compressed curve name");
        budget.reserve(name.size() + 128);
        names.push_back(std::move(name));
    }
    const auto num_bytes = r.count(0x7fffffffu);
    limit(num_bytes <= o.max_read_bytes, "Animation compressed buffer exceeds read budget");
    const auto use_bulk = r.boolean();
    const auto buffer_start = r.pos;
    detail::ZenBulkWindow window;
    if (use_bulk) {
        const auto index = r.u32();
        auto reads = read;
        reads.control = o.control;
        detail::ZenBulkReader bulk(container, package, container.entries()[entry], main.bytes, reads,
                                   o.max_read_bytes, "cooked-animation");
        window = bulk.describe(index, r.pos, r.end);
        need(window.bytes == num_bytes, "Animation bulk size disagrees with serialized compressed buffer");
    } else
        r.skip(num_bytes);
    const auto handle = r.string(), curve_path = r.string();
    budget.reserve(handle.size() + curve_path.size());
    const auto curve_bytes = r.count(0x7fffffffu);
    r.range(curve_bytes);
    const auto curve_start = r.pos;
    out->metadata["curves"] = {{"names", std::move(names)},
                               {"codecPath", curve_path},
                               {"bytes", curve_bytes},
                               {"sha256", detail::hash_range(main.bytes, r.pos, curve_bytes, o.control)},
                               {"status", curves == 0 && curve_bytes == 0 ? "empty" : "opaque"}};
    r.skip(curve_bytes);
    out->metadata["compressedBuffer"] = {{"bytes", num_bytes},
                                         {"storage", use_bulk ? "bulk" : "inline"},
                                         {"bulk", use_bulk ? window.metadata : Json(nullptr)}};
    auto &curve_info = out->metadata["curves"];
    curve_info["settingsReference"] =
        reference(properties, "/Script/Engine.AnimSequence.CurveCompressionSettings");
    curve_info["unavailable"] = nullptr;
    if (curves || curve_bytes) {
        const auto curve_settings = resolve(main, curve_info["settingsReference"], o.curve_settings,
                                            "/Script/Engine.AnimCurveCompressionSettings", budget);
        if (!curve_settings.package)
            curve_info["unavailable"] = "curve-compression-settings-not-bound";
        else {
            curve_info["settings"] =
                provenance(*curve_settings.package, curve_settings.object(), curve_settings.index);
            const auto selected_curve = curve_codec(curve_settings, budget);
            if (selected_curve.object)
                curve_info["codec"] =
                    provenance(*curve_settings.package, *selected_curve.object, selected_curve.index);
            if (!selected_curve.unavailable.empty())
                curve_info["unavailable"] = selected_curve.unavailable;
            else {
                CompressedCurveSource curve_source;
                curve_source.codec = selected_curve.codec;
                limit(curve_bytes <= o.curve_channels.max_read_bytes,
                      "Compressed curves exceed channel read budget");
                budget.reserve(curve_bytes);
                curve_source.buffer.assign(main.bytes.begin() + static_cast<std::ptrdiff_t>(curve_start),
                                           main.bytes.begin() +
                                               static_cast<std::ptrdiff_t>(curve_start + curve_bytes));
                budget.reserve(std::uint64_t(curves) * sizeof(std::string));
                curve_source.names.reserve(curves);
                for (const auto &name : curve_info.at("names")) {
                    o.control.check();
                    budget.reserve(name.get_ref<const std::string &>().size());
                    curve_source.names.push_back(name.get<std::string>());
                }
                auto options = o.curve_channels;
                options.profile = CompressedCurveProfile::unreal_5_3_windows;
                options.control = o.control;
                options.max_curves = std::min(options.max_curves, o.max_curves);
                options.max_output_bytes =
                    std::min(options.max_output_bytes, o.max_output_bytes - budget.output);
                out->curves =
                    std::make_shared<CompressedCurves>(decode_compressed_curves(curve_source, options));
                budget.reserve(out->curves->accounted_output_bytes());
                curve_info["status"] = "decoded";
                curve_info["lookup"] =
                    "verified base GetCodec returns the registered Codec; stored path retained";
            }
        }
    }
    const auto bone_ref = reference(properties, "/Script/Engine.AnimSequence.BoneCompressionSettings");
    auto settings =
        resolve(main, bone_ref, o.bone_settings, "/Script/Engine.AnimBoneCompressionSettings", budget);
    const auto skeleton_ref = reference(properties, "/Script/Engine.AnimationAsset.Skeleton");
    auto bound = resolve(main, skeleton_ref, o.skeleton, "/Script/Engine.Skeleton", budget);
    if (bound.package) {
        out->bones = skeleton(bound, skeleton_guid, out->metadata["skeleton"], budget);
        out->bound = true;
        for (auto bone : source.track_to_skeleton)
            need(std::size_t(bone) < out->bones.size(),
                 "Animation compressed track exceeds verified Skeleton");
    }
    out->metadata["boneCodec"]["handle"] = handle;
    if (!settings.package) {
        out->metadata["unavailable"] = "bone-compression-settings-not-bound";
        return finish();
    }
    const auto chosen = codec(settings, handle, budget);
    if (chosen.unavailable == "empty-bone-codec") {
        need(num_bytes == 0 && tracks == 0, "Empty bone codec cannot own compressed bone data");
        out->metadata["boneCodec"] = {
            {"status", "absent"},
            {"handle", handle},
            {"settings", provenance(*settings.package, settings.object(), settings.index)}};
        out->metadata["prefix"]["useRawDataOnly"] = r.boolean();
        out->metadata["unavailable"] = "no-compressed-bone-codec";
        return finish();
    }
    if (!chosen.unavailable.empty()) {
        out->metadata["unavailable"] = chosen.unavailable;
        out->metadata["boneCodec"]["settings"] =
            provenance(*settings.package, settings.object(), settings.index);
        return finish();
    }
    const auto &selected_codec = *chosen.object;
    out->metadata["boneCodec"] = {
        {"status", "verified"},
        {"settingsReference", bone_ref},
        {"settings", provenance(*settings.package, settings.object(), settings.index)},
        {"codec", provenance(*settings.package, selected_codec, chosen.index)},
        {"handle", handle},
        {"canonicalHandle", chosen.canonical_handle},
        {"supported", selected_codec.class_name == "/Script/Engine.AnimCompress_PerTrackCompression"}};
    if (selected_codec.class_name != "/Script/Engine.AnimCompress_PerTrackCompression") {
        out->metadata["unavailable"] = "unsupported-bone-codec-class";
        return finish();
    }
    source.frame_count = r.count(o.channels.max_frames);
    need(source.frame_count > 0, "Animation compressed metadata has no frames");
    out->frames = source.frame_count;
    const auto encoding = r.number(1), translation = r.number(1), rotation = r.number(1), scale = r.number(1);
    supported(encoding == 2 && translation == 6 && rotation == 6 && scale <= 6,
              "Unsupported per-track animation global format tags");
    source.key_byte_count = r.count(0x7fffffffu);
    source.track_offset_count = r.count(0x7fffffffu);
    source.scale_offset_count = r.count(0x7fffffffu);
    source.scale_strip_size = r.count(0x7fffffffu);
    out->metadata["prefix"]["useRawDataOnly"] = r.boolean();
    out->metadata["compressedViews"] = {{"frames", source.frame_count},
                                        {"keyEncoding", encoding},
                                        {"translationFormat", translation},
                                        {"rotationFormat", rotation},
                                        {"scaleFormat", scale},
                                        {"keyBytes", source.key_byte_count},
                                        {"trackOffsetCount", source.track_offset_count},
                                        {"scaleOffsetCount", source.scale_offset_count},
                                        {"scaleStripSize", source.scale_strip_size}};
    need((std::uint64_t(source.track_offset_count) + source.scale_offset_count) * 4 + source.key_byte_count ==
             num_bytes,
         "Animation serialized views disagree with compressed buffer size");
    limit(num_bytes <= o.channels.max_read_bytes, "Compressed animation exceeds channel read budget");
    if (use_bulk) {
        auto reads = read;
        reads.control = o.control;
        detail::ZenBulkReader bulk(container, package, container.entries()[entry], main.bytes, reads,
                                   o.max_read_bytes - (budget.read - main.bytes.size()), "cooked-animation");
        bulk.load(window);
        out->metadata["compressedBuffer"]["bulk"] = window.metadata;
        if (!window.unavailable.empty()) {
            out->metadata["unavailable"] = window.unavailable;
            return finish();
        }
        budget.reserve(num_bytes);
        source.buffer.assign(window.data->begin() + static_cast<std::ptrdiff_t>(window.offset),
                             window.data->begin() +
                                 static_cast<std::ptrdiff_t>(window.offset + window.bytes));
    } else {
        budget.reserve(num_bytes);
        source.buffer.assign(main.bytes.begin() + static_cast<std::ptrdiff_t>(buffer_start),
                             main.bytes.begin() + static_cast<std::ptrdiff_t>(buffer_start + num_bytes));
    }
    auto options = o.channels;
    options.profile = PerTrackAnimationProfile::unreal_5_3_windows;
    options.control = o.control;
    options.max_output_bytes = std::min(options.max_output_bytes, o.max_output_bytes - budget.output);
    out->channels = std::make_shared<PerTrackAnimation>(decode_per_track_animation(source, options));
    out->metadata["unavailable"] = nullptr;
    return finish();
}
Json compare_cooked_animation_frames(const CookedAnimation &before, const CookedAnimation &after, double af,
                                     double bf, const AnimationFrameDiffOptions &o) {
    detail::validate_animation_tolerances(o.position_tolerance, o.rotation_tolerance_degrees,
                                          o.scale_tolerance);
    const auto &control = o.animation.control;
    control.check();
    const auto a = before.sample(af, control), b = after.sample(bf, control);
    const bool known = a.value("ready", false) && b.value("ready", false) &&
                       a.value("identitiesComplete", false) && b.value("identitiesComplete", false);
    std::set<std::string> names;
    for (const auto *side : {&a, &b})
        for (auto it = side->at("tracks").begin(); it != side->at("tracks").end(); ++it) {
            control.check();
            limit(names.size() < o.max_track_pairs || names.count(it.key()),
                  "Cooked animation diff exceeds track-pair budget");
            names.insert(it.key());
        }
    Json out = {{"schema", "cyuediff.cooked-animation-frame-diff/1"},
                {"scope", "selected stored compressed bone-local frames; no retargeting, additive base, "
                          "hierarchy or skinning"},
                {"before", a},
                {"after", b},
                {"complete", known},
                {"status", "incomplete"},
                {"tracks", Json::object()},
                {"changedTracks", 0},
                {"comparedTracks", 0},
                {"unavailableTracks", 0},
                {"positionTolerance", o.position_tolerance},
                {"rotationToleranceDegrees", o.rotation_tolerance_degrees},
                {"scaleTolerance", o.scale_tolerance},
                {"rotationMetric", "normalized shortest quaternion orientation; sign invariant"}};
    std::size_t changed = 0, compared = 0, unavailable = 0;
    for (const auto &name : names) {
        control.checkpoint("compare-cooked-animation", compared + unavailable, names.size());
        const auto x = a.at("tracks").find(name), y = b.at("tracks").find(name);
        const bool has_a = x != a.at("tracks").end(), has_b = y != b.at("tracks").end();
        Json pair = {{"before", has_a ? *x : Json(nullptr)},
                     {"after", has_b ? *y : Json(nullptr)},
                     {"status", "unavailable"},
                     {"ready", false},
                     {"changed", nullptr}};
        if (known) {
            if (has_a && has_b) {
                auto metrics = detail::compare_local_animation_values(
                    *x, *y, o.position_tolerance, o.rotation_tolerance_degrees, o.scale_tolerance, control);
                pair["metrics"] = metrics;
                if (metrics.value("ready", false)) {
                    const bool different = metrics.at("changed").get<bool>();
                    pair["ready"] = true;
                    pair["changed"] = different;
                    pair["status"] = different ? "changed" : "identical";
                    changed += different;
                    ++compared;
                }
            } else {
                pair["ready"] = true;
                pair["changed"] = true;
                pair["status"] = has_a ? "removed" : "added";
                ++changed;
            }
        }
        if (!pair.at("ready").get<bool>()) {
            ++unavailable;
            out["complete"] = false;
        }
        out["tracks"][name] = std::move(pair);
    }
    out["changedTracks"] = changed;
    out["comparedTracks"] = compared;
    out["unavailableTracks"] = unavailable;
    out["status"] = !out.at("complete").get<bool>() ? "incomplete" : changed ? "changed" : "identical";
    return out;
}
Json compare_cooked_animation_curves(const CookedAnimation &before, const CookedAnimation &after,
                                     const CookedAnimationCurveDiffOptions &o) {
    o.control.check();
    for (double t : {o.before_time, o.after_time, o.empty_default})
        need(std::isfinite(t) && std::abs(t) <= std::numeric_limits<float>::max(),
             "Invalid scalar curve comparison time/default");
    need(std::isfinite(o.value_tolerance) && o.value_tolerance >= 0, "Invalid scalar curve tolerance");
    const auto a = before.curve_information(), b = after.curve_information();
    const auto am = before.compressed_curves(), bm = after.compressed_curves();
    auto known = [](const Json &v, const auto &model) {
        return bool(model) || (v.is_object() && v.value("status", std::string{}) == "empty");
    };
    const bool stored = known(a, am) && known(b, bm);
    std::uint64_t output = 4096;
    auto reserve = [&](std::uint64_t n) {
        limit(output <= o.max_output_bytes && n <= o.max_output_bytes - output,
              "Curve comparison exceeds output budget");
        output += n;
    };
    reserve(0);
    std::map<std::string, std::array<std::size_t, 2>> names;
    unsigned side = 0;
    for (const auto *info : {&a, &b}) {
        if (info->is_object())
            for (std::size_t i = 0; i < info->at("names").size(); ++i) {
                o.control.check();
                const auto name = info->at("names").at(i).get<std::string>();
                auto it = names.find(name);
                if (it == names.end()) {
                    limit(names.size() < o.max_curve_pairs, "Curve comparison exceeds pair budget");
                    reserve(name.size() + 256);
                    it = names.emplace(name, std::array<std::size_t, 2>{SIZE_MAX, SIZE_MAX}).first;
                }
                it->second[side] = i;
            }
        ++side;
    }
    Json result = {
        {"schema", "cyuediff.cooked-animation-curve-diff/1"},
        {"scope",
         "stored scalar encoding and selected encoded-order values; no whole-asset/runtime equality"},
        {"beforeTime", o.before_time},
        {"nativeBeforeTime", static_cast<float>(o.before_time)},
        {"nativeAfterTime", static_cast<float>(o.after_time)},
        {"afterTime", o.after_time},
        {"valueTolerance", o.value_tolerance},
        {"emptyDefault", o.empty_default},
        {"nativeEmptyDefault", static_cast<float>(o.empty_default)},
        {"complete", stored},
        {"status", "incomplete"},
        {"curves", Json::object()},
        {"encodingChanges", 0},
        {"valueChanges", 0},
        {"unavailableCurves", 0}};
    std::size_t encoding_changes = 0, value_changes = 0, unavailable = 0, processed = 0;
    bool complete = stored;
    for (const auto &entry : names) {
        o.control.checkpoint("compare-cooked-curves", processed++, names.size());
        reserve(entry.first.size() * 3 + 8192);
        const bool present[2] = {entry.second[0] != SIZE_MAX, entry.second[1] != SIZE_MAX};
        Json metadata[2] = {nullptr, nullptr}, values[2] = {nullptr, nullptr};
        bool ready = stored;
        for (unsigned s = 0; s < 2; ++s) {
            const auto &model = s ? bm : am;
            if (!model || !present[s]) {
                if (present[s])
                    ready = false;
                continue;
            }
            metadata[s] = model->curve_metadata(entry.second[s], o.control);
            try {
                values[s] = {{"ready", true},
                             {"value", model->sample_curve(entry.second[s], s ? o.after_time : o.before_time,
                                                           o.empty_default, o.control)}};
            } catch (const Error &e) {
                if (e.code == "cancelled")
                    throw;
                values[s] = {{"ready", false}, {"value", nullptr}, {"unavailable", e.code + ": " + e.what()}};
                ready = false;
            }
        }
        std::string encoding = "unavailable", value = "unavailable";
        Json delta = nullptr;
        if (stored) {
            encoding = !present[0]   ? "added"
                       : !present[1] ? "removed"
                       : am->codec() == bm->codec() &&
                               metadata[0].at("encodingSha256") == metadata[1].at("encodingSha256")
                           ? "identical"
                           : "changed";
            encoding_changes += encoding != "identical";
        }
        if (ready) {
            if (!present[0])
                value = "added";
            else if (!present[1])
                value = "removed";
            else {
                const double d = values[1].at("value").get<double>() - values[0].at("value").get<double>();
                delta = d;
                value = std::abs(d) > o.value_tolerance ? "changed" : "identical";
            }
            value_changes += value != "identical";
        } else {
            complete = false;
            ++unavailable;
        }
        result["curves"]["name:" + entry.first] = {
            {"before", {{"metadata", std::move(metadata[0])}, {"sample", std::move(values[0])}}},
            {"after", {{"metadata", std::move(metadata[1])}, {"sample", std::move(values[1])}}},
            {"encodingStatus", encoding},
            {"valueStatus", value},
            {"valueDelta", delta}};
    }
    o.control.checkpoint("compare-cooked-curves", names.size(), names.size());
    result["complete"] = complete;
    result["encodingChanges"] = encoding_changes;
    result["valueChanges"] = value_changes;
    result["unavailableCurves"] = unavailable;
    result["status"] = !complete ? "incomplete" : encoding_changes || value_changes ? "changed" : "identical";
    return result;
}
} // namespace cyuediff
