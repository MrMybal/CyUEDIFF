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
std::optional<std::uint32_t> CookedAnimation::frame_count() const {
    return impl_->frames;
}
Json CookedAnimation::to_json(bool keys) const {
    auto j = impl_->metadata;
    j["animation"] = impl_->channels ? impl_->channels->to_json(keys) : Json(nullptr);
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
                           settings = input.bone_settings, skel = input.skeleton] {
        a.check();
        b.check();
        c.check();
        d.check();
        for (const auto *source : {&settings, &skel})
            if (*source) {
                (*source)->zen.control.check();
                (*source)->read.control.check();
            }
        return false;
    };
    o.control.progress = input.control.progress  ? input.control.progress
                         : zen.control.progress  ? zen.control.progress
                         : read.control.progress ? read.control.progress
                                                 : input.channels.control.progress;
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
    const auto curves = r.count(o.max_curves);
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
    out->metadata["curves"] = {{"names", std::move(names)},
                               {"codecPath", curve_path},
                               {"bytes", curve_bytes},
                               {"sha256", detail::hash_range(main.bytes, r.pos, curve_bytes, o.control)},
                               {"status", curves == 0 && curve_bytes == 0 ? "empty" : "opaque"}};
    r.skip(curve_bytes);
    out->metadata["compressedBuffer"] = {{"bytes", num_bytes},
                                         {"storage", use_bulk ? "bulk" : "inline"},
                                         {"bulk", use_bulk ? window.metadata : Json(nullptr)}};
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
} // namespace cyuediff
