#include "animation_internal.hpp"
#include <cmath>
#include <set>
namespace cyuediff {
namespace {
const Json *field(const Json &v, const char *name) {
    if (!v.is_object())
        return nullptr;
    const auto it = v.find(name);
    return it == v.end() ? nullptr : &*it;
}
std::string type(const Json &v) {
    const auto *name = field(v, "name");
    return name && name->is_string() ? name->get<std::string>() : "";
}
const Json *value(const Json *v, const char *name, const char *parameter = nullptr) {
    if (!v)
        return nullptr;
    const auto *t = field(*v, "type"), *result = field(*v, "value");
    if (!t || type(*t) != name || !result)
        return nullptr;
    if (parameter) {
        const auto *p = field(*t, "parameters");
        if (!p || !p->is_array() || p->empty() || type((*p)[0]) != parameter)
            return nullptr;
    }
    return result;
}
bool integer(const Json *v) {
    return v && v->is_number_integer() &&
           (!v->is_number_unsigned() || v->get<std::uint64_t>() <= std::uint64_t(INT32_MAX)) &&
           v->get<std::int64_t>() >= INT32_MIN && v->get<std::int64_t>() <= INT32_MAX;
}
struct Projection {
    const AnimationOptions &o;
    std::size_t nodes = 0, text = 0, tracks = 0, keys = 0;
    void bytes(std::size_t n) {
        if (n > o.max_text_bytes - text)
            throw Error("limit", "Animation text budget exceeded");
        text += n;
    }
    Json copy(const Json &v, unsigned depth = 0) {
        o.control.check();
        if (depth > o.max_depth || nodes++ >= o.max_nodes)
            throw Error("limit", "Animation node/depth budget exceeded");
        if (v.is_string())
            bytes(v.get_ref<const std::string &>().size());
        if (v.is_number_float() && !std::isfinite(v.get<double>()))
            throw Error("animation-value", "Non-finite source animation value");
        if (v.is_array()) {
            Json out = Json::array();
            for (const auto &x : v)
                out.push_back(copy(x, depth + 1));
            return out;
        }
        if (v.is_object()) {
            Json out = Json::object();
            for (auto it = v.begin(); it != v.end(); ++it) {
                bytes(it.key().size());
                out[it.key()] = copy(*it, depth + 1);
            }
            return out;
        }
        return v;
    }
    void unknown(Json &out, const char *name, const Json *tag) {
        out["complete"] = false;
        out["diagnostics"].push_back(std::string("Missing or unresolved stored source field: ") + name);
        if (tag)
            out["unresolvedProperties"][name] = copy(*tag);
    }
    Json channel(const Json *v, unsigned width, bool &valid) {
        valid = v && v->is_array();
        if (!valid)
            return nullptr;
        if (v->size() > o.max_keys - keys)
            throw Error("limit", "Animation key budget exceeded");
        keys += v->size();
        // Copy first: host-provided nested/invalid values are subject to the same resource bounds.
        auto out = copy(*v);
        for (const auto &key : out) {
            o.control.check();
            if (!key.is_array() || key.size() != width) {
                valid = false;
                break;
            }
            for (const auto &x : key)
                if (!x.is_number()) {
                    valid = false;
                    break;
                }
            if (!valid)
                break;
        }
        return out;
    }
    Json model(const ObjectExport &e) {
        Json out = {{"class", e.class_name},           {"complete", true},
                    {"tracksCoverage", "unavailable"}, {"tracks", Json::object()},
                    {"diagnostics", Json::array()},    {"unresolvedProperties", Json::object()}};
        for (const auto &x : {std::make_pair("NumberOfKeys", "numberOfKeys"),
                              std::make_pair("NumberOfFrames", "numberOfFrames")}) {
            const auto *tag = field(e.properties, x.first), *v = value(tag, "IntProperty");
            if (integer(v) && v->get<std::int32_t>() >= 0)
                out[x.second] = copy(*v);
            else
                unknown(out, x.first, tag);
        }
        if (const auto *tag = field(e.properties, "FrameRate")) {
            const auto *v = value(tag, "StructProperty", "FrameRate");
            out["frameRate"] = Json::object();
            if (v && v->is_object()) {
                for (const auto &x : {std::make_pair("Numerator", "numerator"),
                                      std::make_pair("Denominator", "denominator")}) {
                    const auto *part = field(*v, x.first), *n = value(part, "IntProperty");
                    if (integer(n) && n->get<std::int32_t>() > 0)
                        out["frameRate"][x.second] = copy(*n);
                    else
                        unknown(out, x.first, part);
                }
            } else
                unknown(out, "FrameRate", tag);
        } else
            unknown(out, "FrameRate", nullptr);
        if (const auto *tag = field(e.properties, "PlayLength")) {
            const auto *v = value(tag, "FloatProperty");
            if (v && v->is_number() && v->get<double>() >= 0)
                out["playLength"] = copy(*v);
            else
                unknown(out, "PlayLength", tag);
        }
        const auto *tag = field(e.properties, "BoneAnimationTracks"),
                   *array = value(tag, "ArrayProperty", "StructProperty");
        bool shape = array && array->is_array();
        if (shape) {
            const auto &inner = tag->at("type").at("parameters")[0];
            const auto *params = field(inner, "parameters");
            shape = params && params->is_array() && !params->empty() &&
                    type((*params)[0]) == "BoneAnimationTrack";
            if (!params && package_legacy)
                shape = true;
        }
        if (!shape) {
            unknown(out, "BoneAnimationTracks", tag);
            return out;
        }
        if (array->size() > o.max_tracks - tracks)
            throw Error("limit", "Animation track budget exceeded");
        tracks += array->size();
        out["tracksCoverage"] = "stored";
        out["tracksComplete"] = true;
        std::map<std::string, unsigned> counts;
        for (const auto &t : *array) {
            o.control.check();
            const auto *n = value(field(t, "Name"), "NameProperty");
            if (n && n->is_string()) {
                bytes(n->get_ref<const std::string &>().size());
                ++counts[n->get<std::string>()];
            }
        }
        for (std::size_t index = 0; index < array->size(); ++index) {
            o.control.checkpoint("Projecting animation tracks", index, array->size());
            const auto &t = (*array)[index];
            const auto *name = value(field(t, "Name"), "NameProperty");
            const bool named =
                name && name->is_string() && !name->get_ref<const std::string &>().empty() && *name != "None";
            const bool unique = named && counts[name->get<std::string>()] == 1;
            const auto key = unique ? "name:" + name->get<std::string>() : "index:" + std::to_string(index);
            bytes(key.size());
            Json track = {{"complete", unique},
                          {"diagnostics", Json::array()},
                          {"unresolvedProperties", Json::object()}};
            if (named)
                track["name"] = copy(*name);
            if (o.source_indices || !unique)
                track["sourceIndex"] = index;
            if (!unique)
                track["diagnostics"].push_back("Missing or ambiguous stored bone track name");
            if (const auto *b = value(field(t, "BoneTreeIndex"), "IntProperty"); integer(b))
                track["boneIndex"] = copy(*b);
            const auto *rawtag = field(t, "InternalTrackData"),
                       *raw = value(rawtag, "StructProperty", "RawAnimSequenceTrack");
            const bool bulk =
                raw && raw->is_object() && raw->value("kind", std::string()) == "raw-animation-keys";
            for (const auto &ch :
                 {std::make_pair("PosKeys", "positions"), std::make_pair("RotKeys", "rotations"),
                  std::make_pair("ScaleKeys", "scales")}) {
                const Json *v = nullptr;
                if (raw && raw->is_object())
                    v = bulk ? field(*raw, ch.second)
                             : value(field(*raw, ch.first), "ArrayProperty", "StructProperty");
                bool valid = false;
                auto result = channel(v, std::string(ch.second) == "rotations" ? 4 : 3, valid);
                if (valid)
                    track[ch.second] = std::move(result);
                else
                    unknown(track, ch.first, rawtag);
            }
            if (!track["complete"].get<bool>()) {
                out["complete"] = false;
                out["tracksComplete"] = false;
            }
            out["tracks"][key] = std::move(track);
        }
        return out;
    }
    bool package_legacy = false;
};
} // namespace
Json animation_snapshot(const Package &p, const AnimationOptions &o) {
    Projection project{o};
    project.package_legacy = p.summary.ue5_version > 0 && p.summary.ue5_version < 1012;
    Json out = {
        {"schema", "cyuediff.animation/1"},
        {"scope",
         "stored AnimDataModel bone key streams; no defaults, external loading or runtime evaluation"},
        {"complete", p.complete_input},
        {"models", Json::object()}};
    std::size_t count = 0;
    for (const auto &e : p.exports) {
        o.control.check();
        if (e.class_name != "/Script/Engine.AnimDataModel")
            continue;
        if (++count > o.max_models)
            throw Error("limit", "Animation model budget exceeded");
        if (e.path.empty() || out["models"].contains(e.path))
            throw Error("animation-identity", "Missing or duplicate animation model path");
        project.bytes(e.path.size());
        auto model = project.model(e);
        if (!model["complete"].get<bool>())
            out["complete"] = false;
        out["models"][e.path] = std::move(model);
    }
    if (!count)
        out["complete"] = false;
    return out;
}
Json detail::sample_animation_frame(const Json &model, std::uint32_t frame, const std::string &path,
                                    bool complete_input, const OperationControl &control) {
    control.check();
    if (!model.contains("numberOfKeys") || !model["numberOfKeys"].is_number_integer() ||
        model["numberOfKeys"].get<std::int64_t>() <= 0)
        throw Error("animation-frame", "Stored number of source keys is unavailable");
    const auto count = model["numberOfKeys"].get<std::uint32_t>();
    if (frame >= count)
        throw Error("animation-frame", "Source frame is outside the stored key range");
    Json out = {
        {"schema", "cyuediff.animation-frame/1"},
        {"scope",
         "stored bone-local source keys; no interpolation, hierarchy, defaults or runtime evaluation"},
        {"object", path},
        {"frame", frame},
        {"numberOfKeys", count},
        {"complete", complete_input && model.value("tracksCoverage", std::string()) == "stored"},
        {"tracks", Json::object()}};
    if (model.contains("frameRate")) {
        out["frameRate"] = model["frameRate"];
        const auto &rate = model["frameRate"];
        if (rate.contains("numerator") && rate.contains("denominator"))
            out["timeSeconds"] =
                double(frame) * rate["denominator"].get<double>() / rate["numerator"].get<double>();
    }
    for (auto t = model.at("tracks").begin(); t != model.at("tracks").end(); ++t) {
        control.checkpoint("Sampling animation source frame", out["tracks"].size(),
                           model.at("tracks").size());
        Json point = {{"ready", t->value("complete", false)}, {"diagnostics", Json::array()}};
        if (t->contains("name"))
            point["name"] = t->at("name");
        for (const auto *channel : {"positions", "rotations", "scales"}) {
            if (!t->contains(channel) || !t->at(channel).is_array() ||
                (t->at(channel).size() != 1 && t->at(channel).size() != count)) {
                point["ready"] = false;
                point["diagnostics"].push_back(std::string("Unavailable source channel or key count: ") +
                                               channel);
                continue;
            }
            const auto &key = t->at(channel)[t->at(channel).size() == 1 ? 0 : frame];
            point[channel] = key;
            if (std::string(channel) == "rotations") {
                double norm = 0;
                for (const auto &c : key)
                    norm += c.get<double>() * c.get<double>();
                if (!std::isfinite(norm) || std::abs(norm - 1.) > 1e-4) {
                    point["ready"] = false;
                    point["diagnostics"].push_back("Stored quaternion is not unit length");
                }
            }
        }
        if (!point["ready"].get<bool>())
            out["complete"] = false;
        out["tracks"][t.key()] = std::move(point);
    }
    return out;
}
Json animation_frame(const Package &p, std::uint32_t frame, const std::string &object_path,
                     const AnimationOptions &o) {
    const auto root = animation_snapshot(p, o);
    const auto &models = root.at("models");
    const auto path = object_path.empty() && models.size() == 1 ? models.begin().key() : object_path;
    if (path.empty() || !models.contains(path))
        throw Error("animation-object", "Select exactly one stored animation model");
    return detail::sample_animation_frame(models.at(path), frame, path, p.complete_input, o.control);
}
} // namespace cyuediff
