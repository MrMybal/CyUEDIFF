#include <cyuediff/tagged_types.hpp>
#include <cyuediff/schema.hpp>
#include "struct_identity.hpp"
#include <cyuediff/string_encoding.hpp>
#include <set>
#include <cmath>
namespace cyuediff {
namespace {
void need(bool ok, const char *why) {
    if (!ok)
        throw Error("tagged-types", why);
}
void validate_options(const TaggedTypesOptions &o) {
    need(o.max_nodes && o.max_nodes <= 4'000'000 && o.max_text_bytes &&
             o.max_text_bytes <= 256 * 1024 * 1024 && o.max_depth && o.max_depth <= 128,
         "Invalid metadata budgets");
}
std::string short_name(const std::string &s) {
    const auto at = s.find_last_of("./");
    return at == std::string::npos ? s : s.substr(at + 1);
}
bool identity(const std::string &a, const std::string &b) {
    return a == b || (a.find_first_of("./") == std::string::npos && a == short_name(b)) ||
           (b.find_first_of("./") == std::string::npos && b == short_name(a));
}
void fields(const Json &v, std::initializer_list<const char *> keys) {
    need(v.is_object(), "Expected metadata object");
    for (auto it = v.begin(); it != v.end(); ++it) {
        bool ok = false;
        for (auto k : keys)
            ok |= it.key() == k;
        need(ok, "Unknown tagged type metadata field");
    }
}
std::string text(const Json &v) {
    need(v.is_string(), "Expected metadata identity");
    auto s = v.get<std::string>();
    need(!s.empty() && s.size() <= 4096 && s.find('\0') == std::string::npos, "Invalid metadata identity");
    return s;
}
VersionRange range(const Json &v) {
    need(v.is_array() && v.size() == 2, "Expected archive interval");
    for (const auto &x : v)
        need(x.is_number_integer() && x >= INT32_MIN && x <= INT32_MAX, "Invalid archive interval endpoint");
    VersionRange r{v[0].get<int>(), v[1].get<int>()};
    need(r.min <= r.max, "Inverted metadata archive interval");
    return r;
}
SerializationProfile profile(const Json &v) {
    fields(v, {"ue4", "ue5", "customVersions"});
    SerializationProfile p;
    p.ue4 = range(v.at("ue4"));
    p.ue5 = range(v.at("ue5"));
    if (v.contains("customVersions")) {
        need(v["customVersions"].is_object(), "Expected custom version intervals");
        for (auto it = v["customVersions"].begin(); it != v["customVersions"].end(); ++it) {
            need(it.key().size() == 32 && it.key().find_first_not_of("0123456789abcdef") == std::string::npos,
                 "Invalid metadata custom version GUID");
            p.custom_versions.emplace(it.key(), range(it.value()));
        }
    }
    return p;
}
Json profile_json(const SerializationProfile &p) {
    Json j = {{"ue4", {p.ue4.min, p.ue4.max}}, {"ue5", {p.ue5.min, p.ue5.max}}};
    if (!p.custom_versions.empty()) {
        j["customVersions"] = Json::object();
        for (const auto &x : p.custom_versions)
            j["customVersions"][x.first] = {x.second.min, x.second.max};
    }
    return j;
}
const char *kinds[] = {"ByteProperty",        "BoolProperty",
                       "IntProperty",         "FloatProperty",
                       "ObjectProperty",      "NameProperty",
                       "DelegateProperty",    "DoubleProperty",
                       "ArrayProperty",       "StructProperty",
                       "StrProperty",         "TextProperty",
                       "InterfaceProperty",   "MulticastDelegateProperty",
                       "WeakObjectProperty",  "LazyObjectProperty",
                       "AssetObjectProperty", "SoftObjectProperty",
                       "UInt64Property",      "UInt32Property",
                       "UInt16Property",      "Int64Property",
                       "Int16Property",       "Int8Property",
                       "MapProperty",         "SetProperty",
                       "EnumProperty",        "FieldPathProperty",
                       "OptionalProperty",    "Utf8StrProperty",
                       "AnsiStrProperty"};
struct Budget {
    const TaggedTypesOptions &o;
    std::size_t nodes = 0, bytes = 0;
    void visit(const Json &j, unsigned depth = 0) {
        o.control.check();
        if (depth > o.max_depth || nodes++ >= o.max_nodes)
            throw Error("tagged-types-limit", "Metadata work/depth budget exceeded");
        auto add = [&](const std::string &s) {
            if (s.size() > o.max_text_bytes - bytes)
                throw Error("tagged-types-limit", "Metadata text budget exceeded");
            decode_narrow_string(s, NarrowStringEncoding::utf8, o.max_text_bytes - bytes);
            bytes += s.size();
        };
        if (j.is_string())
            add(j.get_ref<const std::string &>());
        else if (j.is_structured())
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (j.is_object())
                    add(it.key());
                visit(it.value(), depth + 1);
            }
        else
            need(!j.is_binary() && !j.is_discarded() &&
                     (!j.is_number_float() || std::isfinite(j.get<double>())),
                 "Expected ordinary metadata JSON");
    }
};
TaggedType parse_type(const Json &j, unsigned depth, const TaggedTypesOptions &o, bool symbol = false) {
    if (depth > o.max_depth)
        throw Error("tagged-types-limit", "Metadata type nesting exceeded");
    fields(j, {"name", "parameters", "schema", "structGuid"});
    TaggedType t;
    t.name = text(j.at("name"));
    if (j.contains("structGuid")) {
        t.struct_guid = text(j["structGuid"]);
        need(t.name == "StructProperty" && detail::canonical_struct_guid(t.struct_guid),
             "Expected a canonical struct-property GUID");
    }
    if (j.contains("schema"))
        t.schema = text(j["schema"]);
    if (j.contains("parameters")) {
        need(j["parameters"].is_array(), "Expected type parameters");
        for (const auto &v : j["parameters"])
            t.parameters.push_back(parse_type(v, depth + 1, o,
                                              t.name == "StructProperty" || t.name == "EnumProperty" ||
                                                  t.name == "ByteProperty"));
    }
    if (symbol) {
        need(t.parameters.empty() && t.schema.empty() && t.struct_guid.empty(),
             "Metadata identity cannot contain a wire type");
        return t;
    }
    bool known = false;
    for (auto k : kinds)
        known |= t.name == k;
    need(known, "Unknown metadata property kind");
    const auto n = t.parameters.size();
    if (t.name == "MapProperty")
        need(n == 2, "Map metadata requires key and value types");
    else if (t.name == "ArrayProperty" || t.name == "SetProperty" || t.name == "OptionalProperty" ||
             t.name == "StructProperty")
        need(n == 1, "Metadata requires one type parameter");
    else if (t.name == "EnumProperty")
        need(n == 1, "Enum metadata requires one identity");
    else if (t.name == "ByteProperty")
        need(n <= 1, "Byte metadata has at most one enum identity");
    else
        need(n == 0, "Scalar metadata cannot contain type parameters");
    need(t.schema.empty() || t.name == "StructProperty", "Only struct types have metadata owner identities");
    if (!t.schema.empty())
        need(identity(t.parameters[0].name, t.schema),
             "Struct metadata identity disagrees with its tag name");
    if (t.name == "StructProperty" && t.schema.empty())
        t.schema = t.parameters[0].name;
    return t;
}
} // namespace
struct TaggedTypes::Impl {
    struct Property {
        TaggedType type;
        std::uint32_t count = 1;
    };
    struct Schema {
        std::string name, super;
        bool native = false;
        TaggedSerialization serialization = TaggedSerialization::unspecified;
        std::string struct_guid;
        std::map<std::string, Property> own, all;
    };
    Json document;
    SerializationProfile profile;
    std::string hash;
    std::map<std::string, Schema> schemas;
    std::map<std::string, std::vector<std::string>> aliases;
    const Schema *find(const std::string &owner) const {
        auto a = aliases.find(owner);
        if (a != aliases.end() && a->second.size() > 1)
            throw Error("tagged-types-ambiguous", "Use a qualified source type identity: " + owner);
        auto exact = schemas.find(owner);
        if (exact != schemas.end())
            return &exact->second;
        if (a != aliases.end())
            return &schemas.at(a->second.front());
        return nullptr;
    }
};
TaggedTypes::TaggedTypes(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
TaggedTypes TaggedTypes::parse(const Json &j, const TaggedTypesOptions &o) {
    try {
        validate_options(o);
        Budget{o}.visit(j);
        fields(j, {"format", "profile", "schemas", "usmapSha256"});
        need(j.at("format") == "cyuediff.tagged-types/1", "Unsupported tagged type context");
        if (j.contains("usmapSha256")) {
            auto h = text(j["usmapSha256"]);
            need(h.size() == 64 && h.find_first_not_of("0123456789abcdef") == std::string::npos,
                 "Invalid metadata mapping fingerprint");
        }
        auto p = std::make_shared<Impl>();
        p->document = j;
        p->profile = profile(j.at("profile"));
        need(j.at("schemas").is_object(), "Expected explicit source schemas");
        for (auto it = j["schemas"].begin(); it != j["schemas"].end(); ++it) {
            o.control.check();
            text(it.key());
            fields(it.value(), {"super", "native", "properties", "serialization", "structGuid"});
            Impl::Schema s;
            s.name = it.key();
            if (it.value().contains("super") && !it.value()["super"].get_ref<const std::string &>().empty())
                s.super = text(it.value()["super"]);
            if (it.value().contains("native")) {
                need(it.value()["native"].is_boolean(), "Expected native serializer metadata flag");
                s.native = it.value()["native"].get<bool>();
            }
            if (it.value().contains("serialization")) {
                const auto mode = text(it.value()["serialization"]);
                need(mode == "tagged" || mode == "native", "Unsupported struct serialization certificate");
                s.serialization =
                    mode == "tagged" ? TaggedSerialization::tagged : TaggedSerialization::native;
            }
            if (it.value().contains("structGuid")) {
                s.struct_guid = text(it.value()["structGuid"]);
                need(detail::canonical_struct_guid(s.struct_guid), "Invalid source schema struct GUID");
            }
            const auto &props = it.value().at("properties");
            need(props.is_object(), "Expected named metadata properties");
            for (auto f = props.begin(); f != props.end(); ++f) {
                text(f.key());
                fields(f.value(), {"type", "arraySize"});
                Impl::Property prop;
                prop.type = parse_type(f.value().at("type"), 0, o);
                if (f.value().contains("arraySize")) {
                    auto n = f.value()["arraySize"];
                    need(n.is_number_integer() && n >= 1 && n <= 65535, "Invalid static array dimension");
                    prop.count = n.get<std::uint32_t>();
                }
                s.own.emplace(f.key(), std::move(prop));
            }
            p->schemas.emplace(it.key(), std::move(s));
            p->aliases[short_name(it.key())].push_back(it.key());
        }
        std::size_t inherited_work = 0;
        std::set<std::string> visiting, done;
        std::function<void(const std::string &)> build = [&](const std::string &name) {
            o.control.check();
            if (done.count(name))
                return;
            need(visiting.insert(name).second, "Cyclic source metadata inheritance");
            if (visiting.size() > o.max_depth)
                throw Error("tagged-types-limit", "Metadata inheritance exceeds depth budget");
            auto &s = p->schemas.at(name);
            s.all = s.own;
            if (!s.super.empty()) {
                const auto *parent = p->find(s.super);
                need(parent != nullptr, "Missing source metadata parent");
                build(parent->name);
                for (const auto &f : parent->all) {
                    if (++inherited_work > o.max_nodes)
                        throw Error("tagged-types-limit", "Inherited metadata exceeds work budget");
                    need(s.all.emplace(f).second, "Ambiguous inherited source property");
                }
            }
            visiting.erase(name);
            done.insert(name);
        };
        for (const auto &s : p->schemas)
            build(s.first);
        auto canonical = j.dump();
        p->hash = sha256_bytes(std::vector<std::uint8_t>(canonical.begin(), canonical.end()), o.control);
        return TaggedTypes(std::move(p));
    } catch (const Json::exception &) {
        throw Error("tagged-types", "Invalid or missing source metadata field");
    }
}
bool TaggedTypes::matches(const PackageSummary &s) const {
    return impl_->profile.matches(s);
}
const TaggedType *TaggedTypes::property(const std::string &owner, const std::string &field,
                                        std::uint32_t index, const PackageSummary &s) const {
    const auto *type = impl_->find(owner);
    if (!type)
        return nullptr;
    if (!matches(s))
        throw Error("tagged-types-profile", "Source metadata archive profile disagrees");
    auto it = type->all.find(field);
    if (it == type->all.end())
        return nullptr;
    if (index >= it->second.count)
        throw Error("tagged-types-index", "Stored static-array index exceeds its declared dimension");
    return &it->second.type;
}
bool TaggedTypes::native(const std::string &owner) const {
    const auto *s = impl_->find(owner);
    return s && s->native;
}
TaggedSerialization TaggedTypes::serialization(const std::string &owner, const std::string &guid,
                                               const PackageSummary &summary) const {
    const auto *s = impl_->find(owner);
    if (!s)
        return TaggedSerialization::unspecified;
    if (!matches(summary))
        throw Error("tagged-types-profile", "Source serialization profile disagrees");
    if (!s->struct_guid.empty() && !guid.empty() && s->struct_guid != guid)
        throw Error("tagged-types-guid", "Stored struct GUID contradicts its source context");
    if (s->serialization != TaggedSerialization::unspecified)
        return s->serialization;
    return s->native ? TaggedSerialization::native : TaggedSerialization::unspecified;
}
const std::string &TaggedTypes::sha256() const {
    return impl_->hash;
}
Json TaggedTypes::to_json() const {
    return impl_->document;
}
Json tagged_type_json(const TaggedType &t, bool metadata) {
    Json j = {{"name", t.name}};
    if (!t.parameters.empty()) {
        j["parameters"] = Json::array();
        for (const auto &p : t.parameters)
            j["parameters"].push_back(tagged_type_json(p, metadata));
    }
    if (!t.struct_guid.empty())
        j["structGuid"] = t.struct_guid;
    if (metadata && !t.schema.empty())
        j["schema"] = t.schema;
    return j;
}
TaggedType TaggedTypes::complete(const TaggedType &wire, const TaggedType &decl) {
    if (wire.name != decl.name) {
        if (wire.name == "ByteProperty" && decl.name == "EnumProperty") {
            TaggedType converted = decl;
            converted.name = "ByteProperty";
            converted.parameters.resize(1);
            return complete(wire, converted);
        }
        throw Error("tagged-types-conflict", "Stored property kind contradicts metadata");
    }
    auto out = wire;
    if (wire.name == "StructProperty") {
        if (!wire.struct_guid.empty() && !decl.struct_guid.empty() && wire.struct_guid != decl.struct_guid)
            throw Error("tagged-types-conflict", "Stored struct GUID contradicts declared type");
        if (out.struct_guid.empty())
            out.struct_guid = decl.struct_guid;
    }
    if (wire.name == "StructProperty" || wire.name == "EnumProperty" || wire.name == "ByteProperty") {
        if (!wire.parameters.empty() && !decl.parameters.empty() &&
            !identity(wire.parameters[0].name, decl.parameters[0].name))
            throw Error("tagged-types-conflict", "Stored struct/enum identity contradicts metadata");
        if (!wire.parameters.empty() && !wire.parameters[0].parameters.empty()) {
            std::string qualified;
            for (const auto &outer : wire.parameters[0].parameters) {
                if (!outer.parameters.empty())
                    throw Error("tagged-types-conflict", "Unsupported stored type owner qualifiers");
                if (!qualified.empty())
                    qualified += '.';
                qualified += outer.name;
            }
            qualified += '.' + wire.parameters[0].name;
            const auto expected = wire.name == "StructProperty" ? decl.schema
                                  : decl.parameters.empty()     ? std::string{}
                                                                : decl.parameters[0].name;
            if (expected.find_first_of("./") != std::string::npos && qualified != expected)
                throw Error("tagged-types-conflict", "Stored type package contradicts metadata");
        }
        if (wire.parameters.empty())
            out.parameters = decl.parameters;
        // Full-type identity nodes may carry package/GUID qualifiers. Do not erase stored qualifiers.
        if (wire.name == "StructProperty")
            out.schema = decl.schema;
        return out;
    }
    if (wire.parameters.size() > decl.parameters.size())
        throw Error("tagged-types-conflict", "Stored type has incompatible parameters");
    for (std::size_t i = 0; i < decl.parameters.size(); ++i) {
        if (i < wire.parameters.size())
            out.parameters[i] = complete(wire.parameters[i], decl.parameters[i]);
        else
            out.parameters.push_back(decl.parameters[i]);
    }
    return out;
}
TaggedTypes TaggedTypes::from_schemas(const UnversionedSchemas &schemas,
                                      const std::vector<std::string> &roots, const SerializationProfile &p,
                                      const TaggedTypesOptions &o) {
    validate_options(o);
    need(!roots.empty() && roots.size() <= o.max_nodes, "Select explicit source metadata roots");
    Json j = {{"format", "cyuediff.tagged-types/1"},
              {"profile", profile_json(p)},
              {"usmapSha256", schemas.sha256()},
              {"schemas", Json::object()}};
    std::set<std::string> done;
    std::size_t work = 0;
    unsigned converting_depth = 0;
    std::function<void(const std::string &)> add;
    std::function<TaggedType(const SchemaType &, unsigned)> convert = [&](const SchemaType &s,
                                                                          unsigned depth) {
        o.control.check();
        if (++work > o.max_nodes || depth > o.max_depth)
            throw Error("tagged-types-limit", "Source metadata conversion exceeds its budget");
        need(s.kind < 31, "Unsupported mapping property kind");
        TaggedType t;
        t.name = kinds[s.kind];
        if (s.kind == 9) {
            const auto &target = schemas.schema(s.name);
            t.parameters.push_back({target.name, {}, {}});
            t.schema = target.path;
            add(target.path);
        } else if (s.kind == 26) {
            const auto &e = schemas.enumeration(s.name);
            t.parameters.push_back({e.path, {}, {}});
        } else
            for (const auto &a : s.arguments)
                t.parameters.push_back(convert(a, depth + 1));
        return t;
    };
    add = [&](const std::string &name) {
        o.control.check();
        const auto &s = schemas.schema(name);
        if (!done.insert(s.path).second)
            return;
        if (++converting_depth > o.max_depth)
            throw Error("tagged-types-limit", "Source metadata dependency depth exceeded");
        if (++work > o.max_nodes)
            throw Error("tagged-types-limit", "Too many source metadata schemas");
        Json fields = Json::object();
        if (!s.super.empty())
            add(s.super);
        for (const auto &entry : s.properties) {
            const auto &f = entry.second;
            if (entry.first != f.index)
                continue;
            need(!fields.contains(f.name), "Duplicate mapped source property identity");
            fields[f.name] = {{"arraySize", f.array_size}, {"type", tagged_type_json(convert(f.type, 0))}};
        }
        j["schemas"][s.path] = {{"super", s.super},
                                {"native", s.attributes_present && s.struct_kind == 2},
                                {"properties", std::move(fields)}};
        --converting_depth;
    };
    for (const auto &root : roots)
        add(root);
    return parse(j, o);
}
} // namespace cyuediff
