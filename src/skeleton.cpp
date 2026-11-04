#include <cyuediff/skeleton.hpp>
#include <cmath>
#include <set>
namespace cyuediff {
namespace {
struct Projection {
    const SkeletonOptions &options;
    std::size_t nodes = 0, text_bytes = 0, bone_count = 0;
    void require(bool ok, const char *message) {
        if (!ok)
            throw Error("skeleton-value", message);
    }
    void text(std::size_t count) {
        if (count > options.max_text_bytes - text_bytes)
            throw Error("limit", "Skeleton text budget exceeded");
        text_bytes += count;
    }
    Json copy(const Json &v, unsigned depth = 0) {
        options.control.check();
        if (depth > options.max_depth || nodes++ >= options.max_nodes)
            throw Error("limit", "Skeleton node/depth budget exceeded");
        if (v.is_object()) {
            Json out = Json::object();
            for (auto it = v.begin(); it != v.end(); ++it) {
                text(it.key().size());
                out[it.key()] = copy(*it, depth + 1);
            }
            return out;
        }
        if (v.is_array()) {
            Json out = Json::array();
            for (const auto &x : v)
                out.push_back(copy(x, depth + 1));
            return out;
        }
        if (v.is_string())
            text(v.get_ref<const std::string &>().size());
        if (v.is_number_float())
            require(std::isfinite(v.get<double>()), "Non-finite skeleton value");
        return v;
    }
    Json body(const ObjectExport &e) {
        Json out = {{"class", e.class_name},
                    {"properties", copy(e.properties)},
                    {"bones", Json::object()},
                    {"native", copy(Json{{"coverage", e.native_coverage}, {"opaque", e.opaque}})},
                    {"complete", false},
                    {"diagnostics", Json::array()}};
        if (e.native_coverage != "reference-skeleton" ||
            e.native_data.value("kind", "") != "reference-skeleton") {
            out["diagnostics"].push_back("Supported raw reference skeleton is unavailable");
            return out;
        }
        const auto &raw = e.native_data;
        require(raw.contains("bones") && raw.at("bones").is_array() && raw.contains("localPoses") &&
                    raw.at("localPoses").is_array() && raw.contains("nameToIndex") &&
                    raw.at("nameToIndex").is_object(),
                "Malformed reference skeleton model");
        const auto &bones = raw.at("bones"), &poses = raw.at("localPoses"), &lookup = raw.at("nameToIndex");
        require(bones.size() == poses.size() && bones.size() == lookup.size(),
                "Skeleton model counts differ");
        if (bones.size() > options.max_bones - bone_count)
            throw Error("limit", "Skeleton bone budget exceeded");
        bone_count += bones.size();
        for (std::size_t i = 0; i < bones.size(); ++i) {
            options.control.checkpoint("Projecting reference skeleton", i, bones.size());
            const auto &bone = bones[i], &pose = poses[i];
            require(bone.is_object() && bone.contains("name") && bone.at("name").is_string() &&
                        bone.contains("parentIndex") && bone.at("parentIndex").is_number_integer(),
                    "Malformed stored bone identity");
            const auto &name = bone.at("name").get_ref<const std::string &>();
            const auto parent = bone.at("parentIndex").get<std::int64_t>();
            require(!name.empty() && name != "None" && !out["bones"].contains(name) && parent >= -1 &&
                        parent < std::int64_t(i),
                    "Invalid stored skeleton hierarchy");
            require(lookup.contains(name) && lookup.at(name).is_number_integer() &&
                        lookup.at(name).get<std::int64_t>() == std::int64_t(i),
                    "Inconsistent stored skeleton map");
            text(name.size());
            require(pose.is_object(), "Malformed stored local pose");
            for (auto component : {std::make_pair("rotation", 4u), std::make_pair("translation", 3u),
                                   std::make_pair("scale", 3u)}) {
                require(pose.contains(component.first) && pose.at(component.first).is_array() &&
                            pose.at(component.first).size() == component.second,
                        "Malformed local transform components");
                for (const auto &x : pose.at(component.first))
                    require(x.is_number() && std::isfinite(x.get<double>()), "Invalid local transform value");
            }
            Json entry = {
                {"parent", parent < 0 ? Json(nullptr) : copy(bones[std::size_t(parent)].at("name"))},
                {"localPose", copy(pose)}};
            if (options.source_indices)
                entry["sourceIndex"] = i;
            if (bone.contains("exportName"))
                entry["exportName"] = copy(bone.at("exportName"));
            out["bones"][name] = std::move(entry);
        }
        if (raw.contains("layout"))
            out["native"]["layout"] = copy(raw.at("layout"));
        out["complete"] = e.coverage == "properties";
        if (e.coverage != "properties")
            out["diagnostics"].push_back("Stored UObject properties are incomplete");
        return out;
    }
};
} // namespace
Json skeleton_snapshot(const Package &package, const SkeletonOptions &options) {
    options.control.check();
    Projection projection{options};
    Json bodies = Json::object();
    bool complete = package.complete_input;
    for (const auto &e : package.exports)
        if (e.class_name == "/Script/Engine.Skeleton") {
            if (bodies.size() >= options.max_skeletons)
                throw Error("limit", "Skeleton object budget exceeded");
            if (bodies.contains(e.path))
                throw Error("skeleton-identity", "Duplicate skeleton object path");
            projection.text(e.path.size());
            auto body = projection.body(e);
            complete = complete && body.at("complete").get<bool>();
            bodies[e.path] = std::move(body);
        }
    return {{"schema", "cyuediff.skeleton/1"},
            {"scope", "stored raw reference hierarchy and local pose"},
            {"complete", complete && !bodies.empty()},
            {"skeletons", std::move(bodies)}};
}
Json Package::skeleton_snapshot() const { return cyuediff::skeleton_snapshot(*this); }
} // namespace cyuediff
