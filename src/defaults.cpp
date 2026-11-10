#include <cyuediff/defaults.hpp>
#include "string_value.hpp"
#include <algorithm>
#include <fstream>
#include <set>
namespace cyuediff {
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw Error("default-capture", message);
}
std::uint64_t unsigned_value(const Json &value, std::uint64_t maximum) {
    require(value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>() >= 0) &&
                value.get<std::uint64_t>() <= maximum,
            "Invalid capture unsigned integer");
    return value.get<std::uint64_t>();
}
std::int32_t signed_value(const Json &value) {
    require(value.is_number_integer() &&
                (value.is_number_unsigned()
                     ? value.get<std::uint64_t>() <= INT32_MAX
                     : value.get<std::int64_t>() >= INT32_MIN && value.get<std::int64_t>() <= INT32_MAX),
            "Invalid capture int32");
    return value.get<std::int32_t>();
}
std::string word(const Json &value) {
    const auto result = value.get<std::string>();
    require(!result.empty() && result.find('\0') == std::string::npos, "Invalid capture identity");
    return result;
}
bool hex_string(const std::string &value, std::size_t length) {
    return value.size() == length && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
void object_reference(const Json &value) {
    require(value.is_object(), "Expected captured object identity");
    const auto path = word(value.at("path")), cls = word(value.at("class"));
    require(path.front() == '/' && cls.front() == '/' && hex_string(value.at("flags").get<std::string>(), 16),
            "Invalid captured object identity");
}
Json archive_profile(const Json &wire) {
    require(wire.at("format") == "cyuediff.engine-field-wire/1" && wire.at("available") == true &&
                wire.at("unversioned") == true && wire.at("isCooking") == false &&
                wire.at("forceUnicode") == true && wire.at("comparisonDefaults") == "none" &&
                wire.at("filterEditorOnly").is_boolean(),
            "Unsupported default field archive");
    signed_value(wire.at("ue4"));
    signed_value(wire.at("ue5"));
    const auto &versions = wire.at("customVersions");
    require(versions.is_array(), "Expected captured custom versions");
    std::set<std::string> guids;
    for (const auto &version : versions) {
        const auto guid = version.at("guid").get<std::string>();
        require(hex_string(guid, 32) && guids.insert(guid).second, "Invalid captured custom version GUID");
        signed_value(version.at("version"));
    }
    return {{"format", wire.at("format")},
            {"unversioned", true},
            {"filterEditorOnly", wire.at("filterEditorOnly")},
            {"isCooking", false},
            {"forceUnicode", true},
            {"comparisonDefaults", "none"},
            {"ue4", wire.at("ue4")},
            {"ue5", wire.at("ue5")},
            {"customVersions", versions}};
}
struct CaptureReader {
    const DefaultSnapshotReadOptions &options;
    const Json &capture;
    const UnversionedSchemas &schemas;
    Json profile;
    UnversionedReadOptions read_options;
    std::uint64_t wire_bytes = 0, text_bytes = 0;
    std::uint32_t values_read = 0;
    std::uint32_t identity_names = 0, functional_names = 0;
    std::uint64_t functional_text = 0;
    std::uint64_t identity_text = 0;
    CaptureReader(const DefaultSnapshotReadOptions &opts, const Json &document,
                  const UnversionedSchemas &registry)
        : options(opts), capture(document), schemas(registry),
          profile(archive_profile(document.at("archive"))), read_options(opts.values) {
        auto validate_bindings = [&](const auto &bindings) {
            for (const auto &binding : bindings) {
                options.capture.control.check();
                const auto &definition = schemas.schema(binding.first);
                require(binding.first == definition.path && definition.struct_kind != 0,
                        "Default snapshot bindings require exact qualified struct paths");
            }
        };
        validate_bindings(read_options.native_structs);
        validate_bindings(read_options.struct_layouts);
        validate_bindings(read_options.struct_tails);
        for (const auto &name : read_options.reflected_structs) {
            const auto &declaration = capture.at("declarations").at(name);
            require(name == schemas.schema(name).path && declaration.at("kind") == "struct" &&
                        declaration.at("binarySerialization") == false &&
                        declaration.at("nativeSerialization") == false,
                    "Captured native/binary structs cannot use a reflected route");
        }
        std::function<void(const Json &)> validate_type = [&](const Json &type) {
            options.capture.control.check();
            if (type.at("name") == "StructProperty") {
                const auto &target = capture.at("declarations").at(word(type.at("schema")));
                require(type.at("nativeSerialization").is_boolean() &&
                            target.at("nativeSerialization").is_boolean() &&
                            target.at("binarySerialization").is_boolean() &&
                            type.at("nativeSerialization") == target.at("nativeSerialization") &&
                            hex_string(type.at("structGuid").get<std::string>(), 32) &&
                            type.at("structGuid") == target.at("structGuid"),
                        "Captured struct descriptor disagrees with its GUID/native declaration");
                require(
                    (target.at("kind") == "struct" && target.at("nativeSerialization") == false &&
                     target.at("binarySerialization") == false) ||
                        (target.at("kind") == "native-struct" && (target.at("nativeSerialization") == true ||
                                                                  target.at("binarySerialization") == true)),
                    "Conflicting captured struct serialization traits");
            }
            if (type.contains("arguments"))
                for (const auto &argument : type.at("arguments"))
                    validate_type(argument);
        };
        for (auto it = capture.at("declarations").begin(); it != capture.at("declarations").end(); ++it) {
            options.capture.control.check();
            for (const auto &property : it->at("properties"))
                validate_type(property.at("type"));
            if (it->at("kind") == "struct") {
                require(it->at("binarySerialization") == false && it->at("nativeSerialization") == false,
                        "Conflicting captured reflected struct traits");
                if (!read_options.native_structs.count(it.key()) &&
                    !read_options.struct_layouts.count(it.key()) &&
                    !read_options.struct_tails.count(it.key()))
                    read_options.reflected_structs.insert(it.key());
            }
        }
        for (const auto &binding : read_options.native_structs) {
            native_struct_layout_name(binding.second);
            require(!read_options.reflected_structs.count(binding.first) &&
                        !read_options.struct_layouts.count(binding.first) &&
                        !read_options.struct_tails.count(binding.first),
                    "Conflicting default field native routes");
        }
        for (const auto *bindings : {&read_options.struct_layouts, &read_options.struct_tails})
            for (const auto &binding : *bindings) {
                require(read_options.binary_layouts &&
                            read_options.binary_layouts->contains(binding.second) &&
                            !read_options.reflected_structs.count(binding.first),
                        "Invalid default field binary route");
                require(!(read_options.struct_layouts.count(binding.first) &&
                          read_options.struct_tails.count(binding.first)),
                        "Conflicting default field binary routes");
            }
    }
    Json field(const Json &node, const SchemaProperty &property) {
        options.capture.control.check();
        Json result = {{"owner", node.at("owner")},
                       {"name", node.at("name")},
                       {"arrayIndex", node.at("arrayIndex")},
                       {"arrayDim", node.at("arrayDim")},
                       {"flags", node.at("flags")},
                       {"type", node.at("type")},
                       {"available", false}};
        if (node.contains("propertyGuid")) {
            const auto guid = node.at("propertyGuid").get<std::string>();
            require(hex_string(guid, 32), "Invalid captured property GUID");
            result["propertyGuid"] = guid;
        }
        const auto &wire = node.at("wire");
        require(wire.at("available").is_boolean(), "Expected captured field wire availability");
        if (wire.at("available") == false) {
            result["reason"] = word(wire.at("reason"));
            return result;
        }
        require(archive_profile(wire) == profile, "Captured fields use inconsistent archive policies");
        const auto &hex = wire.at("hex").get_ref<const std::string &>();
        require(!(hex.size() & 1), "Captured field hex has an odd byte count");
        if (hex.size() / 2 > options.max_wire_bytes - wire_bytes)
            throw Error("default-limit", "Captured field wire budget exceeded");
        wire_bytes += hex.size() / 2;
        std::vector<std::uint8_t> bytes(hex.size() / 2);
        auto nibble = [](unsigned char c) -> unsigned {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            throw Error("default-capture", "Captured field hex is not lowercase hexadecimal");
        };
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (!(i & 4095))
                options.capture.control.check();
            bytes[i] = static_cast<std::uint8_t>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
        }
        require(wire.at("names").is_array() && wire.at("objects").is_array(),
                "Expected captured field reference tables");
        std::vector<std::string> names;
        for (const auto &name : wire.at("names")) {
            options.capture.control.check();
            names.push_back(word(name));
        }
        const auto &objects = wire.at("objects");
        std::set<std::string> object_paths;
        for (const auto &object : objects) {
            options.capture.control.check();
            object_reference(object);
            require(object_paths.insert(object.at("path").get<std::string>()).second,
                    "Duplicate captured object reference");
        }
        auto read = read_options;
        read.name_values.max_names -= functional_names;
        read.name_values.max_text_bytes -= functional_text;
        read.name_values.charge = [&](std::uint32_t count, std::uint64_t text) {
            functional_names += count;
            functional_text += text;
            if (options.values.name_values.charge)
                options.values.name_values.charge(count, text);
        };
        read.value_identities.max_names -= identity_names;
        read.value_identities.max_text_bytes -= identity_text;
        read.value_identities.charge = [&](std::uint32_t count, std::uint64_t text) {
            identity_names += count;
            identity_text += text;
            if (options.values.value_identities.charge)
                options.values.value_identities.charge(count, text);
        };
        read.names = &names;
        read.reference = [&](std::int32_t index) -> Json {
            if (!index)
                return nullptr;
            require(index > 0 && static_cast<std::size_t>(index) <= objects.size(),
                    "Captured object reference index is out of range");
            return objects.at(static_cast<std::size_t>(index - 1)).at("path");
        };
        read.reference_identity = "cyuediff.default-capture-object-path/1";
        read.max_values = options.values.max_values - values_read;
        read.max_text_bytes = options.values.max_text_bytes - text_bytes;
        const auto value_control = read.control;
        read.control.cancelled = [&, value_control] {
            return (options.capture.control.cancelled && options.capture.control.cancelled()) ||
                   (value_control.cancelled && value_control.cancelled());
        };
        read.charge = [&](std::uint32_t work, std::uint64_t text) {
            if (work > options.values.max_values - values_read ||
                text > options.values.max_text_bytes - text_bytes)
                throw Error("default-limit", "Default snapshot aggregate value budget exceeded");
            values_read += work;
            text_bytes += text;
            if (options.values.charge)
                options.values.charge(work, text);
        };
        result["wireSha256"] = sha256_bytes(bytes, read.control);
        result["wireBytes"] = bytes.size();
        result["names"] = wire.at("names");
        result["objects"] = objects;
        try {
            auto value = decode_unversioned_value(bytes, 0, bytes.size(), schemas, property.type, read);
            require(value.consumed_bytes == bytes.size(), "Captured field wire was not fully consumed");
            result["available"] = true;
            result["value"] = std::move(value.value);
            result["interpretation"] = std::move(value.interpretation);
            result["nameValues"] = std::move(value.name_values);
            if (!value.value_identities.is_null())
                result["valueIdentities"] = std::move(value.value_identities);
        } catch (const Error &error) {
            if (error.code != "schema-type" && error.code != "schema-context")
                throw;
            result["reason"] = error.code;
            result["message"] = error.what();
        }
        options.capture.control.checkpoint("default-fields", values_read, options.values.max_values);
        return result;
    }
    Json fields(const std::string &schema, const Json &nodes) {
        require(nodes.is_object(), "Expected captured fields object");
        Json result = Json::object();
        const auto *definition = &schemas.schema(schema);
        const auto root_kind = definition->struct_kind;
        std::set<std::string> owners;
        while (definition) {
            options.capture.control.check();
            require((definition->struct_kind == 0) == (root_kind == 0) &&
                        owners.insert(definition->path).second,
                    "Invalid captured declaration inheritance");
            const auto &declared = capture.at("declarations").at(definition->path).at("properties");
            std::map<std::string, const Json *> declared_fields;
            for (const auto &candidate : declared) {
                options.capture.control.check();
                declared_fields.emplace(candidate.at("name").get<std::string>(), &candidate);
            }
            for (const auto &pair : definition->properties) {
                options.capture.control.check();
                const auto &property = pair.second;
                const auto found = declared_fields.find(property.name);
                const Json *declaration = found == declared_fields.end() ? nullptr : found->second;
                require(declaration && declaration->at("serialize").is_boolean(),
                        "Missing captured property serialization policy");
                if (declaration->at("serialize") == false)
                    continue;
                const auto array_index = pair.first - property.index;
                auto key = definition->path + "." + property.name;
                if (property.array_size > 1)
                    key += "[" + std::to_string(array_index) + "]";
                const auto &node = nodes.at(key);
                require(node.at("owner") == definition->path && node.at("name") == property.name &&
                            unsigned_value(node.at("arrayIndex"), UINT8_MAX) == array_index &&
                            unsigned_value(node.at("arrayDim"), UINT8_MAX) == property.array_size &&
                            node.at("type") == declaration->at("type") &&
                            node.at("flags") == declaration->at("flags") &&
                            hex_string(node.at("flags").get<std::string>(), 16),
                        "Captured field disagrees with its declaration");
                require(!result.contains(key), "Duplicate qualified captured field");
                if (declaration->contains("propertyGuid"))
                    require(node.contains("propertyGuid") &&
                                node.at("propertyGuid") == declaration->at("propertyGuid"),
                            "Captured property GUID disagrees with declaration");
                result[key] = field(node, property);
            }
            if (definition->super.empty())
                break;
            definition = &schemas.schema(definition->super);
        }
        require(result.size() == nodes.size(), "Captured fields include an undeclared/ineligible property");
        return result;
    }
};
} // namespace
struct DefaultSnapshot::Impl {
    UnversionedSchemas schemas;
    std::string hash;
    Json fields, structures, provenance;
    explicit Impl(UnversionedSchemas registry) : schemas(std::move(registry)) {}
};
DefaultSnapshot::DefaultSnapshot(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
DefaultSnapshot DefaultSnapshot::parse(const std::vector<std::uint8_t> &bytes,
                                       const DefaultSnapshotReadOptions &options) {
    options.capture.control.checkpoint("default-capture");
    options.values.control.check();
    if (bytes.size() > options.capture.max_bytes)
        throw Error("default-limit", "Default capture byte limit exceeded");
    require(!options.expected_engine_version.empty() && options.expected_ue4 >= 0 &&
                options.expected_ue5 >= 0,
            "Default capture requires an explicit matching Engine/archive version");
    require(options.expected_sha256.empty() || hex_string(options.expected_sha256, 64),
            "Expected default capture SHA256 must be lowercase hexadecimal");
    const auto hash = sha256_bytes(bytes, options.capture.control);
    require(options.expected_sha256.empty() || hash == options.expected_sha256,
            "Default capture fingerprint does not match");
    try {
        std::vector<std::set<std::string>> keys;
        std::uint64_t text = 0, nodes = 0;
        auto document = Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json &value) {
            options.capture.control.check();
            if (depth < 0 || static_cast<unsigned>(depth) >= options.capture.max_depth)
                throw Error("default-limit", "Default capture depth limit exceeded");
            if (event == Json::parse_event_t::object_start || event == Json::parse_event_t::array_start ||
                event == Json::parse_event_t::value || event == Json::parse_event_t::key) {
                if (nodes >= options.capture.max_entries)
                    throw Error("default-limit", "Default capture node limit exceeded");
                ++nodes;
            }
            if (event == Json::parse_event_t::object_start)
                keys.emplace_back();
            if (event == Json::parse_event_t::key ||
                (event == Json::parse_event_t::value && value.is_string())) {
                const auto &word = value.get_ref<const std::string &>();
                if (word.size() > options.capture.max_text_bytes - text)
                    throw Error("default-limit", "Default capture text limit exceeded");
                text += word.size();
                detail::validate_byte_string(word, false, options.capture.control, "default-capture", 0);
                if (event == Json::parse_event_t::key)
                    require(!keys.empty() && keys.back().insert(word).second,
                            "Duplicate default capture key");
            }
            if (event == Json::parse_event_t::object_end)
                keys.pop_back();
            return true;
        });
        require(document.at("format") == "cyuediff.engine-default-capture/1" &&
                    document.at("completeCapture") == true && document.at("classDefault") == true,
                "Expected a complete explicit class-default capture");
        require(document.at("engineVersion") == options.expected_engine_version &&
                    document.at("filterEditorOnly").is_boolean() &&
                    document.at("filterEditorOnly") == options.filter_editor_only &&
                    document.at("blueprintClass").is_boolean(),
                "Default capture producer/filter does not match");
        object_reference(document.at("object"));
        if (!document.at("archetype").is_null())
            object_reference(document.at("archetype"));
        if (!document.at("classGeneratedBy").is_null())
            object_reference(document.at("classGeneratedBy"));
        const auto &flags = document.at("object").at("flags").get_ref<const std::string &>();
        require((std::stoull(flags, nullptr, 16) & 0x10) != 0,
                "Captured object is not a class default object");
        const auto &archive = document.at("archive");
        const auto profile = archive_profile(archive);
        require(profile.at("filterEditorOnly") == options.filter_editor_only &&
                    profile.at("ue4") == options.expected_ue4 && profile.at("ue5") == options.expected_ue5 &&
                    archive.at("hex") == "" && archive.at("names").is_array() &&
                    archive.at("names").empty() && archive.at("objects").is_array() &&
                    archive.at("objects").empty(),
                "Default capture archive does not match");
        auto impl =
            std::make_shared<Impl>(UnversionedSchemas::from_runtime_capture(document, options.capture));
        const auto cls = word(document.at("object").at("class"));
        require(impl->schemas.schema(cls).path == cls && impl->schemas.schema(cls).struct_kind == 0,
                "Default capture class declaration does not match");
        CaptureReader reader(options, document, impl->schemas);
        impl->fields = reader.fields(cls, document.at("fields"));
        require(document.at("structures").is_object(), "Expected captured initialized structures");
        impl->structures = Json::object();
        for (auto it = document.at("structures").begin(); it != document.at("structures").end(); ++it) {
            options.capture.control.check();
            require(it->at("schema") == it.key() && impl->schemas.schema(it.key()).struct_kind != 0,
                    "Captured structure declaration does not match");
            const auto &guid = it->at("structGuid");
            require(hex_string(guid.get<std::string>(), 32) &&
                        guid == document.at("declarations").at(it.key()).at("structGuid"),
                    "Captured structure GUID does not match");
            require(it->at("available").is_boolean(), "Expected initialized structure availability");
            if (it->at("available") == false) {
                impl->structures[it.key()] = {{"schema", it.key()},
                                              {"structGuid", guid},
                                              {"available", false},
                                              {"reason", word(it->at("reason"))}};
            } else {
                impl->structures[it.key()] = {{"schema", it.key()},
                                              {"structGuid", guid},
                                              {"available", true},
                                              {"fields", reader.fields(it.key(), it->at("fields"))}};
            }
        }
        for (auto it = document.at("structures").begin(); it != document.at("structures").end(); ++it)
            if (it->contains("postSerialize")) {
                require(it->at("postSerialize").is_boolean(), "Invalid captured post-serializer policy");
                impl->structures[it.key()]["postSerialize"] = it->at("postSerialize");
            }
        impl->hash = hash;
        impl->provenance = {{"schema", "cyuediff.default-snapshot/1"},
                            {"binding", "unbound"},
                            {"defaultsApplied", false},
                            {"captureSha256", hash},
                            {"schemaSha256", impl->schemas.sha256()},
                            {"engineVersion", document.at("engineVersion")},
                            {"object", document.at("object")},
                            {"archetype", document.at("archetype")},
                            {"classDefault", true},
                            {"blueprintClass", document.at("blueprintClass")},
                            {"classGeneratedBy", document.at("classGeneratedBy")},
                            {"archive", profile},
                            {"wireBytes", reader.wire_bytes},
                            {"valuesRead", reader.values_read},
                            {"valueTextBytes", reader.text_bytes}};
        if (document.contains("factory"))
            impl->provenance["factory"] = document.at("factory");
        if (document.contains("environment"))
            impl->provenance["environment"] = document.at("environment");
        if (options.values.value_identities.names) {
            impl->provenance["identityNamesRead"] = reader.identity_names;
            impl->provenance["identityTextBytes"] = reader.identity_text;
        }
        options.capture.control.check();
        options.values.control.check();
        return DefaultSnapshot(std::move(impl));
    } catch (const Json::exception &) {
        throw Error("default-capture", "Invalid default capture JSON");
    }
}
DefaultSnapshot DefaultSnapshot::read(const fs::path &path, const DefaultSnapshotReadOptions &options) {
    options.capture.control.check();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw Error("file-open", "Cannot open default capture: " + path.u8string());
    const auto length = file.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > options.capture.max_bytes ||
        static_cast<std::uint64_t>(length) > std::vector<std::uint8_t>{}.max_size())
        throw Error("default-limit", "Default capture byte limit exceeded");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    for (std::size_t pos = 0; pos < bytes.size();) {
        options.capture.control.check();
        const auto count = std::min<std::size_t>(1024 * 1024, bytes.size() - pos);
        if (!file.read(reinterpret_cast<char *>(bytes.data() + pos), static_cast<std::streamsize>(count)))
            throw Error("file-read", "Cannot read default capture");
        pos += count;
    }
    if (file.peek() != EOF)
        throw Error("file-read", "Default capture changed size while reading");
    return parse(bytes, options);
}
const UnversionedSchemas &DefaultSnapshot::schemas() const { return impl_->schemas; }
const Json &DefaultSnapshot::fields() const { return impl_->fields; }
const Json &DefaultSnapshot::structures() const { return impl_->structures; }
const Json &DefaultSnapshot::provenance() const { return impl_->provenance; }
const std::string &DefaultSnapshot::sha256() const { return impl_->hash; }
Json DefaultSnapshot::to_json() const {
    auto result = impl_->provenance;
    result["fields"] = impl_->fields;
    result["structures"] = impl_->structures;
    return result;
}
} // namespace cyuediff
