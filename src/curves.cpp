#include "curves_internal.hpp"
#include <cmath>
#include <cstring>
#include <limits>
namespace cyuediff {
namespace {
const Json *field(const Json &v, const std::string &name) {
    if (!v.is_object())
        return nullptr;
    auto it = v.find(name);
    return it == v.end() ? nullptr : &*it;
}
std::string name(const Json &v) {
    const auto *n = field(v, "name");
    return n && n->is_string() ? n->get<std::string>() : "";
}
const Json *value(const Json *tag, const char *type, const char *parameter = nullptr) {
    if (!tag)
        return nullptr;
    const auto *desc = field(*tag, "type"), *out = field(*tag, "value");
    if (!desc || name(*desc) != type || !out)
        return nullptr;
    if (parameter) {
        const auto *p = field(*desc, "parameters");
        if (!p || !p->is_array() || p->empty() || name((*p)[0]) != parameter)
            return nullptr;
    }
    return out;
}
bool finite(const Json *v) { return v && v->is_number() && std::isfinite(v->get<double>()); }
std::string time_key(double time) {
    static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559,
                  "Curve identities require IEEE754 binary64");
    const double normalized = time == 0. ? 0. : time;
    std::uint64_t bits = 0;
    std::memcpy(&bits, &normalized, sizeof(bits));
    std::string out = "time:";
    constexpr char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        out.push_back(digits[(bits >> shift) & 15]);
    return out;
}

struct Projection {
    const CurveOptions &o;
    bool legacy = false;
    std::size_t nodes = 0, text = 0, lanes = 0, keys = 0;
    void bytes(std::size_t n) {
        if (n > o.max_text_bytes - text)
            throw Error("limit", "Curve text budget exceeded");
        text += n;
    }
    Json copy(const Json &v, unsigned depth = 0) {
        o.control.check();
        if (depth > o.max_depth || nodes++ >= o.max_nodes)
            throw Error("limit", "Curve node/depth budget exceeded");
        if (v.is_string())
            bytes(v.get_ref<const std::string &>().size());
        if (v.is_number_float() && !finite(&v))
            throw Error("curve-value", "Non-finite stored curve value");
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
    void missing(Json &out, const std::string &key, const Json *raw) {
        out["complete"] = false;
        out["diagnostics"].push_back("Missing or unresolved stored curve field: " + key);
        if (raw)
            out["unresolvedProperties"][key] = copy(*raw);
    }
    bool array_shape(const Json *tag, const char *structure) const {
        const auto *v = value(tag, "ArrayProperty", "StructProperty");
        if (!v || !v->is_array())
            return false;
        const auto &inner = tag->at("type").at("parameters")[0];
        const auto *p = field(inner, "parameters");
        return (!p && legacy) || (p && p->is_array() && !p->empty() && name((*p)[0]) == structure);
    }
    Json lane(const Json *tag) {
        if (lanes++ >= o.max_lanes)
            throw Error("limit", "Curve lane budget exceeded");
        Json out = {{"complete", true},
                    {"keysCoverage", "unavailable"},
                    {"keys", Json::object()},
                    {"keyOrder", Json::array()},
                    {"timeOrder", "unavailable"},
                    {"diagnostics", Json::array()},
                    {"unresolvedProperties", Json::object()}};
        const auto *curve = value(tag, "StructProperty", "RichCurve");
        if (!curve || !curve->is_object()) {
            missing(out, "RichCurve", tag);
            return out;
        }
        for (const auto &part : {std::make_pair("DefaultValue", "defaultValue"),
                                 std::make_pair("PreInfinityExtrap", "preInfinity"),
                                 std::make_pair("PostInfinityExtrap", "postInfinity")}) {
            if (const auto *t = field(*curve, part.first)) {
                const auto *v =
                    value(t, std::string(part.first) == "DefaultValue" ? "FloatProperty" : "ByteProperty");
                if (v && (std::string(part.first) == "DefaultValue"
                              ? finite(v)
                              : v->is_string() || v->is_number_unsigned() ||
                                    (v->is_number_integer() && v->get<std::int64_t>() >= 0 &&
                                     v->get<std::int64_t>() <= 255)))
                    out[part.second] = copy(*v);
                else
                    missing(out, part.first, t);
            }
        }
        for (auto it = curve->begin(); it != curve->end(); ++it)
            if (it.key() != "Keys" && it.key() != "DefaultValue" && it.key() != "PreInfinityExtrap" &&
                it.key() != "PostInfinityExtrap")
                out["extraProperties"][it.key()] = copy(*it);
        const auto *raw = field(*curve, "Keys"), *array = value(raw, "ArrayProperty", "StructProperty");
        if (!array_shape(raw, "RichCurveKey")) {
            missing(out, "Keys", raw);
            return out;
        }
        if (array->size() > o.max_keys - keys)
            throw Error("limit", "Curve key budget exceeded");
        keys += array->size();
        out["keysCoverage"] = "stored";
        std::map<double, unsigned> counts;
        for (const auto &key : *array) {
            o.control.check();
            const auto *t = field(key, "time");
            if (finite(t))
                ++counts[t->get<double>()];
        }
        bool ascending = true;
        double previous = 0;
        bool first = true;
        for (std::size_t index = 0; index < array->size(); ++index) {
            o.control.checkpoint("Projecting rich curve keys", index, array->size());
            const auto &key = (*array)[index];
            const auto *t = field(key, "time"), *kind = field(key, "kind");
            const bool unique = finite(t) && counts[t->get<double>()] == 1;
            const auto id = unique ? time_key(t->get<double>()) : "index:" + std::to_string(index);
            bytes(id.size());
            Json record = {{"complete", unique}, {"diagnostics", Json::array()}};
            bool valid = kind && kind->is_string() && *kind == "rich-curve-key";
            for (const char *f : {"time", "value", "arriveTangent", "arriveTangentWeight", "leaveTangent",
                                  "leaveTangentWeight"}) {
                const auto *v = field(key, f);
                if (finite(v))
                    record[f] = copy(*v);
                else
                    valid = false;
            }
            for (const char *f : {"interpMode", "tangentMode", "tangentWeightMode"}) {
                const auto *v = field(key, f);
                if (v && v->is_number_integer() &&
                    (!v->is_number_unsigned() || v->get<std::uint64_t>() <= 255) &&
                    v->get<std::int64_t>() >= 0 && v->get<std::int64_t>() <= 255)
                    record[f] = copy(*v);
                else
                    valid = false;
            }
            if (!valid) {
                record["complete"] = false;
                record["unresolvedKey"] = copy(key);
                record["diagnostics"].push_back("Unresolved stored rich key");
            }
            if (!unique)
                record["diagnostics"].push_back("Missing or duplicate stored key time");
            if (o.source_indices || !unique)
                record["sourceIndex"] = index;
            if (!record["complete"].get<bool>())
                out["complete"] = false;
            if (!finite(t) || (!first && t->get<double>() <= previous))
                ascending = false;
            if (finite(t))
                previous = t->get<double>();
            first = false;
            out["keyOrder"].push_back(id);
            out["keys"][id] = std::move(record);
        }
        out["timeOrder"] = ascending ? "strict" : "non-strict";
        if (!ascending) {
            out["complete"] = false;
            out["diagnostics"].push_back("Stored key times are not strictly increasing");
        }
        return out;
    }
    Json owner(const ObjectExport &e) {
        Json out = {{"class", e.class_name},        {"complete", true},
                    {"lanesCoverage", "stored"},    {"lanes", Json::object()},
                    {"diagnostics", Json::array()}, {"unresolvedProperties", Json::object()}};
        auto add = [&](const std::string &id, const Json *tag) {
            bytes(id.size());
            auto v = lane(tag);
            if (!v["complete"].get<bool>())
                out["complete"] = false;
            out["lanes"][id] = std::move(v);
        };
        if (e.class_name == "/Script/Engine.CurveFloat")
            add("value", field(e.properties, "FloatCurve"));
        else if (e.class_name == "/Script/Engine.AnimDataModel") {
            const auto *tag = field(e.properties, "CurveData"),
                       *data = value(tag, "StructProperty", "AnimationCurveData");
            const auto *raw = data ? field(*data, "FloatCurves") : nullptr,
                       *array = value(raw, "ArrayProperty", "StructProperty");
            if (!array_shape(raw, "FloatCurve")) {
                out["lanesCoverage"] = "unavailable";
                missing(out, "CurveData.FloatCurves", tag);
                return out;
            }
            if (array->size() > o.max_lanes - lanes)
                throw Error("limit", "Curve lane budget exceeded");
            std::map<std::string, unsigned> names;
            for (const auto &curve : *array) {
                o.control.check();
                const auto *v = value(field(curve, "CurveName"), "NameProperty");
                if (v && v->is_string()) {
                    bytes(v->get_ref<const std::string &>().size());
                    ++names[v->get<std::string>()];
                }
            }
            for (std::size_t i = 0; i < array->size(); ++i) {
                o.control.check();
                const auto &curve = (*array)[i];
                const auto *n = value(field(curve, "CurveName"), "NameProperty");
                const bool named =
                    n && n->is_string() && !n->get_ref<const std::string &>().empty() && *n != "None";
                const bool unique = named && names[n->get<std::string>()] == 1;
                const auto id = unique ? "name:" + n->get<std::string>() : "index:" + std::to_string(i);
                add(id, field(curve, "FloatCurve"));
                auto &l = out["lanes"][id];
                if (named)
                    l["name"] = copy(*n);
                if (!unique) {
                    l["complete"] = out["complete"] = false;
                    l["diagnostics"].push_back("Missing or ambiguous stored curve name");
                }
                if (o.source_indices || !unique)
                    l["sourceIndex"] = i;
                if (const auto *tag = field(curve, "CurveTypeFlags")) {
                    const auto *flags = value(tag, "IntProperty");
                    if (flags && flags->is_number_integer() &&
                        (!flags->is_number_unsigned() || flags->get<std::uint64_t>() <= INT32_MAX) &&
                        flags->get<std::int64_t>() >= INT32_MIN && flags->get<std::int64_t>() <= INT32_MAX)
                        l["flags"] = copy(*flags);
                    else {
                        missing(l, "CurveTypeFlags", tag);
                        out["complete"] = false;
                    }
                }
            }
        } else {
            const bool color = e.class_name == "/Script/Engine.CurveLinearColor";
            const char *axes = color ? "rgba" : "xyz";
            for (unsigned i = 0; i < (color ? 4u : 3u); ++i)
                add(std::string(1, axes[i]),
                    field(e.properties, i ? "FloatCurves[" + std::to_string(i) + "]" : "FloatCurves"));
        }
        return out;
    }
};
} // namespace
Json curve_snapshot(const Package &p, const CurveOptions &o) {
    Projection project{o};
    project.legacy = p.summary.ue5_version < 1012;
    Json out = {{"schema", "cyuediff.curves/1"},
                {"scope",
                 "stored rich-curve keys and explicitly stored settings; no defaults or external resolution"},
                {"completeInput", p.complete_input},
                {"complete", p.complete_input},
                {"owners", Json::object()}};
    for (const auto &e : p.exports) {
        o.control.check();
        if (e.class_name != "/Script/Engine.CurveFloat" && e.class_name != "/Script/Engine.CurveVector" &&
            e.class_name != "/Script/Engine.CurveLinearColor" &&
            e.class_name != "/Script/Engine.AnimDataModel")
            continue;
        if (out["owners"].size() >= o.max_owners)
            throw Error("limit", "Curve owner budget exceeded");
        if (e.path.empty() || out["owners"].contains(e.path))
            throw Error("curve-identity", "Missing or duplicate curve owner path");
        project.bytes(e.path.size());
        auto owner = project.owner(e);
        if (!owner["complete"].get<bool>())
            out["complete"] = false;
        out["owners"][e.path] = std::move(owner);
    }
    if (out["owners"].empty())
        out["complete"] = false;
    return out;
}
Json detail::evaluate_curve_lane(const Json &lane, double time, bool complete_input,
                                 const OperationControl &control) {
    control.check();
    if (!std::isfinite(time))
        throw Error("curve-time", "Curve time must be finite");
    Json out = {{"time", time},
                {"ready", false},
                {"value", nullptr},
                {"method", "unavailable"},
                {"diagnostics", Json::array()}};
    auto unavailable = [&](const char *message) {
        out["diagnostics"].push_back(message);
        return out;
    };
    if (!complete_input || !lane.value("complete", false) ||
        lane.value("timeOrder", std::string()) != "strict")
        return unavailable("Stored curve or input coverage is incomplete");
    const auto &order = lane.at("keyOrder"), &keys = lane.at("keys");
    if (order.empty())
        return unavailable("No stored source keys");
    std::size_t first = 0, last = order.size();
    while (first < last) {
        control.check();
        const auto middle = first + (last - first) / 2;
        if (keys.at(order[middle].get<std::string>()).at("time").get<double>() < time)
            first = middle + 1;
        else
            last = middle;
    }
    if (first < order.size()) {
        const auto &key = keys.at(order[first].get<std::string>());
        if (key.at("time").get<double>() == time) {
            out["ready"] = true;
            out["value"] = key.at("value");
            out["method"] = "stored-key";
            out["key"] = order[first];
            return out;
        }
    }
    if (first == 0 || first >= order.size())
        return unavailable("Time is outside the stored key range; extrapolation is unavailable");
    const auto &a = keys.at(order[first - 1].get<std::string>()),
               &b = keys.at(order[first].get<std::string>());
    const double delta = b.at("time").get<double>() - a.at("time").get<double>();
    const double alpha = (time - a.at("time").get<double>()) / delta;
    if (!std::isfinite(delta) || !std::isfinite(alpha))
        return unavailable("Source segment arithmetic overflow");
    const int mode = a.at("interpMode").get<int>();
    double result = 0;
    if (mode == 1) {
        result = a.at("value").get<double>();
        out["method"] = "constant";
    } else if (mode == 0) {
        result = (1. - alpha) * a.at("value").get<double>() + alpha * b.at("value").get<double>();
        out["method"] = "linear";
    } else if (mode == 2) {
        const auto left = a.at("tangentWeightMode").get<int>(), right = b.at("tangentWeightMode").get<int>();
        if ((left != 0 && left != 1) || (right != 0 && right != 2))
            return unavailable("Weighted or unknown tangent segment is not evaluated");
        const double t2 = alpha * alpha, t3 = t2 * alpha;
        result = (2 * t3 - 3 * t2 + 1) * a.at("value").get<double>() +
                 (t3 - 2 * t2 + alpha) * delta * a.at("leaveTangent").get<double>() +
                 (-2 * t3 + 3 * t2) * b.at("value").get<double>() +
                 (t3 - t2) * delta * b.at("arriveTangent").get<double>();
        out["method"] = "cubic-hermite";
    } else
        return unavailable("Stored interpolation mode is not evaluated");
    if (!std::isfinite(result))
        return unavailable("Curve value arithmetic overflow");
    out["ready"] = true;
    out["value"] = result;
    out["beforeKey"] = order[first - 1];
    out["afterKey"] = order[first];
    return out;
}
Json evaluate_curve(const Package &p, double time, const std::string &object_path,
                    const std::string &lane_path, const CurveOptions &o) {
    const auto root = curve_snapshot(p, o);
    const auto &owners = root.at("owners");
    const auto owner = object_path.empty() && owners.size() == 1 ? owners.begin().key() : object_path;
    if (owner.empty() || !owners.contains(owner))
        throw Error("curve-object", "Select exactly one stored curve owner");
    const auto &lanes = owners.at(owner).at("lanes");
    const auto id = lane_path.empty() && lanes.size() == 1 ? lanes.begin().key() : lane_path;
    if (id.empty() || !lanes.contains(id))
        throw Error("curve-lane", "Select exactly one stored curve lane");
    auto out = detail::evaluate_curve_lane(lanes.at(id), time, p.complete_input, o.control);
    out["schema"] = "cyuediff.curve-value/1";
    out["scope"] = "double-precision evaluation of supported stored segments; no defaults or extrapolation";
    out["object"] = owner;
    out["lane"] = id;
    return out;
}
} // namespace cyuediff
