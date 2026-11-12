#include "source_availability.hpp"
#include "key_identity.hpp"
#include "name_value_diff.hpp"
#include <algorithm>
namespace cyuediff::detail {
namespace {
void append_token(std::string &out, const std::string &s) {
    if (s.find_first_of("~/") == std::string::npos) {
        out += s;
        return;
    }
    for (char c : s) {
        if (c == '~')
            out += "~0";
        else if (c == '/')
            out += "~1";
        else
            out += c;
    }
}
std::string token(const std::string &s) {
    std::string out;
    append_token(out, s);
    return out;
}
// Extends a shared path buffer for one nested call and restores it afterwards.
class PathPush {
    std::string &path_;
    std::size_t size_;

  public:
    explicit PathPush(std::string &path) : path_(path), size_(path.size()) {}
    ~PathPush() { path_.resize(size_); }
    PathPush(const PathPush &) = delete;
    PathPush &operator=(const PathPush &) = delete;
};
bool fingerprint(const Json &v) {
    if (!v.is_object() || !v.contains("bytes") || !v["bytes"].is_number_integer() || !v.contains("sha256") ||
        !v["sha256"].is_string())
        return false;
    const auto &sha = v["sha256"].get_ref<const std::string &>();
    return (!v["bytes"].is_number_unsigned() ? v["bytes"].get<std::int64_t>() >= 0 : true) &&
           sha.size() == 64 && sha.find_first_not_of("0123456789abcdef") == std::string::npos;
}
// Equality of type descriptors, where a string type means {"name": type}; no copies.
bool same_descriptor(const Json &x, const Json &y) {
    if (x.is_string() == y.is_string())
        return x == y;
    const auto &name = x.is_string() ? x : y;
    const auto &object = x.is_string() ? y : x;
    if (!object.is_object() || object.size() != 1)
        return false;
    const auto i = object.find("name");
    return i != object.end() && *i == name;
}
bool same_bytes(const Json &a, const Json &b) {
    return fingerprint(a) && fingerprint(b) && a["bytes"] == b["bytes"] && a["sha256"] == b["sha256"];
}
const Json &member(const Json &v, const char *key) {
    static const Json empty = nullptr;
    auto i = v.find(key);
    return i == v.end() ? empty : *i;
}
bool same_tag(const Json &a, const Json &b) {
    for (const auto *key : {"type", "guid", "serializationFlags", "extensions", "wireTypes"})
        if (member(a, key) != member(b, key))
            return false;
    const auto &x = a.contains("resolvedType") ? a["resolvedType"] : a["type"],
               &y = b.contains("resolvedType") ? b["resolvedType"] : b["type"];
    return x == y;
}
const Json &opaque(const ObjectExport &e, const char *name) {
    static const Json empty = nullptr;
    auto i = e.opaque.find(name);
    return i == e.opaque.end() ? empty : *i;
}
bool properties_known(const ObjectExport &e) {
    return e.coverage == "properties" || e.coverage == "partial-properties" || e.coverage == "empty";
}
bool tag(const Json &v) {
    if (!v.is_object() || !v.contains("type") || (!v.contains("value") && !v.contains("opaque")))
        return false;
    const auto &t = v["type"];
    const Json *n = &t;
    if (t.is_object()) {
        auto i = t.find("name");
        if (i == t.end())
            return false;
        n = &*i;
    }
    if (!n->is_string())
        return false;
    const auto &s = n->get_ref<const std::string &>();
    return s.size() > 8 && s.compare(s.size() - 8, 8, "Property") == 0;
}
bool skipped(const Json &v) {
    auto i = v.find("serializationFlags");
    if (i == v.end())
        return false;
    if (!i->is_number_integer() ||
        (i->is_number_unsigned() ? i->get<std::uint64_t>() > 63
                                 : i->get<std::int64_t>() < 0 || i->get<std::int64_t>() > 63))
        throw Error("source-coverage-identity", "Invalid stored property flags");
    return (i->get<unsigned>() & 0x20) != 0;
}
struct Builder {
    const SourceCoverageOptions &o;
    const std::map<std::string, std::string> &before_definitions, &after_definitions;
    SourceCoverage out;
    bool before_names_ready = true, after_names_ready = true;
    struct Definitions {
        const std::string *before = nullptr, *after = nullptr;
        bool compatible() const { return (!before && !after) || (before && after && *before == *after); }
    };
    Definitions definitions(const Json &a, const Json &b) const {
        const auto find = [](const auto &entries, const Json &id) -> const std::string * {
            const auto &name = id.get_ref<const std::string &>();
            if (name.size() > 256)
                throw Error("source-coverage-identity", "Decoder identity exceeds its budget");
            const auto i = entries.find(name);
            if (i == entries.end())
                return nullptr;
            if (i->second.size() != 64 ||
                i->second.find_first_not_of("0123456789abcdef") != std::string::npos)
                throw Error("source-coverage-identity", "Invalid decoder definition SHA-256");
            return &i->second;
        };
        return {find(before_definitions, a), find(after_definitions, b)};
    }
    void definition_evidence(const Definitions &pair) {
        if (pair.before) {
            path_cost(pair.before->size());
            out.unavailable.back()["beforeDefinitionSha256"] = *pair.before;
        }
        if (pair.after) {
            path_cost(pair.after->size());
            out.unavailable.back()["afterDefinitionSha256"] = *pair.after;
        }
    }
    std::size_t nodes = 0, path_bytes = 0;
    std::string object;
    void tick() {
        if (++nodes > o.max_nodes)
            throw Error("source-coverage-limit", "Source coverage node budget exceeded");
        if (!(nodes & 1023))
            o.control.checkpoint("source-coverage", nodes);
    }
    void path_cost(std::size_t n) {
        if (n > o.max_path_bytes - path_bytes)
            throw Error("source-coverage-limit", "Source coverage path/report budget exceeded");
        path_bytes += n;
    }
    void cut(const std::string &path) {
        if (!out.suppressed.count(path)) {
            path_cost(path.size() + 1);
            out.suppressed.insert(path);
        }
    }
    void unknown(const std::string &path, bool before, bool after, const char *reason,
                 const Json &a = nullptr, const Json &b = nullptr) {
        if (out.unavailable.size() >= o.max_scopes)
            throw Error("source-coverage-limit", "Source unavailable-scope budget exceeded");
        path_cost(path.size() + 256);
        Json row = {{"object", object},
                    {"path", path},
                    {"beforeUnavailable", before},
                    {"afterUnavailable", after},
                    {"reason", reason}};
        if (fingerprint(a))
            row["beforeOpaque"] = {{"bytes", a["bytes"]}, {"sha256", a["sha256"]}};
        if (fingerprint(b))
            row["afterOpaque"] = {{"bytes", b["bytes"]}, {"sha256", b["sha256"]}};
        out.unavailable.push_back(std::move(row));
        out.affected_objects.insert(object);
    }
    using KeyFormats = std::map<std::string, std::set<std::string>>;
    void key_format(KeyFormats &formats, const std::string &path, std::string identity) {
        auto i = formats.find(path);
        if (i != formats.end() && i->second.count(identity))
            return;
        path_cost(path.size() + identity.size() + 64);
        formats[path].insert(std::move(identity));
    }
    bool unread_key(const Json &v, bool &definition_incompatible, KeyFormats &formats,
                    const std::string &path = {}, unsigned depth = 0) {
        tick();
        if (depth > 256)
            throw Error("source-coverage-limit", "Source key depth exceeded");
        if (v.is_object() && v.contains("decoder") && v["decoder"].is_string() && v.contains("data")) {
            definition_incompatible =
                definition_incompatible || !definitions(v["decoder"], v["decoder"]).compatible();
            const auto &id = v["decoder"].get_ref<const std::string &>();
            const auto prefix = v.value("prefix", Json{});
            if (!prefix.is_null() &&
                (!prefix.is_string() || prefix.get_ref<const std::string &>().size() > 256))
                throw Error("source-coverage-identity", "Invalid source key prefix identity");
            key_format(formats, path,
                       "native:" + id + ":" + (prefix.is_null() ? std::string{} : prefix.get<std::string>()));
            if (prefix == "tagged-properties") {
                const auto &data = v.at("data");
                auto i = data.find("properties");
                return i != data.end() &&
                       unread_key(*i, definition_incompatible, formats, path + "/prefix", depth + 1);
            }
            return false;
        }
        if (tag(v)) {
            if (!v.contains("value") || skipped(v))
                return true;
            const auto &type = v.contains("resolvedType") ? v["resolvedType"] : v["type"];
            const auto descriptor = type.is_string() ? Json{{"name", type}} : type;
            auto identity =
                source_key_identity(descriptor, o.control, [&](std::uint64_t n) { path_cost(n); }, false);
            key_format(formats, path, "type:" + identity);
            auto f = v.find("readFormats");
            if (f != v.end() && f->is_object())
                for (auto i = f->begin(); i != f->end(); ++i) {
                    auto value = source_key_identity(
                        i.value(), o.control, [&](std::uint64_t n) { path_cost(n); }, false);
                    key_format(formats, path + "/reader" + i.key(), "reader:" + value);
                }
            return unread_key(v["value"], definition_incompatible, formats, path + "/value", depth + 1);
        }
        key_format(formats, path, "source");
        if (v.is_array()) {
            for (const auto &child : v)
                if (unread_key(child, definition_incompatible, formats, path + "/*", depth + 1))
                    return true;
        } else if (v.is_object()) {
            const auto kind = v.value("kind", Json{});
            if (kind == "stored-map-delta" || kind == "stored-set-delta") {
                const bool map = kind == "stored-map-delta";
                collection_key_encodings(v, [&](const std::string &encoding) {
                    tick();
                    key_format(formats, path + "/identity", encoding);
                });
                for (const auto &key : v.at("removed"))
                    if (unread_key(key, definition_incompatible, formats, path + "/key", depth + 1))
                        return true;
                for (const auto &entry : v.at("entries")) {
                    if (unread_key(map ? entry.at("key") : entry, definition_incompatible, formats,
                                   path + "/key", depth + 1))
                        return true;
                    if (map && unread_key(entry.at("value"), definition_incompatible, formats,
                                          path + "/mapped", depth + 1))
                        return true;
                }
            } else
                for (auto i = v.begin(); i != v.end(); ++i)
                    if (unread_key(i.value(), definition_incompatible, formats, path + "/" + token(i.key()),
                                   depth + 1))
                        return true;
        }
        return false;
    }
    bool unread_map_keys(const Json &v, bool &definition_incompatible, KeyFormats &formats, bool map) {
        if (!v.is_object())
            return false;
        collection_key_encodings(v, [&](const std::string &encoding) {
            tick();
            key_format(formats, "/identity", encoding);
        });
        auto removed = v.find("removed");
        if (removed != v.end())
            for (const auto &key : *removed)
                if (unread_key(key, definition_incompatible, formats))
                    return true;
        auto entries = v.find("entries");
        if (entries != v.end() && entries->is_object())
            for (const auto &entry : *entries) {
                if (map) {
                    auto key = entry.find("key");
                    if (key != entry.end() && unread_key(*key, definition_incompatible, formats))
                        return true;
                } else if (unread_key(entry, definition_incompatible, formats))
                    return true;
            }
        return false;
    }
    static bool key_formats_disagree(const KeyFormats &a, const KeyFormats &b) {
        for (const auto &entry : a) {
            auto i = b.find(entry.first);
            if (i != b.end() && i->second != entry.second)
                return true;
        }
        return false;
    }
    static bool readers_disagree(const Json &a, const Json &b) {
        const auto &x = member(a, "readFormats"), &y = member(b, "readFormats");
        if (x.is_null() && y.is_null())
            return false;
        if (!x.is_object() || !y.is_object())
            return true;
        for (auto i = x.begin(); i != x.end(); ++i) {
            auto j = y.find(i.key());
            if (j != y.end() && *i != *j)
                return true;
        }
        return false;
    }
    bool needs_names(const Json &type, unsigned depth = 0) {
        tick();
        if (depth > 256)
            throw Error("source-coverage-limit", "Name type depth exceeded");
        const auto name = type.is_string() ? type.get<std::string>() : type.value("name", "");
        if (name == "ByteProperty")
            return true; // Enum-backed bytes may serialize an FName.
        if (name == "NameProperty" || name == "EnumProperty" || name == "FieldPathProperty" ||
            name == "DelegateProperty" || name == "MulticastDelegateProperty" ||
            name == "SoftObjectProperty" || name == "SoftClassProperty")
            return true;
        if (type.is_object())
            for (const char *key : {"parameters", "arguments"})
                if (type.contains(key))
                    for (const auto &child : type.at(key))
                        if (needs_names(child, depth + 1))
                            return true;
        return false;
    }
    // `path` is a shared buffer: nested calls extend it and restore it on return.
    void values(const Json *a, const Json *b, std::string &path, unsigned depth = 0) {
        tick();
        if (depth > 256)
            throw Error("source-coverage-limit", "Source coverage depth exceeded");
        // Scalars carry no tags, wrappers or containers: nothing below applies to them.
        if ((!a || a->is_primitive()) && (!b || b->is_primitive()))
            return;
        const bool ta = a && tag(*a), tb = b && tag(*b);
        if (ta || tb) {
            const bool sa = ta && skipped(*a), sb = tb && skipped(*b);
            const bool ua = ta && (!a->contains("value") || sa), ub = tb && (!b->contains("value") || sb);
            const Json empty = nullptr;
            const auto &oa = ta && a->contains("opaque") ? (*a)["opaque"] : empty;
            const auto &ob = tb && b->contains("opaque") ? (*b)["opaque"] : empty;
            if (member(a ? *a : empty, "readFormats") != member(b ? *b : empty, "readFormats"))
                cut(path + "/readFormats");
            if ((a && a->contains("resolvedType")) || (b && b->contains("resolvedType"))) {
                const auto &x = a ? member(*a, "resolvedType") : Json{},
                           &y = b ? member(*b, "resolvedType") : Json{};
                if (x != y)
                    cut(path + "/resolvedType");
            }
            if (ua || ub) {
                cut(path + "/value");
                if (!(ua && ub && same_tag(*a, *b) && ((sa && sb) || same_bytes(oa, ob))))
                    unknown(path + "/value", ua, ub,
                            sa || sb ? "skipped-property-value" : "stored-property-unavailable", oa, ob);
                if (!(ua && ub))
                    cut(path + "/opaque");
                else
                    cut(path + "/opaque/reason");
                return;
            }
            const auto numeric_bytes = [](const Json &tag) {
                const auto &type = tag.at("type");
                const auto name = type.is_string() ? type.get<std::string>() : type.value("name", "");
                if (name == "ByteProperty")
                    return tag.at("value").is_number();
                if (name != "ArrayProperty" || !tag.at("value").is_array())
                    return false;
                return std::all_of(tag.at("value").begin(), tag.at("value").end(),
                                   [](const Json &v) { return v.is_number(); });
            };
            if (ta && tb && (!before_names_ready || !after_names_ready) &&
                !(numeric_bytes(*a) && numeric_bytes(*b)) &&
                (needs_names(a->contains("resolvedType") ? a->at("resolvedType") : a->at("type")) ||
                 needs_names(b->contains("resolvedType") ? b->at("resolvedType") : b->at("type")))) {
                cut(path + "/value");
                unknown(path + "/value", false, false, "stored-name-components-unavailable");
                return;
            }
            if (ta && tb) {
                if ((*a)["type"] != (*b)["type"] && same_descriptor((*a)["type"], (*b)["type"]))
                    cut(path + "/type");
                auto effective = [](const Json &value) -> const Json & {
                    return value.contains("resolvedType") ? value["resolvedType"] : value["type"];
                };
                if (readers_disagree(*a, *b)) {
                    cut(path + "/value");
                    unknown(path + "/value", false, false, "source-reader-format-incompatible");
                    return;
                }
                if (!same_descriptor(effective(*a), effective(*b))) {
                    cut(path + "/value");
                    unknown(path + "/value", false, false, "source-type-format-incompatible");
                    return;
                }
            }
        }
        if (!a || !b)
            return;
        if (a->is_object() && b->is_object() && member(*a, "kind") == member(*b, "kind") &&
            (member(*a, "kind") == "stored-map-delta" || member(*a, "kind") == "stored-set-delta")) {
            const bool map = (*a)["kind"] == "stored-map-delta";
            bool da = false, db = false;
            KeyFormats fa, fb;
            const bool ka = unread_map_keys(*a, da, fa, map), kb = unread_map_keys(*b, db, fb, map);
            const bool formats = key_formats_disagree(fa, fb);
            if (ka || kb || da || db || formats) {
                cut(path + "/entries");
                cut(path + "/removed");
                const auto reason =
                    da || db   ? (map ? "map-key-definition-incompatible" : "set-key-definition-incompatible")
                    : ka || kb ? (map ? "map-key-identity-unavailable" : "set-key-identity-unavailable")
                               : (map ? "map-key-format-incompatible" : "set-key-format-incompatible");
                unknown(path + "/entries", false, false, reason);
                out.unavailable.back()["beforeKeysUnavailable"] = ka;
                out.unavailable.back()["afterKeysUnavailable"] = kb;
                if (da || db) {
                    out.unavailable.back()["beforeKeyDefinitionsIncompatible"] = da;
                    out.unavailable.back()["afterKeyDefinitionsIncompatible"] = db;
                }
                return;
            }
        }
        const auto wrapper = [](const Json &v) {
            return v.is_object() && v.contains("decoder") && v["decoder"].is_string() && v.contains("data");
        };
        if (wrapper(*a) != wrapper(*b)) {
            cut(path);
            unknown(path, false, false, "source-value-format-incompatible");
            return;
        }
        if (a->is_object() && b->is_object() && a->contains("decoder") && b->contains("decoder") &&
            (*a)["decoder"].is_string() && (*b)["decoder"].is_string() && a->contains("data") &&
            b->contains("data")) {
            if ((*a)["decoder"] != (*b)["decoder"])
                cut(path + "/decoder");
            if (member(*a, "prefix") != member(*b, "prefix"))
                cut(path + "/prefix");
            const bool hybrid =
                member(*a, "prefix") == "tagged-properties" && member(*b, "prefix") == "tagged-properties";
            const auto identity = definitions((*a)["decoder"], (*b)["decoder"]);
            if ((*a)["decoder"] != (*b)["decoder"] || member(*a, "prefix") != member(*b, "prefix") ||
                !identity.compatible()) {
                const auto target = path + (hybrid ? "/data/native" : "/data");
                cut(target);
                unknown(target, false, false,
                        identity.compatible() ? "native-struct-format-incompatible"
                                              : "native-struct-definition-incompatible");
                definition_evidence(identity);
                for (const auto *key : {"beforeDecoder", "afterDecoder"}) {
                    const auto &id = std::string(key) == "beforeDecoder" ? (*a)["decoder"] : (*b)["decoder"];
                    const auto &name = id.get_ref<const std::string &>();
                    if (name.size() > 256)
                        throw Error("source-coverage-identity", "Struct decoder identity exceeds its budget");
                    path_cost(name.size());
                    out.unavailable.back()[key] = name;
                }
            }
            if (hybrid) {
                const auto &x = a->at("data"), &y = b->at("data");
                const auto i = x.find("properties"), j = y.find("properties");
                if (i != x.end() && j != y.end()) {
                    const PathPush restore(path);
                    path += "/data/properties";
                    values(&*i, &*j, path, depth + 1);
                }
            }
            return;
        }
        if (a->is_object() && b->is_object()) {
            if (member(*a, "prefix") == "tagged-properties" && member(*b, "prefix") == "tagged-properties")
                cut(path + "/decoder");
            for (auto i = a->begin(); i != a->end(); ++i) {
                auto j = b->find(i.key());
                const PathPush restore(path);
                path += '/';
                append_token(path, i.key());
                values(&i.value(), j == b->end() ? nullptr : &j.value(), path, depth + 1);
            }
            for (auto i = b->begin(); i != b->end(); ++i)
                if (!a->contains(i.key())) {
                    const PathPush restore(path);
                    path += '/';
                    append_token(path, i.key());
                    values(nullptr, &i.value(), path, depth + 1);
                }
        } else if (a->is_array() && b->is_array()) {
            for (std::size_t i = 0; i < std::max(a->size(), b->size()); ++i) {
                const PathPush restore(path);
                path += '/';
                path += std::to_string(i);
                values(i < a->size() ? &(*a)[i] : nullptr, i < b->size() ? &(*b)[i] : nullptr, path, depth + 1);
            }
        }
    }
    void exports(const ObjectExport &a, const ObjectExport &b) {
        tick();
        object = a.path;
        const auto base = "/objects/" + token(object);
        const bool ka = properties_known(a), kb = properties_known(b);
        if (ka && kb) {
            auto path = base + "/properties";
            values(&a.properties, &b.properties, path);
        }
        else {
            cut(base + "/properties");
            if (!same_bytes(opaque(a, "export"), opaque(b, "export")))
                unknown(base + "/properties", !ka, !kb, "export-properties-unavailable", opaque(a, "export"),
                        opaque(b, "export"));
            if (ka != kb)
                cut(base + "/opaque");
        }
        const bool na = !a.native_data.empty(), nb = !b.native_data.empty();
        const bool same_decoder =
            a.native_data.is_object() && b.native_data.is_object() &&
            a.native_data.value("decoder", Json{}) == b.native_data.value("decoder", Json{});
        Definitions identity;
        if (a.native_coverage == "extension" && b.native_coverage == "extension" &&
            a.native_data.contains("decoder") && a.native_data["decoder"].is_string() &&
            b.native_data.contains("decoder") && b.native_data["decoder"].is_string())
            identity = definitions(a.native_data["decoder"], b.native_data["decoder"]);
        if (na && nb && a.native_coverage == b.native_coverage && identity.compatible() &&
            (a.native_coverage != "extension" || same_decoder)) {
            if (a.native_coverage == "extension" &&
                a.native_data.value("decoder", Json{}) != b.native_data.value("decoder", Json{}))
                cut(base + "/native/decoder");
            // Callback/native models are explicit decoded data; their field names are not property tags.

        } else {
            cut(base + "/native");
            if (na || nb) {
                unknown(base + "/native", !na, !nb,
                        !identity.compatible() ? "native-definition-incompatible"
                        : na && nb             ? "native-format-incompatible"
                                               : "native-projection-unavailable");
                definition_evidence(identity);
                for (const auto *side : {"beforeDecoder", "afterDecoder"}) {
                    const auto &data = std::string(side) == "beforeDecoder" ? a.native_data : b.native_data;
                    auto id = data.find("decoder");
                    if (id != data.end() && id->is_string()) {
                        const auto &name = id->get_ref<const std::string &>();
                        if (name.size() > 256)
                            throw Error("source-coverage-identity",
                                        "Native decoder identity exceeds its budget");
                        path_cost(name.size());
                        out.unavailable.back()[side] = name;
                    }
                }
                cut(base + "/opaque");
            }
        }
        if (out.suppressed.count(base + "/opaque"))
            return;
        std::set<std::string> ranges;
        for (const auto *e : {&a, &b})
            for (auto i = e->opaque.begin(); i != e->opaque.end(); ++i)
                ranges.insert(i.key());
        for (const auto &name : ranges) {
            const auto &x = opaque(a, name.c_str()), &y = opaque(b, name.c_str());
            if (same_bytes(x, y))
                continue;
            if (name == "export" && (!ka || !kb))
                continue;
            unknown(base + "/opaque/" + token(name), true, true, "opaque-bytes-not-decoded", x, y);
            if (x.is_null() || y.is_null())
                cut(base + "/opaque/" + token(name));
        }
    }
};
} // namespace
bool source_property_tag(const Json &v) { return tag(v); }
SourceCoverage projection_coverage(const SourceCoverage &raw, const Package &a, const Package &b,
                                   const Json &left, const Json &right, const std::string &domain,
                                   const OperationControl &control) {
    SourceCoverage result;
    std::set<std::string> blocked;
    std::map<const Package *, std::map<std::string, std::size_t>> indices;
    std::map<const Package *, std::set<std::string>> scanned;
    auto ancestors = [&](const Package &p, const std::string &path) {
        if (!scanned[&p].insert(path).second)
            return;
        auto found = indices.find(&p);
        if (found == indices.end()) {
            auto &index = indices[&p];
            for (std::size_t i = 0; i < p.exports.size(); ++i) {
                if (!(i & 1023))
                    control.check();
                if (!index.emplace(p.exports[i].path, i).second)
                    throw Error("source-coverage-identity", "Duplicate projected export identity");
            }
            found = indices.find(&p);
        }
        auto entry = found->second.find(path);
        if (entry == found->second.end())
            return;
        auto i = entry->second;
        std::set<std::size_t> seen;
        for (;;) {
            control.check();
            if (!seen.insert(i).second || seen.size() > 128)
                throw Error("source-coverage-identity", "Invalid source outer chain");
            blocked.insert(p.exports[i].path);
            const auto outer = p.exports[i].outer_index;
            if (outer <= 0)
                break;
            if (static_cast<std::size_t>(outer) > p.exports.size())
                throw Error("source-coverage-identity", "Source outer exceeds export table");
            i = static_cast<std::size_t>(outer - 1);
        }
    };
    for (const auto &scope : raw.unavailable) {
        control.check();
        const auto reason = scope.at("reason").get<std::string>();
        if (reason == "opaque-bytes-not-decoded")
            continue;
        const bool native =
            reason == "native-projection-unavailable" || reason == "native-format-incompatible";
        if (native && (domain == "materials" || domain == "collisions"))
            continue;
        const auto path = scope.at("object").get<std::string>();
        blocked.insert(path);
        if (domain == "blueprint" || domain == "materials" || domain == "collisions" ||
            domain == "skeletons") {
            ancestors(a, path);
            ancestors(b, path);
        }
    }
    std::size_t nodes = 0, bytes = 0;
    auto tick = [&] {
        if (++nodes > 8'000'000)
            throw Error("source-coverage-limit", "Projected coverage budget exceeded");
        if (!(nodes & 1023))
            control.check();
    };
    auto identity = [&](const Json &value) {
        if (!value.is_object())
            return false;
        for (const auto *key : {"path", "object", "objectPath"}) {
            auto i = value.find(key);
            if (i != value.end() && i->is_string() && blocked.count(i->get<std::string>()))
                return true;
        }
        return false;
    };
    auto mask = [&](const std::string &path) {
        if (result.suppressed.insert(path).second) {
            if (path.size() + 256 > 16 * 1024 * 1024 - bytes)
                throw Error("source-coverage-limit", "Projected coverage report budget exceeded");
            bytes += path.size() + 256;
            result.unavailable.push_back(
                {{"domain", domain}, {"path", path}, {"reason", "projection-depends-on-unavailable-source"}});
        }
    };
    std::vector<std::string> collections;
    if (domain == "blueprint")
        collections = {"graphs"};
    else if (domain == "materials")
        collections = {"graphs", "instances"};
    else if (domain == "textures")
        collections = {"textures"};
    else if (domain == "meshes")
        collections = {"meshes", "sources"};
    else if (domain == "skeletons")
        collections = {"skeletons"};
    else if (domain == "collisions")
        collections = {"bodies", "owners"};
    if (!blocked.empty())
        for (const auto *model : {&left, &right})
            for (const auto &name : collections) {
                auto collection = model->find(name);
                if (collection == model->end() || !collection->is_object())
                    continue;
                for (auto i = collection->begin(); i != collection->end(); ++i) {
                    tick();
                    bool unknown = blocked.count(i.key()) || identity(i.value());
                    // Graph nodes are explicit projector-owned exports, not arbitrary reflected field names.
                    if (!unknown && (domain == "blueprint" || domain == "materials") && name == "graphs") {
                        auto children = i->find("nodes");
                        if (children != i->end() && children->is_object())
                            for (const auto &child : *children) {
                                tick();
                                if (identity(child)) {
                                    unknown = true;
                                    break;
                                }
                            }
                    }
                    if (unknown)
                        mask("/" + name + "/" + token(i.key()));
                }
            }
    if (!result.unavailable.empty()) {
        result.suppressed.insert("/complete");
        result.suppressed.insert("/diagnostics");
    }
    control.check();
    return result;
}

bool SourceCoverage::intersects(const std::string &path) const {
    if (excludes(path))
        return true;
    auto prefix = path + "/";
    auto i = suppressed.lower_bound(prefix);
    return i != suppressed.end() && i->compare(0, prefix.size(), prefix) == 0;
}
bool SourceCoverage::names_within(const std::string &path) const {
    const auto prefix = path + "/";
    for (const auto *names : {before_names, after_names}) {
        if (!names || !names->is_object())
            continue;
        const auto &entries = names->get_ref<const Json::object_t &>();
        if (entries.count(path))
            return true;
        const auto i = entries.lower_bound(prefix);
        if (i != entries.end() && i->first.compare(0, prefix.size(), prefix) == 0)
            return true;
    }
    return false;
}
bool SourceCoverage::excludes(const std::string &path) const {
    auto current = path;
    for (;;) {
        if (suppressed.count(current))
            return true;
        auto slash = current.find_last_of('/');
        if (slash == std::string::npos)
            break;
        current.resize(slash);
    }
    return false;
}
SourceCoverage source_coverage(const Package &a, const Package &b, const SourceCoverageOptions &o) {
    if (!o.max_nodes || o.max_nodes > 128'000'000 || !o.max_scopes || o.max_scopes > 1'000'000 ||
        !o.max_path_bytes || o.max_path_bytes > 256 * 1024 * 1024)
        throw Error("source-coverage-limit", "Invalid source coverage budgets");
    o.control.checkpoint("source-coverage");
    Builder r{o, a.decoder_definitions, b.decoder_definitions};
    r.before_names_ready = a.name_values.is_object() && a.name_values.value("status", "") == "complete";
    r.after_names_ready = b.name_values.is_object() && b.name_values.value("status", "") == "complete";
    std::map<std::string, const ObjectExport *> left, right;
    for (const auto &e : a.exports) {
        r.tick();
        if (!left.emplace(e.path, &e).second)
            throw Error("source-coverage-identity", "Duplicate before export identity");
    }
    for (const auto &e : b.exports) {
        r.tick();
        if (!right.emplace(e.path, &e).second)
            throw Error("source-coverage-identity", "Duplicate after export identity");
    }
    for (const auto &e : left) {
        auto i = right.find(e.first);
        if (i != right.end())
            r.exports(*e.second, *i->second);
    }
    auto names = compare_name_values(
        a.name_values, b.name_values, [&](const std::string &path) { return source_name_value(left, path); },
        [&](const std::string &path) { return source_name_value(right, path); }, "semantic-snapshot", "",
        o.control);
    for (const auto &row : names.unavailable) {
        const auto path = row.at("path").get<std::string>();
        const auto slash = path.find('/', 9);
        r.object = Json::json_pointer("/" + path.substr(9, slash - 9)).back();
        if (!r.out.excludes(path)) {
            r.cut(path);
            r.unknown(path, false, false, "stored-name-components-unavailable");
        }
    }
    for (auto &row : names.changes) {
        const auto path = row.at("path").get<std::string>();
        r.tick();
        r.path_cost(path.size());
        if (!r.out.excludes(path))
            r.out.name_changes.push_back(std::move(row));
    }
    r.out.before_names = &name_value_entries(a.name_values, "semantic-snapshot");
    r.out.after_names = &name_value_entries(b.name_values, "semantic-snapshot");
    o.control.check();
    return std::move(r.out);
}
} // namespace cyuediff::detail
