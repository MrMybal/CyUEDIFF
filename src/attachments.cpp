#include <cyuediff/attachments.hpp>
#include <algorithm>
#include <cmath>
#include <set>
namespace cyuediff {
namespace {
const Json *tag(const Json &properties, const char *key) {
    if (!properties.is_object())
        return nullptr;
    const auto it = properties.find(key);
    return it == properties.end() ? nullptr : &*it;
}
std::string type_name(const Json &t) {
    return t.is_object() && t.contains("name") && t.at("name").is_string() ? t.at("name").get<std::string>()
                                                                           : "";
}
const Json *value(const Json *t, const char *type, const char *parameter = nullptr) {
    if (!t || !t->is_object() || !t->contains("type") || type_name(t->at("type")) != type ||
        !t->contains("value"))
        return nullptr;
    if (parameter) {
        const auto &desc = t->at("type");
        if (!desc.contains("parameters") || !desc.at("parameters").is_array() ||
            desc.at("parameters").empty() || type_name(desc.at("parameters")[0]) != parameter)
            return nullptr;
    }
    return &t->at("value");
}
bool named(const Json *v) { return v && v->is_string() && !v->get_ref<const std::string &>().empty(); }
bool finite(const Json &v) { return v.is_number() && std::isfinite(v.get<double>()); }
Json vector_value(const Json &v, bool rotation) {
    if (v.is_array() && v.size() == 3 && std::all_of(v.begin(), v.end(), finite))
        return v;
    if (v.is_object()) {
        Json out = Json::array();
        for (auto key : rotation ? std::initializer_list<const char *>{"Pitch", "Yaw", "Roll"}
                                 : std::initializer_list<const char *>{"X", "Y", "Z"}) {
            const auto *t = tag(v, key);
            if (!t || !t->is_object() || !t->contains("value") || !finite(t->at("value")))
                return nullptr;
            out.push_back(t->at("value"));
        }
        return out;
    }
    return nullptr;
}
struct Projection {
    const Package &package;
    const AttachmentOptions &options;
    std::size_t nodes = 0, text_bytes = 0, references = 0, virtual_count = 0, reference_bones = 0;
    void text(std::size_t n) {
        if (n > options.max_text_bytes - text_bytes)
            throw Error("limit", "Attachment text budget exceeded");
        text_bytes += n;
    }
    Json copy(const Json &v, unsigned depth = 0) {
        options.control.check();
        if (depth > options.max_depth || nodes++ >= options.max_nodes)
            throw Error("limit", "Attachment node/depth budget exceeded");
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
        if (v.is_number_float() && !std::isfinite(v.get<double>()))
            throw Error("attachment-value", "Non-finite attachment property");
        return v;
    }
    Json socket(const ObjectExport &e) {
        if (!e.properties.is_object())
            throw Error("attachment-value", "Socket properties must be an object");
        Json out = {{"class", e.class_name},
                    {"name", nullptr},
                    {"bone", nullptr},
                    {"outer", nullptr},
                    {"complete", e.coverage == "properties"},
                    {"extraProperties", Json::object()},
                    {"unresolvedProperties", Json::object()},
                    {"diagnostics", Json::array()}};
        auto unresolved = [&](const char *field, const Json *t) {
            out["complete"] = false;
            out["diagnostics"].push_back(std::string("Missing or unresolved stored socket field: ") + field);
            if (t)
                out["unresolvedProperties"][field] = copy(*t);
        };
        for (auto field : {std::make_pair("SocketName", "name"), std::make_pair("BoneName", "bone")}) {
            const auto *t = tag(e.properties, field.first), *v = value(t, "NameProperty");
            if (named(v))
                out[field.second] = copy(*v);
            else
                unresolved(field.first, t);
        }
        for (auto field :
             {std::make_pair("RelativeLocation", "location"), std::make_pair("RelativeRotation", "rotation"),
              std::make_pair("RelativeScale", "scale")}) {
            const auto *t = tag(e.properties, field.first);
            if (!t)
                continue; // Omitted values are not synthesized from engine defaults.
            const bool rotation = std::string(field.second) == "rotation";
            const auto *v = value(t, "StructProperty", rotation ? "Rotator" : "Vector");
            const auto decoded = v ? vector_value(*v, rotation) : Json(nullptr);
            if (!decoded.is_null())
                out[field.second] = copy(decoded);
            else
                unresolved(field.first, t);
        }
        if (const auto *t = tag(e.properties, "bForceAlwaysAnimated")) {
            const auto *v = value(t, "BoolProperty");
            if (v && v->is_boolean())
                out["forceAlwaysAnimated"] = copy(*v);
            else
                unresolved("bForceAlwaysAnimated", t);
        }
        if (e.outer_index) {
            try {
                out["outer"] = copy(package.reference(e.outer_index));
            } catch (const Error &error) {
                if (error.code == "cancelled")
                    throw;
                out["outerIndex"] = e.outer_index;
                out["complete"] = false;
                out["diagnostics"].push_back("Stored socket outer reference is unresolved");
            }
        }
        const std::set<std::string> handled = {"SocketName",       "BoneName",      "RelativeLocation",
                                               "RelativeRotation", "RelativeScale", "bForceAlwaysAnimated"};
        for (auto it = e.properties.begin(); it != e.properties.end(); ++it)
            if (!handled.count(it.key()))
                out["extraProperties"][it.key()] = copy(*it);
        if (e.coverage != "properties")
            out["diagnostics"].push_back("Stored socket properties are incomplete");
        return out;
    }
    Json virtual_bones(const ObjectExport &e) {
        Json out = {
            {"coverage", e.class_name == "/Script/Engine.Skeleton" ? "unavailable" : "not-applicable"},
            {"definitions", Json::object()},
            {"complete", e.class_name != "/Script/Engine.Skeleton"}};
        if (e.class_name != "/Script/Engine.Skeleton")
            return out;
        const auto *t = tag(e.properties, "VirtualBones"),
                   *array = value(t, "ArrayProperty", "StructProperty");
        bool shape = false;
        if (array && array->is_array()) {
            const auto &inner = t->at("type").at("parameters")[0];
            shape = inner.contains("parameters") && inner.at("parameters").is_array() &&
                    !inner.at("parameters").empty() && type_name(inner.at("parameters")[0]) == "VirtualBone";
            if(!inner.contains("parameters") && package.summary.ue5_version>0 && package.summary.ue5_version<1012)
                shape=true; // Legacy descriptors omit the decoded element struct name.

        }
        if (!shape) {
            if (t) {
                out["coverage"] = "malformed";
                out["unresolvedProperty"] = copy(*t);
            }
            return out;
        }
        if (array->size() > options.max_virtual_bones - virtual_count ||
            array->size() > (options.max_references - references) / 2)
            throw Error("limit", "Virtual-bone aggregate budget exceeded");
        virtual_count += array->size();
        references += array->size() * 2;
        out["coverage"] = "stored";
        out["complete"] = e.coverage == "properties";
        if (const auto *guid = tag(e.properties, "VirtualBoneGuid"))
            out["storedGuidProperty"] = copy(*guid);
        std::map<std::string, unsigned> labels;
        for (const auto &entry : *array) {
            options.control.check();
            if (const auto *v = value(tag(entry, "VirtualBoneName"), "NameProperty");
                named(v) && *v != "None")
                ++labels[v->get<std::string>()];
        }
        std::set<std::string> raw;
        if (!array->empty() && e.native_coverage == "reference-skeleton" && e.native_data.is_object() &&
            e.native_data.value("kind", "") == "reference-skeleton") {
            const auto it = e.native_data.find("bones");
            if (it == e.native_data.end() || !it->is_array())
                throw Error("attachment-value", "Malformed raw bone name table");
            if (it->size() > options.max_reference_bones - reference_bones)
                throw Error("limit", "Attachment raw bone budget exceeded");
            reference_bones += it->size();
            for (const auto &bone : *it) {
                options.control.check();
                if (!bone.is_object() || !bone.contains("name") || !named(&bone.at("name")) ||
                    !raw.insert(bone.at("name").get<std::string>()).second)
                    throw Error("attachment-value", "Invalid raw bone identities");
            }
        }
        auto resolution = [&](const Json &v) -> std::string {
            if (!v.is_string())
                return "unresolved";
            const auto name = v.get<std::string>();
            const bool is_raw = raw.count(name) != 0;
            const auto virtual_name = labels.find(name);
            const bool is_virtual = virtual_name != labels.end() && virtual_name->second == 1;
            if (is_raw && is_virtual)
                return "ambiguous";
            return is_raw ? "raw" : is_virtual ? "virtual" : "unresolved";
        };
        auto &definitions = out["definitions"];
        std::map<std::string, std::string> keys;
        for (std::size_t i = 0; i < array->size(); ++i) {
            options.control.checkpoint("Projecting virtual-bone definitions", i, array->size());
            const auto &entry = (*array)[i];
            const auto *name = value(tag(entry, "VirtualBoneName"), "NameProperty");
            const bool unique = named(name) && *name != "None" && labels.at(name->get<std::string>()) == 1;
            const auto key = unique ? "name:" + name->get<std::string>() : "index:" + std::to_string(i);
            text(key.size());
            Json bone = {{"identity", unique ? "name" : "index"},
                         {"name", nullptr},
                         {"source", nullptr},
                         {"target", nullptr},
                         {"sourceResolution", "unresolved"},
                         {"targetResolution", "unresolved"},
                         {"cycleBlocked", false},
                         {"complete", unique},
                         {"extraProperties", Json::object()},
                         {"unresolvedProperties", Json::object()},
                         {"diagnostics", Json::array()}};
            if (options.source_indices || !unique)
                bone["sourceIndex"] = i;
            if (unique)
                keys[name->get<std::string>()] = key;
            for (auto field :
                 {std::make_pair("VirtualBoneName", "name"), std::make_pair("SourceBoneName", "source"),
                  std::make_pair("TargetBoneName", "target")}) {
                const auto *stored = tag(entry, field.first), *v = value(stored, "NameProperty");
                if (named(v))
                    bone[field.second] = copy(*v);
                else {
                    bone["complete"] = false;
                    if (stored)
                        bone["unresolvedProperties"][field.first] = copy(*stored);
                }
            }
            if (!entry.is_object()) {
                bone["unresolvedProperties"]["element"] = copy(entry);
                bone["complete"] = false;
            } else
                for (auto it = entry.begin(); it != entry.end(); ++it)
                    if (it.key() != "VirtualBoneName" && it.key() != "SourceBoneName" &&
                        it.key() != "TargetBoneName")
                        bone["extraProperties"][it.key()] = copy(*it);
            if (!unique)
                bone["diagnostics"].push_back(
                    "No unique stored virtual-bone name; identity uses array index");
            if (bone["name"].is_string() && raw.count(bone["name"].get<std::string>())) {
                bone["complete"] = false;
                bone["diagnostics"].push_back("Virtual-bone name collides with a raw bone");
            }
            for (const auto *field : {"source", "target"}) {
                const auto state = resolution(bone[field]);
                bone[std::string(field) + "Resolution"] = state;
                if (state != "raw" && state != "virtual")
                    bone["complete"] = false;
            }
            definitions[key] = std::move(bone);
        }
        // Iterative dependency pass. A blocked node may depend on a cycle without being on it.
        std::map<std::string, std::size_t> indegree;
        std::map<std::string, std::vector<std::string>> dependents;
        for (auto it = definitions.begin(); it != definitions.end(); ++it) {
            indegree[it.key()] = 0;
            std::set<std::string> deps;
            for (const auto *field : {"source", "target"})
                if (it->at(std::string(field) + "Resolution") == "virtual")
                    deps.insert(keys.at(it->at(field).get<std::string>()));
            indegree[it.key()] = deps.size();
            for (const auto &dep : deps)
                dependents[dep].push_back(it.key());
        }
        std::vector<std::string> ready;
        for (const auto &entry : indegree)
            if (!entry.second)
                ready.push_back(entry.first);
        for (std::size_t i = 0; i < ready.size(); ++i) {
            options.control.check();
            for (const auto &child : dependents[ready[i]])
                if (--indegree.at(child) == 0)
                    ready.push_back(child);
        }
        for (const auto &entry : indegree)
            if (entry.second) {
                definitions[entry.first]["cycleBlocked"] = true;
                definitions[entry.first]["complete"] = false;
                definitions[entry.first]["diagnostics"].push_back(
                    "Virtual definition depends on a cycle; no pose is rebuilt");
            }
        for (const auto &entry : definitions)
            if (!entry.at("complete").get<bool>())
                out["complete"] = false;
        return out;
    }
    Json owner(const ObjectExport &e, const Json &sockets,
               const std::map<std::string, std::vector<std::string>> &contained) {
        if (!e.properties.is_object())
            throw Error("attachment-value", "Attachment owner properties must be an object");
        Json out = {{"class", e.class_name},
                    {"socketListCoverage", "unavailable"},
                    {"socketReferences", Json::array()},
                    {"containedSockets", Json::array()},
                    {"socketsComplete", false},
                    {"complete", false},
                    {"diagnostics", Json::array()}};
        const auto children = contained.find(e.path);
        if (children != contained.end())
            for (const auto &path : children->second)
                out["containedSockets"].push_back(copy(path));
        const auto *t = tag(e.properties, "Sockets"), *refs = value(t, "ArrayProperty", "ObjectProperty");
        if (refs && refs->is_array()) {
            if (refs->size() > options.max_references - references)
                throw Error("limit", "Socket reference budget exceeded");
            references += refs->size();
            out["socketListCoverage"] = "stored";
            out["socketsComplete"] = e.coverage == "properties";
            std::map<std::string, unsigned> labels;
            for (const auto &ref : *refs) {
                options.control.check();
                Json entry = {{"path", copy(ref)}, {"resolved", false}, {"resolution", "malformed"}};
                if (named(&ref)) {
                    const auto path = ref.get<std::string>();
                    const auto found = sockets.find(path);
                    if (path == "None")
                        entry["resolution"] = "null";
                    else if (found != sockets.end()) {
                        entry["resolution"] = "local";
                        entry["resolved"] = true;
                        out["socketsComplete"] =
                            out["socketsComplete"].get<bool>() && found->at("complete").get<bool>();
                        if (found->at("name").is_string())
                            ++labels[found->at("name").get<std::string>()];
                    } else {
                        entry["resolution"] = "unresolved";
                        out["socketsComplete"] = false;
                    }
                } else
                    out["socketsComplete"] = false;
                out["socketReferences"].push_back(std::move(entry));
            }
            for (const auto &label : labels)
                if (label.second > 1)
                    out["diagnostics"].push_back(
                        "Repeated stored socket name; list order may affect lookup: " + label.first);
        } else {
            if (t) {
                out["socketListCoverage"] = "malformed";
                out["unresolvedSocketList"] = copy(*t);
            }
            out["diagnostics"].push_back(
                "Stored socket list is unavailable; an omitted list is not reconstructed");
        }
        if (e.class_name == "/Script/Engine.SkeletalMesh")
            if (const auto *s = tag(e.properties, "Skeleton")) {
                const auto *v = value(s, "ObjectProperty");
                if (named(v))
                    out["skeletonReference"] = copy(*v);
                else
                    out["unresolvedSkeletonReference"] = copy(*s);
            }
        out["virtualBones"] = virtual_bones(e);
        out["complete"] = out["socketsComplete"].get<bool>() && out["virtualBones"]["complete"].get<bool>();
        return out;
    }
};
} // namespace
Json attachment_snapshot(const Package &package, const AttachmentOptions &options) {
    options.control.check();
    Projection p{package, options};
    Json sockets = Json::object(), owners = Json::object();
    std::set<std::string> paths;
    bool complete = package.complete_input;
    for (const auto &e : package.exports) {
        options.control.check();
        if (e.class_name != "/Script/Engine.SkeletalMeshSocket" &&
            e.class_name != "/Script/Engine.Skeleton" && e.class_name != "/Script/Engine.SkeletalMesh")
            continue;
        if (!paths.insert(e.path).second)
            throw Error("attachment-identity", "Duplicate attachment object path");
        p.text(e.path.size());
        if (e.class_name == "/Script/Engine.SkeletalMeshSocket") {
            if (sockets.size() >= options.max_sockets)
                throw Error("limit", "Socket object budget exceeded");
            sockets[e.path] = p.socket(e);
            complete = complete && sockets[e.path]["complete"].get<bool>();
        }
    }
    std::map<std::string, std::vector<std::string>> contained;
    for (auto it = sockets.begin(); it != sockets.end(); ++it)
        if (it->at("outer").is_string())
            contained[it->at("outer").get<std::string>()].push_back(it.key());
    for (const auto &e : package.exports)
        if (e.class_name == "/Script/Engine.Skeleton" || e.class_name == "/Script/Engine.SkeletalMesh") {
            options.control.checkpoint("Projecting stored socket owners", owners.size());
            if (owners.size() >= options.max_owners)
                throw Error("limit", "Attachment owner budget exceeded");
            auto owner = p.owner(e, sockets, contained);
            complete = complete && owner["complete"].get<bool>();
            owners[e.path] = std::move(owner);
        }
    return {{"schema", "cyuediff.attachments/1"},
            {"scope", "stored skeletal sockets, explicit membership and virtual-bone definitions; no "
                      "defaults or external loading"},
            {"complete", complete && !owners.empty()},
            {"owners", std::move(owners)},
            {"sockets", std::move(sockets)}};
}
} // namespace cyuediff
