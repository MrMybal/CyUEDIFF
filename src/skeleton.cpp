#include <cyuediff/skeleton.hpp>
#include <cmath>
#include <set>
namespace cyuediff {
namespace {
struct Projection {
    const SkeletonOptions &options;
    std::size_t nodes = 0, text_bytes = 0, bone_count = 0, material_count = 0, source_count = 0,
                retarget_transforms = 0, container_count = 0, curve_count = 0, link_count = 0,
                marker_count = 0;
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
    void transform(const Json &pose) {
        require(pose.is_object(), "Malformed stored local pose");
        for (auto field : {std::make_pair("rotation", 4u), std::make_pair("translation", 3u),
                           std::make_pair("scale", 3u)}) {
            require(pose.contains(field.first) && pose.at(field.first).is_array() &&
                        pose.at(field.first).size() == field.second,
                    "Malformed local transform components");
            for (const auto &x : pose.at(field.first))
                require(x.is_number() && std::isfinite(x.get<double>()), "Invalid local transform value");
        }
    }
    Json retargeting(const ObjectExport &e, const Json &bones) {
        Json out = {
            {"coverage", e.class_name == "/Script/Engine.Skeleton" ? "unavailable" : "not-applicable"},
            {"sources", Json::object()}};
        if (!e.native_data.contains("retargeting"))
            return out;
        require(e.class_name == "/Script/Engine.Skeleton",
                "Retarget sources are only supported on Skeleton objects");
        const auto &raw = e.native_data.at("retargeting");
        require(raw.is_object() && raw.contains("coverage") && raw.at("coverage").is_string(),
                "Invalid retarget coverage model");
        const auto coverage = raw.at("coverage").get<std::string>();
        require(coverage == "stored" || coverage == "opaque" || coverage == "disabled",
                "Invalid retarget coverage");
        out["coverage"] = coverage;
        if (coverage != "stored") {
            if (raw.contains("reason"))
                out["reason"] = copy(raw.at("reason"));
            return out;
        }
        require(raw.contains("sources") && raw.at("sources").is_object() && raw.contains("skeletonGuid") &&
                    raw.at("skeletonGuid").is_string(),
                "Missing stored retarget table or Skeleton GUID");
        const auto &guid = raw.at("skeletonGuid").get_ref<const std::string &>();
        require(guid.size() == 32 && guid.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos,
                "Malformed Skeleton GUID");
        out["skeletonGuid"] = copy(raw.at("skeletonGuid"));
        const auto &sources = raw.at("sources");
        if (sources.size() > options.max_retarget_sources - source_count)
            throw Error("limit", "Retarget source budget exceeded");
        source_count += sources.size();
        for (auto it = sources.begin(); it != sources.end(); ++it) {
            options.control.checkpoint("Projecting retarget sources", out["sources"].size(), sources.size());
            require(!it.key().empty() && it->is_object(), "Invalid stored retarget identity");
            text(it.key().size());
            const auto &source = *it;
            require(source.contains("poseName") && source.at("poseName").is_string() &&
                        source.contains("localPoses") && source.at("localPoses").is_array(),
                    "Malformed retarget pose model");
            const auto &poses = source.at("localPoses");
            if (poses.size() > options.max_retarget_transforms - retarget_transforms)
                throw Error("limit", "Retarget transform budget exceeded");
            retarget_transforms += poses.size();
            for (const auto &pose : poses) {
                options.control.check();
                transform(pose);
            }
            Json entry = {{"poseName", copy(source.at("poseName"))},
                          {"bones", Json::object()},
                          {"complete", poses.size() == bones.size()},
                          {"diagnostics", Json::array()}};
            if (source.contains("sourceReferenceMesh")) {
                const auto &mesh = source.at("sourceReferenceMesh");
                require(mesh.is_object() && mesh.contains("kind") && mesh.at("kind").is_string() &&
                            (mesh.at("kind") == "object" || mesh.at("kind") == "soft-object") &&
                            mesh.contains("path") && mesh.at("path").is_string(),
                        "Malformed stored source mesh reference");
                entry["sourceReferenceMesh"] = copy(mesh);
            }
            if (poses.size() == bones.size()) {
                for (std::size_t i = 0; i < bones.size(); ++i) {
                    const auto &name = bones[i].at("name").get_ref<const std::string &>();
                    text(name.size());
                    Json local = {{"localPose", copy(poses[i])}};
                    if (options.source_indices)
                        local["sourceIndex"] = i;
                    entry["bones"][name] = std::move(local);
                }
            } else {
                entry["unboundLocalPoses"] = copy(poses);
                entry["diagnostics"].push_back(
                    "Stored pose count differs from raw bones; no named correspondence is inferred");
            }
            out["sources"][it.key()] = std::move(entry);
        }
        return out;
    }
    Json animation(const ObjectExport &e) {
        const auto fallback = e.class_name == "/Script/Engine.Skeleton" ? "unavailable" : "not-applicable";
        Json out = {{"curveMetadata", {{"coverage", fallback}, {"containers", Json::object()}}},
                    {"syncMarkers", {{"coverage", fallback}, {"names", Json::object()}}}};
        if (e.class_name != "/Script/Engine.Skeleton" || e.native_coverage != "reference-skeleton" ||
            e.native_data.value("kind", "") != "reference-skeleton")
            return out;
        auto charge_count = [&](std::size_t n, std::size_t &used, std::size_t limit) {
            if (n > limit - used)
                throw Error("limit", "Skeleton animation metadata budget exceeded");
            used += n;
        };
        auto coverage = [&](const Json &raw, const std::string &field) {
            require(raw.is_object() && raw.contains("coverage") && raw.at("coverage").is_string(),
                    "Malformed animation metadata coverage");
            const auto c = raw.at("coverage").get<std::string>();
            require(c == "stored" || c == "opaque" || c == "disabled" || c == "unavailable" ||
                        (field == "syncMarkers" && (c == "stripped" || c == "not-stored")),
                    "Unsupported animation metadata coverage");
            out[field]["coverage"] = copy(raw.at("coverage"));
            if (raw.contains("reason")) {
                require(raw.at("reason").is_string(), "Invalid animation metadata reason");
                out[field]["reason"] = copy(raw.at("reason"));
            }
            return c;
        };
        if (e.native_data.contains("curveMetadata")) {
            const auto &raw = e.native_data.at("curveMetadata");
            if (coverage(raw, "curveMetadata") == "stored") {
                require(raw.contains("containers") && raw.at("containers").is_object(),
                        "Missing curve containers");
                const auto &containers = raw.at("containers");
                charge_count(containers.size(), container_count, options.max_curve_containers);
                for (auto it = containers.begin(); it != containers.end(); ++it) {
                    options.control.checkpoint("Projecting skeleton curve metadata", curve_count);
                    require(!it.key().empty() && it->is_object(), "Invalid curve container");
                    text(it.key().size());
                    charge_count(it->size(), curve_count, options.max_curves);
                    Json curves = Json::object();
                    for (auto curve = it->begin(); curve != it->end(); ++curve) {
                        options.control.check();
                        require(!curve.key().empty() && curve->is_object(), "Invalid stored curve");
                        for (auto field : {"material", "morphTarget"})
                            require(curve->contains(field) && curve->at(field).is_boolean(),
                                    "Invalid curve type flag");
                        require(curve->contains("linkedBones") && curve->at("linkedBones").is_array(),
                                "Missing curve bone links");
                        charge_count(curve->at("linkedBones").size(), link_count, options.max_curve_links);
                        for (const auto &name : curve->at("linkedBones"))
                            require(name.is_string() && !name.get_ref<const std::string &>().empty(),
                                    "Invalid curve bone name");
                        if (curve->contains("maxLOD")) {
                            const auto &lod = curve->at("maxLOD");
                            require(lod.is_number_integer() &&
                                        (!lod.is_number_unsigned() || lod.get<std::uint64_t>() <= 255) &&
                                        lod.get<std::int64_t>() >= 0 && lod.get<std::int64_t>() <= 255,
                                    "Invalid stored curve LOD");
                        }
                        text(curve.key().size());
                        curves[curve.key()] = copy(*curve);
                    }
                    out["curveMetadata"]["containers"][it.key()] = std::move(curves);
                }
            }
        }
        if (e.native_data.contains("syncMarkers")) {
            const auto &raw = e.native_data.at("syncMarkers");
            const auto c = coverage(raw, "syncMarkers");
            if (raw.contains("stripFlags")) {
                const auto &flags = raw.at("stripFlags");
                require(flags.is_object(), "Invalid marker strip flags");
                for (auto field : {"global", "class"}) {
                    require(flags.contains(field) && flags.at(field).is_number_integer(),
                            "Invalid marker strip byte");
                    const auto &v = flags.at(field);
                    require((!v.is_number_unsigned() || v.get<std::uint64_t>() <= 255) &&
                                v.get<std::int64_t>() >= 0 && v.get<std::int64_t>() <= 255,
                            "Invalid marker strip byte");
                }
                require(flags.at("global").get<unsigned>() <= 3, "Unknown marker global strip bits");
                require(c != "stored" || !(flags.at("global").get<unsigned>() & 1),
                        "Stored markers cannot be editor-stripped");
                require(c != "stripped" || (flags.at("global").get<unsigned>() & 1),
                        "Stripped markers need editor strip flag");
                out["syncMarkers"]["stripFlags"] = copy(flags);
            } else
                require(c != "stripped", "Missing marker strip flags");
            if (c == "stored") {
                require(raw.contains("names") && raw.at("names").is_array(), "Missing stored marker names");
                charge_count(raw.at("names").size(), marker_count, options.max_markers);
                Json names = Json::object();
                for (const auto &name : raw.at("names")) {
                    options.control.check();
                    require(name.is_string() && !name.get_ref<const std::string &>().empty(),
                            "Invalid sync marker name");
                    const auto &key = name.get_ref<const std::string &>();
                    text(key.size());
                    if (!names.contains(key))
                        names[key] = {{"occurrences", 0}};
                    names[key]["occurrences"] = names[key]["occurrences"].get<std::size_t>() + 1;
                }
                // Charge the resulting object against the same bounded JSON node budget.
                out["syncMarkers"]["names"] = copy(names);
            } else
                require(!raw.contains("names"), "Unstored marker names must remain absent");
        }
        return out;
    }
    Json body(const ObjectExport &e) {
        Json out = {
            {"class", e.class_name},
            {"properties", copy(e.properties)},
            {"bones", Json::object()},
            {"native", copy(Json{{"coverage", e.native_coverage}, {"opaque", e.opaque}})},
            {"complete", false},
            {"referenceComplete", false},
            {"retargeting",
             {{"coverage", e.class_name == "/Script/Engine.Skeleton" ? "unavailable" : "not-applicable"},
              {"sources", Json::object()}}},
            {"diagnostics", Json::array()}};
        const auto metadata = animation(e);
        out["curveMetadata"] = metadata.at("curveMetadata");
        out["syncMarkers"] = metadata.at("syncMarkers");
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
            const auto &stored_parent = bone.at("parentIndex");
            require(!stored_parent.is_number_unsigned() || stored_parent.get<std::uint64_t>() < i,
                    "Unsigned parent index is out of range");
            const auto parent = stored_parent.get<std::int64_t>();
            require(!name.empty() && name != "None" && !out["bones"].contains(name) && parent >= -1 &&
                        parent < std::int64_t(i),
                    "Invalid stored skeleton hierarchy");
            require(lookup.contains(name) && lookup.at(name).is_number_integer() &&
                        lookup.at(name).get<std::int64_t>() == std::int64_t(i),
                    "Inconsistent stored skeleton map");
            text(name.size());
            transform(pose);
            Json entry = {
                {"parent", parent < 0 ? Json(nullptr) : copy(bones[std::size_t(parent)].at("name"))},
                {"localPose", copy(pose)}};
            if (options.source_indices)
                entry["sourceIndex"] = i;
            if (bone.contains("exportName"))
                entry["exportName"] = copy(bone.at("exportName"));
            out["bones"][name] = std::move(entry);
        }
        out["retargeting"] = retargeting(e, bones);
        if (raw.contains("objectGuid"))
            out["native"]["objectGuid"] = copy(raw.at("objectGuid"));
        if (raw.contains("layout"))
            out["native"]["layout"] = copy(raw.at("layout"));
        if (e.class_name == "/Script/Engine.SkeletalMesh") {
            require(raw.contains("mesh") && raw.at("mesh").is_object(), "Missing skeletal mesh prefix model");
            const auto &mesh = raw.at("mesh");
            require(mesh.contains("importedBounds") && mesh.at("importedBounds").is_object() &&
                        mesh.contains("materials") && mesh.at("materials").is_array() &&
                        mesh.contains("stripFlags") && mesh.at("stripFlags").is_object(),
                    "Malformed skeletal mesh metadata");
            const auto &bounds = mesh.at("importedBounds");
            for (auto field : {"origin", "boxExtent"})
                require(bounds.contains(field) && bounds.at(field).is_array() && bounds.at(field).size() == 3,
                        "Malformed imported bounds vector");
            require(bounds.contains("sphereRadius") && bounds.at("sphereRadius").is_number(),
                    "Malformed imported sphere radius");
            for (auto field : {"origin", "boxExtent"})
                for (const auto &x : bounds.at(field))
                    require(x.is_number(), "Invalid imported bounds component");
            for (auto field : {"global", "class"})
                require(mesh.at("stripFlags").contains(field) &&
                            mesh.at("stripFlags").at(field).is_number_integer() &&
                            mesh.at("stripFlags").at(field).get<std::int64_t>() >= 0 &&
                            mesh.at("stripFlags").at(field).get<std::uint64_t>() <= 255,
                        "Invalid stored mesh strip byte");
            const auto &materials = mesh.at("materials");
            if (materials.size() > options.max_materials - material_count)
                throw Error("limit", "Skeletal material budget exceeded");
            material_count += materials.size();
            for (const auto &material : materials) {
                options.control.check();
                require(material.is_object(), "Malformed skeletal material");
                for (auto field : {"material", "slotName"})
                    require(material.contains(field) && material.at(field).is_string(),
                            "Malformed skeletal material identity");
                require(material.contains("importedSlotNameStored") &&
                            material.at("importedSlotNameStored").is_boolean(),
                        "Missing imported slot presence flag");
                require(material.at("importedSlotNameStored").get<bool>()
                            ? material.contains("importedSlotName") &&
                                  material.at("importedSlotName").is_string()
                            : !material.contains("importedSlotName"),
                        "Inconsistent imported slot presence");
                require(material.contains("uvChannels") && material.at("uvChannels").is_object(),
                        "Missing skeletal UV metadata");
                const auto &uv = material.at("uvChannels");
                for (auto field : {"initialized", "overrideDensities"})
                    require(uv.contains(field) && uv.at(field).is_boolean(), "Invalid stored UV flag");
                require(uv.contains("localDensities") && uv.at("localDensities").is_array() &&
                            uv.at("localDensities").size() == 4,
                        "Invalid stored UV densities");
                for (const auto &x : uv.at("localDensities"))
                    require(x.is_number(), "Invalid stored UV density");
            }
            // Material array order is significant: opaque sections may refer to numeric slot indices.
            out["mesh"] = copy(mesh);
        }
        out["referenceComplete"] = true;
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
        if (e.class_name == "/Script/Engine.Skeleton" || e.class_name == "/Script/Engine.SkeletalMesh") {
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
            {"scope", "stored raw hierarchy, reference and retarget poses"},
            {"complete", complete && !bodies.empty()},
            {"skeletons", std::move(bodies)}};
}
Json skeleton_animation_snapshot(const Package &package, const SkeletonOptions &options) {
    options.control.check();
    Projection projection{options};
    Json bodies = Json::object();
    bool complete = package.complete_input;
    for (const auto &e : package.exports)
        if (e.class_name == "/Script/Engine.Skeleton") {
            if (bodies.size() >= options.max_skeletons)
                throw Error("limit", "Skeleton animation object budget exceeded");
            if (bodies.contains(e.path))
                throw Error("skeleton-identity", "Duplicate Skeleton animation object path");
            projection.text(e.path.size());
            auto body = projection.animation(e);
            const bool known = body.at("curveMetadata").at("coverage") == "stored" &&
                               (body.at("syncMarkers").at("coverage") == "stored" ||
                                body.at("syncMarkers").at("coverage") == "not-stored");
            body["complete"] = known;
            body["class"] = e.class_name;
            complete = complete && known;
            bodies[e.path] = std::move(body);
        }
    return {{"schema", "cyuediff.skeleton-animation/1"},
            {"scope", "stored Skeleton curve metadata and sync marker names"},
            {"complete", complete && !bodies.empty()},
            {"skeletons", std::move(bodies)}};
}
Json Package::skeleton_snapshot() const { return cyuediff::skeleton_snapshot(*this); }
} // namespace cyuediff
