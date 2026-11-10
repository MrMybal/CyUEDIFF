#include <cyuediff/defaults.hpp>
#include <cyuediff/tagged_types.hpp>
#include "key_identity.hpp"
#include "effective_defaults_budget.hpp"
#include <set>
namespace cyuediff {
namespace {
std::string token(const std::string &text) {
    std::string out;
    for (char c : text)
        out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return out;
}
bool root_class_matches(const Package &p, const ObjectExport &object, const DefaultContext &context) {
    const auto cls = context.snapshot().provenance().at("object").at("class").get<std::string>();
    const bool class_default =
        context.source_profile().at("profile") == "ue5.3.2-win64-blueprint-tagged-ascii/1" &&
        context.source_profile().at("objectKind") == "class-default";
    if (object.outer_index != 0 || bool(object.flags & 0x10) != class_default)
        return false;
    if (!class_default)
        return object.class_name == cls;
    if (object.class_index <= 0 || static_cast<std::size_t>(object.class_index) > p.exports.size())
        return false;
    const auto &generated = p.exports[static_cast<std::size_t>(object.class_index - 1)];
    const auto local = cls.substr(cls.rfind('.') + 1);
    return object.class_name == local && generated.path == local && generated.outer_index == 0 &&
           generated.class_name == "/Script/Engine.BlueprintGeneratedClass";
}
struct Input {
    const Json *value = nullptr;
    const Json *names = nullptr;
    std::string path;
    bool source = false;
};
struct Resolver : detail::EffectiveBudget {
    const DefaultContext &context;
    EffectiveDefaults result;
    Resolver(const DefaultContext &c, const EffectiveDefaultsOptions &o) : EffectiveBudget(o), context(c) {}
    Input child(const Input &input, const Json &v, const std::string &part) {
        return {&v, input.names, path_child(input.path, part), input.source};
    }
    [[noreturn]] void unknown(const char *reason) { throw Error("default-unavailable", reason); }
    Input captured(const Json &field) {
        if (!field.value("available", false) || !field.contains("value"))
            return {};
        const Json *names = nullptr;
        const auto ids = field.find("valueIdentities");
        if (ids != field.end() && ids->is_object() && ids->contains("names"))
            names = &ids->at("names");
        return {&field.at("value"), names, "", false};
    }
    const Json &struct_fields(const Json &type) {
        const auto name = type.at("schema").get<std::string>();
        const auto it = context.snapshot().structures().find(name);
        if (it == context.snapshot().structures().end() || !it->value("available", false))
            unknown("initialized-struct-default-unavailable");
        return it->at("fields");
    }
    void validate_type(const Json &tag, const Json &type, unsigned d) {
        depth(d);
        const auto &actual = tag.contains("resolvedType") ? tag.at("resolvedType") : tag.at("type");
        std::function<void(const Json &, const Json &, unsigned)> validate = [&](const Json &a, const Json &b,
                                                                                 unsigned level) {
            depth(level);
            const auto an = a.is_string() ? a.get<std::string>() : a.at("name").get<std::string>();
            // UE5.3 FObjectPropertyBase::GetID emits ObjectProperty for FClassProperty as well.
            const bool class_wire = an == "ObjectProperty" && b.at("name") == "ClassProperty";
            if (an != b.at("name") && !class_wire)
                unknown("stored-type-does-not-match-context");
            if (an == "StructProperty") {
                const auto schema = b.at("schema").get<std::string>();
                if (a.contains("schema") && a.at("schema") != schema)
                    unknown("stored-struct-owner-does-not-match-context");
                if (a.contains("parameters") && !a.at("parameters").empty()) {
                    const auto short_name = a.at("parameters")[0].at("name").get<std::string>();
                    if (!a.contains("schema")) {
                        try {
                            if (context.snapshot().schemas().schema(short_name).path != schema)
                                unknown("stored-struct-name-does-not-match-context");
                        } catch (const Error &e) {
                            if (e.code == "cancelled" || e.code == "schema-limit")
                                throw;
                            unknown("stored-struct-name-does-not-match-context");
                        }
                    }
                } else if (!a.contains("schema"))
                    unknown("stored-struct-identity-unavailable");
                if (!a.contains("structGuid"))
                    unknown("stored-struct-guid-unavailable");
                if (a.at("structGuid") != b.at("structGuid"))
                    unknown("stored-struct-guid-does-not-match-context");
            }
            if (b.contains("arguments")) {
                if (!a.contains("parameters") || a.at("parameters").size() != b.at("arguments").size())
                    unknown("stored-container-type-unavailable");
                for (std::size_t i = 0; i < b.at("arguments").size(); ++i)
                    validate(a.at("parameters")[i], b.at("arguments")[i], level + 1);
            }
        };
        validate(actual, type, d + 1);
    }
    Json name(const Input &input) {
        if (!input.names || !input.names->is_object())
            unknown("typed-name-components-unavailable");
        const auto it = input.names->find(input.path);
        if (it == input.names->end() || !it->is_object() || !it->at("base").is_string() ||
            !it->at("number").is_number_integer())
            unknown("typed-name-components-unavailable");
        const auto n = it->at("number").get<std::int64_t>();
        if (n < 0 || n > INT32_MAX)
            unknown("invalid-typed-name-number");
        const auto &base = it->at("base").get_ref<const std::string &>();
        spend(1, base.size());
        const auto display = base + (n ? "_" + std::to_string(n - 1) : "");
        if (!input.value || *input.value != display)
            unknown("typed-name-observation-does-not-match-value");
        return {{"base", base}, {"number", n}};
    }
    std::string key_id(const Json &key, const Json &type) {
        const auto kind = type.at("name").get<std::string>();
        if (kind == "NameProperty") {
            const auto &original = key.at("base").get_ref<const std::string &>();
            spend(1, original.size());
            auto base = original;
            for (char &c : base) {
                const auto byte = static_cast<unsigned char>(c);
                if (byte >= 128)
                    unknown("name-key-equality-outside-verified-ascii-profile");
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + ('a' - 'A'));
            }
            const auto encoded = Json::array({base, key.at("number")});
            return detail::source_key_identity(
                encoded, options.control, [&](std::uint64_t n) { spend(0, n); }, false);
        }
        if (kind == "StrProperty") {
            const auto &original = key.get_ref<const std::string &>();
            spend(1, original.size());
            auto folded = original;
            for (char &c : folded) {
                if (!c || static_cast<unsigned char>(c) >= 128)
                    unknown("string-key-equality-outside-verified-ascii-profile");
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + ('a' - 'A'));
            }
            return detail::source_key_identity(
                Json(folded), options.control, [&](std::uint64_t n) { spend(0, n); }, false);
        }
        const std::set<std::string> supported = {"IntProperty",    "Int8Property",   "Int16Property",
                                                 "Int64Property",  "UInt16Property", "UInt32Property",
                                                 "UInt64Property", "ByteProperty",   "BoolProperty"};
        if (!supported.count(kind))
            unknown("key-equality-requires-an-explicit-runtime-contract");
        return detail::source_key_identity(
            key, options.control, [&](std::uint64_t n) { spend(0, n); }, false);
    }
    Json value(const Json &type, const Input *base, const Input *stored, const std::string &path, unsigned d,
               bool initialize = false) {
        depth(d);
        const auto kind = type.at("name").get<std::string>();
        const Input *selected = stored ? stored : base;
        if (!selected || !selected->value)
            unknown("default-value-unavailable");
        const auto &v = *selected->value;
        if (kind == "NameProperty")
            return name(*selected);
        if (kind == "StructProperty") {
            const auto schema = type.at("schema").get<std::string>();
            const auto &definition = context.snapshot().schemas().schema(schema);
            if (definition.struct_kind == 1) {
                if (schema.compare(0, 8, "/Script/") != 0)
                    unknown("user-defined-struct-loader-profile-required");
                if (context.snapshot().structures().at(schema).value("postSerialize", true))
                    unknown("native-post-serializer-requires-runtime-evaluation");
                if (!v.is_object() || v.contains("decoder"))
                    unknown("reflected-struct-value-unavailable");
                return fields(struct_fields(type), base, stored, path, d + 1, initialize);
            }
            // The source builtin reader and captured builtin must use the same qualified math layout.
            const std::map<std::string, std::string> layouts = {{"/Script/CoreUObject.IntPoint", "int-point"},
                                                                {"/Script/CoreUObject.Vector", "vector3-f64"},
                                                                {"/Script/CoreUObject.Quat", "quat-f64"}};
            const auto found = layouts.find(schema);
            if (found == layouts.end() || !v.is_array())
                unknown("native-struct-default-merge-unsupported");
            return copy(v, d + 1);
        }
        if (kind == "ArrayProperty") {
            if (!v.is_array())
                unknown("stored-array-value-unavailable");
            Json out = Json::array();
            const auto &element = type.at("arguments")[0];
            for (std::size_t i = 0; i < v.size(); ++i) {
                auto item = child(*selected, v[i], std::to_string(i));
                out.push_back(value(element, stored ? nullptr : &item, stored ? &item : nullptr,
                                    path + "/" + std::to_string(i), d + 1, stored != nullptr));
            }
            return out;
        }
        if (kind == "MapProperty" || kind == "SetProperty") {
            const bool map = kind == "MapProperty";
            const auto &key_type = type.at("arguments")[0];
            if (stored && !initialize && (!base || !base->value) && !v.value("replace", false))
                unknown("container-base-default-unavailable");
            Json entries = Json::object();
            std::map<std::string, Input> initial_values;
            auto apply = [&](const Input &input, bool initial) {
                const auto &delta = *input.value;
                if (!delta.is_object() ||
                    delta.value("kind", "") != (map ? "stored-map-delta" : "stored-set-delta") ||
                    !delta.at("entries").is_object() || !delta.at("removed").is_object())
                    unknown("stored-container-value-unavailable");
                if (initial && !delta.at("removed").empty())
                    unknown("captured-container-is-not-a-complete-value");
                if (delta.value("replace", false))
                    entries = Json::object();
                if (!initial)
                    for (auto it = delta.at("removed").begin(); it != delta.at("removed").end(); ++it) {
                        auto key = child(child(input, delta.at("removed"), "removed"), it.value(), it.key());
                        auto canonical = value(key_type, initial ? &key : nullptr, initial ? nullptr : &key,
                                               path, d + 1, true);
                        const auto id = key_id(canonical, key_type);
                        entries.erase(id);
                        initial_values.erase(id);
                    }
                std::set<std::string> seen;
                for (auto it = delta.at("entries").begin(); it != delta.at("entries").end(); ++it) {
                    auto item = child(child(input, delta.at("entries"), "entries"), it.value(), it.key());
                    auto key = map ? child(item, it->at("key"), "key") : item;
                    auto canonical = value(key_type, initial ? &key : nullptr, initial ? nullptr : &key, path,
                                           d + 1, true);
                    auto id = key_id(canonical, key_type);
                    if (!seen.insert(id).second)
                        unknown("duplicate-runtime-container-key");
                    if (map) {
                        auto mapped = child(item, it->at("value"), "value");
                        const auto existing = initial_values.find(id);
                        // Loading into a matched map entry retains its initialized storage; it does not
                        // supply that storage as comparison defaults. Nested containers still receive none.
                        const Input *initial_data = initial                            ? &mapped
                                                    : existing == initial_values.end() ? nullptr
                                                                                       : &existing->second;
                        auto resolved =
                            value(type.at("arguments")[1], initial_data, initial ? nullptr : &mapped,
                                  path + "/entries/" + token(id) + "/value", d + 1, true);
                        if (initial) {
                            spend(1, id.size());
                            initial_values.emplace(id, std::move(mapped));
                        }
                        // FindOrAdd updates a matched map value without replacing its existing key.
                        if (entries.contains(id))
                            canonical = copy(entries.at(id).at("key"), d + 1);
                        entries[id] = {{"key", std::move(canonical)}, {"value", std::move(resolved)}};
                    } else if (!entries.contains(id))
                        entries[id] = std::move(canonical);
                }
            };
            if (base && base->value && !(stored && initialize))
                apply(*base, true);
            if (stored)
                apply(*stored, false);
            return {{"kind", map ? "effective-map" : "effective-set"}, {"entries", std::move(entries)}};
        }
        if (kind == "ObjectProperty" || kind == "ClassProperty") {
            if (v.is_null() || v == "" || v == "None")
                return nullptr;
            if (!v.is_string())
                unknown("object-reference-unavailable");
            const auto &target = v.get_ref<const std::string &>();
            const auto cdo = context.snapshot().provenance().at("object").at("path").get<std::string>();
            if (target.compare(0, cdo.size(), cdo) == 0)
                unknown("default-subobject-reference-needs-rebasing");
            return copy(v, d + 1);
        }
        const std::set<std::string> scalars = {
            "BoolProperty",   "ByteProperty",   "IntProperty",    "Int8Property",   "Int16Property",
            "Int64Property",  "UInt16Property", "UInt32Property", "UInt64Property", "FloatProperty",
            "DoubleProperty", "StrProperty",    "TextProperty"};
        if (!scalars.count(kind))
            unknown("property-default-resolution-unsupported");
        if (kind == "BoolProperty" && !v.is_boolean())
            unknown("invalid-effective-boolean");
        if (kind == "StrProperty" && !v.is_string())
            unknown("invalid-effective-string");
        if (kind == "TextProperty" && !v.is_object())
            unknown("invalid-effective-text");
        if ((kind == "FloatProperty" || kind == "DoubleProperty") && !v.is_number())
            unknown("invalid-effective-number");
        if (kind.find("Int") != std::string::npos || kind == "ByteProperty") {
            if (!v.is_number_integer())
                unknown("invalid-effective-integer");
            const bool unsigned_kind = kind.compare(0, 4, "UInt") == 0 || kind == "ByteProperty";
            const unsigned width = kind == "Int8Property" || kind == "ByteProperty"      ? 8
                                   : kind == "Int16Property" || kind == "UInt16Property" ? 16
                                   : kind == "Int64Property" || kind == "UInt64Property" ? 64
                                                                                         : 32;
            if (unsigned_kind) {
                if (!v.is_number_unsigned() && v.get<std::int64_t>() < 0)
                    unknown("invalid-effective-unsigned-integer");
                if (width < 64 && v.get<std::uint64_t>() > ((1ull << width) - 1))
                    unknown("effective-integer-out-of-range");
            } else {
                if (v.is_number_unsigned() && v.get<std::uint64_t>() > static_cast<std::uint64_t>(INT64_MAX))
                    unknown("effective-integer-out-of-range");
                const auto number = v.get<std::int64_t>();
                if (width < 64 && (number < -(1ll << (width - 1)) || number > ((1ll << (width - 1)) - 1)))
                    unknown("effective-integer-out-of-range");
            }
        }
        return copy(v, d + 1);
    }
    Json fields(const Json &metadata, const Input *base, const Input *stored, const std::string &path,
                unsigned d, bool initialize = false, bool captured_root = false) {
        depth(d);
        Json out = Json::object();
        std::set<std::string> consumed;
        for (auto it = metadata.begin(); it != metadata.end(); ++it) {
            spend();
            const auto &meta = it.value();
            const auto &name = meta.at("name").get_ref<const std::string &>();
            const auto suffix = meta.at("arrayDim").get<unsigned>() > 1
                                    ? "[" + std::to_string(meta.at("arrayIndex").get<unsigned>()) + "]"
                                    : std::string{};
            spend(0, it.key().size() + name.size() + suffix.size() + path.size() + 64);
            const auto short_name = name + suffix;
            const auto location = path_child(path, it.key());
            consumed.insert(short_name);
            Json field = {{"type", copy(meta.at("type"), d + 1)}, {"available", false}, {"complete", false}};
            try {
                const auto flags = std::stoull(meta.at("flags").get<std::string>(), nullptr, 16);
                if (flags & (0x80000ull | 0x8000000000ull))
                    unknown("instanced-default-reference-needs-rebasing");
                if (meta.at("type").at("name") == "StructProperty") {
                    const auto schema = meta.at("type").at("schema").get<std::string>();
                    if (context.snapshot().schemas().schema(schema).struct_kind == 2) {
                        const std::map<std::string, std::string> layouts = {
                            {"/Script/CoreUObject.IntPoint", "int-point"},
                            {"/Script/CoreUObject.Vector", "vector3-f64"},
                            {"/Script/CoreUObject.Quat", "quat-f64"}};
                        const auto layout = layouts.find(schema);
                        if (layout == layouts.end() || !meta.contains("interpretation") ||
                            meta.at("interpretation").at("type").value("route", "") != "builtin" ||
                            meta.at("interpretation").at("type").value("layout", "") != layout->second)
                            unknown("captured-native-layout-does-not-match-source-profile");
                    }
                }
                Input inherited;
                bool have_base = false;
                if (captured_root) {
                    inherited = captured(meta);
                    have_base = inherited.value != nullptr;
                } else if (base && base->value && base->value->is_object() &&
                           base->value->contains(it.key())) {
                    const auto &wrapper = base->value->at(it.key());
                    if (wrapper.contains("value")) {
                        inherited = child(child(*base, wrapper, it.key()), wrapper.at("value"), "value");
                        have_base = true;
                    }
                } else if (initialize) {
                    inherited = captured(meta);
                    have_base = inherited.value != nullptr;
                }
                Input override;
                const Json *tag = nullptr;
                if (stored && stored->value && stored->value->is_object()) {
                    auto candidate = stored->value->find(short_name);
                    if (candidate != stored->value->end()) {
                        consumed.insert(short_name);
                        tag = &*candidate;
                        if (!tag->is_object() || !tag->contains("value") || tag->contains("opaque") ||
                            (tag->contains("serializationFlags") &&
                             tag->at("serializationFlags").is_number_integer() &&
                             (tag->at("serializationFlags").get<std::uint64_t>() & 32)))
                            unknown("stored-property-unavailable");
                        if (meta.contains("propertyGuid") &&
                            meta.at("propertyGuid") != std::string(32, '0') &&
                            (!tag->contains("guid") || tag->at("guid") != meta.at("propertyGuid")))
                            unknown("stored-blueprint-property-guid-does-not-match");
                        validate_type(*tag, meta.at("type"), d + 1);
                        override = child(child(*stored, *tag, short_name), tag->at("value"), "value");
                    }
                }
                const auto unavailable_before = result.unavailable.size();
                field["value"] = value(meta.at("type"), have_base ? &inherited : nullptr,
                                       tag ? &override : nullptr, location + "/value", d + 1, initialize);
                field["complete"] = result.unavailable.size() == unavailable_before;
                field["available"] = true;
                field["origin"] = tag          ? "stored-override"
                                  : initialize ? "initialized-type-default"
                                               : "captured-default";
            } catch (const Error &e) {
                if (e.code != "default-unavailable")
                    throw;
                field["reason"] = e.what();
                spend(1, location.size() + std::string(e.what()).size());
                result.unavailable.push_back({{"path", location}, {"reason", e.what()}});
            } catch (const Json::exception &) {
                field["reason"] = "invalid-stored-value-shape";
                spend(1, location.size() + 64);
                result.unavailable.push_back({{"path", location}, {"reason", "invalid-stored-value-shape"}});
            }
            out[it.key()] = std::move(field);
        }
        if (stored && stored->value && stored->value->is_object())
            for (auto it = stored->value->begin(); it != stored->value->end(); ++it)
                if (!consumed.count(it.key())) {
                    spend(1, path.size() + it.key().size() + 64);
                    result.unavailable.push_back({{"path", path + "/" + token(it.key())},
                                                  {"reason", "stored-field-not-in-default-context"}});
                }
        return out;
    }
};
} // namespace
std::vector<std::string> default_root_exports(const Package &p, const DefaultContext &context,
                                              const OperationControl &control) {
    std::vector<std::string> roots;
    for (const auto &object : p.exports) {
        control.check();
        if (root_class_matches(p, object, context))
            roots.push_back(object.path);
    }
    return roots;
}
Json EffectiveDefaults::to_json() const {
    auto j = provenance;
    j["schema"] = "cyuediff.effective-defaults/1";
    j["scope"] = "effective-reflected-properties";
    j["complete"] = complete;
    j["fields"] = fields;
    j["unavailable"] = unavailable;
    return j;
}
EffectiveDefaults resolve_source_defaults(const Package &package, const std::string &export_path,
                                          const DefaultContext &context,
                                          const EffectiveDefaultsOptions &options) {
    options.control.check();
    const auto &profile = context.source_profile();
    const auto &snapshot = context.snapshot();
    if (!package.complete_input || package.source_read_limit_failed ||
        package.summary.ue4_version != profile.at("ue4") ||
        package.summary.ue5_version != profile.at("ue5") ||
        package.summary.licensee_version != profile.at("licensee") ||
        package.summary.saved_engine != profile.at("savedEngine") ||
        package.summary.custom_versions != profile.at("customVersions") ||
        bool(package.summary.flags & 0x80000000u) !=
            snapshot.provenance().at("archive").at("filterEditorOnly").get<bool>())
        throw Error("default-context", "Source archive does not match prepared default context");
    const ObjectExport *object = nullptr;
    for (const auto &e : package.exports)
        if (e.path == export_path) {
            if (object)
                throw Error("default-context", "Ambiguous source export identity");
            object = &e;
        }
    const bool blueprint = profile.at("profile") == "ue5.3.2-win64-blueprint-tagged-ascii/1";
    const bool class_default = blueprint && profile.at("objectKind") == "class-default";
    const auto &binding = context.provenance();
    const auto expected_template =
        blueprint ? binding.at("blueprintBinding").at(class_default ? "parentDefault" : "sourceDefault")
                  : snapshot.provenance().at("object").at("path");
    if (!object || !root_class_matches(package, *object, context) || object->outer_index != 0 ||
        bool(object->flags & 0x10) != class_default ||
        (object->template_index != 0 && package.reference(object->template_index) != expected_template) ||
        (object->coverage != "properties" && object->coverage != "partial-properties"))
        throw Error("default-context",
                    "Root source class/template/property inventory does not match default context");
    if (class_default) {
        const auto &bp = binding.at("blueprintBinding");
        const auto cls = snapshot.provenance().at("object").at("class").get<std::string>();
        if (object->path != "Default__" + cls.substr(cls.rfind('.') + 1) ||
            !package.fingerprints.contains("package") ||
            package.fingerprints.at("package").at("sha256") != bp.at("packageSha256"))
            throw Error(
                "default-context",
                "Blueprint CDO source bytes or export identity do not match the prepared factory context");
        // Companions belong to the same captured package epoch as its header.
        const auto id = "package:" + cls.substr(0, cls.rfind('.'));
        for (const char *ext : {"uexp", "ubulk", "uptnl", "m.ubulk", "o.ubulk", "upayload"}) {
            const auto &dep = binding.at("dependencies").at(id + ":" + ext);
            const auto file = package.fingerprints.find(std::string(".") + ext);
            if (dep.contains("absent")
                    ? file != package.fingerprints.end()
                    : (file == package.fingerprints.end() || file->at("sha256") != dep.at("sha256") ||
                       file->at("bytes") != dep.at("bytes")))
                throw Error("default-context",
                            "Blueprint CDO companion bytes differ from the prepared factory epoch");
        }
    }
    Resolver resolver{context, options};
    const Json *source_names = nullptr;
    if (package.value_identities.is_object() && package.value_identities.contains("names"))
        source_names = &package.value_identities.at("names");
    Input stored{&object->properties, source_names,
                 resolver.path_child(resolver.path_child("/objects", object->path), "properties"), true};
    resolver.result.fields = resolver.fields(snapshot.fields(), nullptr, &stored, "/fields", 0, false, true);
    resolver.result.complete = resolver.result.unavailable.empty();
    resolver.result.provenance = {{"defaultsApplied", true},
                                  {"binding", context.provenance().at("binding")},
                                  {"contextSha256", context.sha256()},
                                  {"captureSha256", snapshot.sha256()},
                                  {"object", object->path},
                                  {"class", snapshot.provenance().at("object").at("class")},
                                  {"keyEquality", "ue5.3-windows-ascii-keys/1"},
                                  {"workNodes", resolver.nodes},
                                  {"textBytes", resolver.text}};
    options.control.check();
    return std::move(resolver.result);
}
} // namespace cyuediff
